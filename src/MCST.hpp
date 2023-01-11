#ifndef MCST_HPP
#define MCST_HPP

constexpr r64 EXPLORATION_FACTOR = 1.41421356237;

enum class TerminalType
{
    NOT_TERMINAL,
    LOSING,
    NEUTRAL,
    WINNING,

    TerminalType_Size
};

enum class ControlledType
{
    NONE,
    CONTROLLED,
    UNCONTROLLED,

    ControlledType_Size
};

struct TerminalDepth
{
    u16 winning;
    u16 losing;
    u16 neutral;
};

struct TerminalInfo
{
    TerminalType terminal_type;
    // TODO(david): this about this and how to implement it, but one problem was propagating back the actual probability, which is not hard (multiply branching until we get to the terminal node), however to make it even more useful, instead of treating all the moves as equal probability, a heuristic evaluation would also need to be stored (or processed dynamically), so that the moves are weighted according to the heuristic value
    // r32 p_of_terminal_outcome; // NOTE(david): probability that the outcomes from the Node results in a terminal outcome, useful information to determine best next move
                               // probability of terminal outcome for node = (heuristic weight for node / number of node's children / sum of heuristic weights for node) + sum of probability of terminal outcomes for node's children
    // u16 terminal_depth; // NOTE(david): this refers to the max depth when the node first found to be terminal, which then propagates back to find in how many moves does it take to reach terminal game state, can be useful to choose the next move that wins in the least moves and loses in the most moves, also based on the depth, extra weighted info could be propagated up the root, that is a likelyhood of the outcome: for example if there are 20 of terminally losing nodes scattered down the tree at depth 10, this could be weighted in to the starting move as 20 * 1 / 10 chance of losing (the reciprocal function is not the best used here, it should be a function that accounts for the branching)
    TerminalDepth terminal_depth;
};

// using MoveSequence: vector<Move> -> { Move[Move::NONE], u32 }
struct MoveSequence
{
    Move moves[Move::NONE];
    u32 number_of_moves;
};

// NOTE(david): must be signed
typedef i32 NodeIndex;
struct Node
{
    r32 value;
    u32 num_simulations;

    NodeIndex index;
    // TODO(david): maybe it makes sense to store the parent as a pointer to give up some extra space in order to avoid the extra lookup through NodePool
    NodeIndex parent;

    ControlledType controlled_type;
    TerminalInfo terminal_info;

    Move move_to_get_here;

    u16 depth;
};

struct NodePool
{
    const NodeIndex _number_of_nodes_allocated;

    Node *_nodes;
    NodeIndex _available_node_index;

    NodeIndex *_free_nodes;
    NodeIndex _free_nodes_index;

    struct MoveToNodeTable
    {
        NodeIndex children[Move::NONE];
    };

    MoveToNodeTable *_move_to_node_tables;

public:
    NodePool(NodeIndex number_of_nodes_to_allocate);
    ~NodePool();

    Node *AllocateNode(Node *parent);
    void FreeNode(Node *node);

    void AddChild(Node *node, Node *child, Move move);
    Node *GetChild(Node *node, Move move);
    MoveToNodeTable *GetChildren(Node *node);
    Node *GetParent(Node *node) const;
    Node *SetParent(Node *node, Node *parent);

    void Clear();
};

using MoveProcessor = function<void(MoveSet &available_moves, Move move)>;
using SimulateFromState = function<void(const MoveSequence &move_chain_from_world_state, const GameState &game_state, Node *node, const NodePool &node_pool)>;
using TerminationPredicate = function<bool(bool found_perfect_move)>;

class MCST
{
private:
    Node *_root_node;

public:
    MCST();
    MCST(const MCST &other) = delete;
    const MCST &operator=(const MCST &other) = delete;

    Move Evaluate(const MoveSet &legal_moves_at_root_node, TerminationPredicate terminate_condition_fn, SimulateFromState simulation_from_state, MoveProcessor move_processor, NodePool &node_pool, const GameState &game_state);

    u32 NumberOfSimulationsRan(void);

private:
    struct SelectionResult
    {
        Node *selected_node;
        MoveSequence movesequence_from_position;
    };

    SelectionResult _Selection(const MoveSet &legal_moveset_at_root_node, MoveProcessor move_processor, NodePool &node_pool);
    Node *_SelectChild(Node *from_node, const MoveSet &legal_moves_from_node, const MoveSequence &movesequence_from_position, bool focus_on_lowest_utc_to_prune, NodePool &node_pool);
    Node *_Expansion(Node *from_node, NodePool &node_pool);
    void _BackPropagate(Node *from_node, NodePool &node_pool);

    using WinningMoveSelectionStrategy = function<Move(Node *from_node, const MoveSet &legal_moves_at_root_node, NodePool &node_pool)>;
    WinningMoveSelectionStrategy winning_move_selection_strategy_fn;
};

#endif
