#include "MCST.hpp"
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>

// static r64 g_tuned_exploration_factor_weight = 0.422;
static r64 g_tuned_exploration_factor_weight = 1.0;
static r64 UCT(Node *node, u32 number_of_branches)
{
    /*
        parent num of simulations | max exploration factor (child num of simulation is 1)
                                1 | 0
                               10 | 1.51743
                              100 | 2.14597
                            1.000 | 2.62826
                           10.000 | 3.03485
                          100.000 | 3.39307
                        1.000.000 | 3.71692
                       10.000.000 | 4.01473
    */
    assert(node != nullptr);
    assert(node->parent != nullptr && "don't care about root uct, as the root node isn't a possible move, so there is no reason to compare its uct");
    assert(node->num_simulations != 0);

    r64 result = 0.0;

    r64 depth_weight = 1.0 / (r32)node->depth;
    // TODO(david): think about this number and how it should affect explitation vs exploration
    // NOTE(david): go with exploration if there are a lot of branches and exploit more the less choices there are
    // r64 number_of_branches_weight = 0.2 * number_of_branches;
    r64 number_of_branches_weight = 1.0;
    // r64 weighted_exploration_factor = number_of_branches_weight * g_tuned_exploration_factor_weight * EXPLORATION_FACTOR / (node->depth * depth_weight);
    assert(node->parent != nullptr);
    result = (r64)node->value / (r64)node->num_simulations +
             EXPLORATION_FACTOR * sqrt(log((r64)node->parent->num_simulations) / (r64)node->num_simulations);

    return result;
}

static r64 UCT_Uncontrolled(Node *node, u32 number_of_branches)
{
    // NOTE(david): favors low exploit and normal explore
    assert(node != nullptr);
    assert(node->parent != nullptr && "don't care about root uct, as the root node isn't a possible move, so there is no reason to compare its uct");
    assert(node->num_simulations != 0);

    r64 result = 0.0;

    r64 depth_weight = 1.0 / (r32)node->depth;
    // TODO(david): think about this number and how it should affect explitation vs exploration
    // NOTE(david): go with exploration if there are a lot of branches and exploit more the less choices there are
    // r64 number_of_branches_weight = 0.2 * number_of_branches;
    r64 number_of_branches_weight = 1.0;
    // r64 weighted_exploration_factor = number_of_branches_weight * g_tuned_exploration_factor_weight * EXPLORATION_FACTOR / (node->depth * depth_weight);
    assert(node->parent != nullptr);
    result = EXPLORATION_FACTOR * sqrt(log((r64)node->parent->num_simulations) / (r64)node->num_simulations) - (r64)node->value / (r64)node->num_simulations;

    return result;
}

static string MoveToWord(Move move)
{
    if (move.IsValid() == false)
    {
        return "NONE";
    }
    return "(" + to_string(move.col) + ", " + to_string(move.row) + ")";
}

static string TerminalTypeToWord(TerminalType terminal_type)
{
    switch (terminal_type)
    {
        case TerminalType::NOT_TERMINAL: {
            return "not terminal";
        } break ;
        case TerminalType::LOSING: {
            return "losing";
        } break ;
        case TerminalType::NEUTRAL: {
            return "neutral";
        } break ;
        case TerminalType::WINNING: {
            return "winning";
        } break ;
        default: {
            UNREACHABLE_CODE;
            return "breh";
        }
    }
}

static string ControlledTypeToWord(ControlledType controlled_type)
{
    switch (controlled_type)
    {
        case ControlledType::NONE: {
            return "uninitialized";
        } break ;
        case ControlledType::CONTROLLED: {
            return "controlled";
        } break ;
        case ControlledType::UNCONTROLLED: {
            return "uncontrolled";
        } break ;
        default: {
            UNREACHABLE_CODE;
            return "breh";
        }
    }
}

void MoveSequence::AddMove(Move move)
{
    assert(number_of_moves < ArrayCount(MoveSequence::moves) && "not enough space in the move sequence");
    moves[number_of_moves++] = move;
}

NodePool *debug_node_pool;
ostream &operator<<(ostream &os, Node *node)
{
    NodePool::ChildrenTables *children_table = debug_node_pool->GetChildren(node);
    Move highest_move_cycled = Deserialize(children_table->highest_serialized_move);
    Move highest_possible_move = { GRID_DIM_ROW - 1, GRID_DIM_COL - 1 };
    LOGN(os, "index: " << node->index << ", " << MoveToWord(node->move_to_get_here) << ", value: " << node->value << ", sims: " << node->num_simulations << ", " << ControlledTypeToWord(node->controlled_type) << ", " << TerminalTypeToWord(node->terminal_info.terminal_type) << ", terminal depth(W/L/N): (" << node->terminal_info.terminal_depth.winning << "," << node->terminal_info.terminal_depth.losing << "," << node->terminal_info.terminal_depth.neutral << "), uct: " << (node->parent == nullptr ? 0.0 : (node->controlled_type == ControlledType::UNCONTROLLED ? UCT(node, 0) : UCT_Uncontrolled(node, 0))) << ", highest move cycled: " << (highest_move_cycled == highest_possible_move ? "all moves are cycled" : MoveToWord(highest_move_cycled)));

    return os;
}

static u32 GetNextPowerOfTwo(u32 number)
{
    u32 result = pow(2.0, ceil(log2(number)));

    return result;
}

void NodePool::ClearChildTable(u32 table_index)
{
    assert(table_index < _number_of_nodes_allocated);
    ChildrenTables *child_table = &_move_to_node_tables[table_index];
    memset(child_table, 0, sizeof(*child_table));
    child_table->highest_serialized_move = -1;
}

NodePool::NodePool(NodeIndex number_of_nodes_to_allocate)
    : _number_of_nodes_allocated(number_of_nodes_to_allocate),
      _available_node_index(0),
      _free_nodes_index(-1),
      _total_number_of_freed_nodes(0)
{
    u32 node_alignment = GetNextPowerOfTwo(sizeof(*_nodes));
    _nodes = (Node *)_aligned_malloc(_number_of_nodes_allocated * sizeof(*_nodes), node_alignment);
    if (_nodes == nullptr)
    {
        throw runtime_error("couldn't allocate _nodes in NodePool");
    }

    u32 move_to_node_hash_table_alignment = GetNextPowerOfTwo(sizeof(*_move_to_node_tables));
    _move_to_node_tables = (ChildrenTables *)_aligned_malloc(_number_of_nodes_allocated * sizeof(*_move_to_node_tables), move_to_node_hash_table_alignment);
    if (_move_to_node_tables == nullptr)
    {
        throw runtime_error("couldn't allocate _move_to_node_tables in NodePool");
    }

    u32 nodes_free_list_alignment = GetNextPowerOfTwo(sizeof(*_free_nodes));
    _free_nodes = (NodeIndex *)_aligned_malloc(_number_of_nodes_allocated * sizeof(*_free_nodes), nodes_free_list_alignment);
    if (_free_nodes == nullptr)
    {
        throw runtime_error("couldn't allocate _move_to_node_tables in NodePool");
    }

    for (u32 child_table_index = 0; child_table_index < _number_of_nodes_allocated; ++child_table_index)
    {
        ClearChildTable(child_table_index);
    }
}

NodePool::~NodePool()
{
    assert(_nodes);
    assert(_move_to_node_tables);
    assert(_free_nodes);

    _aligned_free(_nodes);
    _aligned_free(_move_to_node_tables);
    _aligned_free(_free_nodes);
}

static Node *InitializeNode(Node *node, Node *parent)
{
    // NOTE(david): can't 0 out the Node struct here, as the index is persistent
    // TODO(david): separate persistent and transient data in Node
    node->value = 0.0;
    node->num_simulations = 0;
    node->parent = parent;
    if (parent)
    {
        node->depth = parent->depth + 1;
    }
    else
    {
        node->depth = 0;
    }
    node->terminal_info.terminal_type = TerminalType::NOT_TERMINAL;
    node->terminal_info.terminal_depth = {};
    node->controlled_type = ControlledType::NONE;
    node->move_to_get_here.Invalidate();

    return node;
}

Node *NodePool::AllocateNode(Node *parent)
{
    Node *result_node = nullptr;

    if (_free_nodes_index >= 0)
    {
        u32 node_index = _free_nodes[_free_nodes_index];
        result_node = &_nodes[node_index];
        --_free_nodes_index;
    }
    else if (_available_node_index < _number_of_nodes_allocated)
    {
        result_node = &_nodes[_available_node_index];
        result_node->index = _available_node_index;
        ++_available_node_index;
    }
    else
    {
        throw runtime_error("NodePool out of nodes to allocate from!");
    }

    InitializeNode(result_node, parent);

    return result_node;
}

void NodePool::FreeNodeHelper(Node *node)
{
    ++_total_number_of_freed_nodes;
    ++_free_nodes_index;
    _free_nodes[_free_nodes_index] = node->index;
    
    ChildrenTables *children_table = GetChildren(node);
    for (u32 child_index = 0; child_index < children_table->number_of_children; ++child_index)
    {
        assert(children_table->children[child_index] != nullptr);
        FreeNodeHelper(children_table->children[child_index]);
    }
    ClearChildTable(node->index);
}

void NodePool::FreeNode(Node *node)
{
    if (node->parent)
    {
        ChildrenTables *children_table = &_move_to_node_tables[node->parent->index];
        bool debug_make_sure_child_is_found = false;
        for (u32 child_index = 0; child_index < children_table->number_of_children; ++child_index)
        {
            if (children_table->children[child_index] == node)
            {
                children_table->children[child_index] = children_table->children[children_table->number_of_children - 1];
                children_table->children[children_table->number_of_children - 1] = nullptr;
                --children_table->number_of_children;
                debug_make_sure_child_is_found = true;
                break ;
            }
        }
        assert(debug_make_sure_child_is_found == true);
    }
    FreeNodeHelper(node);
}

void NodePool::AddChild(Node *node, Node *child, Move move)
{
    ChildrenTables *table = &_move_to_node_tables[node->index];
    assert(table->number_of_children < ArrayCount(ChildrenTables::children));
    assert(table->children[table->number_of_children] == nullptr);
    table->children[table->number_of_children] = child;

    ++table->number_of_children;

    u32 move_serialized = move.Serialize();
    assert((table->highest_serialized_move == -1 || move_serialized > table->highest_serialized_move) && "the selected new node at the moment is always the next up in line by its serialized order from the available set of moves");
    table->highest_serialized_move = move_serialized;

    child->move_to_get_here = move;
}

NodePool::ChildrenTables *NodePool::GetChildren(Node *node)
{
    assert(node->index >= 0 && node->index < _available_node_index);
    return (&_move_to_node_tables[node->index]);
}

void NodePool::Clear()
{
    for (u32 table_index = 0; table_index < _available_node_index; ++table_index)
    {
        ClearChildTable(table_index);
    }
    _available_node_index = 0;
    _free_nodes_index = -1;
    _total_number_of_freed_nodes = 0;
}

u32 NodePool::TotalNumberOfFreedNodes(void)
{
    return _total_number_of_freed_nodes;
}

u32 NodePool::CurrentAllocatedNodes(void)
{
    u32 current_available_free_nodes = _free_nodes_index + 1;

    assert(_available_node_index >= current_available_free_nodes);
    return _available_node_index - current_available_free_nodes;
}

// TODO(david): implement
static Move _EvaluateBasedOnMostSimulated(Node *node_to_evaluate_from, NodePool &node_pool)
{
    // TODO(david): implement
    throw runtime_error("Not implemented");
}

static Move _EvaluateBasedOnUCT(Node *node_to_evaluate_from, NodePool &node_pool)
{
    r64 highest_uct = -INFINITY;
    Node *current_highest_uct_node = nullptr;

    assert(node_to_evaluate_from->controlled_type == ControlledType::CONTROLLED && "the strategy here is to win, so if it's an uncontrolled node than the strategy would be to choose the worst uct or losing move");

    NodePool::ChildrenTables *children_nodes = node_pool.GetChildren(node_to_evaluate_from);
    u32 number_of_branches = children_nodes->number_of_children;
    for (u32 child_index = 0; child_index < children_nodes->number_of_children; ++child_index)
    {
        Node *child = children_nodes->children[child_index];
        assert(child != nullptr && child->move_to_get_here.IsValid());

        if (child->terminal_info.terminal_type != TerminalType::NOT_TERMINAL)
        {
            switch (child->terminal_info.terminal_type)
            {
                case TerminalType::LOSING: {
                    r64 uct = UCT(child, number_of_branches);
                    if (current_highest_uct_node == nullptr)
                    {
                        highest_uct = uct;
                        current_highest_uct_node = child;
                    }
                    else if (current_highest_uct_node->terminal_info.terminal_type == TerminalType::LOSING)
                    {
                        // NOTE(david): choose the one with the higher terminal depth to lose as slowly as possible
                        if (child->terminal_info.terminal_depth.losing > current_highest_uct_node->terminal_info.terminal_depth.losing)
                        {
                            highest_uct = uct;
                            current_highest_uct_node = child;
                        }
                        else if (child->terminal_info.terminal_depth.losing == current_highest_uct_node->terminal_info.terminal_depth.losing)
                        {
                            if (uct > highest_uct)
                            {
                                highest_uct = uct;
                                current_highest_uct_node = child;                                
                            }
                        }
                    }
                    else
                    {
                        // NOTE(david): if current_highest_uct_node isn't losing, then we wouldn't want to update it with a losing one -> continue
                    }
                } break ;
                case TerminalType::WINNING: {
                    // TODO(david): choose the one with the lowest terminal depth to win asap
                    r64 uct = UCT(child, number_of_branches);
                    if (current_highest_uct_node == nullptr)
                    {
                        highest_uct = uct;
                        current_highest_uct_node = child;
                    }
                    switch (current_highest_uct_node->terminal_info.terminal_type)
                    {
                        case TerminalType::WINNING: {
                            // NOTE(david): select the winning move with the least terminal winning depth
                            if (child->terminal_info.terminal_depth.winning < current_highest_uct_node->terminal_info.terminal_depth.winning)
                            {
                                current_highest_uct_node = child;
                                highest_uct = uct;
                            }
                            else if (child->terminal_info.terminal_depth.winning == current_highest_uct_node->terminal_info.terminal_depth.winning)
                            {
                                if (uct > highest_uct)
                                {
                                    current_highest_uct_node = child;
                                    highest_uct = uct;
                                }
                            }
                        } break ;
                        case TerminalType::LOSING:
                        case TerminalType::NEUTRAL:
                        case TerminalType::NOT_TERMINAL: {
                            highest_uct = uct;
                            current_highest_uct_node = child;
                        } break ;
                        default: {
                            UNREACHABLE_CODE;
                        }
                    }
                } break ;
                case TerminalType::NEUTRAL: {
                    // NOTE(david): dispatching over the previous highest uct node's terminal type
                    r64 uct = UCT(child, number_of_branches);
                    if (current_highest_uct_node == nullptr)
                    {
                        highest_uct = uct;
                        current_highest_uct_node = child;
                    }
                    switch (current_highest_uct_node->terminal_info.terminal_type)
                    {
                        case TerminalType::WINNING: {
                            // NOTE(david): select the winning move with the least terminal winning depth -> skip
                        } break ;
                        case TerminalType::LOSING: {
                            // NOTE(david): always replace losing moves with the current non-terminal move as there might be moves that aren't terminally losing
                            highest_uct = uct;
                            current_highest_uct_node = child;
                        } break ;
                        case TerminalType::NEUTRAL: {
                            // NOTE(david): choose the one with the highest terminal depth for optimistic strategy (wait for opponent to make a mistake)
                            if (child->terminal_info.terminal_depth.neutral > current_highest_uct_node->terminal_info.terminal_depth.neutral)
                            {
                                current_highest_uct_node = child;
                                highest_uct = uct;
                            }
                            else if (child->terminal_info.terminal_depth.neutral == current_highest_uct_node->terminal_info.terminal_depth.neutral)
                            {
                                if (uct > highest_uct)
                                {
                                    current_highest_uct_node = child;
                                    highest_uct = uct;
                                }
                            }
                        } break ;
                        case TerminalType::NOT_TERMINAL: {
                            // NOTE(david): it makes sense to go with a terminally neutral move which is guaranteed to be neutral outcome than to risk a potentially worse outcome -> choose the child for safety
                            highest_uct = uct;
                            current_highest_uct_node = child;
                        } break ;
                        default: {
                            UNREACHABLE_CODE;
                        }
                    }
                } break ;
                default: {
                    assert(false && "other terminal types are not supported at the moment");
                }
            }
        }
        else
        {
            r64 uct = UCT(child, number_of_branches);
            if (current_highest_uct_node == nullptr)
            {
                highest_uct = uct;
                current_highest_uct_node = child;
            }
            else
            {
                // NOTE(david): dispatching over the previous highest uct node's terminal type
                switch (current_highest_uct_node->terminal_info.terminal_type)
                {
                    case TerminalType::WINNING: {
                        // NOTE(david): select the winning move with the least terminal winning depth -> skip
                    } break ;
                    case TerminalType::LOSING: {
                        // NOTE(david): always replace losing moves with the current non-terminal move as there might be moves that aren't terminally losing
                        highest_uct = uct;
                        current_highest_uct_node = child;
                    } break ;
                    case TerminalType::NEUTRAL: {
                        // NOTE(david): it makes sense to go with a terminally neutral move which is guaranteed to be neutral outcome than to risk a potentially worse outcome -> so do nothing, continue
                        assert(current_highest_uct_node != nullptr);
                        // if (uct > highest_uct)
                        // {
                            // highest_uct = uct;
                            // current_highest_uct_node = child;
                        // }
                    } break ;
                    case TerminalType::NOT_TERMINAL: {
                        assert(current_highest_uct_node != nullptr);
                        if (uct > highest_uct)
                        {
                            highest_uct = uct;
                            current_highest_uct_node = child;
                        }
                    } break ;
                    default: {
                        UNREACHABLE_CODE;
                    }
                }
            }
        }
    }

    if (current_highest_uct_node == nullptr)
    {
        assert(false && "no children are expanded, implementation error as there should have been at least 1 simulation");
    }

    return current_highest_uct_node->move_to_get_here;
}

MCST::MCST()
{
    winning_move_selection_strategy_fn = &_EvaluateBasedOnUCT;
}

static void DebugPrintDecisionTreeHelper(Node *from_node, Player player_to_move, ofstream &tree_fs, NodePool &node_pool)
{
    if (from_node->depth > 3)
    {
        return ;
    }
    LOG(tree_fs, string(from_node->depth * 4, ' ') << "(player to move: " << PlayerToWord(player_to_move) << ", depth: " << from_node->depth << ", " << from_node << ")");

    NodePool::ChildrenTables *children_nodes = node_pool.GetChildren(from_node);
    for (u32 child_index = 0; child_index < children_nodes->number_of_children; ++child_index)
    {
        Node *child_node = children_nodes->children[child_index];
        assert(child_node != nullptr && child_node->move_to_get_here.IsValid());

        DebugPrintDecisionTreeHelper(child_node, player_to_move == Player::CIRCLE ? Player::CROSS : Player::CIRCLE, tree_fs, node_pool);
    }
}

static void DebugPrintDecisionTree(Node *from_node, u32 move_counter, NodePool &node_pool, const GameState &game_state)
{
    ofstream tree_fs("debug/trees/tree" + to_string(move_counter));
    DebugPrintDecisionTreeHelper(from_node, game_state.player_to_move, tree_fs, node_pool);
}

static u32 g_move_counter;

const GameState *debug_game_state;

Move MCST::Evaluate(const MoveSet &legal_moveset_at_root_node, TerminationPredicate termination_predicate, SimulateFromState simulation_from_state, NodePool &node_pool, const GameState &game_state)
{
    debug_game_state = &game_state;
    debug_node_pool = &node_pool;

    if (legal_moveset_at_root_node.moves_left == 0)
    {
        Move move;
        move.Invalidate();
        return move;
    }
    node_pool.Clear();
    _root_node = node_pool.AllocateNode(nullptr);
    _root_node->controlled_type = ControlledType::CONTROLLED;

    while (termination_predicate(false) == false)
    {
        TIMED_BLOCK(SelectionResult selection_result = _Selection(legal_moveset_at_root_node, node_pool), JobNames::Selection);

        SimulationResult simulation_result = {};
        if (selection_result.selected_node->terminal_info.terminal_type != TerminalType::NOT_TERMINAL)
        {
            if (selection_result.selected_node == _root_node)
            {
                // NOTE(david): if root node is a terminal node, it means that no more simulations are needed
                termination_predicate(true);
                break ;
            }

            // simulation_result.value = selection_result.selected_node->value;

            // TODO(david): these values should be set by some function by the user of MCST
            // IMPORTANT(david): think about these values.. how to propagate back? value and num_simulations is high if I don't reset these
            switch (selection_result.selected_node->terminal_info.terminal_type)
            {
                case TerminalType::WINNING: {
                    simulation_result.value = 1.0;
                } break ;
                case TerminalType::LOSING: {
                    simulation_result.value = -1.0;
                } break ;
                case TerminalType::NEUTRAL: {
                    simulation_result.value = 0.0;
                } break ;
                default: UNREACHABLE_CODE;
            }
            selection_result.selected_node->num_simulations = 1;

// #if defined(DEBUG_WRITE_OUT)
//             DebugPrintDecisionTree(_root_node, g_move_counter, node_pool);
// #endif

        }
        else
        {
            TIMED_BLOCK(simulation_result = simulation_from_state(selection_result.movesequence_from_position, game_state, selection_result.selected_node, node_pool), JobNames::Simulation);
        }
//         if (selection_result.selected_node->num_simulations > 10000000)
//         {
// #if defined(DEBUG_WRITE_OUT)
//             DebugPrintDecisionTree(_root_node, g_move_counter, node_pool, game_state);
// #endif
//             assert(false && "suspicious amount of simulations, make sure this could happen");
//         }
        TIMED_BLOCK(_BackPropagate(selection_result.selected_node, node_pool, simulation_result), JobNames::BackPropagate);
    }

#if defined(DEBUG_WRITE_OUT)
    DebugPrintDecisionTree(_root_node, g_move_counter, node_pool, game_state);
#endif

    // TIMED_BLOCK("winning_move_selection_strategy_fn", Move result_move = winning_move_selection_strategy_fn(_root_node, node_pool));
    Move result_move = winning_move_selection_strategy_fn(_root_node, node_pool);

    return result_move;
}

Node *MCST::_SelectChild(Node *from_node, const MoveSet &legal_moves_from_node, bool focus_on_lowest_utc_to_prune, NodePool &node_pool)
{
    assert((from_node->controlled_type == ControlledType::CONTROLLED || from_node->controlled_type == ControlledType::UNCONTROLLED) && "from_node's controlled type is not initialized");
    /*
      if controlled     ->  if winning      -> choose this
                            if neutral      -> choose this if exists at the end with the best utc
                            if losing       -> only choose this if all losing with the best utc
                            if not terminal -> choose this with best utc
      if uncontrolled   ->  if winning      -> only choose this if all winning with the worst utc
                            if neutral      -> choose this if exists at the end with the worst utc
                            if losing       -> choose this
                            if not terminal -> choose this with worst utc
    */
    // TODO(david): if empty node is selected, choose them randomly or based of some heuristic of the game state
    Node *selected_node = nullptr;
    // Find the maximum UCT value and its corresponding legal move
    r64 selected_uct = -INFINITY;

    Node *neutral_controlled_node = nullptr;
    r64 neutral_controlled_uct = -INFINITY;

    Node *neutral_uncontrolled_node = nullptr;
    r64 neutral_uncontrolled_uct = INFINITY;

    Node *losing_node = nullptr;
    r64 losing_uct = -INFINITY;

    Node *winning_node = nullptr;
    r64 winning_uct = INFINITY;

    NodePool::ChildrenTables *children_nodes = node_pool.GetChildren(from_node);
    // TODO(david): dynamically control branching factor for example based on depth
    u32 number_of_branches = legal_moves_from_node.moves_left;

    MoveSet cur_legal_moves_from_node = legal_moves_from_node;

    for (u32 child_index = 0; child_index < children_nodes->number_of_children; ++child_index)
    {
        Node *child_node = children_nodes->children[child_index];
        assert(child_node != nullptr && child_node->move_to_get_here.IsValid());

        cur_legal_moves_from_node.DeleteMove(child_node->move_to_get_here);

        if (child_node->terminal_info.terminal_type != TerminalType::NOT_TERMINAL)
        {
            assert(child_node->num_simulations > 0 && "how is this child node chosen as a move but not simulated once? Did it not get to backpropagation?");

            // TODO(david): only choose the winning node if its controlled
            // if its uncontrolled, then choose losing or anything but winning
            // TODO(david): think about this, having this reversed is a bit confusing, so think about how this makes sense
            // TODO(david): yet another place where using a rule table kind of structure makes sense
            if (child_node->terminal_info.terminal_type == TerminalType::WINNING)
            {
                // NOTE(david): if current node is controlled, then choose the terminal node (aka move) that is winning
                if (from_node->controlled_type == ControlledType::CONTROLLED)
                {
                    selected_node = child_node;

                    return selected_node;
                }
                else if (from_node->controlled_type == ControlledType::UNCONTROLLED)
                {
                    // TODO(david): this should never be choosen by the uncontrolled, however note, that if all of the moves are winning, then the controlled parent should be marked as winning
                    if (winning_node == nullptr)
                    {
                        winning_node = child_node;
                        winning_uct = UCT_Uncontrolled(child_node, number_of_branches);
                    }
                    else
                    {
                        r64 uct = UCT_Uncontrolled(child_node, number_of_branches);
                        // NOTE(david): the uncontrolled node wants a uct that is a mix of low exploitation and high exploration
                        if (uct > winning_uct)
                        {
                            winning_node = child_node;
                            winning_uct = uct;
                        }
                    }
                }
                else
                {
                    UNREACHABLE_CODE;
                }
            }
            else if (child_node->terminal_info.terminal_type == TerminalType::NEUTRAL)
            {
                // NOTE(david): if the move is neutral then there is no difference between controlled vs uncontrolled move

                if (from_node->controlled_type == ControlledType::CONTROLLED)
                {
                    if (neutral_controlled_node == nullptr)
                    {
                        neutral_controlled_node = child_node;
                        neutral_controlled_uct = UCT(child_node, number_of_branches);
                    }
                    else
                    {
                        r64 uct = UCT(child_node, number_of_branches);
                        if (uct > neutral_controlled_uct)
                        {
                            neutral_controlled_node = child_node;
                            neutral_controlled_uct = uct;
                        }
                    }
                }
                else if (from_node->controlled_type == ControlledType::UNCONTROLLED)
                {
                    if (neutral_uncontrolled_node == nullptr)
                    {
                        neutral_uncontrolled_node = child_node;
                        neutral_uncontrolled_uct = UCT_Uncontrolled(child_node, number_of_branches);
                    }
                    else
                    {
                        r64 uct = UCT_Uncontrolled(child_node, number_of_branches);
                        // NOTE(david): the uncontrolled node wants a uct that is a mix of low exploitation and high exploration
                        if (uct > neutral_uncontrolled_uct)
                        {
                            neutral_uncontrolled_node = child_node;
                            neutral_uncontrolled_uct = uct;
                        }
                    }
                }
                else
                {
                    UNREACHABLE_CODE;
                }
            }
            else if (child_node->terminal_info.terminal_type == TerminalType::LOSING)
            {
                if (from_node->controlled_type == ControlledType::CONTROLLED)
                {
                    // NOTE(david): never choose this, but note that we had a losing move
                    if (losing_node == nullptr)
                    {
                        losing_node = child_node;
                        losing_uct = UCT(child_node, number_of_branches);
                    }
                    else
                    {
                        r64 uct = UCT(child_node, number_of_branches);
                        // NOTE(david): keep the losing move that loses in the most amount of moves or if tied, the one with the highest uct
                        if (uct > losing_uct)
                        {
                            losing_node = child_node;
                            losing_uct = uct;
                        }
                    }
                }
                else if (from_node->controlled_type == ControlledType::UNCONTROLLED)
                {
                    assert(false && "shouldn't this be propagated back already making the from_node terminally losing?");
                    // NOTE(david): if the node is uncontrolled and the move is losing, then choose this as the uncontrolled player wants us to lose
                    selected_node = child_node;

                    return selected_node;
                }
                else
                {
                    UNREACHABLE_CODE;
                }
            }
            else
            {
                LOGV(cerr, child_node);
                UNREACHABLE_CODE;
            }
        }
        else
        {
            assert(child_node->num_simulations > 0 && "how is this child node chosen as a move but not simulated once? Did it not get to backpropagation?");

            if (selected_node == nullptr)
            {
                selected_node = child_node;
                selected_uct = UCT(child_node, number_of_branches);
            }
            else if (from_node->controlled_type == ControlledType::CONTROLLED)
            {
                r64 uct = UCT(child_node, number_of_branches);
                if (uct > selected_uct)
                {
                    selected_node = child_node;
                    selected_uct = uct;
                }
            }
            else if (from_node->controlled_type == ControlledType::UNCONTROLLED)
            {
                r64 uct = UCT_Uncontrolled(child_node, number_of_branches);
                // NOTE(david): the uncontrolled node wants a uct that is a mix of low exploitation and high exploration, basically we still want the uncontrolled node to explore in order to find terminal moves, but it also wants nodes that have low mean values (aka exploit part of the uct formula)
                if (uct > selected_uct)
                {
                    selected_node = child_node;
                    selected_uct = uct;
                }
            }
            else
            {
                UNREACHABLE_CODE;
            }
        }
    }

    // NODE(david): if no neutral node -> choose an empty one if there is one
    // NOTE(david): if there are still possible moves from the move set, but the children table isn't full yet
    bool check_for_new_moves = cur_legal_moves_from_node.moves_left > 0;
    // ASSUMPTION(david): if there was either a losing choice for an uncontrolled node or a winning choice for a controlled node, we should have already returned at this point
    // TODO(david): think about if there is at least one neutral node, it shouldn't necessarily be selected, as there hasn't been all moves explored yet
    // neutral_controlled_node == nullptr && neutral_uncontrolled_node == nullptr && 
    if (check_for_new_moves == true)
    {
        i32 highest_serialized_move = children_nodes->highest_serialized_move;
        i32 lower_bound_move_index = -1;
        i32 lower_bound_serialized_move = -1;
        // NOTE(david): get the lower bound serialized_move from the set of legal moves
        for (u32 move_index = 0; move_index < cur_legal_moves_from_node.moves_left; ++move_index)
        {
            u32 move_serialized = cur_legal_moves_from_node.moves[move_index].Serialize();
            if (highest_serialized_move == -1 || move_serialized > highest_serialized_move)
            {
                // NOTE(david): if haven't chosen a move before or the current move is higher by order
                if (lower_bound_serialized_move == -1 || move_serialized < lower_bound_serialized_move)
                {
                    lower_bound_serialized_move = move_serialized;
                    lower_bound_move_index = move_index;
                }
            }
        }
        if (lower_bound_move_index != -1)
        {
            assert(lower_bound_move_index >= 0 && lower_bound_move_index < cur_legal_moves_from_node.moves_left);
            Move selected_move = cur_legal_moves_from_node.moves[lower_bound_move_index];
            assert(selected_move.IsValid());

            if (children_nodes->number_of_children >= ArrayCount(NodePool::ChildrenTables::children))
            {
                if (selected_node == nullptr)
                {
                    // NOTE(david): we only have terminal nodes that doesn't favor either side here, so prune one of the children (probably the worst one) and then expand with the new move
                    // TODO(david): select the terminal move that is the worst amongst all of them
                    if (from_node->controlled_type == ControlledType::CONTROLLED)
                    {
                        if (losing_node)
                        {
                            PruneNode(losing_node, node_pool);
                        }
                        else if (neutral_controlled_node)
                        {
                            PruneNode(neutral_controlled_node, node_pool);
                        }
                        else
                        {
                            UNREACHABLE_CODE;
                        }
                    }
                    else if (from_node->controlled_type == ControlledType::UNCONTROLLED)
                    {
                        if (winning_node)
                        {
                            PruneNode(winning_node, node_pool);
                        }
                        else if (neutral_uncontrolled_node)
                        {
                            PruneNode(neutral_uncontrolled_node, node_pool);
                        }
                        else
                        {
                            UNREACHABLE_CODE;
                        }
                    }
                    else
                    {
                        UNREACHABLE_CODE;
                    }

                    assert(children_nodes->number_of_children < ArrayCount(NodePool::ChildrenTables::children));
                    selected_node = _Expansion(from_node, node_pool);
                    node_pool.AddChild(from_node, selected_node, selected_move);
                }
                else
                {
                    // NOTE(david): already have a selected move.. since we have a limited action space to simulate at a given time, it makes sense to keep the currently selected nodes, however, if there is a bad move, we can replace it with a new one
                    // NOTE(david): optimally we would also like to explore all the possibilities to a certain depth in order to determine if there are nearby terminal moves, how to do this? One approach that comes to mind is that after certain amounts of simulations, we could prune nodes further, the idea behind this is if that node is not terminal yet (which it isn't at this point) and if the mean value is below a treshold, then we could assume, that the node is not worth exploring further with a high confidence, so prune the node and choose a new move.. we could arrive at a state where we pruned all children, which should already be taken care of. What should the number of simulations be, where we are certain enough that the node can be condition checked to prune? Also what happens, when none of the nodes meet the condition check? Then all the children slots are taken by nodes which are okayish moves.. but we would still not know if there is a terminal moves from the unexplored ones.. so then we would want to prune the worse out of all the promising moves in order to explore further new ones, as there is no benefit in keeping the worst, as we'd just choose the best anyway.
                    Node *node_to_prune = nullptr;
                    // TODO(david): similar logic exists in backpropagation, maybe store them in a central place?
                    // TODO(david): the number of simulations treshold should maybe be a dynamic parameter depending on how much time is allowed to think
                    u32 min_simulation_confidence_cycle_treshold = (u32)(500.0 / sqrt(from_node->depth + 1)) + 50;
                    // NOTE(david): two tresholds for controlled/uncontrolled
                    constexpr r64 min_mean_value_treshold = -0.95;
                    constexpr r64 max_mean_value_treshold = 0.95;

                    u32 condition_checked_nodes_on_their_simulation_count = 0;
                    pair<Node *, r64> mean_child_extremum = {};

                    for (u32 child_index = 0; child_index < children_nodes->number_of_children && node_to_prune == nullptr; ++child_index)
                    {
                        Node *child_node = children_nodes->children[child_index];
                        switch (from_node->controlled_type)
                        {
                            case ControlledType::CONTROLLED: {
                                assert(child_node->terminal_info.terminal_type != TerminalType::WINNING && "todo: shoulnd't we have returned with this already?");
                                if (child_node->terminal_info.terminal_type == TerminalType::LOSING)
                                {
                                    node_to_prune = child_node;
                                }
                                else if (child_node->num_simulations >= min_simulation_confidence_cycle_treshold)
                                {
                                    ++condition_checked_nodes_on_their_simulation_count;
                                    r64 child_mean_value = child_node->value / (r64)child_node->num_simulations;
                                    if (child_mean_value <= min_mean_value_treshold)
                                    {
                                        node_to_prune = child_node;
                                    }
                                    else
                                    {
                                        // NOTE(david): keep the one with the lowest mean to prune
                                        if (mean_child_extremum.first == nullptr || child_mean_value < mean_child_extremum.second)
                                        {
                                            mean_child_extremum.first = child_node;
                                            mean_child_extremum.second = child_mean_value;
                                        }
                                    }
                                }
                            } break ;
                            case ControlledType::UNCONTROLLED: {
                                assert(child_node->terminal_info.terminal_type != TerminalType::LOSING && "todo: shoulnd't we have returned with this already?");
                                if (child_node->terminal_info.terminal_type == TerminalType::WINNING)
                                {
                                    node_to_prune = child_node;
                                }
                                else if (child_node->num_simulations >= min_simulation_confidence_cycle_treshold)
                                {
                                    ++condition_checked_nodes_on_their_simulation_count;
                                    r64 child_mean_value = child_node->value / (r64)child_node->num_simulations;
                                    if (child_mean_value >= max_mean_value_treshold)
                                    {
                                        node_to_prune = child_node;
                                    }
                                    else
                                    {
                                        // NOTE(david): keep the one with the highest mean to prune
                                        if (mean_child_extremum.first == nullptr || child_mean_value > mean_child_extremum.second)
                                        {
                                            mean_child_extremum.first = child_node;
                                            mean_child_extremum.second = child_mean_value;
                                        }
                                    }
                                }
                            } break ;
                            default: {
                                UNREACHABLE_CODE;
                            }
                        }
                    }
                    if (node_to_prune == nullptr)
                    {
                        // NOTE(david): none of the moves are terminal and none of them falls outside the mean value with enough simulations
                        // NOTE(david): if there is only 1 condition checked node that have enough simulations, don't prune it yet, as it's our current best node
                        if (condition_checked_nodes_on_their_simulation_count != 1)
                        {
                            node_to_prune = mean_child_extremum.first;
                        }
                    }

                    if (node_to_prune != nullptr)
                    {
                        // NOTE(david): if either one of the moves is terminally bad or all the nodes are non-terminal and either we have a node that falls outside of the mean value interval or none of them falls outside, in which case the one with the worst mean is selected
                        PruneNode(node_to_prune, node_pool);
                        selected_node = _Expansion(from_node, node_pool);
                        node_pool.AddChild(from_node, selected_node, selected_move);
                    }
                    else
                    {
                        // NOTE(david): none of the children are terminally bad and none of them have enough simulations to determine what to prune
                    }
                }
            }
            else
            {
                // NOTE(david): can safely expand as there are available children slots
                selected_node = _Expansion(from_node, node_pool);
                node_pool.AddChild(from_node, selected_node, selected_move);
            }

            return selected_node;
        }
        else
        {
            // NOTE(david): none of the legal moves serialized are higher than the current highest serialized move stored in the child_table -> we don't have a new move
            if (from_node->parent == nullptr)
            {
                // LOG(cout, "all moves are exhausted for root node");
            }
        }
    }

    // NOTE(david): no non-terminal moves are found, based on if the node is controlled or uncontrolled, choose the node that is most favorable for the particular side
    if (selected_node == nullptr)
    {
        assert((u32)TerminalType::TerminalType_Size == 4 && (u32)ControlledType::ControlledType_Size == 3 && "if TerminalType/ControlledType needs to be extended, we need to handle more cases here potentially");
        if (from_node->controlled_type == ControlledType::CONTROLLED)
        {
            assert(from_node->terminal_info.terminal_type == TerminalType::NOT_TERMINAL && "if from_node was terminal, we wouldn't need to select its child for the next move");
            if (neutral_controlled_node != nullptr)
            {
                // NOTE(david): we had at least 1 neutral node so choose this over any potentially losing moves as the controlled node
                assert(neutral_controlled_node->terminal_info.terminal_type == TerminalType::NEUTRAL);
                from_node->terminal_info.terminal_type = neutral_controlled_node->terminal_info.terminal_type;

                selected_node = from_node;
            }
            else if (losing_node != nullptr)
            {
                // NOTE(david): all moves are losing, we should have chosen the one, that is the least losing, aka with the highest uct
                assert(losing_node->terminal_info.terminal_type == TerminalType::LOSING);
                from_node->terminal_info.terminal_type = losing_node->terminal_info.terminal_type;

                selected_node = from_node;
            }
            else
            {
                // NOTE(david): all children nodes are pruned out -> mark the node as a losing one.. during backpropagation, its parent is updated
                from_node->terminal_info.terminal_type = TerminalType::LOSING;
                selected_node = from_node;
            }
        }
        else if (from_node->controlled_type == ControlledType::UNCONTROLLED)
        {
            if (neutral_uncontrolled_node != nullptr)
            {
                // NOTE(david): we had at least 1 neutral node so choose this over any potentially winning moves as the uncontrolled node
                assert(neutral_uncontrolled_node->terminal_info.terminal_type == TerminalType::NEUTRAL);
                from_node->terminal_info.terminal_type = neutral_uncontrolled_node->terminal_info.terminal_type;

                selected_node = from_node;
            }
            else if (winning_node != nullptr)
            {
                assert(winning_node->terminal_info.terminal_type == TerminalType::WINNING);
                from_node->terminal_info.terminal_type = winning_node->terminal_info.terminal_type;

                selected_node = from_node;
            }
            else
            {
                // NOTE(david): all children nodes are pruned out -> mark the node as a losing one.. during backpropagation, its parent is updated
                from_node->terminal_info.terminal_type = TerminalType::LOSING;
                selected_node = from_node;
            }
        }
        else
        {
            UNREACHABLE_CODE;
        }
    }

    return selected_node;
}

// TODO(david): use transposition table to speed up the selection, which would store previous searches, allowing to avoid re-exploring parts of the tree that have already been searched
// TODO(david): LRU?
MCST::SelectionResult MCST::_Selection(const MoveSet &legal_moveset_at_root_node, NodePool &node_pool)
{
    SelectionResult selection_result = {};

    if (_root_node->terminal_info.terminal_type != TerminalType::NOT_TERMINAL)
    {
        selection_result.selected_node = _root_node;

        return selection_result;
    }

    Node *current_node = _root_node;
    MoveSet current_legal_moves = legal_moveset_at_root_node;
    // TODO(david): move depth into the node as it makes sense when calculating the best next move to return from Evaluate
    // bool focus_on_lowest_utc_to_prune = GetRandomNumber(0, 10) < 0;
    bool focus_on_lowest_utc_to_prune = false;
    while (1)
    {
        if (current_legal_moves.moves_left == 0)
        {
            // NOTE(david): all moves are exhausted, reached terminal node
            break;
        }

        assert(current_node->terminal_info.terminal_type == TerminalType::NOT_TERMINAL && "if current node is a terminal type, we must have returned it already after _SelectChild");
        // Select a child node and its corresponding legal move based on maximum UCT value and some other heuristic
        Node *selected_child_node = _SelectChild(current_node, current_legal_moves, focus_on_lowest_utc_to_prune, node_pool);
        assert(selected_child_node != nullptr);

        if (selected_child_node->terminal_info.terminal_type != TerminalType::NOT_TERMINAL)
        {
            selection_result.selected_node = selected_child_node;

            return selection_result;
            // NOTE(david): current node isnt winning and it's parent is updated in _SelectChild, start with a new selection from the root
            // TODO(david): make sure there isn't an infinite loop, meaning that the parent is indeed updated
            // return _Selection(legal_moveset_at_root_node, move_processor, node_pool);
        }

        selection_result.selected_node = selected_child_node;
        selection_result.movesequence_from_position.AddMove(selected_child_node->move_to_get_here);
        // NOTE(david): the selected child is an unexplored one
        if (selected_child_node->num_simulations == 0)
        {
            return selection_result;
        }

        current_legal_moves.DeleteMove(selected_child_node->move_to_get_here);

        current_node = selected_child_node;
    }

    return selection_result;
}

Node *MCST::_Expansion(Node *from_node, NodePool &node_pool)
{
    Node *result = node_pool.AllocateNode(from_node);
    if (!(from_node->controlled_type == ControlledType::CONTROLLED || from_node->controlled_type == ControlledType::UNCONTROLLED))
    {
        DebugPrintDecisionTree(_root_node, g_move_counter, node_pool, *debug_game_state);
        LOG(cerr, "from_node: " << from_node);
        assert((from_node->controlled_type == ControlledType::CONTROLLED || from_node->controlled_type == ControlledType::UNCONTROLLED) && "from_node's controlled type is not initialized");
    }
    result->controlled_type = from_node->controlled_type == ControlledType::CONTROLLED ? ControlledType::UNCONTROLLED : ControlledType::CONTROLLED;

    return result;
}

static void _updateTerminalDepthForParentNode(Node *cur_node)
{
    Node *parent_node = cur_node->parent;
    assert(parent_node != nullptr);
    if (parent_node->terminal_info.terminal_depth.winning == 0)
    {
        parent_node->terminal_info.terminal_depth.winning = cur_node->terminal_info.terminal_depth.winning;
    }
    if (parent_node->terminal_info.terminal_depth.losing == 0)
    {
        parent_node->terminal_info.terminal_depth.losing = cur_node->terminal_info.terminal_depth.losing;
    }
    if (parent_node->terminal_info.terminal_depth.neutral == 0)
    {
        parent_node->terminal_info.terminal_depth.neutral = cur_node->terminal_info.terminal_depth.neutral;
    }

    if (cur_node->terminal_info.terminal_depth.winning < parent_node->terminal_info.terminal_depth.winning)
    {
        parent_node->terminal_info.terminal_depth.winning = cur_node->terminal_info.terminal_depth.winning;
    }
    if (cur_node->terminal_info.terminal_depth.losing > parent_node->terminal_info.terminal_depth.losing)
    {
        parent_node->terminal_info.terminal_depth.losing = cur_node->terminal_info.terminal_depth.losing;
    }
    if (cur_node->terminal_info.terminal_depth.neutral > parent_node->terminal_info.terminal_depth.neutral)
    {
        parent_node->terminal_info.terminal_depth.neutral = cur_node->terminal_info.terminal_depth.neutral;
    }
}

void MCST::PruneNode(Node *node_to_prune, NodePool &node_pool)
{
    assert(node_to_prune != _root_node && "TODO: what does it mean to prune the root node?");
    // LOG(cout, "pruning node..: " << node_to_prune);

    // NOTE(david): backpropagate the fact that the node is pruned -> update variance and other values of the parent nodes
    // NOTE(david): the selection algorithm knows that this move shouldn't be picked anymore, as the moves are selected sequentially by their order of serialization
    r64 from_node_mean = node_to_prune->value / (r64)node_to_prune->num_simulations;
    Node *cur_node = node_to_prune->parent;
    while (cur_node != nullptr)
    {
        cur_node->value -= node_to_prune->value;
        assert(cur_node->num_simulations >= node_to_prune->num_simulations);
        cur_node->num_simulations -= node_to_prune->num_simulations;
        if (cur_node->num_simulations == 0)
        {
            DebugPrintDecisionTree(_root_node, g_move_counter, node_pool, *debug_game_state);
            LOG(cerr, "node_to_prune: " << node_to_prune);
            LOG(cerr, "cur_node: " << cur_node);
            assert(false && "this shouldn't be possible, as the node's children's total number of simulation should be less than the parent's, as there is at least 1 unique simulation from the parent");
        }

        cur_node = cur_node->parent;
    }

    // NOTE(david): after done updating the parents, free the node and its children
    node_pool.FreeNode(node_to_prune);
}

void MCST::_BackPropagate(Node *from_node, NodePool &node_pool, SimulationResult simulation_result)
{
    assert(from_node != _root_node && "root node is not a valid move so it couldn't have been simulated");
    assert(_root_node->terminal_info.terminal_type == TerminalType::NOT_TERMINAL);

    assert(from_node->terminal_info.terminal_type != TerminalType::NOT_TERMINAL || from_node->num_simulations == 1 && "TODO: current implementation: simulate once, I need to reintroduce this number to the simulation result maybe");

    /*
        NOTE(david): don't backpropagate if the node needs to be pruned
        Criterias under which the node needs to be pruned:
            // NOTE(david): implemented this, but didn't find it to be necessary, the mean value is enough? - with high certainty (narrow confidence interval) the value is below a certain threshold
            // TODO(david): - understand and maybe implement this: if the confidence interval doesn't overlap with the parent's confidence interval (? because the outcome of the node isn't very different therefore it's not worth exploring further)
    */

    if (from_node->terminal_info.terminal_type != TerminalType::NOT_TERMINAL)
    {
        Node *parent_node = from_node->parent;
        assert(parent_node != nullptr && "node can't be root to propagate back from, as if it was terminal we should have already returned an evaluation result");

        switch (from_node->terminal_info.terminal_type)
        {
            case TerminalType::WINNING: {
                from_node->terminal_info.terminal_depth.winning = from_node->depth;
            } break ;
            case TerminalType::LOSING: {
                from_node->terminal_info.terminal_depth.losing = from_node->depth;
            } break ;
            case TerminalType::NEUTRAL: {
                from_node->terminal_info.terminal_depth.neutral = from_node->depth;
            } break ;
            default: UNREACHABLE_CODE;
        }

        if (from_node->controlled_type == ControlledType::UNCONTROLLED && from_node->terminal_info.terminal_type == TerminalType::WINNING || from_node->controlled_type == ControlledType::CONTROLLED && from_node->terminal_info.terminal_type == TerminalType::LOSING)
        {
            parent_node->terminal_info.terminal_type = from_node->terminal_info.terminal_type;
        }
        _updateTerminalDepthForParentNode(from_node);
    }

    Node *cur_node = from_node->parent;
    while (cur_node != nullptr)
    {
        Node *parent_node = cur_node->parent;

        // TODO(david): add backpropagating rules terminal rules here?
        if (parent_node)
        {
            _updateTerminalDepthForParentNode(cur_node);
        }

        cur_node->num_simulations += from_node->num_simulations;
        cur_node->value += from_node->value;

        if (cur_node != _root_node)
        {
            // TODO(david): is this the right place to prune the node based on its mean value
            // NOTE(david): once the node is eligible for pruning, derail this subroutine to pruning instead of updating

            assert(cur_node->depth > 0);
            // TODO(david): the number of simulations treshold should maybe be a dynamic parameter depending on how much time is allowed to think
            u32 number_of_simulations_treshold = (u32)(3000.0 / sqrt(cur_node->depth)) + 50;
            if (cur_node->num_simulations >= number_of_simulations_treshold)
            {
                constexpr r64 lower_mean_prune_treshold = -0.95;
                constexpr r64 upper_mean_prune_treshold = 0.95;
                r64 mean = cur_node->value / (r64)cur_node->num_simulations;
                if (mean <= lower_mean_prune_treshold)
                {
                    if (cur_node->controlled_type == ControlledType::CONTROLLED)
                    {
                        cur_node->terminal_info.terminal_type = TerminalType::LOSING;
                        cur_node->terminal_info.terminal_depth.losing = cur_node->depth;

                        if (parent_node)
                        {
                            parent_node->terminal_info.terminal_type = cur_node->terminal_info.terminal_type;
                            _updateTerminalDepthForParentNode(cur_node);
                        }
                    }
                    else
                    {
                        // NOTE(david): haven't finished backpropagation of the simulation, so account for this fact
                        cur_node->num_simulations -= from_node->num_simulations;
                        cur_node->value -= from_node->value;
                        PruneNode(cur_node, node_pool);
                        return ;
                    }
                }
                else if (mean >= upper_mean_prune_treshold)
                {
                    if (cur_node->controlled_type == ControlledType::UNCONTROLLED)
                    {
                        cur_node->terminal_info.terminal_type = TerminalType::WINNING;
                        cur_node->terminal_info.terminal_depth.winning = cur_node->depth;

                        if (parent_node)
                        {
                            parent_node->terminal_info.terminal_type = cur_node->terminal_info.terminal_type;
                            _updateTerminalDepthForParentNode(cur_node);                        
                        }
                    }
                    else
                    {
                        cur_node->num_simulations -= from_node->num_simulations;
                        cur_node->value -= from_node->value;
                        PruneNode(cur_node, node_pool);
                        return ;
                    }
                }
            }
        }

        cur_node = parent_node;
    }
}

u32 MCST::NumberOfSimulationsRan(void)
{
    return _root_node->num_simulations;
}
