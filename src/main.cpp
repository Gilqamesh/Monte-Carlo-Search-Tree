#include <thread>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#if 0
# define NDEBUG
#endif
#include <random>
#include <stdexcept>
#include <cassert>
#include <functional>
#include <sstream>
#include "types.hpp"
#include "raylib.h"
#include <intrin.h>

using namespace std;

constexpr u32 GRID_DIM_ROW = 7;
constexpr u32 GRID_DIM_COL = 7;
constexpr u32 ConnectToWinCount = 5;
constexpr std::chrono::milliseconds max_evaluation_time = 5000ms;

#if 1
# define DEBUG_TIME
#endif

#if 1
# define DEBUG_WRITE_OUT
#endif

#if 0
# define DEBUG_WRITE_OUT_SIM_RESULT
#endif

#if 0
# define DEBUG_PRINT
#endif

#define ArrayCount(array) (sizeof(array) / sizeof((array)[0]))

#define LOG(os, msg) (os << msg << endl)
#define LOGN(os, msg) (os << msg)
#define LOGV(os, msg) (os << msg << " - " << __LINE__ << " " << __FILE__ << endl)
#define LOGVN(os, msg) (os << msg << " - " << __LINE__ << " " << __FILE__)
#define UNREACHABLE_CODE (assert(false && "Invalid code path"))

enum class JobNames
{
    Evaluate,
    Selection,
    Simulation,
    BackPropagate,
    SelectBestChild,
    DetermineGameOutcomeDuringSimulation,
    DetermineGameOutcomeAfterMoveDuringSimulation,
    DeleteMoveDuringSimulation,
    GetPlayerDuringSimulation,
    AddPlayerDuringSimulation,
    InitializeRandomNumberSequenceDuringSimulation,
    GetRandomNumberDuringSimulation,
    PopMoveAtIndexDuringSimulation,
    SimulationFromPositionOnce,

    JobNamesSize
};

// TODO(david): store these in some global storage and write them out in a file at some point
#if defined (DEBUG_TIME)
struct TimedBlocks
{
    struct
    {
        // TODO(david): for no reason other than I need a unique clock variable in the TIMED_BLOCK macro, as I'd like the job name to be scoped, so I can't use that as the varname
        u64 unique_clock;
        u64 total_elapsed_number_of_clock_cycles;
        u32 counter_since_last_clear;
    } timed_results[JobNames::JobNamesSize];
};
static TimedBlocks g_timed_blocks = {};

constexpr r64 processor_clock_cycles_per_second = 2.11 * 1000000000;
# define TIMED_BLOCK(job_expression, scoped_job_name) \
    assert((u32)scoped_job_name < ArrayCount(g_timed_blocks.timed_results));\
    g_timed_blocks.timed_results[(u32)scoped_job_name].unique_clock = __rdtsc(); \
    job_expression; \
    g_timed_blocks.timed_results[(u32)scoped_job_name].total_elapsed_number_of_clock_cycles += __rdtsc() - g_timed_blocks.timed_results[(u32)scoped_job_name].unique_clock;\
    g_timed_blocks.timed_results[(u32)scoped_job_name].counter_since_last_clear++;

string NumberToPrettyFormat(string str)
{
    return str;
}

string NumberToPrettyFormat(r64 number)
{
    static string prefixes[] = {
        "(G)",
        "(M)",
        "(k)",
        "(1)",
        "(m)",
        "(u)",
        "(n)"
    };
    u32 prefix_index = 3;
    while (number >= 1000.0)
    {
        number /= 1000.0;
        assert(prefix_index > 0 && "didn't take care of bigger prefix than Giga");
        --prefix_index;
    }
    while (number < 1.0)
    {
        number *= 1000.0;
        assert(prefix_index < ArrayCount(prefixes) - 1 && "didn't take care of lower prefix than nano");
        ++prefix_index;
    }
    assert(prefix_index < ArrayCount(prefixes));
    ostringstream ss;
    ss << fixed << setprecision(2) << number;
    return ss.str() + prefixes[prefix_index];
}

# define NONAPI_LOG_JOB_FORMAT(os, job_name, total_elapsed_time, total_clock_cycles, number_of_samples, elapsed_time_for_one, clock_cycles_for_one) \
    LOG(os, setw(50) << job_name << ": " << setw(20) << NumberToPrettyFormat(total_elapsed_time) << " | " << setw(20) << NumberToPrettyFormat(total_clock_cycles) << " | " << setw(20) << NumberToPrettyFormat(number_of_samples) << " | " << setw(20) << NumberToPrettyFormat(elapsed_time_for_one) << " | " << setw(20) << NumberToPrettyFormat(clock_cycles_for_one));
# define NONAPI_LOG_JOB_SCOPED(os, job, job_non_scoped) \
    assert((u32)job < ArrayCount(g_timed_blocks.timed_results));\
    if (g_timed_blocks.timed_results[(u32)job].counter_since_last_clear > 0)\
    {\
        r64 clock_cycles = (r64)g_timed_blocks.timed_results[(u32)job].total_elapsed_number_of_clock_cycles;\
        u32 sample_count = g_timed_blocks.timed_results[(u32)job].counter_since_last_clear;\
        NONAPI_LOG_JOB_FORMAT(os, #job_non_scoped, clock_cycles / processor_clock_cycles_per_second, clock_cycles, sample_count, clock_cycles / processor_clock_cycles_per_second / (r64)sample_count, clock_cycles / (r64)sample_count);\
    }
# define NONAPI_LOG_JOB_NON_SCOPED(os, job) NONAPI_LOG_JOB_SCOPED(os, JobNames::job, job)

// TODO(david): is there a way to iterate over the enumeration here?
# define LOG_JOBS(os) \
    ios::fmtflags old_os_flags = os.flags();\
    os << fixed << setprecision(3);\
    LOG(os, string(82, '-') + "== TIMED JOBS ==" + string(82, '-'));\
    NONAPI_LOG_JOB_FORMAT(os, "Job name", "Total elapsed time", "Total clock cycles", "Number of samples", "Elapsed time for one", "Clock cycles for one");\
    NONAPI_LOG_JOB_NON_SCOPED(os, Evaluate);\
    NONAPI_LOG_JOB_NON_SCOPED(os, Selection);\
    NONAPI_LOG_JOB_NON_SCOPED(os, Simulation);\
    NONAPI_LOG_JOB_NON_SCOPED(os, BackPropagate);\
    NONAPI_LOG_JOB_NON_SCOPED(os, SelectBestChild);\
    NONAPI_LOG_JOB_NON_SCOPED(os, DetermineGameOutcomeDuringSimulation);\
    NONAPI_LOG_JOB_NON_SCOPED(os, DetermineGameOutcomeAfterMoveDuringSimulation);\
    NONAPI_LOG_JOB_NON_SCOPED(os, DeleteMoveDuringSimulation);\
    NONAPI_LOG_JOB_NON_SCOPED(os, GetPlayerDuringSimulation);\
    NONAPI_LOG_JOB_NON_SCOPED(os, AddPlayerDuringSimulation);\
    NONAPI_LOG_JOB_NON_SCOPED(os, InitializeRandomNumberSequenceDuringSimulation);\
    NONAPI_LOG_JOB_NON_SCOPED(os, GetRandomNumberDuringSimulation);\
    NONAPI_LOG_JOB_NON_SCOPED(os, PopMoveAtIndexDuringSimulation);\
    NONAPI_LOG_JOB_NON_SCOPED(os, SimulationFromPositionOnce);\
    LOG(os, string(181, '-'));\
    os.flags(old_os_flags);
# define LOG_JOB(os, job_name) \
    ios::fmtflags old_os_flags = os.flags();\
    os << fixed << setprecision(3);\
    NONAPI_LOG_JOB_SCOPED(os, job_name, job_name);\
    os.flags(old_os_flags)

# define CLEAR_JOBS memset(&g_timed_blocks, 0, sizeof(g_timed_blocks))

#else
# define TIMED_BLOCK(job_name, job_expression) job_expression
# define LOG_JOBS(os)
# define LOG_JOB(os, job_name)
# define CLEAR_JOBS
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

struct Move
{
    // TODO(david): 1 param uniquely identifies the move, no need for two
    // NOTE(david): first param
    u32 row;
    // NOTE(david): second param
    u32 col;

    bool IsValid(void) const;
    void Invalidate(void);
    u32 GetIndex(void) const;

    static Move MoveFromIndex(u32 move_index);
};

Move Move::MoveFromIndex(u32 move_index)
{
    Move result = {};

    result.row = move_index / GRID_DIM_COL;
    result.col = move_index - result.row * GRID_DIM_COL;

    return result;
}

bool operator==(const Move &a, const Move &b)
{
    return (a.row == b.row && a.col == b.col);
}

bool operator!=(const Move &a, const Move &b)
{
    return (a.row != b.row || a.col != b.col);
}

bool operator<(const Move &a, const Move &b)
{
    return a.GetIndex() < b.GetIndex();
}

u32 Move::GetIndex(void) const
{
    u32 move_index = row * GRID_DIM_COL + col;

    return move_index;
}

bool Move::IsValid(void) const
{
    return (row < GRID_DIM_ROW && col < GRID_DIM_COL);
}

void Move::Invalidate(void)
{
    row = GRID_DIM_ROW;
    col = GRID_DIM_COL;
}

enum class GameOutcome
{
    WIN,
    LOSS,
    DRAW,
    NONE
};

// NOTE(david): this doesn't apply to all games, but for board games where each grid is taken by 1 player is fine for now
struct MoveToPlayerMap
{
    Player map[GRID_DIM_COL * GRID_DIM_ROW];
    u32 available_grids;

    Player GetPlayer(u32 row, u32 col) const;
    Player GetPlayer(Move move) const;
    void   AddPlayer(Move move, Player player);
    void   Clear(void);
    bool   IsFull(void);
};

bool MoveToPlayerMap::IsFull(void)
{
    return available_grids == 0;
}

Player MoveToPlayerMap::GetPlayer(u32 row, u32 col) const
{
    Move move = { row, col };
    u32 map_index = move.GetIndex();
    assert(map_index < ArrayCount(MoveToPlayerMap::map));
    return map[map_index];
}

Player MoveToPlayerMap::GetPlayer(Move move) const
{
    u32 map_index = move.GetIndex();
    assert(map_index < ArrayCount(MoveToPlayerMap::map));
    return map[map_index];
}

void MoveToPlayerMap::AddPlayer(Move move, Player player)
{
    u32 map_index = move.GetIndex();
    assert(available_grids > 0);
    --available_grids;
    assert(map_index < ArrayCount(MoveToPlayerMap::map));
    map[map_index] = player;
}

void MoveToPlayerMap::Clear(void)
{
    available_grids = GRID_DIM_ROW * GRID_DIM_COL;
    for (u32 row = 0; row < GRID_DIM_ROW; ++row)
    {
        for (u32 col = 0; col < GRID_DIM_COL; ++col)
        {
            Move move = { row, col };
            u32 map_index = move.GetIndex();
            map[map_index] = Player::NONE;
        }
    }
}

struct MoveSet
{
    // TODO(david): may want a dynamic size structure as this can get big or may want to store the action space differently and with more domain knowledge
    Move  moves[GRID_DIM_ROW * GRID_DIM_COL];
    u32   moves_left;

    void  DeleteMove(Move move);
    void  AddMove(Move move);
    void  Clear(void);
};

void MoveSet::Clear(void)
{
    for (u32 move_index = 0; move_index < ArrayCount(moves); ++move_index)
    {
        moves[move_index].Invalidate();
    }
}

void MoveSet::DeleteMove(Move move)
{
    assert(moves_left > 0);
    --moves_left;
    assert(move.IsValid());
    u32 move_index = move.GetIndex();
    assert(moves[move_index].IsValid());
    moves[move_index].Invalidate();
    // for (u32 move_index = 0; move_index < moves_left; ++move_index)
    // {
    //     if (moves[move_index] == move)
    //     {
    //         assert(moves[move_index].IsValid());
    //         moves[move_index] = moves[moves_left - 1];
    //         moves[moves_left - 1].Invalidate();
    //         --moves_left;
    //         return ;
    //     }
    // }
    // assert(false && "tried to delete a move that didn't exist");
}

void MoveSet::AddMove(Move move)
{
    assert(move.IsValid());
    u32 move_index = move.GetIndex();
    assert(move_index < ArrayCount(MoveSet::moves));
    assert(moves[move_index].IsValid() == false);
    moves[move_index] = move;
    ++moves_left;
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
    for (u32 row = 0; row < GRID_DIM_ROW; ++row)
    {
        for (u32 col = 0; col < GRID_DIM_COL; ++col)
        {
            Player player_that_made_move = game_state.move_to_player_map.GetPlayer(row, col);
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

// ASSUMPTION(david): prior GameState before the move was NONE, we want to see if that changed
GameOutcome DetermineGameOutcomeAfterMove(GameState &game_state, Player player_to_move_and_win, Move last_move)
{
    assert(game_state.move_to_player_map.GetPlayer(last_move) == player_to_move_and_win);
    // check all 8 directions
    u32 north_counter = 0;
    for (Move move = { last_move.row - 1, last_move.col }; ; --move.row, ++north_counter)
    {
        if (move.IsValid() == false || game_state.move_to_player_map.GetPlayer(move) != player_to_move_and_win)
        {
            break ;
        }
    }
    u32 south_counter = 0;
    for (Move move = { last_move.row + 1, last_move.col }; ; ++move.row, ++south_counter)
    {
        if (move.IsValid() == false || game_state.move_to_player_map.GetPlayer(move) != player_to_move_and_win)
        {
            break ;
        }
    }

    if (north_counter + south_counter + 1 == ConnectToWinCount)
    {
        return GameOutcome::WIN;
    }

    u32 west_counter = 0;
    for (Move move = { last_move.row, last_move.col - 1 }; ; --move.col, ++west_counter)
    {
        if (move.IsValid() == false || game_state.move_to_player_map.GetPlayer(move) != player_to_move_and_win)
        {
            break ;
        }
    }
    u32 east_counter = 0;
    for (Move move = { last_move.row, last_move.col + 1 }; ; ++move.col, ++east_counter)
    {
        if (move.IsValid() == false || game_state.move_to_player_map.GetPlayer(move) != player_to_move_and_win)
        {
            break ;
        }
    }

    if (west_counter + east_counter + 1 == ConnectToWinCount)
    {
        return GameOutcome::WIN;
    }

    u32 north_west_counter = 0;
    for (Move move = { last_move.row - 1, last_move.col - 1 }; ; --move.row, --move.col, ++north_west_counter)
    {
        if (move.IsValid() == false || game_state.move_to_player_map.GetPlayer(move) != player_to_move_and_win)
        {
            break ;
        }
    }
    u32 south_east_counter = 0;
    for (Move move = { last_move.row + 1, last_move.col + 1 }; ; ++move.row, ++move.col, ++south_east_counter)
    {
        if (move.IsValid() == false || game_state.move_to_player_map.GetPlayer(move) != player_to_move_and_win)
        {
            break ;
        }
    }

    if (north_west_counter + south_east_counter + 1 == ConnectToWinCount)
    {
        return GameOutcome::WIN;
    }

    u32 north_east_counter = 0;
    for (Move move = { last_move.row - 1, last_move.col + 1 }; ; --move.row, ++move.col, ++north_east_counter)
    {
        if (move.IsValid() == false || game_state.move_to_player_map.GetPlayer(move) != player_to_move_and_win)
        {
            break ;
        }
    }
    u32 south_west_counter = 0;
    for (Move move = { last_move.row + 1, last_move.col - 1 }; ; ++move.row, --move.col, ++south_west_counter)
    {
        if (move.IsValid() == false || game_state.move_to_player_map.GetPlayer(move) != player_to_move_and_win)
        {
            break ;
        }
    }

    if (north_east_counter + south_west_counter + 1 == ConnectToWinCount)
    {
        return GameOutcome::WIN;
    }

    if (game_state.move_to_player_map.IsFull())
    {
        return GameOutcome::DRAW;
    }

    return GameOutcome::NONE;
}

GameOutcome DetermineGameOutcome(GameState &game_state, Player player_to_win)
{
    // check rows/cols
    for (u32 row = 0; row < GRID_DIM_ROW; ++row)
    {
        u32 cur_circles = 0;
        u32 cur_crosses = 0;
        for (u32 col = 0; col < GRID_DIM_COL; ++col)
        {
            Player cur_player = game_state.move_to_player_map.GetPlayer(row, col);
            if (cur_player == Player::CIRCLE)
            {
                if (++cur_circles == ConnectToWinCount)
                {
                    return player_to_win == Player::CIRCLE ? GameOutcome::WIN : GameOutcome::LOSS;
                }
                cur_crosses = 0;
            }
            else if (cur_player == Player::CROSS)
            {
                if (++cur_crosses == ConnectToWinCount)
                {
                    return player_to_win == Player::CROSS ? GameOutcome::WIN : GameOutcome::LOSS;
                }
                cur_circles = 0;
            }
            else
            {
                cur_crosses = 0;
                cur_circles = 0;
            }
        }
    }
    for (u32 col = 0; col < GRID_DIM_COL; ++col)
    {
        u32 cur_circles = 0;
        u32 cur_crosses = 0;
        for (u32 row = 0; row < GRID_DIM_ROW; ++row)
        {
            Player cur_player = game_state.move_to_player_map.GetPlayer(row, col);
            if (cur_player == Player::CIRCLE)
            {
                if (++cur_circles == ConnectToWinCount)
                {
                    return player_to_win == Player::CIRCLE ? GameOutcome::WIN : GameOutcome::LOSS;
                }
                cur_crosses = 0;
            }
            else if (cur_player == Player::CROSS)
            {
                if (++cur_crosses == ConnectToWinCount)
                {
                    return player_to_win == Player::CROSS ? GameOutcome::WIN : GameOutcome::LOSS;
                }
                cur_circles = 0;
            }
            else
            {
                cur_crosses = 0;
                cur_circles = 0;
            }
        }
    }

    // check diagonals
    for (u32 start_col = 0, start_row_offset = 0; start_col < GRID_DIM_COL || start_row_offset < GRID_DIM_ROW; ++start_col)
    {
        if (start_col == GRID_DIM_COL)
        {
            start_col = GRID_DIM_COL - 1;
            ++start_row_offset;
        }
        u32 cur_circles = 0;
        u32 cur_crosses = 0;
        for (i32 col = start_col, row = GRID_DIM_ROW - 1 - start_row_offset;
             col >= 0 && row >= 0;
             --col, --row)
        {
            Player cur_player = game_state.move_to_player_map.GetPlayer(row, col);
            if (cur_player == Player::CIRCLE)
            {
                if (++cur_circles == ConnectToWinCount)
                {
                    return player_to_win == Player::CIRCLE ? GameOutcome::WIN : GameOutcome::LOSS;
                }
                cur_crosses = 0;
            }
            else if (cur_player == Player::CROSS)
            {
                if (++cur_crosses == ConnectToWinCount)
                {
                    return player_to_win == Player::CROSS ? GameOutcome::WIN : GameOutcome::LOSS;
                }
                cur_circles = 0;
            }
            else
            {
                cur_crosses = 0;
                cur_circles = 0;
            }
        }

        cur_circles = 0;
        cur_crosses = 0;
        for (i32 col = start_col, row = start_row_offset;
             col >= 0 && row < GRID_DIM_ROW;
             --col, ++row)
        {
            Player cur_player = game_state.move_to_player_map.GetPlayer(row, col);
            if (cur_player == Player::CIRCLE)
            {
                if (++cur_circles == ConnectToWinCount)
                {
                    return player_to_win == Player::CIRCLE ? GameOutcome::WIN : GameOutcome::LOSS;
                }
                cur_crosses = 0;
            }
            else if (cur_player == Player::CROSS)
            {
                if (++cur_crosses == ConnectToWinCount)
                {
                    return player_to_win == Player::CROSS ? GameOutcome::WIN : GameOutcome::LOSS;
                }
                cur_circles = 0;
            }
            else
            {
                cur_crosses = 0;
                cur_circles = 0;
            }
        }
    }

    if (game_state.move_to_player_map.IsFull())
    {
        return GameOutcome::DRAW;
    }

    return GameOutcome::NONE;
}

bool g_should_write_out_simulation;
ofstream g_simresult_fs;

SimulationResult simulation_from_position_once(const MoveSequence<max_move_chain_depth> &movesequence_from_position, const GameState &game_state, Node *node, const NodePool &node_pool)
{
    GameState cur_game_state = game_state;
    Player player_that_needs_to_win = cur_game_state.player_to_move;
    SimulationResult simulation_result = {};

#if defined(DEBUG_WRITE_OUT_SIM_RESULT)
    if (g_should_write_out_simulation)
    {
        LOG(g_simresult_fs, "Player about to move: " << PlayerToWord(cur_game_state.player_to_move));
        PrintGameState(cur_game_state, g_simresult_fs);
        LOGN(g_simresult_fs, "move sequence from state: ");
        for (u32 movesequence_index = 0; movesequence_index < movesequence_from_position.number_of_moves; ++movesequence_index)
        {
            Move move = movesequence_from_position.moves[movesequence_index];
            assert(move.IsValid());
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
    TIMED_BLOCK(cur_game_state.outcome_for_previous_player = DetermineGameOutcome(cur_game_state, last_player_to_move), JobNames::DetermineGameOutcomeDuringSimulation);

    bool initialized_legal_move_sequence = false;
    MoveSequence<GRID_DIM_COL * GRID_DIM_ROW> legal_move_sequence = {};

    while (cur_game_state.outcome_for_previous_player == GameOutcome::NONE)
    {
        Move last_move;
        last_move.Invalidate();
        // make a move from the move chain
        if (movesequence_index < movesequence_from_position.moves_left)
        {
            last_move = movesequence_from_position.moves[movesequence_index++];
            
        }
        // generate a random move
        else
        {
            if (initialized_legal_move_sequence == false)
            {
                TIMED_BLOCK(
                    initialized_legal_move_sequence = true;
                    for (u32 move_index = 0; move_index < ArrayCount(cur_game_state.legal_moveset.moves); ++move_index)
                    {
                        if (cur_game_state.legal_moveset.moves[move_index].IsValid())
                        {
                            legal_move_sequence.AddMove(cur_game_state.legal_moveset.moves[move_index]);
                        }
                    }
                    , JobNames::InitializeRandomNumberSequenceDuringSimulation
                );
            }
            last_move_terminal_type = TerminalType::NOT_TERMINAL;

            assert(legal_move_sequence.moves_left > 0 && "if there aren't any more legal moves that means DetermineGameOutcomeAfterMove should have returned draw.. to be more precise, this is more of a stalemate position");

            TIMED_BLOCK(u32 random_move_index = GetRandomNumber(0, legal_move_sequence.moves_left - 1), JobNames::GetRandomNumberDuringSimulation);
            TIMED_BLOCK(last_move = legal_move_sequence.PopMoveAtIndex(random_move_index), JobNames::PopMoveAtIndexDuringSimulation);
        }
        assert(last_move.IsValid());

        TIMED_BLOCK(cur_game_state.legal_moveset.DeleteMove(last_move), JobNames::DeleteMoveDuringSimulation);

        TIMED_BLOCK(assert(cur_game_state.move_to_player_map.GetPlayer(last_move) == Player::NONE), JobNames::GetPlayerDuringSimulation);
        TIMED_BLOCK(cur_game_state.move_to_player_map.AddPlayer(last_move, cur_game_state.player_to_move), JobNames::AddPlayerDuringSimulation);

        last_player_to_move = cur_game_state.player_to_move;

        TIMED_BLOCK(cur_game_state.outcome_for_previous_player = DetermineGameOutcomeAfterMove(cur_game_state, last_player_to_move, last_move), JobNames::DetermineGameOutcomeAfterMoveDuringSimulation);

        cur_game_state.player_to_move = (cur_game_state.player_to_move == Player::CIRCLE) ? Player::CROSS : Player::CIRCLE;
    }

    simulation_result.num_simulations = 1;
    assert(player_that_needs_to_win == Player::CROSS && "below values are only for uncontrolled node");
    switch (cur_game_state.outcome_for_previous_player)
    {
        // TODO(david): define a terminal interval for values, as there can be other values than winning and losing
        // NOTE(david): maybe there is no winning/losing terminal type, only values
        case GameOutcome::WIN: {
            switch (player_that_needs_to_win)
            {
                case Player::CIRCLE: {
                    simulation_result.value = player_that_needs_to_win == last_player_to_move ? 1.0 : -1.0;
                    if (last_move_terminal_type != TerminalType::NOT_TERMINAL)
                    {
                        node->terminal_info.terminal_type = player_that_needs_to_win == last_player_to_move ? TerminalType::WINNING : TerminalType::LOSING;
                    }
                } break ;
                case Player::CROSS: {
                    // TODO(david): opposing player wins, it doesn't have to be cross if we don't start with cross
                    simulation_result.value = player_that_needs_to_win == last_player_to_move ? -1.0 : 1.0;
                    if (last_move_terminal_type != TerminalType::NOT_TERMINAL)
                    {
                        node->terminal_info.terminal_type = player_that_needs_to_win == last_player_to_move ? TerminalType::LOSING : TerminalType::WINNING;
                    }
                } break ;
                default: UNREACHABLE_CODE;
            }
        } break ;
        case GameOutcome::LOSS: {
            switch (player_that_needs_to_win)
            {
                case Player::CIRCLE: {
                    simulation_result.value = player_that_needs_to_win == last_player_to_move ? -1.0 : 1.0;
                    if (last_move_terminal_type != TerminalType::NOT_TERMINAL)
                    {
                        node->terminal_info.terminal_type = player_that_needs_to_win == last_player_to_move ? TerminalType::LOSING : TerminalType::WINNING;
                    }
                } break ;
                case Player::CROSS: {
                    // TODO(david): opposing player loses, it doesn't have to be cross if we don't start with cross
                    simulation_result.value = player_that_needs_to_win == last_player_to_move ? 1.0 : -1.0;
                    if (last_move_terminal_type != TerminalType::NOT_TERMINAL)
                    {
                        node->terminal_info.terminal_type = player_that_needs_to_win == last_player_to_move ? TerminalType::WINNING : TerminalType::LOSING;
                    }

                } break ;
                default: UNREACHABLE_CODE;
            }
        } break ;
        case GameOutcome::DRAW: {
            switch (player_that_needs_to_win)
            {
                case Player::CIRCLE: {
                    simulation_result.value = 0.0;
                    if (last_move_terminal_type != TerminalType::NOT_TERMINAL)
                    {
                        node->terminal_info.terminal_type = TerminalType::NEUTRAL;
                    }
                } break ;
                case Player::CROSS: {
                    simulation_result.value = 0.0;
                    if (last_move_terminal_type != TerminalType::NOT_TERMINAL)
                    {
                        node->terminal_info.terminal_type = TerminalType::NEUTRAL;
                    }
                } break ;
                default: UNREACHABLE_CODE;
            }
        } break ;
        default: {
            UNREACHABLE_CODE;
        }
    }

    node->value += simulation_result.value;
    node->num_simulations += simulation_result.num_simulations;

#if defined(DEBUG_WRITE_OUT_SIM_RESULT)
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

    if (movesequence_index != movesequence_from_position.moves_left)
    {
        assert(movesequence_index == movesequence_from_position.moves_left && "still have moves to apply to the position from movesequence so the simulation can't end before that");
    }

    return simulation_result;
}

SimulationResult simulation_from_position(const MoveSequence<max_move_chain_depth> &movesequence_from_position, const GameState &game_state, Node *node, const NodePool &node_pool)
{
    SimulationResult simulation_result_total = {};

    assert(movesequence_from_position.moves_left > 0 && "must have at least one move to apply to the position");

    g_should_write_out_simulation = true;

#if defined(DEBUG_WRITE_OUT_SIM_RESULT)
    static u32 sim_counter = 0;
    g_simresult_fs = ofstream("debug/sim_results/sim_result" + to_string(sim_counter++));
#endif

    // choose the number of simulations based on the number of possible moves
    assert(game_state.legal_moveset.moves_left >= movesequence_from_position.moves_left);
    u32 number_of_moves_available_from_position = game_state.legal_moveset.moves_left - movesequence_from_position.moves_left;
    u32 number_of_simulations_weight = number_of_moves_available_from_position;
    u32 number_of_simulations = 1;
    // u32 number_of_simulations = max((u32)1, (u32)(number_of_simulations_weight));
    // NOTE(david): it will be a terminal move, but it hasn't yet been simulated
    assert(number_of_simulations > 0 && "assumption, debug to make sure this is true, for example get the selected node and check if it is not terminal yet at this point");
    for (u32 current_simulation_count = 0;
         current_simulation_count < number_of_simulations;
         ++current_simulation_count)
    {
        TIMED_BLOCK(SimulationResult simulation_subresult = simulation_from_position_once(movesequence_from_position, game_state, node, node_pool), JobNames::SimulationFromPositionOnce);
        simulation_result_total.value += simulation_subresult.value;
        simulation_result_total.num_simulations += simulation_subresult.num_simulations;

#if defined(DEBUG_WRITE_OUT_SIM_RESULT)
        LOGN(g_simresult_fs, node->value << " ");
        g_should_write_out_simulation = false;
#endif

        if (node->terminal_info.terminal_type != TerminalType::NOT_TERMINAL)
        {
            // number_of_simulations = 1;
            // number_of_simulations = max((u32)1, (u32)(number_of_simulations_weight));
            // assert(node->num_simulations == 1);

            // node->value *= number_of_simulations;

            assert(current_simulation_count == 0);
            break ;
        }
    }

#if defined(DEBUG_WRITE_OUT_SIM_RESULT)
    LOG(g_simresult_fs, "");
#endif

    return simulation_result_total;
}

static void InitializeGameState(GameState *game_state)
{
    *game_state = {};
    game_state->player_to_move = Player::CIRCLE;
    game_state->move_to_player_map.Clear();
    game_state->legal_moveset.Clear();
    for (u32 row = 0; row < GRID_DIM_ROW; ++row)
    {
        for (u32 col = 0; col < GRID_DIM_COL; ++col)
        {
            Move move = { row, col };
            game_state->legal_moveset.AddMove(move);
        }
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
Move g_selected_move;
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
            r64 evaluate_time_result_m = 0.0;
            TIMED_BLOCK(selected_move = mcst->Evaluate(game_state->legal_moveset, [&stop_parent_sleep, &force_end_of_evaluation](bool found_move){
                if (found_move)
                {
                    stop_parent_sleep = true;
                    return true;
                }
                return force_end_of_evaluation;
            }, simulation_from_position, *node_pool, *game_state), JobNames::Evaluate);
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

    assert(game_state->move_to_player_map.GetPlayer(move) == Player::NONE);
    game_state->move_to_player_map.AddPlayer(move, game_state->player_to_move);

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
    if (game_state->outcome_for_previous_player == GameOutcome::NONE)
    {
        if (game_state->player_to_move == Player::CROSS)
        {
            if (g_finished_evaluation == false)
            {

                if (g_evaluate_thread_is_working == false)
                {
                    g_selected_move.Invalidate();
                    g_evaluate_thread_is_working = true;
                    g_evaluate_thread = thread([](GameState *game_state, MCST *mcst, NodePool *node_pool, std::chrono::milliseconds max_evaluation_time) {
                        EvaluateMove(game_state, mcst, node_pool, max_evaluation_time);
                    }, game_state, mcst, node_pool, max_evaluation_time);
                }
            }
            else
            {
                // NOTE(david): Finished evaluation
                g_evaluate_thread.join();
                g_evaluate_thread_is_working = false;
                g_finished_evaluation = false;

                static u32 timed_blocks_counter = 0;
                ofstream timed_block_ofs("debug/timed_blocks/timed_block" + to_string(timed_blocks_counter++));
                LOG_JOBS(timed_block_ofs);
                CLEAR_JOBS;
                LOG(cout, "Currently allocated nodes: " << node_pool->CurrentAllocatedNodes());
                LOG(cout, "Total freed nodes: " << node_pool->TotalNumberOfFreedNodes());
                if (g_selected_move.IsValid())
                {
                    UpdateMove(game_state, g_selected_move);
                }
                else
                {
                    assert(false && "player to move is not updated");
                }
            }
        }
        else
        {
            assert(game_state->player_to_move == Player::CIRCLE);
            Vector2 mouse_position = GetMousePosition();
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                u32 selected_grid_col = (u32)(mouse_position.x * GRID_DIM_COL / game_window->width);
                u32 selected_grid_row = (u32)(mouse_position.y * GRID_DIM_ROW / game_window->height);
                Move selected_move = { selected_grid_row, selected_grid_col };
                if (game_state->move_to_player_map.GetPlayer(selected_move) == Player::NONE)
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
    constexpr r32 grid_line_thickness = 3.5f;
    for (u32 row = 0; row < GRID_DIM_ROW - 1; ++row)
    {
        Vector2 horizontal_start = { 0.0f, (r32)game_window->height * (row + 1) / (r32)GRID_DIM_ROW };
        Vector2 horizontal_end = { (r32)game_window->width, (r32)game_window->height * (row + 1) / (r32)GRID_DIM_ROW };
        DrawLineEx(horizontal_start, horizontal_end, grid_line_thickness, BLACK);
    }
    for (u32 col = 0; col < GRID_DIM_COL - 1; ++col)
    {
        Vector2 vertical_start = { (r32)game_window->width * (col + 1) / (r32)GRID_DIM_COL, 0.0f };
        Vector2 vertical_end = { (r32)game_window->width * (col + 1) / (r32)GRID_DIM_COL, (r32)game_window->height };
        DrawLineEx(vertical_start, vertical_end, grid_line_thickness, BLACK);
    }

    for (u32 row = 0; row < GRID_DIM_ROW; ++row)
    {
        for (u32 col = 0; col < GRID_DIM_COL; ++col)
        {
            Move move = { row, col };
            Vector2 grid_offset = { (r32)game_window->width / (r32)GRID_DIM_COL * (r32)col, (r32)game_window->height / (r32)GRID_DIM_ROW * (r32)row };
            Vector2 grid_size   = { (r32)game_window->width / (r32)GRID_DIM_COL, (r32)game_window->height / (r32)GRID_DIM_ROW };
            constexpr r32 size_ratio = 0.8f;
            switch (game_state->move_to_player_map.GetPlayer(move))
            {
                case Player::CIRCLE: {
                    DrawEllipseLines(grid_size.x / 2.0f + grid_offset.x, grid_size.y / 2.0f + grid_offset.y, size_ratio * grid_size.x / 2.0f, size_ratio * grid_size.y / 2.0f, RED);
                } break ;
                case Player::CROSS: {
                    constexpr r32 inner_offset_ratio = 0.9f;
                    constexpr r32 cross_line_thickness = 3.0f;
                    Vector2 inner_offset = { inner_offset_ratio * grid_size.x, inner_offset_ratio * grid_size.y };
                    Vector2 cross_start1 = { grid_offset.x + inner_offset.x, grid_offset.y + inner_offset.y };
                    Vector2 cross_end1   = { grid_offset.x + grid_size.x - inner_offset.x, grid_offset.y + grid_size.y - inner_offset.y };
                    DrawLineEx(cross_start1, cross_end1, cross_line_thickness, BLUE);

                    Vector2 cross_start2 = { grid_offset.x + inner_offset.x, grid_offset.y + grid_size.y - inner_offset.y };
                    Vector2 cross_end2   = { grid_offset.x + grid_size.x - inner_offset.x, grid_offset.y + inner_offset.y };
                    DrawLineEx(cross_start2, cross_end2, cross_line_thickness, BLUE);
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

    constexpr NodeIndex node_pool_size = 2097152;
    NodePool node_pool(node_pool_size);
    MCST mcst;

    // u32 number_of_wins = 0;
    // u32 number_of_losses = 0;
    // u32 number_of_draws = 0;
    gen.seed(0);
    GameState game_state;
    g_selected_move.Invalidate();
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
