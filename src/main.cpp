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

using namespace std;

#if 0
# define DEBUG_PRINT_OUT
#endif

#if 0
# define DEBUG_PRINT_GAME
#endif

#define LOG(msg) (cout << msg << endl)
#define LINE() (LOG(__LINE__ << " " << __FILE__))
#define UNREACHABLE_CODE (assert(false && "Invalid code path"))

static std::mt19937 gen;
int GetRandomNumber(int min, int max)
{
    // static std::random_device rd;  // Will be used to obtain a seed for the random number engine
    // static std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<> dis(min, max);
    return dis(gen);
}

enum class Player
{
    CROSS,
    CIRCLE
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

using MoveToPlayerMap = unordered_map<Move, Player>;

enum class GameOutcome
{
    WIN,
    LOSS,
    DRAW,
    NONE
};

using MoveSet = unordered_set<Move>;

struct GameState
{
    MoveToPlayerMap move_to_player_map;
    Player player_to_move;
    GameOutcome outcome_for_previous_player;
    MoveSet legal_moveset;
};

GameState g_game_state;

static string GameOutcomeToWord(GameOutcome game_outcome)
{
    switch (game_outcome)
    {
        case GameOutcome::WIN: return "WIN";
        case GameOutcome::LOSS: return "LOSS";
        case GameOutcome::DRAW: return "DRAW";
        case GameOutcome::NONE: return "NONE";
        default: {
            UNREACHABLE_CODE;
            return "Breh";
        }
    }
}

static string PlayerToWord(Player player)
{
    switch (player)
    {
        case Player::CIRCLE:
            return "Player::CIRCLE";
        case Player::CROSS:
            return "Player::CROSS";
        default:
        {
            UNREACHABLE_CODE;
            return "Breh";
        }
    }
}

#include "MCST.cpp"

void PrintGameState(const GameState &game_state, ostream &os)
{
#if defined(DEBUG_PRINT_GAME)
    for (unsigned int row = 0; row < 3; ++row)
    {
        for (unsigned int column = 0; column < 3; ++column)
        {
            Move inspected_move = static_cast<Move>(row * 3 + column);
            auto player_it = game_state.move_to_player_map.find(inspected_move);
            if (player_it != game_state.move_to_player_map.end())
            {
                Player player_that_made_move = player_it->second;
                os << (player_that_made_move == Player::CIRCLE ? "O" : "X");
            }
            else
            {
                os << ".";
            }
            os << " ";
        }
        os << endl;
    }
#endif
}

GameOutcome DetermineGameOutcome(const MoveToPlayerMap &move_to_player_map, Player previous_player)
{
    auto check_for_player_wincon = [&move_to_player_map](const MoveSequence& predicate_moves, Player predicate) {
        return std::all_of(predicate_moves.begin(), predicate_moves.end(), [&](Move move) {
            auto it = move_to_player_map.find(move);
            return it != move_to_player_map.end() && it->second == predicate;
        });
    };
    for (int row_orientation = 0; row_orientation < 2; ++row_orientation)
    {
        int col_term = (row_orientation == 0 ? 3 : 1);
        int row_term = (row_orientation == 0 ? 1 : 3);
        for (int row_index = 0; row_index < 3; ++row_index)
        {
            MoveSequence predicate_moves;
            for (int column_index = 0; column_index < 3; ++column_index)
            {
                int move_index = row_term * row_index + col_term * column_index;
                assert(move_index < Move::NONE);
                predicate_moves.push_back(static_cast<Move>(move_index));
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
    for (int direction = 0; direction < 2; ++direction)
    {
        int a = (direction == 0) ? 4 : 0;
        int b = (direction == 0) ? 0 : 2;
        MoveSequence predicate_moves;
        for (int i = 0; i < 3; ++i)
        {
            int move_index = i * a + b * (i + 1);
            assert(move_index < Move::NONE);
            predicate_moves.push_back(static_cast<Move>(move_index));
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
    
    if (move_to_player_map.size() == Move::NONE)
    {
        return GameOutcome::DRAW;
    }

    return GameOutcome::NONE;
}

void ProcessMove(MoveSet &available_moves, Move move)
{
    assert(available_moves.count(move));
    available_moves.erase(move);
}

bool g_should_write_out_simulation;
ofstream g_write_out_for_simulation;

struct SimulationSubResult
{
    double value;
    Player last_player;
};

SimulationSubResult simulation_from_state_once(const MoveSequence &move_sequence_from_state)
{
    SimulationSubResult simulation_subresult = {};

    GameState cur_world_state = g_game_state;
    Player player_that_needs_to_win = cur_world_state.player_to_move;

#if defined(DEBUG_PRINT_OUT)
    if (g_should_write_out_simulation)
    {
        g_write_out_for_simulation << "Player about to move: " << PlayerToWord(cur_world_state.player_to_move) << endl;
        PrintGameState(cur_world_state, g_write_out_for_simulation);
        g_write_out_for_simulation << "move sequence from state: ";
        for (auto move : move_sequence_from_state)
        {
            g_write_out_for_simulation << MoveToWord(move) << " ";
        }
        g_write_out_for_simulation << endl;
    }
#endif

    Player previous_player = (cur_world_state.player_to_move == Player::CIRCLE) ? Player::CROSS : Player::CIRCLE;

    cur_world_state.outcome_for_previous_player = DetermineGameOutcome(cur_world_state.move_to_player_map, previous_player);
    auto move_it = move_sequence_from_state.begin();
    // make moves to arrive at the position and simulate the rest of the game
    while (cur_world_state.outcome_for_previous_player == GameOutcome::NONE)
    {
        // make a move from the move chain
        if (move_it != move_sequence_from_state.end())
        {
            auto move = *move_it++;
            assert(cur_world_state.move_to_player_map.count(move) == 0);
            cur_world_state.move_to_player_map.insert({move, cur_world_state.player_to_move});
            cur_world_state.legal_moveset.erase(move);
        }
        // generate a random move
        else
        {
            assert(cur_world_state.legal_moveset.empty() == false && "if there aren't any more legal moves that means DetermineGameOutcome should have returned draw");

            unsigned int random_move_offset = GetRandomNumber(0, cur_world_state.legal_moveset.size() - 1);
            auto it = cur_world_state.legal_moveset.begin();
            std::advance(it, random_move_offset);
            cur_world_state.move_to_player_map.insert({*it, cur_world_state.player_to_move});
            cur_world_state.legal_moveset.erase(it);
        }

        previous_player = cur_world_state.player_to_move;
        cur_world_state.outcome_for_previous_player = DetermineGameOutcome(cur_world_state.move_to_player_map, previous_player);

        cur_world_state.player_to_move = (cur_world_state.player_to_move == Player::CIRCLE) ? Player::CROSS : Player::CIRCLE;
    }

#if defined(DEBUG_PRINT_OUT)
    if (g_should_write_out_simulation)
    {
        g_write_out_for_simulation << "Player to move: " << PlayerToWord(cur_world_state.player_to_move) << endl;
        PrintGameState(cur_world_state, g_write_out_for_simulation);
        g_write_out_for_simulation << "Game outcome for previous player: " << GameOutcomeToWord(cur_world_state.outcome_for_previous_player) << endl;
        g_write_out_for_simulation << "Previous player: " << PlayerToWord(previous_player) << endl;
        g_write_out_for_simulation << "" << endl;
    }
#endif

    simulation_subresult.last_player = previous_player;
    switch (cur_world_state.outcome_for_previous_player)
    {
        case GameOutcome::WIN: {
            simulation_subresult.value = player_that_needs_to_win == previous_player ? 1.0 : -1.0;
        } break ;
        case GameOutcome::LOSS: {
            simulation_subresult.value = player_that_needs_to_win == previous_player ? -1.0 : 1.0;
        } break ;
        case GameOutcome::DRAW: {
            simulation_subresult.value = 0.0;
        } break ;
        default:
        {
            UNREACHABLE_CODE;
        }
    }

    return simulation_subresult;
}

SimulationResult simulation_from_state(const MoveSequence &move_sequence_from_state)
{
    SimulationResult simulation_result = {};

    g_should_write_out_simulation = true;

#if defined(DEBUG_PRINT_OUT)
    static unsigned int sim_counter = 0;
    g_write_out_for_simulation = ofstream("debug/sim_result" + to_string(sim_counter++));
#endif

    // choose the number of simulations based on the number of possible moves
    unsigned int number_of_moves_available_from_position = g_game_state.legal_moveset.size() - move_sequence_from_state.size();
    unsigned int number_of_simulations_weight = number_of_moves_available_from_position;
    unsigned int number_of_simulations = 15 * number_of_simulations_weight;
    if (number_of_moves_available_from_position == 0)
    {
        // NOTE(david): if it's a terminal state, only need to do 1 simulation
        number_of_simulations = 1;
        simulation_result.last_move.was_terminal = true;
    }
    simulation_result.num_simulations = number_of_simulations;
    assert(number_of_simulations > 0);
    for (unsigned int current_simulation_count = 0;
         current_simulation_count < number_of_simulations;
         ++current_simulation_count)
    {
        SimulationSubResult simulation_subresult = simulation_from_state_once(move_sequence_from_state);
        simulation_result.last_move.was_controlled = (g_game_state.player_to_move == simulation_subresult.last_player);
        
        simulation_result.total_value += simulation_subresult.value;

#if defined(DEBUG_PRINT_OUT)
        g_write_out_for_simulation << simulation_subresult.value << " ";
        g_should_write_out_simulation = false;
#endif
    }

#if defined(DEBUG_PRINT_OUT)
    g_write_out_for_simulation << endl;
#endif

    return simulation_result;
}

UtilityEstimationResult utility_estimator(const MoveSequence &move_sequence_from_state)
{
    UtilityEstimationResult utility_estimation_result = {};

    // TODO(david): implement heuristic evaluations for the game

    utility_estimation_result.should_prune = false;

    return utility_estimation_result;
}

GameOutcome play_one_game(Player player_to_win)
{
    g_game_state = {};

    g_game_state.player_to_move = player_to_win;
    g_game_state.outcome_for_previous_player = GameOutcome::NONE;
    for (unsigned int row_index = 0; row_index < Move::NONE; ++row_index)
    {
        g_game_state.legal_moveset.insert(static_cast<Move>(row_index));
    }

    constexpr auto max_evaluation_time = 500ms;
    auto start_time = std::chrono::steady_clock::now();

    while (g_game_state.outcome_for_previous_player == GameOutcome::NONE)
    {
        MCST mcst;
        Move selected_move;
        unsigned int number_of_simulations_ran = 0;
        bool should_terminate_simulation = false;
        // NOTE(david): use the concept of confidence interval instead of this?
        double prune_treshhold_for_node = -1.0;
        thread t([&]() {
            try
            {
                selected_move = mcst.Evaluate(g_game_state.legal_moveset, [&](){
                    return should_terminate_simulation;
                }, simulation_from_state, ProcessMove, utility_estimator, prune_treshhold_for_node);
                number_of_simulations_ran = mcst.NumberOfSimulationsRan();
            }
            catch (exception &e)
            {
                LOG(e.what());
                exit(1);
            }
        });
        while (std::chrono::steady_clock::now() - start_time < max_evaluation_time)
        {
            this_thread::sleep_for(1ms);
        }
        should_terminate_simulation = true;
        t.join();


        // play the move
        if (selected_move != Move::NONE)
        {
            g_game_state.legal_moveset.erase(selected_move);
            assert(g_game_state.move_to_player_map.count(selected_move) == 0);
            g_game_state.move_to_player_map.insert({selected_move, g_game_state.player_to_move});
        }

        g_game_state.outcome_for_previous_player = DetermineGameOutcome(g_game_state.move_to_player_map, g_game_state.player_to_move);

#if defined(DEBUG_PRINT_GAME)
        LOG(PlayerToWord(g_game_state.player_to_move) << " moves " << MoveToWord(selected_move) << ", number of simulations ran: " << number_of_simulations_ran << ", move counter: " << g_move_counter);
        PrintGameState(g_game_state, cout);
        LOG("");
        LOG("");
#endif

        switch (g_game_state.outcome_for_previous_player)
        {
            case GameOutcome::WIN:
            {
#if defined(DEBUG_PRINT_GAME)
                LOG("Game over, player " << PlayerToWord(g_game_state.player_to_move) << " has won!");
#endif
            }
            break;
            case GameOutcome::LOSS:
            {
#if defined(DEBUG_PRINT_GAME)
                LOG("Game over, player " << PlayerToWord(g_game_state.player_to_move) << " has won!");
#endif
            }
            break;
            case GameOutcome::DRAW:
            {
#if defined(DEBUG_PRINT_GAME)
                LOG("Game over, it's a draw!");
#endif
            }
            break;
            case GameOutcome::NONE:
            {
                g_game_state.player_to_move = g_game_state.player_to_move == Player::CIRCLE ? Player::CROSS : Player::CIRCLE;
                ++g_move_counter;
                start_time = std::chrono::steady_clock::now();
            }
            break;
        }
    }

    switch (g_game_state.outcome_for_previous_player)
    {
        case GameOutcome::WIN: {
            return (player_to_win == g_game_state.player_to_move ? GameOutcome::WIN : GameOutcome::LOSS);
        }
        break;
        case GameOutcome::LOSS: {
            return (player_to_win == g_game_state.player_to_move ? GameOutcome::LOSS : GameOutcome::WIN);
        }
        break;
        case GameOutcome::DRAW: {
            return GameOutcome::DRAW;
        }
        break;
        default: {
            UNREACHABLE_CODE;
            return GameOutcome::NONE;
        }
    }
}

int main()
{
    ofstream game_outcome_fs("debug/outcomes", ios_base::app);
    unsigned int number_of_games = 100000;
    unsigned int number_of_wins = 0;
    unsigned int number_of_losses = 0;
    unsigned int number_of_draws = 0;
    for (unsigned int current_game_count = 1;
         current_game_count <= number_of_games;
         ++current_game_count)
    {
        gen.seed(current_game_count);
        GameOutcome outcome = play_one_game(Player::CIRCLE);
        switch (outcome)
        {
            case GameOutcome::WIN: {
                ++number_of_wins;
            } break ;
            case GameOutcome::LOSS: {
                ++number_of_losses;
            } break ;
            case GameOutcome::DRAW: {
                ++number_of_draws;
            } break ;
            default: {
                UNREACHABLE_CODE;
            }
        }
        LOG("Wins: " << number_of_wins << ", Losses: " << number_of_losses << ", Draws: " << number_of_draws << ", Total games played: " << current_game_count);
        game_outcome_fs << GameOutcomeToWord(outcome) << endl;
    }
}
