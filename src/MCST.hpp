#ifndef MCST_HPP
#define MCST_HPP

constexpr double EXPLORATION_FACTOR = 1.41421356237;

/*
  0  1  2
  3  4  5
  6  7  8
*/

struct TerminalMoveInfo
{
    bool was_terminal;
    bool was_controlled;
};

struct SimulationResult
{
    unsigned int num_simulations;
    double total_value;
    TerminalMoveInfo last_move;
};

struct UtilityEstimationResult
{
    double utility;
    bool should_prune;
};

using MoveSequence = vector<Move>;
using MoveProcessor = function<void(MoveSet &available_moves, Move move)>;
using SimulateFromState = function<SimulationResult(const MoveSequence &move_chain_from_world_state)>;
using TerminationPredicate = function<bool()>;
using UtilityEstimationFromState = function<UtilityEstimationResult(const MoveSequence &move_chain_from_world_state)>;

struct Node
{
    double value;
    unsigned int num_simulations;
    bool is_pruned;

    unordered_map<Move, Node *> children;
    Node *parent;
};

class MCST
{
private:
    Node *_root_node;

public:
    MCST();
    ~MCST();
    MCST(const MCST &other) = delete;
    const MCST &operator=(const MCST &other) = delete;

    Move Evaluate(const MoveSet &legal_moves_at_root_node, TerminationPredicate terminate_condition_fn, SimulateFromState simulation_from_state, MoveProcessor move_processor, UtilityEstimationFromState utility_estimation_from_state, double prune_treshhold_for_node);

    unsigned int NumberOfSimulationsRan(void);

private:
    struct SelectionResult
    {
        Node *selected_node;
        MoveSequence movechain_from_state;
    };

    SelectionResult _Selection(const MoveSet &legal_moveset_at_root_node, MoveProcessor move_processor, UtilityEstimationFromState utility_estimation_from_state);
    pair<Move, Node *> _SelectChild(Node *from_node, const MoveSet &legal_moves_from_node, UtilityEstimationFromState utility_estimation_from_state, const MoveSequence &movechain_from_state, unsigned int depth, bool focus_on_lowest_utc_to_prune);
    Node *_Expansion(Node *from_node);
    void _BackPropagate(Node *from_node, SimulationResult simulation_result, double prune_treshhold_for_node);

    Node *_AllocateNode(void);
    void _DeleteNode(Node *node);

    using WinningMoveSelectionStrategy = function<Move(Node *from_node, const MoveSet &legal_moves_at_root_node)>;
    WinningMoveSelectionStrategy winning_move_selection_strategy_fn;
};

#endif
