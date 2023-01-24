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
    // NOTE(david): the moves are needed in order to know which child does the terminal depth belong to
    Move winning_continuation;
    Move losing_continuation;
    Move neutral_continuation;
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

template <u32 MoveSize>
struct MoveSequence
{
    Move moves[MoveSize];
    u32 moves_left;

    void AddMove(Move move);
    Move PopMoveAtIndex(u32 move_index);
};

struct NodePool;

// NOTE(david): must be signed
typedef i32 NodeIndex;
struct Node
{
    r32 value;
    u32 num_simulations;

    // NOTE(david): unique index to the child table as well as for the node
    // TODO(david): maybe store the actual pointer to the children table for the same reasons as to store the parent pointer instead of the index of the parent
    NodeIndex index;
    // NOTE(david): maybe it makes sense to store the parent as a pointer to give up some extra space in order to avoid the extra lookup through NodePool
    Node *parent;

    ControlledType controlled_type;
    TerminalInfo terminal_info;

    Move move_to_get_here;

    u16 depth;

    // TODO(david): change this to ChangeTerminalType maybe as they are kind of coupled? Terminal Depth only has to be updated when the terminal type of a node changes from not terminal to terminal, in which case this should be recursing back to root, in which case it'd only have to be called once during backpropagation
    // NOTE(david): returns terminal type if parent's terminal type's depth has been changed, as that signals that the grandparent's terminal depth might also need to be updated by its children's terminal depth
    // TODO(david): shouldn't this return more information other than the fact that the parent's terminal depth has been updated? For example: there has been a prune of a terminally losing node for a controlled parent node. The update shouldn't update losing depth. Also if it happens to update something else other than losing terminal depth even though the losing node doesn't exist anymore, this'll force updating all terminal depths from all children, even tho some children for the grapndparent might already have been cycled that'd give the best terminal depth of a certain type.
    TerminalType UpdateTerminalDepthForParentNode(TerminalType terminal_type_to_update, NodePool &node_pool);

    void UpdateTerminalType(TerminalType terminal_type);
};

struct SimulationResult
{
    r32 value;
    u32 num_simulations;
};

// TODO(david): reallocation of more nodes if the nodepool is full?
struct NodePool
{
    const NodeIndex _number_of_nodes_allocated;

    Node *_nodes;
    NodeIndex _available_node_index;

    NodeIndex *_free_nodes;
    NodeIndex _free_nodes_index;

    // NOTE(david): shouldn't be more than the max action space at any point
    // TODO(david): should also be dynamic, as depending on the state, the action space is vastly different in size
    // TODO(david): actually this could be limited in size, and with pruning existing nodes (based on some upper confidence bound) can be updated with new ones
    // NOTE(david): the children nodes are stored randomly
    static constexpr u32 allowed_branching_factor = 3;
    struct ChildrenTables
    {
        Node *children[allowed_branching_factor];
        u32 number_of_children;
        // NOTE(david): since 0 index is a legal move, this starts at -1
        i32 highest_move_index;
    };

    ChildrenTables *_move_to_node_tables;

    // TODO(david): count the number of allocated and freed nodes
    u32 _total_number_of_freed_nodes;
public:
    NodePool(NodeIndex number_of_nodes_to_allocate);
    ~NodePool();

    Node *AllocateNode(Node *parent);
    void FreeNode(Node *node);

    void AddChild(Node *node, Node *child, Move move);
    ChildrenTables *GetChildren(Node *node);

    void Clear();
    void ClearChildTable(u32 table_index);

    u32 TotalNumberOfFreedNodes(void);
    u32 CurrentAllocatedNodes(void);
private:
    void FreeNodeHelper(Node *node);
};

constexpr u32 max_move_chain_depth = 32;
using SimulateFromState = function<SimulationResult(const MoveSequence<max_move_chain_depth> &move_chain_from_world_state, const GameState &game_state, Node *node, const NodePool &node_pool)>;
using TerminationPredicate = function<bool(bool found_perfect_move)>;

class MCST
{
private:
    Node *_root_node;

public:
    MCST() = default;
    MCST(const MCST &other) = delete;
    const MCST &operator=(const MCST &other) = delete;

    Move Evaluate(const MoveSet &legal_moves_at_root_node, TerminationPredicate terminate_condition_fn, SimulateFromState simulation_from_state, NodePool &node_pool, const GameState &game_state);

    u32 NumberOfSimulationsRan(void);

private:
    struct SelectionResult
    {
        Node *selected_node;
        MoveSequence<max_move_chain_depth> movesequence_from_position;
    };

    struct ExtremumChildren
    {
        Node *best_non_terminal;
        Node *worst_non_terminal;

        Node *best_winning;
        Node *worst_winning;

        Node *best_losing;
        Node *worst_losing;

        Node *best_neutral;
        Node *worst_neutral;

        u32 condition_checked_nodes_on_their_simulation_count;
    };
    ExtremumChildren GetExtremumChildren(Node *from_node, NodePool &node_pool, u32 min_simulation_confidence_cycle_treshold = 0);

    Node *SelectBestChild(Node *from_node, NodePool &node_pool);

    SelectionResult _Selection(const MoveSet &legal_moveset_at_root_node, NodePool &node_pool);
    Node *_SelectChild(Node *from_node, const MoveSet &legal_moves_from_node, bool focus_on_lowest_utc_to_prune, NodePool &node_pool);
    Node *_Expansion(Node *from_node, NodePool &node_pool);
    void _BackPropagate(Node *from_node, NodePool &node_pool, SimulationResult simulation_result);
    void PruneNode(Node *from_node, NodePool &node_pool);
};

#endif
