#include <thread>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <cassert>
#include <functional>
#include "types.hpp"
#include "raylib.h"

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
    // TODO(david): this might not need to depend on the size of the moveset
    Move  moves[Move::NONE];
    u32   moves_left;

    void  DeleteMove(Move move);
};

void MoveSet::DeleteMove(Move move)
{
    assert(moves_left > 0);
    for (u32 move_index = 0; move_index < moves_left; ++move_index)
    {
        if (moves[move_index] == move)
        {
            assert(moves[move_index] != Move::NONE);
            moves[move_index] = moves[moves_left - 1];
            moves[moves_left - 1] = Move::NONE;
            --moves_left;
            return ;
        }
    }
    assert(false && "tried to delete a move that didn't exist");
}

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
            cur_game_state.move_to_player_map[move] = cur_game_state.player_to_move;

            cur_game_state.legal_moveset.DeleteMove(move);
        }
        // generate a random move
        else
        {
            last_move_terminal_type = TerminalType::NOT_TERMINAL;

            assert(cur_game_state.legal_moveset.moves_left > 0 && "if there aren't any more legal moves that means DetermineGameOutcome should have returned draw.. to be more precise, this is more of a stalemate position");

            u32 random_move_index = GetRandomNumber(0, cur_game_state.legal_moveset.moves_left - 1);
            Move random_move = cur_game_state.legal_moveset.moves[random_move_index];
            assert(random_move != Move::NONE);
            cur_game_state.legal_moveset.DeleteMove(random_move);

            assert(cur_game_state.move_to_player_map[random_move] == Player::NONE);
            cur_game_state.move_to_player_map[random_move] = cur_game_state.player_to_move;
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

static void InitializeGameState(GameState *game_state)
{
    *game_state = {};
    game_state->player_to_move = Player::CIRCLE;
    for (u32 move_index = 0; move_index < ArrayCount(game_state->legal_moveset.moves); ++move_index)
    {
        game_state->move_to_player_map[move_index] = Player::NONE;

        Move move = static_cast<Move>(move_index);
        game_state->legal_moveset.moves[move_index] = move;
        ++game_state->legal_moveset.moves_left;
    }
    Player previous_player = game_state->player_to_move == Player::CIRCLE ? Player::CROSS : Player::CIRCLE;
    game_state->outcome_for_previous_player = DetermineGameOutcome(*game_state, previous_player);
}

struct GameWindow
{
    u32 width;
    u32 height;
};

bool g_finished_evaluation = false;
Move g_selected_move = Move::NONE;
bool g_evaluate_thread_is_working = false;
thread g_evaluate_thread;

static void EvaluateMove(GameState *game_state, MCST *mcst, NodePool *node_pool, std::chrono::milliseconds max_evaluation_time)
{
    bool force_end_of_evaluation = false;
    bool stop_parent_sleep = false;
    Move selected_move;
    auto start_time = std::chrono::steady_clock::now();
    thread t([&selected_move, &stop_parent_sleep, &force_end_of_evaluation](GameState *game_state, MCST *mcst, NodePool *node_pool) {
        try
        {
            TIMED_BLOCK("Evaluate", selected_move = mcst->Evaluate(game_state->legal_moveset, [&stop_parent_sleep, &force_end_of_evaluation](bool found_move){
                if (found_move)
                {
                    stop_parent_sleep = true;
                    return true;
                }
                return force_end_of_evaluation;
            }, simulation_from_position, *node_pool, *game_state));
        }
        catch (exception &e)
        {
            LOG(cerr, e.what());
            exit(1);
        }
    }, game_state, mcst, node_pool);

    while (std::chrono::steady_clock::now() - start_time < max_evaluation_time)
    {
        if (stop_parent_sleep)
        {
            break;
        }
        this_thread::sleep_for(1ms);
    }
    force_end_of_evaluation = true;
    t.join();

    g_selected_move = selected_move;
    g_finished_evaluation = true;
}

static void UpdateMove(GameState *game_state, Move move)
{
    game_state->legal_moveset.DeleteMove(move);

    assert(game_state->move_to_player_map[move] == Player::NONE);
    game_state->move_to_player_map[move] = game_state->player_to_move;

    game_state->outcome_for_previous_player = DetermineGameOutcome(*game_state, game_state->player_to_move);

#if defined(DEBUG_WRITE_OUT)
    static u32 playout_counter = 0;
    ofstream playout_fs("debug/playouts/playout" + to_string(playout_counter++));

    LOG(playout_fs, PlayerToWord(game_state->player_to_move) << " moves " << MoveToWord(move) << ", move counter: " << g_move_counter);
    PrintGameState(*game_state, playout_fs);
    LOG(playout_fs, "");
    LOG(playout_fs, "");
#endif

    switch (game_state->outcome_for_previous_player)
    {
        case GameOutcome::WIN: {
            LOG(cout, "Game over, player " << PlayerToWord(game_state->player_to_move) << " has won!");
            // InitializeGameState(game_state);
        } break ;
        case GameOutcome::LOSS: {
            LOG(cout, "Game over, player " << PlayerToWord(game_state->player_to_move) << " has lost!");
            // InitializeGameState(game_state);
        } break ;
        case GameOutcome::DRAW: {
            LOG(cout, "Game over, it's a draw!");
            // InitializeGameState(game_state);
        } break ;
        case GameOutcome::NONE: {
            game_state->player_to_move = game_state->player_to_move == Player::CIRCLE ? Player::CROSS : Player::CIRCLE;
        } break ;
    }

    ++g_move_counter;
}

static void UpdateGameState(GameState *game_state, MCST *mcst, NodePool *node_pool, GameWindow *game_window)
{
    constexpr std::chrono::milliseconds max_evaluation_time = 2000ms;
    if (game_state->outcome_for_previous_player == GameOutcome::NONE)
    {
        if (game_state->player_to_move == Player::CROSS)
        {
            if (g_finished_evaluation == false)
            {

                if (g_evaluate_thread_is_working == false)
                {
                    g_selected_move = Move::NONE;
                    g_evaluate_thread_is_working = true;
                    g_evaluate_thread = thread([](GameState *game_state, MCST *mcst, NodePool *node_pool, std::chrono::milliseconds max_evaluation_time) {
                        EvaluateMove(game_state, mcst, node_pool, max_evaluation_time);
                    }, game_state, mcst, node_pool, max_evaluation_time);
                }
            }
            else
            {
                g_evaluate_thread.join();
                g_evaluate_thread_is_working = false;
                if (g_selected_move != Move::NONE)
                {
                    g_finished_evaluation = false;

                    UpdateMove(game_state, g_selected_move);
                }
            }
        }
        else
        {
            assert(game_state->player_to_move == Player::CIRCLE);
            Vector2 mouse_position = GetMousePosition();
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                u32 selected_grid_col = (u32)(mouse_position.x * 3.0f / game_window->width);
                u32 selected_grid_row = (u32)(mouse_position.y * 3.0f / game_window->height);
                u32 selected_move_index = selected_grid_row * 3 + selected_grid_col;
                Move selected_move = (Move)selected_move_index;
                if (game_state->move_to_player_map[selected_move] == Player::NONE)
                {
                    UpdateMove(game_state, selected_move);
                }
            }
        }
    }
    else
    {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            InitializeGameState(game_state);
        }
    }
}

static void RenderGameState(GameState *game_state, GameWindow *game_window)
{
    for (u32 i = 0; i < 2; ++i)
    {
        Vector2 horizontal_start = { 0.0f, (r32)game_window->height * (i + 1) / 3.0f };
        Vector2 horizontal_end = { (r32)game_window->width, (r32)game_window->height * (i + 1) / 3.0f };
        DrawLineEx(horizontal_start, horizontal_end, 5.0f, BLACK);

        Vector2 vertical_start = { (r32)game_window->width * (i + 1) / 3.0f, 0.0f };
        Vector2 vertical_end = { (r32)game_window->width * (i + 1) / 3.0f, (r32)game_window->height };
        DrawLineEx(vertical_start, vertical_end, 5.0f, BLACK);
    }

    for (u32 rows = 0; rows < 3; ++rows)
    {
        for (u32 cols = 0; cols < 3; ++cols)
        {
            u32 move_index = rows * 3 + cols;
            Vector2 grid_offset = { (r32)game_window->width / 3.0f * (r32)cols, (r32)game_window->height / 3.0f * (r32)rows };
            Vector2 grid_size   = { (r32)game_window->width / 3.0f, (r32)game_window->height / 3.0f };
            switch (game_state->move_to_player_map[move_index])
            {
                case Player::CIRCLE: {
                    DrawEllipseLines(grid_size.x / 2.0f + grid_offset.x, grid_size.y / 2.0f + grid_offset.y, grid_size.x / 2.0f, grid_size.y / 2.0f, RED);
                } break ;
                case Player::CROSS: {
                    Vector2 cross_start1 = grid_offset;
                    Vector2 cross_end1   = { grid_offset.x + grid_size.x, grid_offset.y + grid_size.y };
                    DrawLineEx(cross_start1, cross_end1, 3.0f, BLUE);

                    Vector2 cross_start2 = { grid_offset.x, grid_offset.y + grid_size.y };
                    Vector2 cross_end2   = { grid_offset.x + grid_size.x, grid_offset.y };
                    DrawLineEx(cross_start2, cross_end2, 3.0f, BLUE);
                } break ;
            }
        }
    }
}

i32 main()
{
    GameWindow game_window = { 800, 600 };
    InitWindow(game_window.width, game_window.height, "Tic-Tac-Toe");

    SetTargetFPS(60);

    constexpr NodeIndex node_pool_size = 65536;
    NodePool node_pool(node_pool_size);
    MCST mcst;

    // u32 number_of_wins = 0;
    // u32 number_of_losses = 0;
    // u32 number_of_draws = 0;
    gen.seed(0);
    GameState game_state;
    InitializeGameState(&game_state);
    while (WindowShouldClose() == false)
    {
        BeginDrawing();
        ClearBackground(WHITE);

        UpdateGameState(&game_state, &mcst, &node_pool, &game_window);
        RenderGameState(&game_state, &game_window);

        EndDrawing();
    }
    CloseWindow();
}
