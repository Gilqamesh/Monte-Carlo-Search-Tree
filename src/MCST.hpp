#ifndef MCST_HPP
#define MCST_HPP

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>

using namespace std;

constexpr double EXPLORATION_FACTOR = 1.41421356237;

/*
  0  1  2
  3  4  5
  6  7  8
*/
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

struct SimulationResult
{
    unsigned int num_simulations;
    double total_value;
};

struct UtilityEstimationResult
{
    double utility;
    bool should_prune;
};

using MoveChain = vector<Move>;
using MoveSet = unordered_set<Move>;
using MoveProcessor = function<void(MoveSet &available_moves, Move move)>;
using SimulateFromState = function<SimulationResult(const MoveChain &move_chain_from_world_state)>;
using TerminationPredicate = function<bool(bool terminate_caller)>;
using UtilityEstimationFromState = function<UtilityEstimationResult(const MoveChain &move_chain_from_world_state)>;

struct Node
{
    double value;
    unsigned int num_simulations;

    unordered_map<Move, Node *> children;
    Node *parent;
};

class MCST
{
private:
    Node *_root;

public:
    MCST();
    ~MCST();
    MCST(const MCST &other) = delete;
    const MCST &operator=(const MCST &other) = delete;

    Move Evaluate(const MoveSet &legal_moves_at_root_node, TerminationPredicate terminate_condition_fn, SimulateFromState simulation_from_state, MoveProcessor move_processor, UtilityEstimationFromState utility_estimation_from_state);

    unsigned int NumberOfSimulationsRan(void);

private:
    struct SelectionResult
    {
        Node *selected_node;
        MoveChain movechain_from_state;
    };

    SelectionResult _Selection(const MoveSet &legal_moveset_at_root_node, MoveProcessor move_processor, UtilityEstimationFromState utility_estimation_from_state);
    Node *_Expansion(Node *from_node);
    void _BackPropagate(Node *from_node, SimulationResult simulation_result);

    Node *_AllocateNode(void);

    using WinningMoveSelectionStrategy = function<Move(Node *from_node, const MoveSet &legal_moves_at_root_node, unsigned int)>;
    WinningMoveSelectionStrategy winning_move_selection_strategy_fn;
};

#endif
