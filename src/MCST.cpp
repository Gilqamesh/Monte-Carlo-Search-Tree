#include "MCST.hpp"
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>

#define ArrayCount(array) (sizeof(array) / sizeof((array)[0]))

static string MoveToWord(Move move)
{
    switch (move)
    {
    case TOP_LEFT:
        return "TOP_LEFT";
    case TOP_MID:
        return "TOP_MID";
    case TOP_RIGHT:
        return "TOP_RIGHT";
    case MID_LEFT:
        return "MID_LEFT";
    case MID_MID:
        return "MID_MID";
    case MID_RIGHT:
        return "MID_RIGHT";
    case BOTTOM_LEFT:
        return "BOTTOM_LEFT";
    case BOTTOM_MID:
        return "BOTTOM_MID";
    case BOTTOM_RIGHT:
        return "BOTTOM_RIGHT";
    case NONE:
        return "NONE";
    default:
    {
        UNREACHABLE_CODE;
        return "Breh";
    }
    }
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

ostream &operator<<(ostream &os, const Node *node)
{
    LOG(os, MoveToWord(node->move_to_get_here) << ", value: " << node->value << ", index: " << node->index << ", sims: " << node->num_simulations << ", parent: " << node->parent << ", " << TerminalTypeToWord(node->terminal_info.terminal_type) << ", " << ControlledTypeToWord(node->controlled_type));

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
    _move_to_node_tables = (MoveToNodeTable *)_aligned_malloc(_number_of_nodes_allocated * sizeof(*_move_to_node_tables), move_to_node_hash_table_alignment);
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

    for (u32 table_index = 0; table_index < _number_of_nodes_allocated; ++table_index)
    {
        for (u32 child_index = 0; child_index < ArrayCount(MoveToNodeTable::children); ++child_index)
        {
            _move_to_node_tables[table_index].children[child_index] = -1;
        }
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
    node->terminal_info.terminal_depth = 0;
    node->controlled_type = ControlledType::NONE;
    node->move_to_get_here = Move::NONE;

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
    for (u32 child_index = 0; child_index < ArrayCount(MoveToNodeTable::children); ++child_index)
    {
        _move_to_node_tables[node->index].children[child_index] = -1;
    }
}

void NodePool::AddChild(Node *node, Node *child, Move move)
{
    _move_to_node_tables[node->index].children[move] = child->index;
    child->move_to_get_here = move;
}

Node *NodePool::GetChild(Node *node, Move move)
{
    Node *child_node = nullptr;

    i32 node_index = _move_to_node_tables[node->index].children[move];
    if (node_index != -1)
    {
        child_node = &_nodes[node_index];
    }

    return child_node;
}

NodePool::MoveToNodeTable *NodePool::GetChildren(Node *node)
{
    assert(false && "not using it, but think about the access pattern of how we iterate over the children of a single node, currently it's only used with root when evaluating the move answer");
    return &_move_to_node_tables[node->index];
}

Node *NodePool::GetParent(Node *node)
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
    for (u32 table_index = 0; table_index < _available_node_index; ++table_index)
    {
        for (u32 child_index = 0; child_index < ArrayCount(MoveToNodeTable::children); ++child_index)
        {
            _move_to_node_tables[table_index].children[child_index] = -1;
        }
    }
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

    r64 depth_weight = 0.0;
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
static Move _EvaluateBasedOnMostSimulated(Node *node_to_evaluate_from, const MoveSet &legal_moveset_at_node_to_evaluate_from, NodePool &node_pool)
{
    // TODO(david): implement
    throw runtime_error("Not implemented");
}

static Move _EvaluateBasedOnUCT(Node *node_to_evaluate_from, const MoveSet &legal_moveset_at_node_to_evaluate_from, NodePool &node_pool)
{
    r64 highest_uct = -INFINITY;
    Node *current_highest_uct_node = nullptr;

    assert(node_to_evaluate_from->controlled_type == ControlledType::CONTROLLED && "the strategy here is to win, so if it's an uncontrolled node than the strategy would be to choose the worst uct or losing move");

    for (u32 move_index = 0; move_index < ArrayCount(legal_moveset_at_node_to_evaluate_from.moves); ++move_index)
    {
        Move move = legal_moveset_at_node_to_evaluate_from.moves[move_index];
        if (move == Move::NONE)
        {
            continue;
        }

        Node *child = node_pool.GetChild(node_to_evaluate_from, move);
        if (child == nullptr)
        {
            // NOTE(david): node isn't simulated yet -> continue
        }
        else if (child->terminal_info.terminal_type != TerminalType::NOT_TERMINAL)
        {
            switch (child->terminal_info.terminal_type)
            {
                case TerminalType::LOSING: {
                    r64 uct = UCT(child, legal_moveset_at_node_to_evaluate_from.moves_left, node_pool);
                    if (current_highest_uct_node == nullptr)
                    {
                        highest_uct = uct;
                        current_highest_uct_node = child;
                    }
                    else if (current_highest_uct_node->terminal_info.terminal_type == TerminalType::LOSING)
                    {
                        // TODO(david): choose the one with the higher terminal depth
                        r64 uct = UCT(child, legal_moveset_at_node_to_evaluate_from.moves_left, node_pool);
                        if (uct > highest_uct)
                        {
                            highest_uct = uct;
                            current_highest_uct_node = child;
                        }
                    }
                    else
                    {
                        // NOTE(david): if current_highest_uct_node isn't losing, then we wouldn't want to update it with a losing one -> continue
                    }
                } break ;
                case TerminalType::WINNING: {
                    return child->move_to_get_here;
                } break ;
                case TerminalType::NEUTRAL: {
                    // NOTE(david): dispatching over the previous highest uct node's terminal type
                    r64 uct = UCT(child, legal_moveset_at_node_to_evaluate_from.moves_left, node_pool);
                    if (current_highest_uct_node == nullptr)
                    {
                        highest_uct = uct;
                        current_highest_uct_node = child;
                    }
                    switch (current_highest_uct_node->terminal_info.terminal_type)
                    {
                        case TerminalType::WINNING: {
                            assert(false && "should already have returned the move if it's a terminal winning move");
                        } break ;
                        case TerminalType::LOSING: {
                            // NOTE(david): always replace losing moves with the current non-terminal move as there might be moves that aren't terminally losing
                            highest_uct = uct;
                            current_highest_uct_node = child;
                        } break ;
                        case TerminalType::NEUTRAL: {
                            assert(current_highest_uct_node != nullptr);
                            if (uct > highest_uct)
                            {
                                highest_uct = uct;
                                current_highest_uct_node = child;
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
            r64 uct = UCT(child, legal_moveset_at_node_to_evaluate_from.moves_left, node_pool);
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
                        assert(false && "should already have returned the move if it's a terminal winning move");
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

MCST::MCST(NodePool &node_pool)
{
    node_pool.Clear();
    _root_node = node_pool.AllocateNode(nullptr);
    _root_node->controlled_type = ControlledType::CONTROLLED;
    winning_move_selection_strategy_fn = &_EvaluateBasedOnUCT;
}

static void DebugPrintDecisionTreeHelper(Node *from_node, Player player_to_move, const MoveSet &legal_moveset_from_node, u32 depth, ofstream &tree_fs, NodePool &node_pool)
{
    // TODO(david): move depth to node
    LOG(tree_fs, string(depth * 4, ' ') << "(player to move: " << PlayerToWord(player_to_move) << ", depth: " << depth << ", " << from_node << ")");

    for (u32 move_index = 0; move_index < ArrayCount(legal_moveset_from_node.moves); ++move_index)
    {
        Move move = legal_moveset_from_node.moves[move_index];
        if (move == Move::NONE)
        {
            continue;
        }

        MoveSet moveset_from_node = legal_moveset_from_node;
        assert(moveset_from_node.moves[move] != Move::NONE);
        moveset_from_node.moves[move] = Move::NONE;
        --moveset_from_node.moves_left;

        Node *child_node = node_pool.GetChild(from_node, move);
        if (child_node == nullptr)
        {
            // isn't simulated yet
            continue;
        }

        DebugPrintDecisionTreeHelper(child_node, player_to_move == Player::CIRCLE ? Player::CROSS : Player::CIRCLE, moveset_from_node, depth + 1, tree_fs, node_pool);
    }
}

static void DebugPrintDecisionTree(Node *from_node, const MoveSet &legal_moveset_from_node, u32 move_counter, NodePool &node_pool, const GameState &game_state)
{
    ofstream tree_fs("debug/trees/tree" + to_string(move_counter));
    DebugPrintDecisionTreeHelper(from_node, game_state.player_to_move, legal_moveset_from_node, 0, tree_fs, node_pool);
}

static u32 g_move_counter;

Move MCST::Evaluate(const MoveSet &legal_moveset_at_root_node, TerminationPredicate termination_predicate, SimulateFromState simulation_from_state, MoveProcessor move_processor, NodePool &node_pool, const GameState &game_state)
{
    if (legal_moveset_at_root_node.moves_left == 0)
    {
        return Move::NONE;
    }

    u32 EvaluateIterations = 0;
    double selection_cycles_total = 0;
    u32 selection_cycles_count = 0;
    double simulation_cycles_total = 0;
    u32 simulation_cycles_count = 0;
    double backpropagate_cycles_total = 0;
    u32 backpropagate_cycles_count = 0;
    while (termination_predicate(false) == false)
    {
        TIMED_BLOCK("_Selection", SelectionResult selection_result = _Selection(legal_moveset_at_root_node, move_processor, node_pool));
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
                // return winning_move_selection_strategy_fn(_root_node, legal_moveset_at_root_node, node_pool);
            }

            SimulationResult simulation_result = {};

            simulation_result.total_value = selection_result.selected_node->value;
            simulation_result.num_simulations = selection_result.selected_node->num_simulations;
            if (simulation_result.num_simulations > 10000000)
            {
#if defined(DEBUG_WRITE_OUT)
    DebugPrintDecisionTree(_root_node, legal_moveset_at_root_node, g_move_counter, node_pool, game_state);
#endif
                assert(false && "suspicious amount of simulations, make sure this could happen");
            }
            simulation_result.last_move.terminal_type = selection_result.selected_node->terminal_info.terminal_type;
            // NOTE(david): it's weird that the controlled type for the simulation result is the opposite of the controlled type of the node. The reason why this is true, because the simulation returns the controlled type of the previous node's selected move and the controlled type is associated with that node. However there is one more node after that, the node which the move has arrived to, which is the selected_node/from_node
            simulation_result.last_controlled_type = selection_result.selected_node->controlled_type == ControlledType::CONTROLLED ? ControlledType::UNCONTROLLED : ControlledType::CONTROLLED;

            TIMED_BLOCK("_BackPropagate", _BackPropagate(selection_result.selected_node, simulation_result, node_pool));
            // _BackPropagate(selection_result.selected_node, simulation_result, node_pool);
            backpropagate_cycles_total += g_clock_cycles_var;
            ++backpropagate_cycles_count;

// #if defined(DEBUG_WRITE_OUT)
//             DebugPrintDecisionTree(_root_node, legal_moveset_at_root_node, g_move_counter, node_pool);
// #endif

            // return winning_move_selection_strategy_fn(_root_node, legal_moveset_at_root_node, node_pool);

        }
        else
        {
            TIMED_BLOCK("simulation_from_state", SimulationResult simulation_result = simulation_from_state(selection_result.movesequence_from_position, game_state));
            // SimulationResult simulation_result = simulation_from_state(selection_result.movesequence_from_position);
            simulation_cycles_total += g_clock_cycles_var;
            ++simulation_cycles_count;

            if (simulation_result.num_simulations > 10000000)
            {
#if defined(DEBUG_WRITE_OUT)
    DebugPrintDecisionTree(_root_node, legal_moveset_at_root_node, g_move_counter, node_pool);
#endif
                assert(false && "suspicious amount of simulations, make sure this could happen");
            }

            TIMED_BLOCK("_BackPropagate", _BackPropagate(selection_result.selected_node, simulation_result, node_pool));
            // _BackPropagate(selection_result.selected_node, simulation_result, node_pool);
            backpropagate_cycles_total += g_clock_cycles_var;
            ++backpropagate_cycles_count;
        }
        ++EvaluateIterations;
    }
    LOG(cout, "Evaluate iterations: " << EvaluateIterations << ", total: " << selection_cycles_total + backpropagate_cycles_total + simulation_cycles_total << "M");
    LOG(cout, "Selection total cycles: " << selection_cycles_total << "M");
    LOG(cout, "Backpropagate total cycles: " << backpropagate_cycles_total << "M");
    LOG(cout, "Simulation total cycles: " << simulation_cycles_total << "M");

#if defined(DEBUG_WRITE_OUT)
    DebugPrintDecisionTree(_root_node, legal_moveset_at_root_node, g_move_counter, node_pool);
#endif

    // TIMED_BLOCK("winning_move_selection_strategy_fn", Move result_move = winning_move_selection_strategy_fn(_root_node, legal_moveset_at_root_node, node_pool));
    Move result_move = winning_move_selection_strategy_fn(_root_node, legal_moveset_at_root_node, node_pool);

    return result_move;
}

Node *MCST::_SelectChild(Node *from_node, const MoveSet &legal_moves_from_node, const MoveSequence &movechain_from_state, bool focus_on_lowest_utc_to_prune, NodePool &node_pool)
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

    Move empty_moves[Move::NONE] = {};
    u32 empty_moves_size = 0;

    assert(legal_moves_from_node.moves_left > 0);
    for (u32 move_index = 0; move_index < ArrayCount(legal_moves_from_node.moves); ++move_index)
    {
        Move move = legal_moves_from_node.moves[move_index];
        if (move == Move::NONE)
        {
            continue ;
        }

        // IMPORTANT(david): go over all the nodes first checking for a terminal winning/losing move depending on if the from_node is controlled or uncontrolled
        // TODO(david): also it makes sense maybe to randomly choose moves from the position or based on some heuristic of the game state
        Node *child_node = node_pool.GetChild(from_node, move);
        if (child_node == nullptr)
        {
            assert(empty_moves_size < ArrayCount(empty_moves));
            empty_moves[empty_moves_size++] = move;
            continue ;
        }
        else if (child_node->terminal_info.terminal_type != TerminalType::NOT_TERMINAL)
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
                        winning_uct = UCT(child_node, legal_moves_from_node.moves_left - 1, node_pool);
                    }
                    else
                    {
                        r64 uct = UCT(child_node, legal_moves_from_node.moves_left - 1, node_pool);
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
                        neutral_controlled_uct = UCT(child_node, legal_moves_from_node.moves_left - 1, node_pool);
                    }
                    else
                    {
                        r64 uct = UCT(child_node, legal_moves_from_node.moves_left - 1, node_pool);
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
                        neutral_uncontrolled_uct = UCT(child_node, legal_moves_from_node.moves_left - 1, node_pool);
                    }
                    else
                    {
                        r64 uct = UCT(child_node, legal_moves_from_node.moves_left - 1, node_pool);
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
                        losing_uct = UCT(child_node, legal_moves_from_node.moves_left - 1, node_pool);
                    }
                    else
                    {
                        r64 uct = UCT(child_node, legal_moves_from_node.moves_left - 1, node_pool);
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
            assert(legal_moves_from_node.moves_left >= 1);

            if (selected_node == nullptr)
            {
                selected_node = child_node;
                selected_uct = UCT(child_node, legal_moves_from_node.moves_left - 1, node_pool);
            }
            else if (from_node->controlled_type == ControlledType::CONTROLLED)
            {
                r64 uct = UCT(child_node, legal_moves_from_node.moves_left - 1, node_pool);
                if (uct > selected_uct)
                {
                    selected_node = child_node;
                    selected_uct = uct;
                }
            }
            else if (from_node->controlled_type == ControlledType::UNCONTROLLED)
            {
                r64 uct = UCT(child_node, legal_moves_from_node.moves_left - 1, node_pool);
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
    if (neutral_controlled_node == nullptr && neutral_uncontrolled_node == nullptr && empty_moves_size > 0)
    {
        // NOTE(david): found a move/node that hasn't been simulated yet -> uct is infinite -> choose this node
        u32 random_move_offset = GetRandomNumber(0, empty_moves_size - 1);
        Move random_move = Move::NONE;
        for (u32 move_index = 0; move_index < empty_moves_size; ++move_index)
        {
            if (random_move_offset-- == 0)
            {
                random_move = empty_moves[move_index];
                break ;
            }
        }
        assert(random_move != Move::NONE);

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
MCST::SelectionResult MCST::_Selection(const MoveSet &legal_moveset_at_root_node, MoveProcessor move_processor, NodePool &node_pool)
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
        Node *selected_child_node = _SelectChild(current_node, current_legal_moves, selection_result.movesequence_from_position, focus_on_lowest_utc_to_prune, node_pool);

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

        u32 movesequence_index = selection_result.movesequence_from_position.number_of_moves;
        selection_result.movesequence_from_position.moves[movesequence_index] = selected_child_node->move_to_get_here;
        ++selection_result.movesequence_from_position.number_of_moves;

        move_processor(current_legal_moves, selected_child_node->move_to_get_here);

        // NOTE(david): the selected child is an unexplored one
        if (selected_child_node->num_simulations == 0)
        {
            return selection_result;
        }

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

void MCST::_BackPropagate(Node *from_node, SimulationResult simulation_result, NodePool &node_pool)
{
    assert(from_node != _root_node && "root node is not a valid move so it couldn't have been simulated");
    assert(_root_node->terminal_info.terminal_type == TerminalType::NOT_TERMINAL);

    if (simulation_result.last_move.terminal_type != TerminalType::NOT_TERMINAL)
    {
        from_node->terminal_info.terminal_type = simulation_result.last_move.terminal_type;

        assert(from_node->controlled_type != simulation_result.last_controlled_type && "from_node is the node of the last move, which should be the opposite as simulation result's controlled type, the reason why this is true, because the simulation returns the controlled type of the previous node's selected move and the controlled type is associated with that node. However there is one more node after that, the node which the move has arrived to, which is the selected_node/from_node");

        Node *parent_node = node_pool.GetParent(from_node);
        assert(parent_node != nullptr && "from_node can't be root to propagate back from, as if it was terminal we should have already returned an evaluation result");

        // TODO(david): add terminal move info rule table maybe
        if (simulation_result.last_controlled_type == ControlledType::CONTROLLED && simulation_result.last_move.terminal_type == TerminalType::WINNING)
        {
            parent_node->terminal_info.terminal_type = simulation_result.last_move.terminal_type;
        }
        if (simulation_result.last_controlled_type == ControlledType::UNCONTROLLED && simulation_result.last_move.terminal_type == TerminalType::LOSING)
        {
            parent_node->terminal_info.terminal_type = simulation_result.last_move.terminal_type;
        }
    }

    Node *cur_node = from_node;
    while (cur_node != nullptr)
    {
        // TODO(david): add backpropagating rules terminal rules here? (as seen above this loop)
        cur_node->num_simulations += simulation_result.num_simulations;
        cur_node->value += simulation_result.total_value;
        Node *parent_node = node_pool.GetParent(cur_node);
        cur_node = parent_node;
    }
}

u32 MCST::NumberOfSimulationsRan(void)
{
    return _root_node->num_simulations;
}
