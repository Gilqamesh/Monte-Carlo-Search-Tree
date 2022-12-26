#include <thread>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <cassert>
#include <numeric>
#include <algorithm>

#define LOG(msg) (cout << msg << endl)
#define LINE() (LOG(__LINE__ << " " << __FILE__))
#define UNREACHABLE_CODE (assert(false && "Invalid code path"))

#include "MCST.cpp"

#include <random>

int GetRandomNumber(int min, int max)
{
    static std::random_device rd;  // Will be used to obtain a seed for the random number engine
    static std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<> dis(min, max);
    return dis(gen);
}

enum PlayerSymbol
{
    CROSS_SYMBOL,
    CIRCLE_SYMBOL,
    NO_WINNER,
    DRAW_OUTCOME
};
typedef unordered_map<Move, PlayerSymbol> GameState;
PlayerSymbol DetermineGameOutcome(const GameState &world_state)
{
    bool IsDraw = true;
    for (int row_orientation = 0; row_orientation < 2; ++row_orientation)
    {
        int row_index_term = (row_orientation == 0) ? 1 : 3;
        int col_index_term = (row_orientation == 0) ? 3 : 1;
        for (int row_index = 0; row_index < 3; ++row_index)
        {
            std::vector<Move> legal_moves;
            for (int column_index = 0; column_index < 3; ++column_index)
            {
                legal_moves.push_back(static_cast<Move>(row_index_term * row_index + col_index_term * column_index));
            }

            // TODO: could check for all PlayerSymbol in one iteration, as if one symbol isn't the matched one, it automatically turns the corresponding boolean to false
            bool all_crosses = std::all_of(legal_moves.begin(), legal_moves.end(), [&world_state](Move lm) {
                auto it = world_state.find(lm);
                return it != world_state.end() && it->second == CROSS_SYMBOL;
            });

            bool all_circles = std::all_of(legal_moves.begin(), legal_moves.end(), [&world_state](Move lm) {
                auto it = world_state.find(lm);
                return it != world_state.end() && it->second == CIRCLE_SYMBOL;
            });

            // TODO: the empty slots only need to be checked once, consider moving this to separate logic
            bool any_empty = std::any_of(legal_moves.begin(), legal_moves.end(), [&world_state](Move lm) {
                return world_state.count(lm) == 0;
            });

            if (all_crosses) return CROSS_SYMBOL;
            if (all_circles) return CIRCLE_SYMBOL;
            if (any_empty) IsDraw = false;
        }
    }

    if (IsDraw) return DRAW_OUTCOME;

    // check diagonals
    for (int direction = 0; direction < 2; ++direction)
    {
        int a = (direction == 0) ? 4 : 0;
        int b = (direction == 0) ? 0 : 2;
        std::vector<Move> legal_moves;
        for (int i = 0; i < 3; ++i)
        {
            legal_moves.push_back(static_cast<Move>(i * a + b * (i + 1)));
        }
        bool all_crosses = std::all_of(legal_moves.begin(), legal_moves.end(), [&world_state](Move lm) {
            auto it = world_state.find(lm);
            return it != world_state.end() && it->second == CROSS_SYMBOL;
        });

        bool all_circles = std::all_of(legal_moves.begin(), legal_moves.end(), [&world_state](Move lm) {
            auto it = world_state.find(lm);
            return it != world_state.end() && it->second == CIRCLE_SYMBOL;
        });

        if (all_crosses) return CROSS_SYMBOL;
        if (all_circles) return CIRCLE_SYMBOL;
    }

    return NO_WINNER;
}

MoveSet g_legal_moveset;
void ProcessMove(MoveSet &available_moves, Move move)
{
    assert(available_moves.count(move));
    available_moves.erase(move);
}

Move g_previous_move;
PlayerSymbol g_cur_player;
GameState g_world_state;
bool g_is_game_over;

// returns the simulation result
double simulation_from_state_once(const MoveChain &move_chain_from_world_state)
{
    GameState cur_world_state = g_world_state;

    // copy current position and set the possible moves
    MoveSet cur_legal_moves;
    for (int i = 0; i < Move::NONE; ++i)
    {
        Move move = static_cast<Move>(i);
        if (cur_world_state.count(move) == 0)
        {
            cur_legal_moves.insert(move);
        }
    }

    PlayerSymbol cur_player = g_cur_player;
    PlayerSymbol is_terminal_world_state = DetermineGameOutcome(cur_world_state);
    if (is_terminal_world_state == NO_WINNER) {
        cur_player = (cur_player == CIRCLE_SYMBOL) ? CROSS_SYMBOL : CIRCLE_SYMBOL;
    }

    auto move_it = move_chain_from_world_state.begin();
    // make moves to arrive at the position and simulate the rest of the game
    while (is_terminal_world_state == NO_WINNER) {
        cur_player = (cur_player == CIRCLE_SYMBOL) ? CROSS_SYMBOL : CIRCLE_SYMBOL;
        // make a move from the move chain
        if (move_it != move_chain_from_world_state.end()) {
            auto move = *move_it++;
            cur_legal_moves.erase(move);
            assert(cur_world_state.count(move) == 0);
            cur_world_state.insert({move, cur_player});
        }
        // generate a random move
        else {
            assert(cur_legal_moves.empty() == false && "if there aren't any more legal moves that means DetermineGameOutcome should have returned draw");

            unsigned int random_move = GetRandomNumber(0, cur_legal_moves.size() - 1);
            auto it = cur_legal_moves.begin();
            std::advance(it, random_move);
            cur_world_state.insert({*it, cur_player});
            cur_legal_moves.erase(it);
        }

        is_terminal_world_state = DetermineGameOutcome(cur_world_state);
    }

    switch (is_terminal_world_state)
    {
        case CROSS_SYMBOL:
        case CIRCLE_SYMBOL: return (cur_player == g_cur_player) * 2.0 - 1.0;
        case DRAW_OUTCOME: return 0.0;
        default:
        {
            UNREACHABLE_CODE;
            return -INFINITY;
        }
    }
}

SimulationResult simulation_from_state(const MoveChain &move_chain_from_world_state)
{
    SimulationResult simulation_result = {};

    unsigned int number_of_simulations = 1;
    simulation_result.num_simulations = number_of_simulations;
    for (unsigned int current_simulation_count = 0;
         current_simulation_count < number_of_simulations;
         ++current_simulation_count)
    {
        simulation_result.total_value += simulation_from_state_once(move_chain_from_world_state);
    }

    return simulation_result;
}

UtilityEstimationResult utility_estimator(const MoveChain &move_chain_from_world_state)
{
    UtilityEstimationResult utility_estimation_result = {};

    // GameState cur_world_state = g_world_state;

    // // initialize the legal moveset from the moves that aren't in the current world state
    // MoveSet cur_legal_moves;
    // for (int i = 0; i < Move::NONE; ++i)
    // {
    //     Move move = static_cast<Move>(i);
    //     if (cur_world_state.count(move) == 0)
    //     {
    //         cur_legal_moves.insert(move);
    //     }
    // }

    // // update current world state and current legal moves from the legal_moves chain
    // PlayerSymbol cur_player = g_cur_player;
    // for (auto move : move_chain_from_world_state)
    // {
    //     cur_legal_moves.erase(move);
    //     assert(cur_world_state.count(move) == 0);
    //     cur_world_state.insert({move, cur_player});

    //     cur_player = (cur_player == CIRCLE_SYMBOL) ? CROSS_SYMBOL : CIRCLE_SYMBOL;
    // }

    // heuristic: number of threats

    utility_estimation_result.should_prune = false;

    return utility_estimation_result;
}

bool should_terminate = false;
bool terminate_condition(bool terminate_caller)
{
    g_is_game_over = terminate_caller;

    return should_terminate;
}

void PrintGameState(void)
{
    for (unsigned int row = 0; row < 3; ++row)
    {
        for (unsigned int column = 0; column < 3; ++column)
        {
            Move inspected_move = static_cast<Move>(row * 3 + column);
            PlayerSymbol player_that_made_move = g_world_state.count(inspected_move) ? g_world_state[inspected_move] : NO_WINNER;
            cout << (player_that_made_move == CIRCLE_SYMBOL ? "O" : (player_that_made_move == CROSS_SYMBOL ? "X" : ".")) << " ";
        }
        cout << endl;
    }
}

string PlayerToWord(PlayerSymbol player)
{
    switch (player)
    {
    case CIRCLE_SYMBOL:
        return "CIRCLE_SYMBOL";
    case CROSS_SYMBOL:
        return "CROSS_SYMBOL";
    default:
    {
        UNREACHABLE_CODE;
        return "Breh";
    }
    }
}

int main()
{
    srand(time(NULL));
    g_previous_move = Move::NONE;
    g_cur_player = CIRCLE_SYMBOL;
    for (unsigned int row_index = 0; row_index < Move::NONE; ++row_index)
    {
        g_legal_moveset.insert(static_cast<Move>(row_index));
    }

    constexpr unsigned int total_number_of_moves = 100;
    constexpr auto max_evaluation_time = 500ms;
    auto start_time = std::chrono::steady_clock::now();

    for (unsigned int move_counter = 0;
         move_counter < total_number_of_moves;
         ++move_counter)
    {
        should_terminate = false;
        MCST mcst;
        Move selected_move;
        unsigned int number_of_simulations_ran = 0;
        thread t([&]() {
            try
            {
                selected_move = mcst.Evaluate(g_legal_moveset, terminate_condition, simulation_from_state, ProcessMove, utility_estimator);
                number_of_simulations_ran = mcst.NumberOfSimulationsRan();
            }
            catch (exception &e)
            {
                LOG(e.what());
            }
        });
        while (std::chrono::steady_clock::now() - start_time < max_evaluation_time)
        {
            if (should_terminate)
            {
                break;
            }
            this_thread::sleep_for(1ms);
        }
        should_terminate = true;
        t.join();

        if (g_is_game_over == false)
        {
            g_legal_moveset.erase(selected_move);
            g_world_state.insert({selected_move, g_cur_player});
        }

        LOG("PlayerSymbol " << PlayerToWord(g_cur_player) << " moves " << MoveToWord(selected_move)
                            << ", number of simulations ran: " << number_of_simulations_ran << ", move counter: " << g_move_counter);
        PrintGameState();
        LOG("");
        LOG("");

        PlayerSymbol player = DetermineGameOutcome(g_world_state);
        switch (player)
        {
            case CIRCLE_SYMBOL:
            case CROSS_SYMBOL:
            case DRAW_OUTCOME:
            {
                g_is_game_over = true;
            }
        }

        if (g_is_game_over == true)
        {
            if (player == DRAW_OUTCOME)
            {
                LOG("Game over, it's a draw!");
            }
            else
            {
                LOG("Game over, player " << PlayerToWord(g_cur_player) << " has won!");
            }
            break;
        }

        g_cur_player = (g_cur_player == CIRCLE_SYMBOL) ? CROSS_SYMBOL : CIRCLE_SYMBOL;
        g_previous_move = selected_move;
        ++g_move_counter;
        start_time = std::chrono::steady_clock::now();
    }
}
