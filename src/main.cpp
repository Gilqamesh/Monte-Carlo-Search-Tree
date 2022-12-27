#include <thread>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <cassert>
#include <numeric>
#include <algorithm>

#if 1
# define DEBUG_PRINT_OUT
#endif

#define LOG(msg) (cout << msg << endl)
#define LINE() (LOG(__LINE__ << " " << __FILE__))
#define UNREACHABLE_CODE (assert(false && "Invalid code path"))

#include "MCST.cpp"

static unsigned int g_seed;
int GetRandomNumber(int min, int max)
{
    // static std::random_device rd;  // Will be used to obtain a seed for the random number engine
    // static std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
    static std::mt19937 gen(g_seed);
    std::uniform_int_distribution<> dis(min, max);
    return dis(gen);
}

enum class Player
{
    CROSS,
    CIRCLE
};

string PlayerToWord(Player player)
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

using MoveToPlayerMap = unordered_map<Move, Player>;

enum class GameOutcome
{
    WIN,
    LOSS,
    DRAW,
    NONE
};

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

struct GameState
{
    MoveToPlayerMap move_to_player_map;
    Player player_to_move;
    GameOutcome outcome_for_previous_player;
    MoveSet legal_moveset;
};
GameState g_game_state;

void PrintGameState(const GameState &game_state, ostream &os)
{
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
// returns the simulation result
double simulation_from_state_once(const MoveSequence &move_sequence_from_state)
{
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

    switch (cur_world_state.outcome_for_previous_player)
    {
        case GameOutcome::WIN: {
            return player_that_needs_to_win == previous_player ? 1.0 : -1.0;
        } break ;
        case GameOutcome::LOSS: {
            return player_that_needs_to_win == previous_player ? -1.0 : 1.0;
        } break ;
        case GameOutcome::DRAW: {
            return 0.0;
        } break ;
        default:
        {
            UNREACHABLE_CODE;
            return -INFINITY;
        }
    }
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
    if (number_of_moves_available_from_position == 0)
    {
        simulation_result.is_terminal_simulation = true;
    }
    unsigned int number_of_simulations_weight = number_of_moves_available_from_position + 1;
    assert(number_of_simulations_weight > 0 );
    unsigned int number_of_simulations = 15 * number_of_simulations_weight;
    simulation_result.num_simulations = number_of_simulations;
    for (unsigned int current_simulation_count = 0;
         current_simulation_count < number_of_simulations;
         ++current_simulation_count)
    {
        double simulation_value = simulation_from_state_once(move_sequence_from_state);
        simulation_result.total_value += simulation_value;

#if defined(DEBUG_PRINT_OUT)
        g_write_out_for_simulation << simulation_value << " ";
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

int main(int argc, char **argv)
{
    if (argc > 1) {
        g_seed = atoi(argv[1]);
    }

    g_game_state.player_to_move = Player::CIRCLE;
    g_game_state.outcome_for_previous_player = GameOutcome::NONE;
    for (unsigned int row_index = 0; row_index < Move::NONE; ++row_index)
    {
        g_game_state.legal_moveset.insert(static_cast<Move>(row_index));
    }

    constexpr unsigned int total_number_of_moves = 100;
    constexpr auto max_evaluation_time = 500ms;
    auto start_time = std::chrono::steady_clock::now();

    while (g_game_state.outcome_for_previous_player == GameOutcome::NONE)
    {
        MCST mcst;
        Move selected_move;
        unsigned int number_of_simulations_ran = 0;
        bool should_terminate_simulation = false;
        // NOTE(david): use the concept of confidence interval instead of this?
        double prune_treshhold_for_node = -100.0;
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

        LOG(PlayerToWord(g_game_state.player_to_move) << " moves " << MoveToWord(selected_move) << ", number of simulations ran: " << number_of_simulations_ran << ", move counter: " << g_move_counter);
        PrintGameState(g_game_state, cout);
        LOG("");
        LOG("");

        switch (g_game_state.outcome_for_previous_player)
        {
            case GameOutcome::WIN:
            {
                LOG("Game over, player " << PlayerToWord(g_game_state.player_to_move) << " has won!");
            }
            break;
            case GameOutcome::LOSS:
            {
                LOG("Game over, player " << PlayerToWord(g_game_state.player_to_move) << " has won!");
            }
            break;
            case GameOutcome::DRAW:
            {
                LOG("Game over, it's a draw!");
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
}
