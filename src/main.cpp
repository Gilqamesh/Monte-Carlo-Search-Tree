#include <thread>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <cassert>
#include <functional>
#include "types.hpp"

using namespace std;

#if 1
#define DEBUG_TIME
#endif

#if 0
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

static std::chrono::time_point<std::chrono::high_resolution_clock> g_start_clock;
static std::chrono::time_point<std::chrono::high_resolution_clock> g_end_clock;
static double g_clock_cycles_var;

#if defined (DEBUG_TIME)
# define TIMED_BLOCK(job_name, job_expression) \
    g_start_clock = std::chrono::high_resolution_clock::now(); \
    job_expression; \
    g_end_clock = std::chrono::high_resolution_clock::now(); \
    g_clock_cycles_var = 2.11 * std::chrono::duration_cast<std::chrono::nanoseconds>(g_end_clock - g_start_clock).count() / 1000000.0; \
    // LOG(cout, "Clock cyles taken for " << job_name << ": " << g_clock_cycles_var << "M")
#else
# define TIMED_BLOCK(job_name, job_expression) job_expression
#endif

#if defined(DEBUG_WRITE_OUT)
# define WRITE_OUT(os, msg) LOG(os, msg)
# define WRITE_OUTN(os, msg) LOGN(os, msg)
# define WRITE_OUTV(os, msg) LOGV(os, msg)
# define WRITE_OUTVN(os, msg) LOGVN(os, msg)
#else
# define WRITE_OUT(os, msg)
# define WRITE_OUTN(os, msg)
# define WRITE_OUTV(os, msg)
# define WRITE_OUTVN(os, msg)
#endif

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

void simulation_from_position_once(const MoveSequence &movesequence_from_position, const GameState &game_state, Node *node, const NodePool &node_pool)
{
    GameState cur_game_state = game_state;
    Player player_that_needs_to_win = cur_game_state.player_to_move;

#if defined(DEBUG_WRITE_OUT)
    if (g_should_write_out_simulation)
    {
        LOG(g_simresult_fs, "Player about to move: " << PlayerToWord(cur_game_state.player_to_move));
        PrintGameState(cur_game_state, g_simresult_fs);
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
    Player last_player_to_move = (cur_game_state.player_to_move == Player::CIRCLE) ? Player::CROSS : Player::CIRCLE;
    TerminalType last_move_terminal_type = TerminalType::NEUTRAL;

    // make moves to arrive at the position and simulate the rest of the game
    cur_game_state.outcome_for_previous_player = DetermineGameOutcome(cur_game_state, last_player_to_move);

    while (cur_game_state.outcome_for_previous_player == GameOutcome::NONE)
    {
        // make a move from the move chain
        if (movesequence_index < movesequence_from_position.number_of_moves)
        {
            Move move = movesequence_from_position.moves[movesequence_index++];
            assert(cur_game_state.move_to_player_map[move] == Player::NONE);
            assert(cur_game_state.legal_moveset.moves[move] != Move::NONE);
            assert(cur_game_state.legal_moveset.moves_left > 0);

            cur_game_state.move_to_player_map[move] = cur_game_state.player_to_move;
            cur_game_state.legal_moveset.moves[move] = Move::NONE;
            --cur_game_state.legal_moveset.moves_left;
        }
        // generate a random move
        else
        {
            last_move_terminal_type = TerminalType::NOT_TERMINAL;

            assert(cur_game_state.legal_moveset.moves_left > 0 && "if there aren't any more legal moves that means DetermineGameOutcome should have returned draw");

            u32 random_move_offset = GetRandomNumber(0, cur_game_state.legal_moveset.moves_left - 1);
            Move random_move = Move::NONE;
            u32 random_move_index = 0;
            for (u32 move_index = 0; move_index < ArrayCount(cur_game_state.legal_moveset.moves); ++move_index)
            {
                if (cur_game_state.legal_moveset.moves[move_index] != Move::NONE && random_move_offset-- == 0)
                {
                    random_move = cur_game_state.legal_moveset.moves[move_index];
                    random_move_index = move_index;
                    break ;
                }
            }
            assert(random_move != Move::NONE);

            cur_game_state.move_to_player_map[random_move] = cur_game_state.player_to_move;
            cur_game_state.legal_moveset.moves[random_move_index] = Move::NONE;
            --cur_game_state.legal_moveset.moves_left;
        }

        last_player_to_move = cur_game_state.player_to_move;

        cur_game_state.outcome_for_previous_player = DetermineGameOutcome(cur_game_state, last_player_to_move);

        cur_game_state.player_to_move = (cur_game_state.player_to_move == Player::CIRCLE) ? Player::CROSS : Player::CIRCLE;
    }

    last_player_to_move = last_player_to_move;
    switch (cur_game_state.outcome_for_previous_player)
    {
        // TODO(david): define a terminal interval for values, as there can be other values than winning and losing
        // NOTE(david): maybe there is no winning/losing terminal type, only values
        case GameOutcome::WIN:
        {
            node->value += player_that_needs_to_win == last_player_to_move ? 1.0 : -1.0;
            if (last_move_terminal_type != TerminalType::NOT_TERMINAL)
            {
                node->terminal_info.terminal_type = player_that_needs_to_win == last_player_to_move ? TerminalType::WINNING : TerminalType::LOSING;
            }
        }
        break;
        case GameOutcome::LOSS:
        {
            node->value += player_that_needs_to_win == last_player_to_move ? -1.0 : 1.0;
            if (last_move_terminal_type != TerminalType::NOT_TERMINAL)
            {
                node->terminal_info.terminal_type = player_that_needs_to_win == last_player_to_move ? TerminalType::LOSING : TerminalType::WINNING;
            }
        }
        break;
        case GameOutcome::DRAW:
        {
            node->value += 0.0;
            if (last_move_terminal_type != TerminalType::NOT_TERMINAL)
            {
                node->terminal_info.terminal_type = TerminalType::NEUTRAL;
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
        LOG(g_simresult_fs, "Player to move: " << PlayerToWord(cur_game_state.player_to_move));
        PrintGameState(cur_game_state, g_simresult_fs);
        LOG(g_simresult_fs, "Game outcome for previous player: " << GameOutcomeToWord(cur_game_state.outcome_for_previous_player));
        LOG(g_simresult_fs, "Previous player: " << PlayerToWord(last_player_to_move));
        LOG(g_simresult_fs, "TerminalType: " << TerminalTypeToWord(last_move_terminal_type));
        LOG(g_simresult_fs, "ControlledType: " << ControlledTypeToWord(last_player_controlled_type));
        LOG(g_simresult_fs, "");
    }
#endif

    if (movesequence_index != movesequence_from_position.number_of_moves)
    {
        assert(movesequence_index == movesequence_from_position.number_of_moves && "still have moves to apply to the position from movesequence so the simulation can't end before that");
    }

    ++node->num_simulations;
}

void simulation_from_position(const MoveSequence &movesequence_from_position, const GameState &game_state, Node *node, const NodePool &node_pool)
{
    assert(movesequence_from_position.number_of_moves > 0 && "must have at least one move to apply to the position");

    g_should_write_out_simulation = true;

#if defined(DEBUG_WRITE_OUT)
    static u32 sim_counter = 0;
    g_simresult_fs = ofstream("debug/sim_results/sim_result" + to_string(sim_counter++));
#endif

    // choose the number of simulations based on the number of possible moves
    assert(game_state.legal_moveset.moves_left >= movesequence_from_position.number_of_moves);
    u32 number_of_moves_available_from_position = game_state.legal_moveset.moves_left - movesequence_from_position.number_of_moves;
    u32 number_of_simulations_weight = number_of_moves_available_from_position;
    u32 number_of_simulations = 1;
    // u32 number_of_simulations = max((u32)1, (u32)(number_of_simulations_weight));
    // NOTE(david): it will be a terminal move, but it hasn't yet been simulated
    assert(number_of_simulations > 0 && "assumption, debug to make sure this is true, for example get the selected node and check if it is not terminal yet at this point");
    for (u32 current_simulation_count = 0;
         current_simulation_count < number_of_simulations;
         ++current_simulation_count)
    {
        simulation_from_position_once(movesequence_from_position, game_state, node, node_pool);

#if defined(DEBUG_WRITE_OUT)
        LOGN(g_simresult_fs, node->value << " ");
        g_should_write_out_simulation = false;
#endif

        if (node->terminal_info.terminal_type != TerminalType::NOT_TERMINAL)
        {
            number_of_simulations = 1;
            // number_of_simulations = max((u32)1, (u32)(number_of_simulations_weight));
            assert(node->num_simulations == 1);

            node->value *= number_of_simulations;

            assert(current_simulation_count == 0);
            break ;
        }
    }

#if defined(DEBUG_WRITE_OUT)
    LOG(g_simresult_fs, "");
#endif
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

    GameState game_state = {};
    game_state.player_to_move = player_to_win;
    for (u32 move_index = 0; move_index < Move::NONE; ++move_index)
    {
        game_state.move_to_player_map[move_index] = Player::NONE;
        Move move = static_cast<Move>(move_index);
        game_state.legal_moveset.moves[move] = move;
        ++game_state.legal_moveset.moves_left;
    }

#if defined(DEBUG_WRITE_OUT)
    static u32 playout_counter = 0;
    ofstream playout_fs("debug/playouts/playout" + to_string(playout_counter++));
#endif

    constexpr auto max_evaluation_time = 2000ms;
    auto start_time = std::chrono::steady_clock::now();

    Player previous_player = game_state.player_to_move == Player::CIRCLE ? Player::CROSS : Player::CIRCLE;
    game_state.outcome_for_previous_player = DetermineGameOutcome(game_state, previous_player);
    while (game_state.outcome_for_previous_player == GameOutcome::NONE)
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
                TIMED_BLOCK("Evaluate", selected_move = mcst.Evaluate(game_state.legal_moveset, [&](bool found_perfect_move){
                    if (found_perfect_move)
                    {
                        stop_parent_sleep = true;
                        return true;
                    }
                    return should_terminate_simulation;
                }, simulation_from_position, ProcessMove, node_pool, game_state));
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
            assert(game_state.legal_moveset.moves[selected_move] != Move::NONE);
            game_state.legal_moveset.moves[selected_move] = Move::NONE;
            --game_state.legal_moveset.moves_left;

            assert(game_state.move_to_player_map[selected_move] == Player::NONE);
            game_state.move_to_player_map[selected_move] = game_state.player_to_move;
        }

        previous_player = game_state.player_to_move;
        game_state.outcome_for_previous_player = DetermineGameOutcome(game_state, previous_player);

#if defined(DEBUG_PRINT)
        LOG(cout, PlayerToWord(previous_player) << " moves " << MoveToWord(selected_move) << ", number of simulations ran: " << number_of_simulations_ran << ", move counter: " << g_move_counter);
        PrintGameState(game_state, cout);
        LOG(cout, "");
        LOG(cout, "");
#endif
#if defined(DEBUG_WRITE_OUT)
        LOG(playout_fs, PlayerToWord(previous_player) << " moves " << MoveToWord(selected_move) << ", number of simulations ran: " << number_of_simulations_ran << ", move counter: " << g_move_counter);
        PrintGameState(game_state, playout_fs);
        LOG(playout_fs, "");
        LOG(playout_fs, "");
#endif

        ++g_move_counter;

        switch (game_state.outcome_for_previous_player)
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
                game_state.player_to_move = game_state.player_to_move == Player::CIRCLE ? Player::CROSS : Player::CIRCLE;
                start_time = std::chrono::steady_clock::now();
            } break ;
            default: {
                UNREACHABLE_CODE;
            }
        }
    }

    switch (game_state.outcome_for_previous_player)
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
    ofstream game_outcome_fs("debug/game_summary", ios_base::out | ios::trunc);

    constexpr NodeIndex node_pool_size = 65536;
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

        TIMED_BLOCK("play_one_game", GameSummary summary = play_one_game(Player::CIRCLE, node_pool));
        // TIMED_BLOCK(GameSummary summary = play_one_game(Player::CIRCLE, node_pool));
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
        static constexpr r64 alpha = 0.1;
        static constexpr r64 gamma = 0.9;

        g_tuned_exploration_factor_weight = (1.0 - alpha) * g_tuned_exploration_factor_weight + alpha * (reward_for_reinforced_learning + gamma * g_tuned_exploration_factor_weight);
        LOG(cout, "Wins: " << number_of_wins << ", Losses: " << number_of_losses << ", Draws: " << number_of_draws << ", Total games played: " << current_game_count);

        LOG(game_outcome_fs, "Number of simulations: " << summary.number_of_simulations << ", number of moves: " << summary.number_of_moves << ", average number of simulations per move: " << (r64)summary.number_of_simulations / (r64)summary.number_of_moves << ", outcome: " << GameOutcomeToWord(summary.outcome));

        LOG(cout, "Number of simulations: " << summary.number_of_simulations << ", number of moves: " << summary.number_of_moves << ", average number of simulations per move: " << (r64)summary.number_of_simulations / (r64)summary.number_of_moves << ", outcome: " << GameOutcomeToWord(summary.outcome));
        LOG(cout, "g_tuned_exploration_factor_weight: " << g_tuned_exploration_factor_weight);
    }
}
