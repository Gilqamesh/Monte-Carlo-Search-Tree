#include "MCST.hpp"
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>

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

ostream &operator<<(ostream &os, const Node *node)
{
    LOGN(os, MoveToWord(node->move_to_get_here) << ", value: " << node->value << ", sims: " << node->num_simulations << ", " << ControlledTypeToWord(node->controlled_type)) << ", " << TerminalTypeToWord(node->terminal_info.terminal_type) << ", terminal depth(W/L/N): (" << node->terminal_info.terminal_depth.winning << "," << node->terminal_info.terminal_depth.losing << "," << node->terminal_info.terminal_depth.neutral << ")";

    return os;
}

static u32 GetNextPowerOfTwo(u32 number)
{
    u32 result = pow(2.0, ceil(log2(number)));

    return result;
}

NodePool::NodePool(NodeIndex number_of_nodes_to_allocate)
    : _number_of_nodes_allocated(number_of_nodes_to_allocate),
      _available_node_index(0),
      _free_nodes_index(-1)
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

    memset(_move_to_node_tables, 0, _number_of_nodes_allocated * sizeof(*_move_to_node_tables));
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
    if (parent)
    {
        node->parent = parent->index;
        node->depth = parent->depth + 1;
    }
    else
    {
        node->depth = 0;
        node->parent = -1;
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

void NodePool::FreeNode(Node *node)
{
    ++_free_nodes_index;
    _move_to_node_tables[node->index] = {};
}

void NodePool::AddChild(Node *node, Node *child, Move move)
{
    ChildrenTables *table = &_move_to_node_tables[node->index];
    assert(table->number_of_children < ArrayCount(ChildrenTables::children));
    assert(table->children[table->number_of_children] == nullptr);
    table->children[table->number_of_children] = child;

    ++table->number_of_children;

    child->move_to_get_here = move;
}

NodePool::ChildrenTables *NodePool::GetChildren(Node *node)
{
    assert(node->index >= 0 && node->index < _available_node_index);
    return (&_move_to_node_tables[node->index]);
}

Node *NodePool::GetParent(Node *node) const
{
    Node *parent_node = nullptr;

    if (node->parent != -1)
    {
        parent_node = &_nodes[node->parent];
    }

    return parent_node;
}

void NodePool::Clear()
{
    memset(_move_to_node_tables, 0, _available_node_index * sizeof(*_move_to_node_tables));
    _available_node_index = 0;
    _free_nodes_index = -1;
}

// static r64 g_tuned_exploration_factor_weight = 0.422;
static r64 g_tuned_exploration_factor_weight = 1.0;
static r64 UCT(Node *node, u32 number_of_branches, NodePool &node_pool)
{
    assert(node != nullptr);
    assert(node->parent != -1);
    assert(node->num_simulations != 0);

    r64 result = 0.0;

    r64 depth_weight = 1.0 / (r32)node->depth;
    // TODO(david): think about this number and how it should affect explitation vs exploration
    // NOTE(david): go with exploration if there are a lot of branches and exploit more the less choices there are
    r64 number_of_branches_weight = 0.2 * number_of_branches;
    r64 weighted_exploration_factor = number_of_branches_weight * g_tuned_exploration_factor_weight * EXPLORATION_FACTOR / (node->depth * depth_weight);
    Node *parent_node = node_pool.GetParent(node);
    assert(parent_node != nullptr);
    result = (r64)node->value / (r64)node->num_simulations +
             weighted_exploration_factor * sqrt(log((r64)parent_node->num_simulations) / (r64)node->num_simulations);

    return result;
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
                    r64 uct = UCT(child, number_of_branches, node_pool);
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
                    r64 uct = UCT(child, number_of_branches, node_pool);
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
                    r64 uct = UCT(child, number_of_branches, node_pool);
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
            r64 uct = UCT(child, number_of_branches, node_pool);
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

static void DebugPrintDecisionTreeHelper(Node *from_node, Player player_to_move, u32 depth, ofstream &tree_fs, NodePool &node_pool)
{
    // TODO(david): move depth to node
    LOG(tree_fs, string(depth * 4, ' ') << "(player to move: " << PlayerToWord(player_to_move) << ", depth: " << depth << ", " << from_node << ")");

    NodePool::ChildrenTables *children_nodes = node_pool.GetChildren(from_node);
    for (u32 child_index = 0; child_index < children_nodes->number_of_children; ++child_index)
    {
        Node *child_node = children_nodes->children[child_index];
        assert(child_node != nullptr && child_node->move_to_get_here.IsValid());

        DebugPrintDecisionTreeHelper(child_node, player_to_move == Player::CIRCLE ? Player::CROSS : Player::CIRCLE, depth + 1, tree_fs, node_pool);
    }
}

static void DebugPrintDecisionTree(Node *from_node, u32 move_counter, NodePool &node_pool, const GameState &game_state)
{
    ofstream tree_fs("debug/trees/tree" + to_string(move_counter));
    DebugPrintDecisionTreeHelper(from_node, game_state.player_to_move, 0, tree_fs, node_pool);
}

static u32 g_move_counter;

Move MCST::Evaluate(const MoveSet &legal_moveset_at_root_node, TerminationPredicate termination_predicate, SimulateFromState simulation_from_state, NodePool &node_pool, const GameState &game_state)
{
    if (legal_moveset_at_root_node.moves_left == 0)
    {
        Move move;
        move.Invalidate();
        return move;
    }
    node_pool.Clear();
    _root_node = node_pool.AllocateNode(nullptr);
    _root_node->controlled_type = ControlledType::CONTROLLED;
    
    u32 EvaluateIterations = 0;
    double selection_cycles_total = 0;
    u32 selection_cycles_count = 0;
    double simulation_cycles_total = 0;
    u32 simulation_cycles_count = 0;
    double backpropagate_cycles_total = 0;
    u32 backpropagate_cycles_count = 0;
    while (termination_predicate(false) == false)
    {
        TIMED_BLOCK("_Selection", SelectionResult selection_result = _Selection(legal_moveset_at_root_node, node_pool));
        // SelectionResult selection_result = _Selection(legal_moveset_at_root_node, move_processor, node_pool);
        selection_cycles_total += g_clock_cycles_var;
        ++selection_cycles_count;

        if (selection_result.selected_node->terminal_info.terminal_type != TerminalType::NOT_TERMINAL)
        {
            if (selection_result.selected_node == _root_node)
            {
                // NOTE(david): if root node is a terminal node, it means that no more simulations are needed
                termination_predicate(true);
                break ;
            }

// #if defined(DEBUG_WRITE_OUT)
//             DebugPrintDecisionTree(_root_node, g_move_counter, node_pool);
// #endif

        }
        else
        {
            TIMED_BLOCK("simulation_from_state", simulation_from_state(selection_result.movesequence_from_position, game_state, selection_result.selected_node, node_pool));
            // simulation_from_state(selection_result.movesequence_from_position, selection_result.selected_node, node_pool);
            simulation_cycles_total += g_clock_cycles_var;
            ++simulation_cycles_count;

            if (selection_result.selected_node->num_simulations > 10000000)
            {
#if defined(DEBUG_WRITE_OUT)
    DebugPrintDecisionTree(_root_node, g_move_counter, node_pool, game_state);
#endif
                assert(false && "suspicious amount of simulations, make sure this could happen");
            }
        }
        TIMED_BLOCK("_BackPropagate", _BackPropagate(selection_result.selected_node, node_pool));
        backpropagate_cycles_total += g_clock_cycles_var;
        ++backpropagate_cycles_count;

        ++EvaluateIterations;
    }
    LOG(cout, "Evaluate iterations: " << EvaluateIterations << ", total: " << selection_cycles_total + backpropagate_cycles_total + simulation_cycles_total << "M");
    LOG(cout, "Selection total cycles: " << selection_cycles_total << "M");
    LOG(cout, "Backpropagate total cycles: " << backpropagate_cycles_total << "M");
    LOG(cout, "Simulation total cycles: " << simulation_cycles_total << "M");

#if defined(DEBUG_WRITE_OUT)
    DebugPrintDecisionTree(_root_node, g_move_counter, node_pool, game_state);
#endif

    // TIMED_BLOCK("winning_move_selection_strategy_fn", Move result_move = winning_move_selection_strategy_fn(_root_node, node_pool));
    Move result_move = winning_move_selection_strategy_fn(_root_node, node_pool);

    return result_move;
}

Node *MCST::_SelectChild(Node *from_node, const MoveSet &legal_moves_from_node, bool focus_on_lowest_utc_to_prune, NodePool &node_pool)
{
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
                        winning_uct = UCT(child_node, number_of_branches, node_pool);
                    }
                    else
                    {
                        r64 uct = UCT(child_node, number_of_branches, node_pool);
                        // TODO(david): the uncontrolled node wants a uct that is a mix of low exploitation and high exploration
                        if (uct < winning_uct)
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
                        neutral_controlled_uct = UCT(child_node, number_of_branches, node_pool);
                    }
                    else
                    {
                        r64 uct = UCT(child_node, number_of_branches, node_pool);
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
                        neutral_uncontrolled_uct = UCT(child_node, number_of_branches, node_pool);
                    }
                    else
                    {
                        r64 uct = UCT(child_node, number_of_branches, node_pool);
                        // TODO(david): the uncontrolled node wants a uct that is a mix of low exploitation and high exploration
                        if (uct < neutral_uncontrolled_uct)
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
                        losing_uct = UCT(child_node, number_of_branches, node_pool);
                    }
                    else
                    {
                        r64 uct = UCT(child_node, number_of_branches, node_pool);
                        // NOTE(david): the uncontrolled node wants a uct that is a mix of low exploitation and high exploration
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
                selected_uct = UCT(child_node, number_of_branches, node_pool);
            }
            else if (from_node->controlled_type == ControlledType::CONTROLLED)
            {
                r64 uct = UCT(child_node, number_of_branches, node_pool);
                if (uct > selected_uct)
                {
                    selected_node = child_node;
                    selected_uct = uct;
                }
            }
            else if (from_node->controlled_type == ControlledType::UNCONTROLLED)
            {
                r64 uct = UCT(child_node, number_of_branches, node_pool);
                // NOTE(david): the uncontrolled node wants a uct that is a mix of low exploitation and high exploration
                if (uct < selected_uct)
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
    // TODO(david): dynamic sized children table, if it's necessary to have more children in a position, then the table shouldn't be bounded in size
    bool found_empty_child_node = (cur_legal_moves_from_node.moves_left > 0 && children_nodes->number_of_children < ArrayCount(NodePool::ChildrenTables::children));
    if (neutral_controlled_node == nullptr && neutral_uncontrolled_node == nullptr && found_empty_child_node == true)
    {
        u32 random_move_index = GetRandomNumber(0, cur_legal_moves_from_node.moves_left - 1);
        Move random_move = cur_legal_moves_from_node.moves[random_move_index];
        assert(random_move.IsValid());

        selected_node = _Expansion(from_node, node_pool);
        node_pool.AddChild(from_node, selected_node, random_move);
        return selected_node;
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
                UNREACHABLE_CODE;
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
                UNREACHABLE_CODE;
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
            // if (selected_child_node->terminal_info.terminal_type == TerminalType::WINNING)
            // {
            //     selection_result.selected_node = selected_child_node;

            //     return selection_result;
            // }
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
    assert((from_node->controlled_type == ControlledType::CONTROLLED || from_node->controlled_type == ControlledType::UNCONTROLLED) && "from_node's controlled type is not initialized");
    result->controlled_type = from_node->controlled_type == ControlledType::CONTROLLED ? ControlledType::UNCONTROLLED : ControlledType::CONTROLLED;

    return result;
}

static void _updateTerminalDepthForParentNode(Node *cur_node, Node *parent_node)
{
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

    // if (parent_node->controlled_type == ControlledType::CONTROLLED)
    // {
    //     // NOTE(david): Controlled Node favors winning, so it should choose the move that leads to the least amount of moves to win, and the move that leads to the most amount of moves to lose
    //     if (cur_node->terminal_info.terminal_depth.winning < parent_node->terminal_info.terminal_depth.winning)
    //     {
    //         parent_node->terminal_info.terminal_depth.winning = cur_node->terminal_info.terminal_depth.winning;
    //     }
    //     if (cur_node->terminal_info.terminal_depth.losing > parent_node->terminal_info.terminal_depth.losing)
    //     {
    //         parent_node->terminal_info.terminal_depth.losing = cur_node->terminal_info.terminal_depth.losing;
    //     }
    //     if (cur_node->terminal_info.terminal_depth.neutral > parent_node->terminal_info.terminal_depth.neutral)
    //     {
    //         parent_node->terminal_info.terminal_depth.neutral = cur_node->terminal_info.terminal_depth.neutral;
    //     }
    // }
    // else if (parent_node->controlled_type == ControlledType::UNCONTROLLED)
    // {
    //     // NOTE(david): Uncontrolled Node favors losing outcome for us, so it should choose the move that leads to the most amount of moves that wins, and the move that leads to the least amount of moves to lose
    //     if (cur_node->terminal_info.terminal_depth.winning > parent_node->terminal_info.terminal_depth.winning)
    //     {
    //         parent_node->terminal_info.terminal_depth.winning = cur_node->terminal_info.terminal_depth.winning;
    //     }
    //     if (cur_node->terminal_info.terminal_depth.losing < parent_node->terminal_info.terminal_depth.losing)
    //     {
    //         parent_node->terminal_info.terminal_depth.losing = cur_node->terminal_info.terminal_depth.losing;
    //     }
    //     if (cur_node->terminal_info.terminal_depth.neutral > parent_node->terminal_info.terminal_depth.neutral)
    //     {
    //         parent_node->terminal_info.terminal_depth.neutral = cur_node->terminal_info.terminal_depth.neutral;
    //     }
    // }
    // else
    // {
    //     UNREACHABLE_CODE;
    // }
}

void MCST::_BackPropagate(Node *from_node, NodePool &node_pool)
{
    assert(from_node != _root_node && "root node is not a valid move so it couldn't have been simulated");
    assert(_root_node->terminal_info.terminal_type == TerminalType::NOT_TERMINAL);

    if (from_node->terminal_info.terminal_type != TerminalType::NOT_TERMINAL)
    {
        Node *parent_node = node_pool.GetParent(from_node);
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
        _updateTerminalDepthForParentNode(from_node, parent_node);
    }

    Node *cur_node = node_pool.GetParent(from_node);
    while (cur_node != nullptr)
    {
        Node *parent_node = node_pool.GetParent(cur_node);

        // TODO(david): add backpropagating rules terminal rules here?
        if (parent_node)
        {
            _updateTerminalDepthForParentNode(cur_node, parent_node);
        }

        cur_node->num_simulations += from_node->num_simulations;
        cur_node->value += from_node->value;

        cur_node = parent_node;
    }
}

u32 MCST::NumberOfSimulationsRan(void)
{
    return _root_node->num_simulations;
}
