#include <thread>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <cassert>
#include <numeric>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include "types.hpp"

using namespace std;

#if 1
#define DEBUG_WRITE_OUT
#endif

#if 0
#define DEBUG_PRINT
#endif

#define LOG(os, msg) (os << msg << endl)
#define LOGN(os, msg) (os << msg)
#define LOGV(os, msg) (os << msg << " - " << __LINE__ << " " << __FILE__ << endl)
#define LOGVN(os, msg) (os << msg << " - " << __LINE__ << " " << __FILE__)
#define UNREACHABLE_CODE (assert(false && "Invalid code path"))

static std::mt19937 gen;
i32 GetRandomNumber(i32 min, i32 max)
{
    std::uniform_int_distribution<> dis(min, max);
    return dis(gen);
}

enum class Player
{
    CROSS,
    CIRCLE,
    NONE
};

enum Move
{
    TOP_LEFT,
    TOP_MID,
    TOP_RIGHT,
    MID_LEFT,
    MID_MID,
    MID_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_MID,
    BOTTOM_RIGHT,
    NONE
};

enum class GameOutcome
{
    WIN,
    LOSS,
    DRAW,
    NONE
};

// using MoveToPlayerMap: unordered_map<Move, Player -> Player[Move::NONE]
using MoveToPlayerMap = Player[Move::NONE];

// using MoveSet: unordered_set<Move> -> { Move[Move::NONE], u32 }
struct MoveSet
{
    Move  moves[Move::NONE];
    u32   moves_left;
};

struct GameState
{
    MoveToPlayerMap  move_to_player_map;
    Player           player_to_move;
    GameOutcome      outcome_for_previous_player;
    MoveSet          legal_moveset;
};

GameState g_game_state;

static string PlayerToWord(Player player)
{
    switch (player)
    {
        case Player::CIRCLE:
            return "CIRCLE";
        case Player::CROSS:
            return "CROSS";
        default:
        {
            UNREACHABLE_CODE;
            return "Breh";
        }
    }
}

#include "MCST.cpp"

static string GameOutcomeToWord(GameOutcome game_outcome)
{
    switch (game_outcome)
    {
        case GameOutcome::WIN:
            return "WIN";
        case GameOutcome::LOSS:
            return "LOSS";
        case GameOutcome::DRAW:
            return "DRAW";
        case GameOutcome::NONE:
            return "NONE";
        default:
        {
            UNREACHABLE_CODE;
            return "Breh";
        }
    }
}

void PrintGameState(const GameState &game_state, ostream &os)
{
    for (u32 row = 0; row < 3; ++row)
    {
        for (u32 column = 0; column < 3; ++column)
        {
            Move inspected_move = static_cast<Move>(row * 3 + column);
            Player player_that_made_move = game_state.move_to_player_map[inspected_move];
            if (player_that_made_move != Player::NONE)
            {
                LOGN(os, (player_that_made_move == Player::CIRCLE ? "O" : "X"));
            }
            else
            {
                LOGN(os, ".");
            }
            LOGN(os, " ");
        }
        LOG(os, "");
    }
}

GameOutcome DetermineGameOutcome(GameState &game_state, Player previous_player)
{
    bool is_draw = true;
    auto check_for_player_wincon = [&game_state, &is_draw](Move predicate_moves[3], Player predicate)
    {
        for (u32 i = 0; i < 3; ++i)
        {
            Player player = game_state.move_to_player_map[predicate_moves[i]];
            if (player == Player::NONE)
            {
                is_draw = false;
            }
            if (player != predicate)
            {
                return false;
            }
        }

        return true;
    };

    for (i32 row_orientation = 0; row_orientation < 2; ++row_orientation)
    {
        i32 col_term = (row_orientation == 0 ? 3 : 1);
        i32 row_term = (row_orientation == 0 ? 1 : 3);
        for (i32 row_index = 0; row_index < 3; ++row_index)
        {
            Move predicate_moves[3];
            for (i32 column_index = 0; column_index < 3; ++column_index)
            {
                i32 move_index = row_term * row_index + col_term * column_index;
                assert(move_index < Move::NONE);
                Move move = static_cast<Move>(move_index);
                predicate_moves[column_index] = move;
            }

            // TODO: could check for all Player in one iteration, as if one symbol isn't the matched one, it automatically turns the corresponding boolean to false
            if (check_for_player_wincon(predicate_moves, Player::CROSS))
            {
                return previous_player == Player::CROSS ? GameOutcome::WIN : GameOutcome::LOSS;
            }
            if (check_for_player_wincon(predicate_moves, Player::CIRCLE))
            {
                return previous_player == Player::CIRCLE ? GameOutcome::WIN : GameOutcome::LOSS;
            }
        }
    }

    // check diagonals
    for (i32 direction = 0; direction < 2; ++direction)
    {
        i32 a = (direction == 0) ? 4 : 0;
        i32 b = (direction == 0) ? 0 : 2;
        Move predicate_moves[3] = {};
        for (i32 i = 0; i < 3; ++i)
        {
            i32 move_index = i * a + b * (i + 1);
            assert(move_index < Move::NONE);
            Move move = static_cast<Move>(move_index);
            predicate_moves[i] = move;
        }

        if (check_for_player_wincon(predicate_moves, Player::CROSS))
        {
            return previous_player == Player::CROSS ? GameOutcome::WIN : GameOutcome::LOSS;
        }
        if (check_for_player_wincon(predicate_moves, Player::CIRCLE))
        {
            return previous_player == Player::CIRCLE ? GameOutcome::WIN : GameOutcome::LOSS;
        }
    }

    if (is_draw)
    {
        return GameOutcome::DRAW;
    }

    return GameOutcome::NONE;
}

void ProcessMove(MoveSet &available_moves, Move move)
{
    assert(available_moves.moves[move] != Move::NONE);
    available_moves.moves[move] = Move::NONE;
    --available_moves.moves_left;
}

bool g_should_write_out_simulation;
ofstream g_simresult_fs;

SimulationResult simulation_from_position_once(const MoveSequence &movesequence_from_position)
{
    SimulationResult simulation_result = {};

    GameState cur_world_state = g_game_state;
    Player player_that_needs_to_win = cur_world_state.player_to_move;

#if defined(DEBUG_WRITE_OUT)
    if (g_should_write_out_simulation)
    {
        LOG(g_simresult_fs, "Player about to move: " << PlayerToWord(cur_world_state.player_to_move));
        PrintGameState(cur_world_state, g_simresult_fs);
        LOGN(g_simresult_fs, "move sequence from state: ");
        for (u32 movesequence_index = 0; movesequence_index < movesequence_from_position.number_of_moves; ++movesequence_index)
        {
            Move move = movesequence_from_position.moves[movesequence_index];
            assert(move != Move::NONE);
            LOGN(g_simresult_fs, MoveToWord(move) << " ");
        }
        LOG(g_simresult_fs, "");
    }
#endif

    // NOTE(david): no randomness were involved, got a proper game out come exactly right after the move sequence was applied to the position
    u32 movesequence_index = 0;
    simulation_result.last_player_to_move = (cur_world_state.player_to_move == Player::CIRCLE) ? Player::CROSS : Player::CIRCLE;
    simulation_result.last_move.terminal_type = TerminalType::NEUTRAL;
    simulation_result.last_move.controlled_type = ControlledType::UNCONTROLLED;

    // make moves to arrive at the position and simulate the rest of the game
    cur_world_state.outcome_for_previous_player = DetermineGameOutcome(cur_world_state, simulation_result.last_player_to_move);
    
    while (cur_world_state.outcome_for_previous_player == GameOutcome::NONE)
    {
        // make a move from the move chain
        if (movesequence_index < movesequence_from_position.number_of_moves)
        {
            Move move = movesequence_from_position.moves[movesequence_index++];
            assert(cur_world_state.move_to_player_map[move] == Player::NONE);
            assert(cur_world_state.legal_moveset.moves[move] != Move::NONE);
            assert(cur_world_state.legal_moveset.moves_left > 0);

            cur_world_state.move_to_player_map[move] = cur_world_state.player_to_move;
            cur_world_state.legal_moveset.moves[move] = Move::NONE;
            --cur_world_state.legal_moveset.moves_left;
        }
        // generate a random move
        else
        {
            simulation_result.last_move.terminal_type = TerminalType::NOT_TERMINAL;

            assert(cur_world_state.legal_moveset.moves_left > 0 && "if there aren't any more legal moves that means DetermineGameOutcome should have returned draw");

            u32 random_move_offset = GetRandomNumber(0, cur_world_state.legal_moveset.moves_left - 1);
            Move random_move = Move::NONE;
            u32 random_move_index = 0;
            for (u32 move_index = 0; move_index < ArrayCount(cur_world_state.legal_moveset.moves); ++move_index)
            {
                if (cur_world_state.legal_moveset.moves[move_index] != Move::NONE && random_move_offset-- == 0)
                {
                    random_move = cur_world_state.legal_moveset.moves[move_index];
                    random_move_index = move_index;
                    break ;
                }
            }
            assert(random_move != Move::NONE);

            cur_world_state.move_to_player_map[random_move] = cur_world_state.player_to_move;
            cur_world_state.legal_moveset.moves[random_move_index] = Move::NONE;
            --cur_world_state.legal_moveset.moves_left;
        }

        simulation_result.last_player_to_move = cur_world_state.player_to_move;
        simulation_result.last_move.controlled_type = (simulation_result.last_player_to_move == player_that_needs_to_win ? ControlledType::CONTROLLED : ControlledType::UNCONTROLLED);

        cur_world_state.outcome_for_previous_player = DetermineGameOutcome(cur_world_state, simulation_result.last_player_to_move);

        cur_world_state.player_to_move = (cur_world_state.player_to_move == Player::CIRCLE) ? Player::CROSS : Player::CIRCLE;
    }

    simulation_result.last_player_to_move = simulation_result.last_player_to_move;
    switch (cur_world_state.outcome_for_previous_player)
    {
        // TODO(david): define a terminal interval for values, as there can be other values than winning and losing
        // NOTE(david): maybe there is no winning/losing terminal type, only values
        case GameOutcome::WIN:
        {
            simulation_result.total_value = player_that_needs_to_win == simulation_result.last_player_to_move ? 1.0 : -1.0;
            if (simulation_result.last_move.terminal_type != TerminalType::NOT_TERMINAL)
            {
                simulation_result.last_move.terminal_type = player_that_needs_to_win == simulation_result.last_player_to_move ? TerminalType::WINNING : TerminalType::LOSING;
            }
        }
        break;
        case GameOutcome::LOSS:
        {
            simulation_result.total_value = player_that_needs_to_win == simulation_result.last_player_to_move ? -1.0 : 1.0;
            if (simulation_result.last_move.terminal_type != TerminalType::NOT_TERMINAL)
            {
                simulation_result.last_move.terminal_type = player_that_needs_to_win == simulation_result.last_player_to_move ? TerminalType::LOSING : TerminalType::WINNING;
            }
        }
        break;
        case GameOutcome::DRAW:
        {
            simulation_result.total_value = 0.0;
            if (simulation_result.last_move.terminal_type != TerminalType::NOT_TERMINAL)
            {
                simulation_result.last_move.terminal_type = TerminalType::NEUTRAL;
            }
        }
        break;
        default:
        {
            UNREACHABLE_CODE;
        }
    }

#if defined(DEBUG_WRITE_OUT)
    if (g_should_write_out_simulation)
    {
        LOG(g_simresult_fs, "Player to move: " << PlayerToWord(cur_world_state.player_to_move));
        PrintGameState(cur_world_state, g_simresult_fs);
        LOG(g_simresult_fs, "Game outcome for previous player: " << GameOutcomeToWord(cur_world_state.outcome_for_previous_player));
        LOG(g_simresult_fs, "Previous player: " << PlayerToWord(simulation_result.last_player_to_move));
        LOG(g_simresult_fs, "TerminalType: " << TerminalTypeToWord(simulation_result.last_move.terminal_type));
        LOG(g_simresult_fs, "ControlledType: " << ControlledTypeToWord(simulation_result.last_move.controlled_type));
        LOG(g_simresult_fs, "");
    }
#endif

    if (movesequence_index != movesequence_from_position.number_of_moves)
    {
        assert(movesequence_index == movesequence_from_position.number_of_moves && "still have moves to apply to the position from movesequence so the simulation can't end before that");
    }

    simulation_result.num_simulations = 1;

    return simulation_result;
}

SimulationResult simulation_from_position(const MoveSequence &movesequence_from_position)
{
    assert(movesequence_from_position.number_of_moves > 0 && "must have at least one move to apply to the position");

    SimulationResult simulation_result = {};
    simulation_result.last_move.terminal_type = TerminalType::NOT_TERMINAL;
    simulation_result.last_move.controlled_type = ControlledType::NONE;

    g_should_write_out_simulation = true;

#if defined(DEBUG_WRITE_OUT)
    static u32 sim_counter = 0;
    g_simresult_fs = ofstream("debug/sim_results/sim_result" + to_string(sim_counter++));
#endif

    // choose the number of simulations based on the number of possible moves
    assert(g_game_state.legal_moveset.moves_left >= movesequence_from_position.number_of_moves);
    u32 number_of_moves_available_from_position = g_game_state.legal_moveset.moves_left - movesequence_from_position.number_of_moves;
    u32 number_of_simulations_weight = number_of_moves_available_from_position * 15;
    u32 number_of_simulations = max((u32)1, (u32)(number_of_simulations_weight));
    // NOTE(david): it will be a terminal move, but it hasn't yet been simulated
    assert(number_of_simulations > 0 && "assumption, debug to make sure this is true, for example get the selected node and check if it is not terminal yet at this point");
    for (u32 current_simulation_count = 0;
         current_simulation_count < number_of_simulations;
         ++current_simulation_count)
    {
        SimulationResult simulation_result_for_one_simulation = simulation_from_position_once(movesequence_from_position);

        simulation_result.last_move = simulation_result_for_one_simulation.last_move;
        simulation_result.last_player_to_move = simulation_result_for_one_simulation.last_player_to_move;
        simulation_result.total_value += simulation_result_for_one_simulation.total_value;
        assert(simulation_result_for_one_simulation.num_simulations == 1);
        simulation_result.num_simulations += simulation_result_for_one_simulation.num_simulations;

#if defined(DEBUG_WRITE_OUT)
        LOGN(g_simresult_fs, simulation_result_for_one_simulation.total_value << " ");
        g_should_write_out_simulation = false;
#endif

        if (simulation_result_for_one_simulation.last_move.terminal_type != TerminalType::NOT_TERMINAL)
        {
            simulation_result.num_simulations = max((u32)1, (u32)(number_of_simulations_weight));

            simulation_result.total_value = simulation_result_for_one_simulation.total_value * simulation_result.num_simulations;
            assert(simulation_result.num_simulations > 0);

            assert(current_simulation_count == 0);
            break;
        }
    }

#if defined(DEBUG_WRITE_OUT)
    LOG(g_simresult_fs, "");
#endif

    return simulation_result;
}

struct GameSummary
{
    GameOutcome outcome;
    u32 number_of_simulations;
    u32 number_of_moves;
};

GameSummary play_one_game(Player player_to_win, NodePool &node_pool)
{
    GameSummary summary = {};
    summary.outcome = GameOutcome::NONE;

    g_game_state = {};
    g_game_state.player_to_move = player_to_win;
    for (u32 move_index = 0; move_index < Move::NONE; ++move_index)
    {
        g_game_state.move_to_player_map[move_index] = Player::NONE;
        Move move = static_cast<Move>(move_index);
        g_game_state.legal_moveset.moves[move] = move;
        ++g_game_state.legal_moveset.moves_left;
    }

#if defined(DEBUG_WRITE_OUT)
    static u32 playout_counter = 0;
    ofstream playout_fs("debug/playouts/playout" + to_string(playout_counter++));
#endif

    constexpr auto max_evaluation_time = 500ms;
    auto start_time = std::chrono::steady_clock::now();

    Player previous_player = g_game_state.player_to_move == Player::CIRCLE ? Player::CROSS : Player::CIRCLE;
    g_game_state.outcome_for_previous_player = DetermineGameOutcome(g_game_state, previous_player);
    while (g_game_state.outcome_for_previous_player == GameOutcome::NONE)
    {
        MCST mcst(node_pool);
        Move selected_move;
        bool should_terminate_simulation = false;
        bool stop_parent_sleep = false;
        u32 number_of_simulations_ran = 0;
        // TODO(david): use the concept of confidence interval instead of this?
        // NOTE(david): not using prune_treshhold_for_node anymore as a node which is terminal should be pruned regardless of a threshhold
        // r64 prune_treshhold_for_node = -1.0;
        thread t([&]() {
            try
            {
                selected_move = mcst.Evaluate(g_game_state.legal_moveset, [&](bool found_perfect_move){
                    if (found_perfect_move)
                    {
                        stop_parent_sleep = true;
                        return true;
                    }
                    return should_terminate_simulation;
                }, simulation_from_position, ProcessMove, node_pool);
                number_of_simulations_ran = mcst.NumberOfSimulationsRan();
            }
            catch (exception &e)
            {
                LOG(cerr, e.what());
                exit(1);
            }
        });
        while (std::chrono::steady_clock::now() - start_time < max_evaluation_time)
        {
            if (stop_parent_sleep)
            {
                break;
            }
            this_thread::sleep_for(1ms);
        }
        should_terminate_simulation = true;
        t.join();

        summary.number_of_simulations += number_of_simulations_ran;

        // play the move
        if (selected_move != Move::NONE)
        {
            ++summary.number_of_moves;
            assert(g_game_state.legal_moveset.moves[selected_move] != Move::NONE);
            g_game_state.legal_moveset.moves[selected_move] = Move::NONE;
            --g_game_state.legal_moveset.moves_left;

            assert(g_game_state.move_to_player_map[selected_move] == Player::NONE);
            g_game_state.move_to_player_map[selected_move] = g_game_state.player_to_move;
        }

        previous_player = g_game_state.player_to_move;
        g_game_state.outcome_for_previous_player = DetermineGameOutcome(g_game_state, previous_player);

#if defined(DEBUG_PRINT)
        LOG(cout, PlayerToWord(previous_player) << " moves " << MoveToWord(selected_move) << ", number of simulations ran: " << number_of_simulations_ran << ", move counter: " << g_move_counter);
        PrintGameState(g_game_state, cout);
        LOG(cout, "");
        LOG(cout, "");
#endif
#if defined(DEBUG_WRITE_OUT)
        LOG(playout_fs, PlayerToWord(previous_player) << " moves " << MoveToWord(selected_move) << ", number of simulations ran: " << number_of_simulations_ran << ", move counter: " << g_move_counter);
        PrintGameState(g_game_state, playout_fs);
        LOG(playout_fs, "");
        LOG(playout_fs, "");
#endif

        ++g_move_counter;

        switch (g_game_state.outcome_for_previous_player)
        {
            case GameOutcome::WIN: {

#if defined(DEBUG_PRINT)
                LOG(cout, "Game over, player " << PlayerToWord(previous_player) << " has won!");
#endif

#if defined(DEBUG_WRITE_OUT)
                LOG(playout_fs, "Game over, player " << PlayerToWord(previous_player) << " has won!");
#endif

            } break;
            case GameOutcome::LOSS: {

#if defined(DEBUG_PRINT)
                LOG(cout, "Game over, player " << PlayerToWord(previous_player) << " has won!");
#endif

#if defined(DEBUG_WRITE_OUT)
                LOG(playout_fs, "Game over, player " << PlayerToWord(previous_player) << " has won!");
#endif

            } break;
            case GameOutcome::DRAW: {

#if defined(DEBUG_PRINT)
                LOG(cout, "Game over, it's a draw!");
#endif

#if defined(DEBUG_WRITE_OUT)
                LOG(playout_fs, "Game over, it's a draw!");
#endif

            } break;
            case GameOutcome::NONE: {
                g_game_state.player_to_move = g_game_state.player_to_move == Player::CIRCLE ? Player::CROSS : Player::CIRCLE;
                start_time = std::chrono::steady_clock::now();
            } break ;
            default: {
                UNREACHABLE_CODE;
            }
        }
    }

    switch (g_game_state.outcome_for_previous_player)
    {
        case GameOutcome::WIN: {
            summary.outcome = player_to_win == previous_player ? GameOutcome::WIN : GameOutcome::LOSS;
        }
        break;
        case GameOutcome::LOSS: {
            summary.outcome = player_to_win == previous_player ? GameOutcome::LOSS : GameOutcome::WIN;
        }
        break;
        case GameOutcome::DRAW: {
            summary.outcome = GameOutcome::DRAW;
        }
        break;
        default: {
            UNREACHABLE_CODE;
        }
    }

    return summary;
}

i32 main()
{
#if defined(DEBUG_WRITE_OUT)
    ofstream game_outcome_fs("debug/game_summary", ios_base::out | ios::trunc);
#endif

    constexpr NodeIndex node_pool_size = 8192;
    NodePool node_pool(node_pool_size);

    constexpr u32 number_of_games = 100000;
    u32 number_of_wins = 0;
    u32 number_of_losses = 0;
    u32 number_of_draws = 0;
    for (u32 current_game_count = 1;
         current_game_count <= number_of_games;
         ++current_game_count)
    {
        gen.seed(current_game_count);
        r64 reward_for_reinforced_learning = 0.0;
        GameSummary summary = play_one_game(Player::CIRCLE, node_pool);
        switch (summary.outcome)
        {
            case GameOutcome::WIN:
            {
                reward_for_reinforced_learning = 1.0;
                ++number_of_wins;
            }
            break;
            case GameOutcome::LOSS:
            {
                reward_for_reinforced_learning = -1.0;
                ++number_of_losses;
            }
            break;
            case GameOutcome::DRAW:
            {
                reward_for_reinforced_learning = 0.0;
                ++number_of_draws;
            }
            break;
            default:
            {
                UNREACHABLE_CODE;
            }
        }
        static constexpr r64 alpha = 0.01;
        static constexpr r64 gamma = 0.99;

        // g_tuned_exploration_factor_weight = (1.0 - alpha) * g_tuned_exploration_factor_weight + alpha * (reward_for_reinforced_learning + gamma * g_tuned_exploration_factor_weight);
        LOG(cout, "Wins: " << number_of_wins << ", Losses: " << number_of_losses << ", Draws: " << number_of_draws << ", Total games played: " << current_game_count);
        // TODO(david): write out outcomes
#if defined(DEBUG_WRITE_OUT)
        LOG(game_outcome_fs, "Number of simulations: " << summary.number_of_simulations << ", number of moves: " << summary.number_of_moves << ", average number of simulations per move: " << (r64)summary.number_of_simulations / (r64)summary.number_of_moves << ", outcome: " << GameOutcomeToWord(summary.outcome));
#endif
        LOG(cout, "Number of simulations: " << summary.number_of_simulations << ", number of moves: " << summary.number_of_moves << ", average number of simulations per move: " << (r64)summary.number_of_simulations / (r64)summary.number_of_moves << ", outcome: " << GameOutcomeToWord(summary.outcome));
        LOG(cout, "g_tuned_exploration_factor_weight: " << g_tuned_exploration_factor_weight);
    }
}
