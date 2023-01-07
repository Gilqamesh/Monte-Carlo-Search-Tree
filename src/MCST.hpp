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

struct TerminalInfo
{
    TerminalType terminal_type;
    // ControlledType controlled_type; // TODO(david): this is not terminal information, it's about who controls the specific node to move from, move this into node
    u16 terminal_depth; // NOTE(david): this refers to the max depth when the node first found to be  terminal, which then propagates back to find in how many moves does it take to reach terminal game state
};

// TODO(david): can this be just a node instead and the simulation will update its values?
// struct SimulationResult
// {
//     u32 num_simulations;
//     r64 total_value;
//     TerminalInfo last_move;
//     ControlledType last_controlled_type;
//     Player last_player_to_move;
// };

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

class NodePool
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
    MCST(NodePool &node_pool);
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
