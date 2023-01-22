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
static r64 UCT(Node *node)
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

    // r64 depth_weight = 1.0 / (r32)node->depth;
    // TODO(david): think about this number and how it should affect explitation vs exploration
    // NOTE(david): go with exploration if there are a lot of branches and exploit more the less choices there are
    // r64 number_of_branches_weight = 0.2 * number_of_branches;
    // r64 number_of_branches_weight = 1.0;
    // r64 weighted_exploration_factor = number_of_branches_weight * g_tuned_exploration_factor_weight * EXPLORATION_FACTOR / (node->depth * depth_weight);
    assert(node->parent != nullptr);
    r64 uct;
    if (node->controlled_type == ControlledType::CONTROLLED)
    {
        uct = EXPLORATION_FACTOR * sqrt(log((r64)node->parent->num_simulations) / (r64)node->num_simulations) - (r64)node->value / (r64)node->num_simulations;
    }
    else if (node->controlled_type == ControlledType::UNCONTROLLED)
    {
        uct = EXPLORATION_FACTOR * sqrt(log((r64)node->parent->num_simulations) / (r64)node->num_simulations) + (r64)node->value / (r64)node->num_simulations;
    }
    else
    {
        UNREACHABLE_CODE;
    }

    return uct;
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

template <u32 MoveSize>
void MoveSequence<MoveSize>::AddMove(Move move)
{
    assert(moves_left < ArrayCount(MoveSequence::moves) && "not enough space in the move sequence");
    moves[moves_left++] = move;
}

template <u32 MoveSize>
Move MoveSequence<MoveSize>::PopMoveAtIndex(u32 move_index)
{
    assert(moves_left > 0);
    assert(move_index < moves_left);
    Move result_move = moves[move_index];
    moves[move_index] = moves[moves_left - 1];
    --moves_left;

    return result_move;
}

NodePool *debug_node_pool;
ostream &operator<<(ostream &os, Node *node)
{
    NodePool::ChildrenTables *children_table = debug_node_pool->GetChildren(node);
    Move highest_move_cycled = Move::MoveFromIndex(children_table->highest_move_index);
    LOGN(os, "index: " << node->index << ", " << MoveToWord(node->move_to_get_here) << ", value: " << node->value << ", sims: " << node->num_simulations << ", " << ControlledTypeToWord(node->controlled_type) << ", " << TerminalTypeToWord(node->terminal_info.terminal_type) << ", terminal depth(W/L/N): (" << node->terminal_info.terminal_depth.winning << "," << node->terminal_info.terminal_depth.losing << "," << node->terminal_info.terminal_depth.neutral << "), uct: " << (node->parent == nullptr ? 0.0 : UCT(node)) << ", highest move index: " << MoveToWord(highest_move_cycled));

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
    child_table->highest_move_index = -1;
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

    assert(move.IsValid());
    u32 move_index = move.GetIndex();
    assert((table->highest_move_index == -1 || move_index > table->highest_move_index) && "the selected new node at the moment is always the next up in line by its move index order from the available set of moves");
    table->highest_move_index = move_index;

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

static void DebugPrintDecisionTreeHelper(Node *from_node, Player player_to_move, ofstream &tree_fs, NodePool &node_pool)
{
    if (from_node->depth > 4)
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

Node *MCST::SelectBestChild(Node *from_node, NodePool &node_pool)
{
    Node *selected_node = nullptr;

    BestChildren best_children = SelectBestChildren(from_node, node_pool);

    switch (from_node->controlled_type)
    {
        case ControlledType::CONTROLLED: {
            if (best_children.winning)
            {
                selected_node = best_children.winning;
            }
            else if (best_children.neutral != nullptr)
            {
                selected_node = best_children.neutral;
            }
            else if (best_children.non_terminal != nullptr)
            {
                selected_node = best_children.non_terminal;
            }
            else if (best_children.losing != nullptr)
            {
                selected_node = best_children.losing;
            }
            else
            {
                assert(false && "all children nodes are pruned out, can't select child");
            }
        } break ;
        case ControlledType::UNCONTROLLED: {
            if (best_children.losing)
            {
                selected_node = best_children.losing;
            }
            else if (best_children.neutral != nullptr)
            {
                selected_node = best_children.neutral;
            }
            else if (best_children.non_terminal != nullptr)
            {
                selected_node = best_children.non_terminal;
            }
            else if (best_children.winning != nullptr)
            {
                selected_node = best_children.winning;
            }
            else
            {
                assert(false && "all children nodes are pruned out, can't select child");
            }
        } break ;
        default: UNREACHABLE_CODE;
    }

    assert(selected_node != nullptr);

    return selected_node;
}

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
    // _root_node->controlled_type = ControlledType::CONTROLLED;
    _root_node->controlled_type = ControlledType::UNCONTROLLED;

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

            // TODO(david): these values should be set by some function by the user of MCST
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
            simulation_result.num_simulations = 1;
        }
        else
        {
            TIMED_BLOCK(simulation_result = simulation_from_state(selection_result.movesequence_from_position, game_state, selection_result.selected_node, node_pool), JobNames::Simulation);
        }
        if (selection_result.selected_node->num_simulations > 10000000)
        {
            DebugPrintDecisionTree(_root_node, g_move_counter, node_pool, game_state);
            assert(false && "suspicious amount of simulations, make sure this could happen");
        }
        TIMED_BLOCK(_BackPropagate(selection_result.selected_node, node_pool, simulation_result), JobNames::BackPropagate);
    }

#if defined(DEBUG_WRITE_OUT)
    DebugPrintDecisionTree(_root_node, g_move_counter, node_pool, game_state);
#endif

    TIMED_BLOCK(Node *best_node = SelectBestChild(_root_node, node_pool), JobNames::SelectBestChild);

    return best_node->move_to_get_here;
}

MCST::BestChildren MCST::SelectBestChildren(Node *from_node, NodePool &node_pool)
{
    BestChildren result = {};

    r64 non_terminal_uct;
    r64 winning_uct;
    r64 losing_uct;
    r64 neutral_uct;

    NodePool::ChildrenTables *children_nodes = node_pool.GetChildren(from_node);
    for (u32 child_index = 0; child_index < children_nodes->number_of_children; ++child_index)
    {
        Node *child_node = children_nodes->children[child_index];
        assert(child_node != nullptr && child_node->move_to_get_here.IsValid());

        assert(child_node->num_simulations > 0 && "how is this child node chosen as a move but not simulated once? Did it not get to backpropagation?");

        // dispatch to fn calls from a rule table based on unique combination of controlled type and terminal type
        r64 child_uct = UCT(child_node);
        switch (from_node->controlled_type)
        {
            case ControlledType::CONTROLLED: {
                switch (child_node->terminal_info.terminal_type)
                {
                    case TerminalType::WINNING: {
                        if (result.winning == nullptr || child_node->terminal_info.terminal_depth.winning < result.winning->terminal_info.terminal_depth.winning || (child_node->terminal_info.terminal_depth.winning == result.winning->terminal_info.terminal_depth.winning && child_uct > winning_uct))
                        {
                            result.winning = child_node;
                            winning_uct    = child_uct;
                        }
                    } break ;
                    case TerminalType::LOSING: {
                        if (result.losing == nullptr || child_node->terminal_info.terminal_depth.losing > result.losing->terminal_info.terminal_depth.losing || (child_node->terminal_info.terminal_depth.losing == result.losing->terminal_info.terminal_depth.losing && child_uct > losing_uct))
                        {
                            result.losing = child_node;
                            losing_uct    = child_uct;
                        }
                    } break ;
                    case TerminalType::NEUTRAL: {
                        if (result.neutral == nullptr || child_node->terminal_info.terminal_depth.neutral > result.neutral->terminal_info.terminal_depth.neutral || (child_node->terminal_info.terminal_depth.neutral == result.neutral->terminal_info.terminal_depth.neutral && child_uct > neutral_uct))
                        {
                            result.neutral = child_node;
                            neutral_uct    = child_uct;
                        }
                    } break ;
                    case TerminalType::NOT_TERMINAL: {
                        if (result.non_terminal == nullptr || child_uct > non_terminal_uct)
                        {
                            result.non_terminal = child_node;
                            non_terminal_uct    = child_uct;
                        }
                    } break ;
                    default: UNREACHABLE_CODE;
                }
            } break ;
            case ControlledType::UNCONTROLLED: {
                switch (child_node->terminal_info.terminal_type)
                {
                    case TerminalType::WINNING: {
                        if (result.winning == nullptr || child_node->terminal_info.terminal_depth.winning > result.winning->terminal_info.terminal_depth.winning || (child_node->terminal_info.terminal_depth.winning == result.winning->terminal_info.terminal_depth.winning && child_uct > winning_uct))
                        {
                            result.winning = child_node;
                            winning_uct    = child_uct;
                        }
                    } break ;
                    case TerminalType::LOSING: {
                        if (result.losing == nullptr || child_node->terminal_info.terminal_depth.losing < result.losing->terminal_info.terminal_depth.losing || (child_node->terminal_info.terminal_depth.losing == result.losing->terminal_info.terminal_depth.losing && child_uct > losing_uct))
                        {
                            result.losing = child_node;
                            losing_uct    = child_uct;
                        }
                    } break ;
                    case TerminalType::NEUTRAL: {
                        if (result.neutral == nullptr || child_node->terminal_info.terminal_depth.neutral > result.neutral->terminal_info.terminal_depth.neutral || (child_node->terminal_info.terminal_depth.neutral == result.neutral->terminal_info.terminal_depth.neutral && child_uct > neutral_uct))
                        {
                            result.neutral = child_node;
                            neutral_uct    = child_uct;
                        }
                    } break ;
                    case TerminalType::NOT_TERMINAL: {
                        if (result.non_terminal == nullptr || child_uct > non_terminal_uct)
                        {
                            result.non_terminal = child_node;
                            non_terminal_uct    = child_uct;
                        }
                    } break ;
                    default: UNREACHABLE_CODE;
                }
            } break ;
            default: UNREACHABLE_CODE;
        }
    }

    return result;
}

Node *MCST::_SelectChild(Node *from_node, const MoveSet &legal_moves_from_node, bool focus_on_lowest_utc_to_prune, NodePool &node_pool)
{
    assert(from_node->terminal_info.terminal_type == TerminalType::NOT_TERMINAL && "if from_node was terminal, we wouldn't need to select its child for the next move");

    Node *selected_node = nullptr;

    BestChildren best_children = SelectBestChildren(from_node, node_pool);

    switch (from_node->controlled_type)
    {
        case ControlledType::CONTROLLED: {
            if (best_children.winning != nullptr)
            {
                from_node->terminal_info.terminal_type = TerminalType::WINNING;
                return best_children.winning;
            }
        } break ;
        case ControlledType::UNCONTROLLED: {
            if (best_children.losing != nullptr)
            {
                from_node->terminal_info.terminal_type = TerminalType::LOSING;
                return best_children.losing;
            }
        } break ;
        default: UNREACHABLE_CODE;
    }

    MoveSet cur_legal_moves_from_node = legal_moves_from_node;
    NodePool::ChildrenTables *children_nodes = node_pool.GetChildren(from_node);
    for (u32 child_index = 0; child_index < children_nodes->number_of_children; ++child_index)
    {
        Node *child_node = children_nodes->children[child_index];
        assert(child_node != nullptr && child_node->move_to_get_here.IsValid());

        cur_legal_moves_from_node.DeleteMove(child_node->move_to_get_here);
    }

    // NOTE(david): there are still possible moves from the move set
    bool check_for_new_moves = cur_legal_moves_from_node.moves_left > 0;
    // ASSUMPTION(david): if there was either a losing choice for an uncontrolled node or a winning choice for a controlled node, we should have already returned at this point
    // TODO(david): think about if there is at least one neutral node, it shouldn't necessarily be selected, as there hasn't been all moves explored yet
    // neutral_controlled_node == nullptr && neutral_uncontrolled_node == nullptr && 
    if (check_for_new_moves == true)
    {
        i32 highest_move_index = children_nodes->highest_move_index;
        i32 lower_bound_move_index = -1;
        // NOTE(david): get the lower bound move index from the set of legal moves
        for (u32 move_index = 0; move_index < ArrayCount(cur_legal_moves_from_node.moves); ++move_index)
        {
            // TODO(david): implement iterator for the MoveSet
            Move cur_move = cur_legal_moves_from_node.moves[move_index];
            if (cur_move.IsValid() == false)
            {
                continue ;
            }
            if (highest_move_index == -1 || move_index > highest_move_index)
            {
                // NOTE(david): if haven't chosen a move before or the current move is higher by order
                if (lower_bound_move_index == -1 || move_index < lower_bound_move_index)
                {
                    lower_bound_move_index = move_index;
                }
            }
        }
        if (lower_bound_move_index != -1)
        {
            // NOTE(david): found a move to be explored from the set

            assert(lower_bound_move_index >= 0 && lower_bound_move_index < ArrayCount(cur_legal_moves_from_node.moves));
            Move selected_move = cur_legal_moves_from_node.moves[lower_bound_move_index];
            assert(selected_move.IsValid());

            if (children_nodes->number_of_children >= ArrayCount(NodePool::ChildrenTables::children))
            {
                // NOTE(david): need to cycle moves as there aren't any slots available

                if (best_children.non_terminal == nullptr)
                {
                    // NOTE(david): we only have terminal nodes that doesn't favor either side here, so prune one of the children (probably the worst one) and then expand with the new move
                    // TODO(david): select the terminal move that is the worst amongst all of them
                    if (from_node->controlled_type == ControlledType::CONTROLLED)
                    {
                        if (best_children.losing)
                        {
                            PruneNode(best_children.losing, node_pool);
                            best_children.losing = nullptr;
                        }
                        else if (best_children.neutral)
                        {
                            PruneNode(best_children.neutral, node_pool);
                            best_children.neutral = nullptr;
                        }
                        else
                        {
                            UNREACHABLE_CODE;
                        }
                    }
                    else if (from_node->controlled_type == ControlledType::UNCONTROLLED)
                    {
                        if (best_children.winning)
                        {
                            PruneNode(best_children.winning, node_pool);
                            best_children.winning = nullptr;
                        }
                        else if (best_children.neutral)
                        {
                            PruneNode(best_children.neutral, node_pool);
                            best_children.neutral = nullptr;
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

                    // TODO(david): choose the worst one -> implement worst children as well
                    switch (from_node->controlled_type)
                    {
                        case ControlledType::CONTROLLED: {
                            if (best_children.losing)
                            {
                                node_to_prune = best_children.losing;
                            }
                        } break ;
                        case ControlledType::UNCONTROLLED: {
                            if (best_children.winning)
                            {
                                node_to_prune = best_children.winning;
                            }
                        } break ;
                        default: UNREACHABLE_CODE;
                    }

                    // TODO(david): similar logic exists in backpropagation, maybe store them in a central place?
                    // TODO(david): this number needs to decrease each depth by a factor of how many children slots there can be at a given time so that each child can contribute the same amount of simulations
                    // TODO(david): Think about this more as there is a problem that the min simulation number approaches 0. Maybe introduce a minimum simulation count.
                    // TODO(david): the number of simulations treshold should maybe be a dynamic parameter depending on how much time is allowed to think, actually thinking about this more, isn't the upper limit the expected number of simulations from root?
                    // IMPORTANT(david): the sum of children's num sim treshold needs to be above the parent's num sim treshold, otherwise the sum of children's simulations will never reach the parent's, resulting in no pruning and no move cycling as a result
                    assert(ArrayCount(NodePool::ChildrenTables::children) > 1);
                    r64 branching_factor = pow(ArrayCount(NodePool::ChildrenTables::children) - 1, from_node->depth + 1);
                    // NOTE(david): so that in case of high branching, the move won't be immediately cycled in case the min treshold approached 0
                    constexpr u32 min_simulations_for_move = 25;
                    // TODO(david): maybe this number should be a power of ArrayCount(NodePool::ChildrenTables::children) to have know at which depth the treshold approaches to 0 -> allowed time / time it takes to complete 1 evaluation
                    constexpr u32 min_simulations_from_root = 243;
                    u32 min_simulation_confidence_cycle_treshold = (u32)(min_simulations_from_root / branching_factor) + min_simulations_for_move;
                    // NOTE(david): two tresholds for controlled/uncontrolled
                    constexpr r64 min_mean_value_treshold = -0.95;
                    constexpr r64 max_mean_value_treshold = 0.95;

                    u32 condition_checked_nodes_on_their_simulation_count = 0;
                    pair<Node *, r64> mean_child_extremum = {};
                    for (u32 child_index = 0; child_index < children_nodes->number_of_children && node_to_prune == nullptr; ++child_index)
                    {
                        Node *child_node = children_nodes->children[child_index];
                        if (child_node->num_simulations >= min_simulation_confidence_cycle_treshold)
                        {
                            ++condition_checked_nodes_on_their_simulation_count;
                            r64 child_mean_value = child_node->value / (r64)child_node->num_simulations;
                            switch (from_node->controlled_type)
                            {
                                case ControlledType::CONTROLLED: {
                                    if (child_mean_value <= min_mean_value_treshold)
                                    {
                                        node_to_prune = child_node;
                                    }
                                    else
                                    {
                                        // TODO(david): take terminal depth into consideration once I figured out how to store terminal depth properly
                                        // NOTE(david): keep the one with the lowest mean to prune
                                        if (mean_child_extremum.first == nullptr || child_mean_value < mean_child_extremum.second)
                                        {
                                            mean_child_extremum.first = child_node;
                                            mean_child_extremum.second = child_mean_value;
                                        }
                                    }
                                } break ;
                                case ControlledType::UNCONTROLLED: {
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
                                } break ;
                                default: UNREACHABLE_CODE;
                            }
                        }
                    }
                    if (node_to_prune == nullptr)
                    {
                        // NOTE(david): none of the moves are terminal and none of them falls outside the mean value with enough simulations
                        // NOTE(david): if there is only 1 condition checked node that have enough simulations, don't prune it yet, as it's our current best node
                        if (condition_checked_nodes_on_their_simulation_count > 1)
                        {
                            node_to_prune = mean_child_extremum.first;
                        }
                    }

                    if (node_to_prune != nullptr)
                    {
                        if (node_to_prune == best_children.winning)
                        {
                            best_children.winning = nullptr;
                        }
                        else if (node_to_prune == best_children.losing)
                        {
                            best_children.losing = nullptr;
                        }
                        else if (node_to_prune == best_children.neutral)
                        {
                            best_children.neutral = nullptr;
                        }
                        else if (node_to_prune == best_children.non_terminal)
                        {
                            best_children.non_terminal = nullptr;
                        }
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
        }
        else
        {
            // NOTE(david): none of the legal moves serialized are higher than the current highest serialized move stored in the child_table -> we don't have a new move
            // if (from_node->parent == nullptr)
            // {
            //     LOG(cout, "all moves are exhausted for root node");
            // }
        }
    }

    // NOTE(david): no move has been selected yet, choose the best amongst the best children
    if (selected_node == nullptr)
    {
        switch (from_node->controlled_type)
        {
            case ControlledType::CONTROLLED: {
                if (best_children.neutral != nullptr && best_children.non_terminal == nullptr)
                {
                    assert(best_children.winning == nullptr && "this should have been selected already");
                    // ASSUMPTION(david): if there is only terminal moves, that means there are no more moves to cycle, so mark from_node as neutral
                    from_node->terminal_info.terminal_type = TerminalType::NEUTRAL;

                    selected_node = from_node;
                }
                else if (best_children.non_terminal != nullptr)
                {
                    selected_node = best_children.non_terminal;
                }
                else if (best_children.losing != nullptr)
                {
                    from_node->terminal_info.terminal_type = TerminalType::LOSING;

                    selected_node = from_node;
                }
                else
                {
                    // NOTE(david): all children nodes are pruned out -> mark controlled node as losing
                    from_node->terminal_info.terminal_type = TerminalType::LOSING;
                    
                    selected_node = from_node;
                }
            } break ;
            case ControlledType::UNCONTROLLED: {
                if (best_children.neutral != nullptr && best_children.non_terminal == nullptr)
                {
                    assert(best_children.losing == nullptr && "this should have been selected already");
                    // ASSUMPTION(david): if there is only terminal moves, that means there are no more moves to cycle, so mark from_node as neutral
                    from_node->terminal_info.terminal_type = TerminalType::NEUTRAL;

                    selected_node = from_node;
                }
                else if (best_children.non_terminal != nullptr)
                {
                    selected_node = best_children.non_terminal;
                }
                else if (best_children.winning != nullptr)
                {
                    // NOTE(david): all moves are winning -> mark uncontrolled node as winning
                    from_node->terminal_info.terminal_type = TerminalType::WINNING;

                    selected_node = from_node;
                }
                else
                {
                    // NOTE(david): all children nodes are pruned out -> mark uncontrolled node as winning as there are no good moves for uncontrolled
                    from_node->terminal_info.terminal_type = TerminalType::WINNING;
                    
                    selected_node = from_node;
                }
            } break ;
            default: UNREACHABLE_CODE;
        }
    }

    assert(selected_node != nullptr);

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
        // NOTE(david): Select a child node and its corresponding legal move based on maximum UCT value and some other heuristic
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
    // NOTE(david): the selection algorithm knows that this move shouldn't be picked anymore, as the moves are selected sequentially by their order of move indices
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

        // NOTE(david): only set the terminal depth if it hasn't already been set, if from_node isn't simulated for example but is terminal, that means it should already have been set
        switch (from_node->terminal_info.terminal_type)
        {
            case TerminalType::WINNING: {
                if (from_node->terminal_info.terminal_depth.winning == 0)
                {
                    from_node->terminal_info.terminal_depth.winning = from_node->depth;
                }
            } break ;
            case TerminalType::LOSING: {
                if (from_node->terminal_info.terminal_depth.losing == 0)
                {
                    from_node->terminal_info.terminal_depth.losing = from_node->depth;
                }
            } break ;
            case TerminalType::NEUTRAL: {
                if (from_node->terminal_info.terminal_depth.neutral == 0)
                {
                    from_node->terminal_info.terminal_depth.neutral = from_node->depth;
                }
            } break ;
            default: UNREACHABLE_CODE;
        }

        if ((from_node->controlled_type == ControlledType::UNCONTROLLED && from_node->terminal_info.terminal_type == TerminalType::WINNING) || (from_node->controlled_type == ControlledType::CONTROLLED && from_node->terminal_info.terminal_type == TerminalType::LOSING))
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

        cur_node->num_simulations += simulation_result.num_simulations;
        cur_node->value += simulation_result.value;

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
                        cur_node->num_simulations -= simulation_result.num_simulations;
                        cur_node->value -= simulation_result.value;
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
                        cur_node->num_simulations -= simulation_result.num_simulations;
                        cur_node->value -= simulation_result.value;
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
