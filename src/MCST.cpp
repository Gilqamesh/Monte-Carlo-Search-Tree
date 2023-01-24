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
    return "(" + to_string(move.row) + ", " + to_string(move.col) + ")";
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
    LOGN(os, "depth: " << node->depth << ", index: " << node->index << ", " << MoveToWord(node->move_to_get_here) << ", value: " << node->value << ", sims: " << node->num_simulations << ", " << ControlledTypeToWord(node->controlled_type) << ", " << TerminalTypeToWord(node->terminal_info.terminal_type) << ", terminal depth(W/L/N): (" << node->terminal_info.terminal_depth.winning << "," << node->terminal_info.terminal_depth.losing << "," << node->terminal_info.terminal_depth.neutral << "), uct: " << (node->parent == nullptr ? 0.0 : UCT(node)) << ", highest move index: " << MoveToWord(highest_move_cycled));

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
    node->terminal_info.terminal_depth.winning_continuation.Invalidate();
    node->terminal_info.terminal_depth.losing_continuation.Invalidate();
    node->terminal_info.terminal_depth.neutral_continuation.Invalidate();
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
    if (from_node->depth > 6)
    {
        return ;
    }
    LOG(tree_fs, string(from_node->depth * 4, ' ') << "(player to move: " << PlayerToWord(player_to_move) << ", " << from_node << ")");

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

    ExtremumChildren extremum_children = GetExtremumChildren(from_node, node_pool);

    switch (from_node->controlled_type)
    {
        case ControlledType::CONTROLLED: {
            if (extremum_children.best_winning != nullptr)
            {
                selected_node = extremum_children.best_winning;
            }
            else if (extremum_children.best_neutral != nullptr)
            {
                selected_node = extremum_children.best_neutral;
            }
            else if (extremum_children.best_non_terminal != nullptr)
            {
                selected_node = extremum_children.best_non_terminal;
            }
            else if (extremum_children.best_losing != nullptr)
            {
                selected_node = extremum_children.best_losing;
            }
            else
            {
                assert(false && "all children nodes are pruned out, can't select child");
            }
        } break ;
        case ControlledType::UNCONTROLLED: {
            if (extremum_children.best_losing != nullptr)
            {
                selected_node = extremum_children.best_losing;
            }
            else if (extremum_children.best_neutral != nullptr)
            {
                selected_node = extremum_children.best_neutral;
            }
            else if (extremum_children.best_non_terminal != nullptr)
            {
                selected_node = extremum_children.best_non_terminal;
            }
            else if (extremum_children.best_winning != nullptr)
            {
                selected_node = extremum_children.best_winning;
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

MCST::ExtremumChildren MCST::GetExtremumChildren(Node *from_node, NodePool &node_pool, u32 min_simulation_confidence_cycle_treshold)
{
    ExtremumChildren result = {};

    r64 best_non_terminal_uct;
    r64 worst_non_terminal_uct;

    r64 best_winning_uct;
    r64 worst_winning_uct;

    r64 best_losing_uct;
    r64 worst_losing_uct;

    r64 best_neutral_uct;
    r64 worst_neutral_uct;

    NodePool::ChildrenTables *children_nodes = node_pool.GetChildren(from_node);
    for (u32 child_index = 0; child_index < children_nodes->number_of_children; ++child_index)
    {
        Node *child_node = children_nodes->children[child_index];
        assert(child_node != nullptr && child_node->move_to_get_here.IsValid());

        assert(child_node->num_simulations > 0 && "how is this child node chosen as a move but not simulated once?");
        if (child_node->num_simulations < min_simulation_confidence_cycle_treshold)
        {
            // NOTE(david): needed for move cycling, above the treshold this node will be considered to be pruned for another move
            continue ;
        }
        ++result.condition_checked_nodes_on_their_simulation_count;

        // dispatch to fn calls from a rule table based on unique combination of controlled type and terminal type
        r64 child_uct = UCT(child_node);
        switch (from_node->controlled_type)
        {
            case ControlledType::CONTROLLED: {
                switch (child_node->terminal_info.terminal_type)
                {
                    case TerminalType::WINNING: {
                        if (result.best_winning == nullptr || child_node->terminal_info.terminal_depth.winning < result.best_winning->terminal_info.terminal_depth.winning || (child_node->terminal_info.terminal_depth.winning == result.best_winning->terminal_info.terminal_depth.winning && child_uct > best_winning_uct))
                        {
                            result.best_winning = child_node;
                            best_winning_uct    = child_uct;
                        }
                        if (result.worst_winning == nullptr || child_node->terminal_info.terminal_depth.winning > result.worst_winning->terminal_info.terminal_depth.winning || (child_node->terminal_info.terminal_depth.winning == result.worst_winning->terminal_info.terminal_depth.winning && child_uct < worst_winning_uct))
                        {
                            result.worst_winning = child_node;
                            worst_winning_uct    = child_uct;
                        }
                    } break ;
                    case TerminalType::LOSING: {
                        if (result.best_losing == nullptr || child_node->terminal_info.terminal_depth.losing > result.best_losing->terminal_info.terminal_depth.losing || (child_node->terminal_info.terminal_depth.losing == result.best_losing->terminal_info.terminal_depth.losing && child_uct > best_losing_uct))
                        {
                            result.best_losing = child_node;
                            best_losing_uct    = child_uct;
                        }
                        if (result.worst_losing == nullptr || child_node->terminal_info.terminal_depth.losing < result.worst_losing->terminal_info.terminal_depth.losing || (child_node->terminal_info.terminal_depth.losing == result.worst_losing->terminal_info.terminal_depth.losing && child_uct < worst_losing_uct))
                        {
                            result.worst_losing = child_node;
                            worst_losing_uct    = child_uct;
                        }
                    } break ;
                    case TerminalType::NEUTRAL: {
                        if (result.best_neutral == nullptr || child_node->terminal_info.terminal_depth.neutral > result.best_neutral->terminal_info.terminal_depth.neutral || (child_node->terminal_info.terminal_depth.neutral == result.best_neutral->terminal_info.terminal_depth.neutral && child_uct > best_neutral_uct))
                        {
                            result.best_neutral = child_node;
                            best_neutral_uct    = child_uct;
                        }
                        if (result.worst_neutral == nullptr || child_node->terminal_info.terminal_depth.neutral < result.worst_neutral->terminal_info.terminal_depth.neutral || (child_node->terminal_info.terminal_depth.neutral == result.worst_neutral->terminal_info.terminal_depth.neutral && child_uct < worst_neutral_uct))
                        {
                            result.worst_neutral = child_node;
                            worst_neutral_uct    = child_uct;
                        }
                    } break ;
                    case TerminalType::NOT_TERMINAL: {
                        if (result.best_non_terminal == nullptr || child_uct > best_non_terminal_uct)
                        {
                            result.best_non_terminal = child_node;
                            best_non_terminal_uct    = child_uct;
                        }
                        if (result.worst_non_terminal == nullptr || child_uct < worst_non_terminal_uct)
                        {
                            result.worst_non_terminal = child_node;
                            worst_non_terminal_uct    = child_uct;
                        }
                    } break ;
                    default: UNREACHABLE_CODE;
                }
            } break ;
            case ControlledType::UNCONTROLLED: {
                switch (child_node->terminal_info.terminal_type)
                {
                    case TerminalType::WINNING: {
                        if (result.best_winning == nullptr || child_node->terminal_info.terminal_depth.winning > result.best_winning->terminal_info.terminal_depth.winning || (child_node->terminal_info.terminal_depth.winning == result.best_winning->terminal_info.terminal_depth.winning && child_uct > best_winning_uct))
                        {
                            result.best_winning = child_node;
                            best_winning_uct    = child_uct;
                        }
                        if (result.worst_winning == nullptr || child_node->terminal_info.terminal_depth.winning < result.worst_winning->terminal_info.terminal_depth.winning || (child_node->terminal_info.terminal_depth.winning == result.worst_winning->terminal_info.terminal_depth.winning && child_uct < worst_winning_uct))
                        {
                            result.worst_winning = child_node;
                            worst_winning_uct    = child_uct;
                        }
                    } break ;
                    case TerminalType::LOSING: {
                        if (result.best_losing == nullptr || child_node->terminal_info.terminal_depth.losing < result.best_losing->terminal_info.terminal_depth.losing || (child_node->terminal_info.terminal_depth.losing == result.best_losing->terminal_info.terminal_depth.losing && child_uct > best_losing_uct))
                        {
                            result.best_losing = child_node;
                            best_losing_uct    = child_uct;
                        }
                        if (result.worst_losing == nullptr || child_node->terminal_info.terminal_depth.losing > result.worst_losing->terminal_info.terminal_depth.losing || (child_node->terminal_info.terminal_depth.losing == result.worst_losing->terminal_info.terminal_depth.losing && child_uct < worst_losing_uct))
                        {
                            result.worst_losing = child_node;
                            worst_losing_uct    = child_uct;
                        }
                    } break ;
                    case TerminalType::NEUTRAL: {
                        if (result.best_neutral == nullptr || child_node->terminal_info.terminal_depth.neutral > result.best_neutral->terminal_info.terminal_depth.neutral || (child_node->terminal_info.terminal_depth.neutral == result.best_neutral->terminal_info.terminal_depth.neutral && child_uct > best_neutral_uct))
                        {
                            result.best_neutral = child_node;
                            best_neutral_uct    = child_uct;
                        }
                        if (result.worst_neutral == nullptr || child_node->terminal_info.terminal_depth.neutral < result.worst_neutral->terminal_info.terminal_depth.neutral || (child_node->terminal_info.terminal_depth.neutral == result.worst_neutral->terminal_info.terminal_depth.neutral && child_uct < worst_neutral_uct))
                        {
                            result.worst_neutral = child_node;
                            worst_neutral_uct    = child_uct;
                        }
                    } break ;
                    case TerminalType::NOT_TERMINAL: {
                        if (result.best_non_terminal == nullptr || child_uct > best_non_terminal_uct)
                        {
                            result.best_non_terminal = child_node;
                            best_non_terminal_uct    = child_uct;
                        }
                        if (result.worst_non_terminal == nullptr || child_uct < worst_non_terminal_uct)
                        {
                            result.worst_non_terminal = child_node;
                            worst_non_terminal_uct    = child_uct;
                        }
                    } break ;
                    default: UNREACHABLE_CODE;
                }
            } break ;
            default: UNREACHABLE_CODE;
        }
    }

    assert(result.best_winning == nullptr || result.best_winning->terminal_info.terminal_type == TerminalType::WINNING);
    assert(result.worst_winning == nullptr || result.worst_winning->terminal_info.terminal_type == TerminalType::WINNING);
    assert((result.best_winning == nullptr && result.worst_winning == nullptr) || (result.best_winning != nullptr && result.worst_winning != nullptr));
   
    assert(result.best_losing == nullptr || result.best_losing->terminal_info.terminal_type == TerminalType::LOSING);
    assert(result.worst_losing == nullptr || result.worst_losing->terminal_info.terminal_type == TerminalType::LOSING);
    assert((result.best_losing == nullptr && result.worst_losing == nullptr) || (result.best_losing != nullptr && result.worst_losing != nullptr));

    assert(result.best_neutral == nullptr || result.best_neutral->terminal_info.terminal_type == TerminalType::NEUTRAL);
    assert(result.worst_neutral == nullptr || result.worst_neutral->terminal_info.terminal_type == TerminalType::NEUTRAL);
    assert((result.best_neutral == nullptr && result.worst_neutral == nullptr) || (result.best_neutral != nullptr && result.worst_neutral != nullptr));
    
    assert(result.best_non_terminal == nullptr || result.best_non_terminal->terminal_info.terminal_type == TerminalType::NOT_TERMINAL);
    assert(result.worst_non_terminal == nullptr || result.worst_non_terminal->terminal_info.terminal_type == TerminalType::NOT_TERMINAL);
    assert((result.best_non_terminal == nullptr && result.worst_non_terminal == nullptr) || (result.best_non_terminal != nullptr && result.worst_non_terminal != nullptr));

    return result;
}

Node *MCST::_SelectChild(Node *from_node, const MoveSet &legal_moves_from_node, bool focus_on_lowest_utc_to_prune, NodePool &node_pool)
{
    assert(from_node->terminal_info.terminal_type == TerminalType::NOT_TERMINAL && "if from_node was terminal, we wouldn't need to select its child for the next move");

    Node *selected_node = nullptr;

    ExtremumChildren extremum_children = GetExtremumChildren(from_node, node_pool);

    switch (from_node->controlled_type)
    {
        case ControlledType::CONTROLLED: {
            if (extremum_children.best_winning != nullptr)
            {
                // TODO(david): not only do these always belong together, but also when a node's terminality is set, might as well prune all its children? Sounds expensive, but probably worth it as it reduces the size of the tree
                assert(from_node->terminal_info.terminal_type == TerminalType::NOT_TERMINAL);
                from_node->terminal_info.terminal_type = TerminalType::WINNING;
                // from_node->terminal_info.terminal_depth.winning = from_node->depth;
                extremum_children.best_winning->UpdateTerminalDepthForParentNode(TerminalType::WINNING, node_pool);

                // TODO(david): prune children of from_node?

                return from_node;
            }
        } break ;
        case ControlledType::UNCONTROLLED: {
            if (extremum_children.best_losing != nullptr)
            {
                assert(from_node->terminal_info.terminal_type == TerminalType::NOT_TERMINAL);
                from_node->terminal_info.terminal_type = TerminalType::LOSING;
                // from_node->terminal_info.terminal_depth.losing = from_node->depth;
                extremum_children.best_losing->UpdateTerminalDepthForParentNode(TerminalType::LOSING, node_pool);

                // TODO(david): prune children of from_node?

                return from_node;
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

                if (extremum_children.best_non_terminal == nullptr)
                {
                    // NOTE(david): we only have terminal nodes that doesn't favor either side here, so prune one of the children (the worst one) and then expand with the new move
                    if (from_node->controlled_type == ControlledType::CONTROLLED)
                    {
                        if (extremum_children.worst_losing)
                        {
                            if (extremum_children.worst_losing == extremum_children.best_losing)
                            {
                                extremum_children.best_losing = nullptr;
                            }
                            PruneNode(extremum_children.worst_losing, node_pool);
                            extremum_children.worst_losing = nullptr;
                        }
                        else if (extremum_children.worst_neutral)
                        {
                            if (extremum_children.worst_neutral == extremum_children.best_neutral)
                            {
                                extremum_children.best_neutral = nullptr;
                            }
                            PruneNode(extremum_children.worst_neutral, node_pool);
                            extremum_children.worst_neutral = nullptr;
                        }
                        else
                        {
                            UNREACHABLE_CODE;
                        }
                    }
                    else if (from_node->controlled_type == ControlledType::UNCONTROLLED)
                    {
                        if (extremum_children.worst_winning)
                        {
                            if (extremum_children.worst_winning == extremum_children.best_winning)
                            {
                                extremum_children.best_winning = nullptr;
                            }
                            PruneNode(extremum_children.worst_winning, node_pool);
                            extremum_children.worst_winning = nullptr;
                        }
                        else if (extremum_children.worst_neutral)
                        {
                            if (extremum_children.worst_neutral == extremum_children.best_neutral)
                            {
                                extremum_children.best_neutral = nullptr;
                            }
                            PruneNode(extremum_children.worst_neutral, node_pool);
                            extremum_children.worst_neutral = nullptr;
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

                    switch (from_node->controlled_type)
                    {
                        case ControlledType::CONTROLLED: {
                            if (extremum_children.worst_losing)
                            {
                                node_to_prune = extremum_children.worst_losing;
                            }
                        } break ;
                        case ControlledType::UNCONTROLLED: {
                            if (extremum_children.worst_winning)
                            {
                                node_to_prune = extremum_children.worst_winning;
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
                    constexpr u32 min_simulations_from_root = 4096;
                    u32 min_simulation_confidence_cycle_treshold = (u32)(min_simulations_from_root / branching_factor) + min_simulations_for_move;
                    // NOTE(david): two tresholds for controlled/uncontrolled, force nodes to be terminal outside this interval
                    constexpr r64 min_mean_value_treshold = -0.95;
                    constexpr r64 max_mean_value_treshold = 0.95;

# if 0
                    for (u32 child_index = 0; child_index < children_nodes->number_of_children && node_to_prune == nullptr; ++child_index)
                    {
                        Node *child_node = children_nodes->children[child_index];
                        if (child_node->num_simulations >= min_simulation_confidence_cycle_treshold)
                        {
                            r64 child_mean_value = child_node->value / (r64)child_node->num_simulations;
                            switch (from_node->controlled_type)
                            {
                                case ControlledType::CONTROLLED: {
                                    if (child_mean_value <= min_mean_value_treshold)
                                    {
                                        assert(false);
                                        node_to_prune = child_node;
                                    }
                                } break ;
                                case ControlledType::UNCONTROLLED: {
                                    if (child_mean_value >= max_mean_value_treshold)
                                    {
                                        node_to_prune = child_node;
                                    }
                                } break ;
                                default: UNREACHABLE_CODE;
                            }
                        }
                    }
#endif
                    if (node_to_prune == nullptr)
                    {
                        // NOTE(david): none of the moves are terminal and none of them falls outside the mean value with enough simulations
                        ExtremumChildren extremum_children_for_cycleing = GetExtremumChildren(from_node, node_pool, min_simulation_confidence_cycle_treshold);
                        // NOTE(david): if there is only 1 condition checked node that have enough simulations, don't prune it yet, as it's our current best node
                        if (extremum_children_for_cycleing.condition_checked_nodes_on_their_simulation_count > 1)
                        {
                            switch (from_node->controlled_type)
                            {
                                case ControlledType::CONTROLLED: {
                                    if (extremum_children_for_cycleing.worst_losing)
                                    {
                                        assert(false && "should already have been selected");
                                    }
                                    else if (extremum_children_for_cycleing.worst_non_terminal)
                                    {
                                        node_to_prune = extremum_children_for_cycleing.worst_non_terminal;
                                    }
                                    else if (extremum_children_for_cycleing.worst_neutral)
                                    {
                                        node_to_prune = extremum_children_for_cycleing.worst_neutral;
                                    }
                                    else
                                    {
                                        assert(false && "if there is a winning move for controlled node, we already should have selected it");
                                    }
                                } break ;
                                case ControlledType::UNCONTROLLED: {
                                    if (extremum_children_for_cycleing.worst_winning)
                                    {
                                        assert(false && "should already have been selected");
                                    }
                                    else if (extremum_children_for_cycleing.worst_non_terminal)
                                    {
                                        node_to_prune = extremum_children_for_cycleing.worst_non_terminal;
                                    }
                                    else if (extremum_children_for_cycleing.worst_neutral)
                                    {
                                        node_to_prune = extremum_children_for_cycleing.worst_neutral;
                                    }
                                    else
                                    {
                                        assert(false && "if there is a losing move for uncontrolled node, we already should have selected it");
                                    }
                                } break ;
                                default: UNREACHABLE_CODE;
                            }
                        }
                    }

                    if (node_to_prune != nullptr)
                    {
                        if (node_to_prune == extremum_children.worst_winning)
                        {
                            if (extremum_children.worst_winning == extremum_children.best_winning)
                            {
                                extremum_children.best_winning = nullptr;
                            }
                            extremum_children.best_winning = nullptr;
                        }
                        else if (node_to_prune == extremum_children.worst_losing)
                        {
                            if (extremum_children.worst_losing == extremum_children.worst_losing)
                            {
                                extremum_children.worst_losing = nullptr;
                            }
                            extremum_children.worst_losing = nullptr;
                        }
                        else if (node_to_prune == extremum_children.worst_neutral)
                        {
                            if (extremum_children.worst_neutral == extremum_children.best_neutral)
                            {
                                extremum_children.best_neutral = nullptr;
                            }
                            extremum_children.worst_neutral = nullptr;
                        }
                        else if (node_to_prune == extremum_children.worst_non_terminal)
                        {
                            if (extremum_children.worst_non_terminal == extremum_children.best_non_terminal)
                            {
                                extremum_children.best_non_terminal = nullptr;
                            }
                            extremum_children.worst_non_terminal = nullptr;
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
            // TODO(david): when transposition table is introduced, this will not yet be the case
        }
    }

    // NOTE(david): no move has been selected yet, choose the best amongst the best children
    if (selected_node == nullptr)
    {
        switch (from_node->controlled_type)
        {
            case ControlledType::CONTROLLED: {
                if (extremum_children.best_neutral != nullptr && extremum_children.best_non_terminal == nullptr)
                {
                    assert(extremum_children.best_winning == nullptr && "this should have been selected already");
                    // TODO(david): rethink this assumption, especially when transposition tables are introduced
                    // ASSUMPTION(david): if there is only terminal moves, that means there are no more moves to cycle, so mark from_node as neutral, update neutral terminal depth potentially
                    assert(from_node->terminal_info.terminal_type == TerminalType::NOT_TERMINAL);
                    from_node->terminal_info.terminal_type = TerminalType::NEUTRAL;
                    // from_node->terminal_info.terminal_depth.neutral = from_node->depth;
                    extremum_children.best_neutral->UpdateTerminalDepthForParentNode(TerminalType::NEUTRAL, node_pool);

                    // TODO(david): prune from_node's children

                    selected_node = from_node;
                }
                else if (extremum_children.best_non_terminal != nullptr)
                {
                    selected_node = extremum_children.best_non_terminal;
                }
                else if (extremum_children.best_losing != nullptr)
                {
                    // NOTE(david): only losing moves are available -> mark controlled node as losing, update losing terminal depth potentially
                    assert(from_node->terminal_info.terminal_type == TerminalType::NOT_TERMINAL);
                    from_node->terminal_info.terminal_type = TerminalType::LOSING;
                    // from_node->terminal_info.terminal_depth.losing = from_node->depth;
                    extremum_children.best_losing->UpdateTerminalDepthForParentNode(TerminalType::LOSING, node_pool);

                    // TODO(david): prune from_node's children

                    selected_node = from_node;
                }
                else
                {
                    // NOTE(david): all children nodes are pruned out -> mark controlled node as losing, update its terminal depth
                    assert(from_node->terminal_info.terminal_type == TerminalType::NOT_TERMINAL);
                    from_node->terminal_info.terminal_type = TerminalType::LOSING;
                    from_node->terminal_info.terminal_depth.losing = from_node->depth + 1;
                    
                    // TODO(david): prune from_node's children

                    selected_node = from_node;
                }
            } break ;
            case ControlledType::UNCONTROLLED: {
                if (extremum_children.best_neutral != nullptr && extremum_children.best_non_terminal == nullptr)
                {
                    assert(extremum_children.best_losing == nullptr && "this should have been selected already");
                    // TODO(david): rethink this assumption, especially when transposition tables are introduced
                    // ASSUMPTION(david): if there is only terminal moves, that means there are no more moves to cycle, so mark from_node as neutral, update its neutral terminal depth potentially
                    assert(from_node->terminal_info.terminal_type == TerminalType::NOT_TERMINAL);
                    from_node->terminal_info.terminal_type = TerminalType::NEUTRAL;
                    // from_node->terminal_info.terminal_depth.neutral = from_node->depth;
                    extremum_children.best_neutral->UpdateTerminalDepthForParentNode(TerminalType::NEUTRAL, node_pool);

                    // TODO(david): prune from_node's children

                    selected_node = from_node;
                }
                else if (extremum_children.best_non_terminal != nullptr)
                {
                    selected_node = extremum_children.best_non_terminal;
                }
                else if (extremum_children.best_winning != nullptr)
                {
                    // NOTE(david): all moves are winning -> mark uncontrolled node as winning, update its winning terminal depth potentially
                    assert(from_node->terminal_info.terminal_type == TerminalType::NOT_TERMINAL);
                    from_node->terminal_info.terminal_type = TerminalType::WINNING;
                    // from_node->terminal_info.terminal_depth.winning = from_node->depth;
                    extremum_children.best_winning->UpdateTerminalDepthForParentNode(TerminalType::WINNING, node_pool);

                    // TODO(david): prune from_node's children

                    selected_node = from_node;
                }
                else
                {
                    // NOTE(david): all children nodes are pruned out -> mark uncontrolled node as winning as there are no good moves for uncontrolled, update its winning terminal depth
                    from_node->terminal_info.terminal_type = TerminalType::WINNING;
                    from_node->terminal_info.terminal_depth.winning = from_node->depth + 1;

                    // TODO(david): prune from_node's children

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

TerminalType Node::UpdateTerminalDepthForParentNode(TerminalType terminal_type_to_update, NodePool &node_pool)
{
    TerminalType result = TerminalType::NOT_TERMINAL;

    // IMPORTANT(david): pruning doesn't change terminal depth, updating when there is a new terminal node does however, which should be backpropagated
    if (parent == nullptr)
    {
        return result;
    }

    switch (terminal_type_to_update)
    {
        case TerminalType::WINNING: {
            assert(this->terminal_info.terminal_depth.winning > 0 && "child's winning terminal depth hasn't been initialized");
            if (parent->terminal_info.terminal_depth.winning == 0)
            {
                // NOTE(david): if winning terminal depth hasn't been initialize yet, initialize it
                parent->terminal_info.terminal_depth.winning = this->terminal_info.terminal_depth.winning;
                parent->terminal_info.terminal_depth.winning_continuation = this->move_to_get_here;

                // NOTE(david): need to update grandparent
                result = terminal_type_to_update;
            }
            else
            {
                assert(parent->terminal_info.terminal_depth.winning_continuation.IsValid() && "if winning continuation isn't initialized, handle it in separate condition");
                switch (parent->controlled_type)
                {
                    case ControlledType::CONTROLLED: {
                        if (this->move_to_get_here == parent->terminal_info.terminal_depth.winning_continuation)
                        {
                            // NOTE(david): winning terminal depth is from the same child

                            if (this->terminal_info.terminal_depth.winning < parent->terminal_info.terminal_depth.winning)
                            {
                                parent->terminal_info.terminal_depth.winning = this->terminal_info.terminal_depth.winning;
                                // NOTE(david): improved winning terminal depth, need to update grandparent
                                result = terminal_type_to_update;
                            }
                            else if (this->terminal_info.terminal_depth.winning > parent->terminal_info.terminal_depth.winning)
                            {
                                // NOTE(david): previously best continuation now has worse winning terminal depth -> recheck parent's children for best winning continuation
                                NodePool::ChildrenTables *children_nodes = node_pool.GetChildren(parent);
                                for (u32 child_index = 0; child_index < children_nodes->number_of_children; ++child_index)
                                {
                                    Node *child = children_nodes->children[child_index];
                                    if (child->terminal_info.terminal_depth.winning == 0)
                                    {
                                        continue ;
                                    }
                                    if (child_index == 0)
                                    {
                                        parent->terminal_info.terminal_depth.winning = child->terminal_info.terminal_depth.winning;
                                        parent->terminal_info.terminal_depth.winning_continuation = child->move_to_get_here;
                                    }
                                    else if (child->terminal_info.terminal_depth.winning < parent->terminal_info.terminal_depth.winning)
                                    {
                                        parent->terminal_info.terminal_depth.winning = child->terminal_info.terminal_depth.winning;
                                        parent->terminal_info.terminal_depth.winning_continuation = child->move_to_get_here;
                                    }
                                }
                                result = terminal_type_to_update;
                            }
                        }
                        else if (this->terminal_info.terminal_depth.winning < parent->terminal_info.terminal_depth.winning)
                        {
                            parent->terminal_info.terminal_depth.winning = this->terminal_info.terminal_depth.winning;
                            parent->terminal_info.terminal_depth.winning_continuation = this->move_to_get_here;

                            result = terminal_type_to_update;
                        }
                    } break ;
                    case ControlledType::UNCONTROLLED: {
                        if (this->move_to_get_here == parent->terminal_info.terminal_depth.winning_continuation)
                        {
                            // NOTE(david): winning terminal depth is from the same child

                            if (this->terminal_info.terminal_depth.winning > parent->terminal_info.terminal_depth.winning)
                            {
                                parent->terminal_info.terminal_depth.winning = this->terminal_info.terminal_depth.winning;
                                // NOTE(david): improved winning terminal depth, need to update grandparent
                                result = terminal_type_to_update;
                            }
                            else if (this->terminal_info.terminal_depth.winning < parent->terminal_info.terminal_depth.winning)
                            {
                                // NOTE(david): previously best continuation now has worse winning terminal depth -> recheck parent's children for best winning continuation
                                NodePool::ChildrenTables *children_nodes = node_pool.GetChildren(parent);
                                for (u32 child_index = 0; child_index < children_nodes->number_of_children; ++child_index)
                                {
                                    Node *child = children_nodes->children[child_index];
                                    if (child->terminal_info.terminal_depth.winning == 0)
                                    {
                                        continue ;
                                    }
                                    if (child_index == 0)
                                    {
                                        parent->terminal_info.terminal_depth.winning = child->terminal_info.terminal_depth.winning;
                                        parent->terminal_info.terminal_depth.winning_continuation = child->move_to_get_here;
                                    }
                                    else if (child->terminal_info.terminal_depth.winning > parent->terminal_info.terminal_depth.winning)
                                    {
                                        parent->terminal_info.terminal_depth.winning = child->terminal_info.terminal_depth.winning;
                                        parent->terminal_info.terminal_depth.winning_continuation = child->move_to_get_here;
                                    }
                                }
                                result = terminal_type_to_update;
                            }
                        }
                        else if (this->terminal_info.terminal_depth.winning > parent->terminal_info.terminal_depth.winning)
                        {
                            parent->terminal_info.terminal_depth.winning = this->terminal_info.terminal_depth.winning;
                            parent->terminal_info.terminal_depth.winning_continuation = this->move_to_get_here;

                            result = terminal_type_to_update;
                        }
                    } break ;
                    default: UNREACHABLE_CODE;
                }
            }
        } break ;
        case TerminalType::LOSING: {
            assert(this->terminal_info.terminal_depth.losing > 0 && "child's losing terminal depth hasn't been initialized");
            if (parent->terminal_info.terminal_depth.losing == 0)
            {
                // NOTE(david): if losing terminal depth hasn't been initialize yet, initialize it
                parent->terminal_info.terminal_depth.losing = this->terminal_info.terminal_depth.losing;
                parent->terminal_info.terminal_depth.losing_continuation = this->move_to_get_here;

                // NOTE(david): need to update grandparent
                result = terminal_type_to_update;
            }
            else
            {
                assert(parent->terminal_info.terminal_depth.losing_continuation.IsValid() && "if losing continuation isn't initialized, handle it in separate condition");
                switch (parent->controlled_type)
                {
                    case ControlledType::CONTROLLED: {
                        if (this->move_to_get_here == parent->terminal_info.terminal_depth.losing_continuation)
                        {
                            // NOTE(david): losing terminal depth is from the same child

                            if (this->terminal_info.terminal_depth.losing > parent->terminal_info.terminal_depth.losing)
                            {
                                parent->terminal_info.terminal_depth.losing = this->terminal_info.terminal_depth.losing;
                                // NOTE(david): improved losing terminal depth, need to update grandparent
                                result = terminal_type_to_update;
                            }
                            else if (this->terminal_info.terminal_depth.losing < parent->terminal_info.terminal_depth.losing)
                            {
                                // NOTE(david): previously best continuation now has worse losing terminal depth -> recheck parent's children for best losing continuation
                                NodePool::ChildrenTables *children_nodes = node_pool.GetChildren(parent);
                                for (u32 child_index = 0; child_index < children_nodes->number_of_children; ++child_index)
                                {
                                    Node *child = children_nodes->children[child_index];
                                    if (child->terminal_info.terminal_depth.losing == 0)
                                    {
                                        continue ;
                                    }
                                    if (child_index == 0)
                                    {
                                        parent->terminal_info.terminal_depth.losing = child->terminal_info.terminal_depth.losing;
                                        parent->terminal_info.terminal_depth.losing_continuation = child->move_to_get_here;
                                    }
                                    else if (child->terminal_info.terminal_depth.losing > parent->terminal_info.terminal_depth.losing)
                                    {
                                        parent->terminal_info.terminal_depth.losing = child->terminal_info.terminal_depth.losing;
                                        parent->terminal_info.terminal_depth.losing_continuation = child->move_to_get_here;
                                    }
                                }
                                result = terminal_type_to_update;
                            }
                        }
                        else if (this->terminal_info.terminal_depth.losing > parent->terminal_info.terminal_depth.losing)
                        {
                            parent->terminal_info.terminal_depth.losing = this->terminal_info.terminal_depth.losing;
                            parent->terminal_info.terminal_depth.losing_continuation = this->move_to_get_here;

                            result = terminal_type_to_update;
                        }
                    } break ;
                    case ControlledType::UNCONTROLLED: {
                        if (this->move_to_get_here == parent->terminal_info.terminal_depth.losing_continuation)
                        {
                            // NOTE(david): losing terminal depth is from the same child

                            if (this->terminal_info.terminal_depth.losing < parent->terminal_info.terminal_depth.losing)
                            {
                                parent->terminal_info.terminal_depth.losing = this->terminal_info.terminal_depth.losing;
                                // NOTE(david): improved losing terminal depth, need to update grandparent
                                result = terminal_type_to_update;
                            }
                            else if (this->terminal_info.terminal_depth.losing > parent->terminal_info.terminal_depth.losing)
                            {
                                // NOTE(david): previously best continuation now has worse losing terminal depth -> recheck parent's children for best losing continuation
                                NodePool::ChildrenTables *children_nodes = node_pool.GetChildren(parent);
                                for (u32 child_index = 0; child_index < children_nodes->number_of_children; ++child_index)
                                {
                                    Node *child = children_nodes->children[child_index];
                                    if (child->terminal_info.terminal_depth.losing == 0)
                                    {
                                        continue ;
                                    }
                                    if (child_index == 0)
                                    {
                                        parent->terminal_info.terminal_depth.losing = child->terminal_info.terminal_depth.losing;
                                        parent->terminal_info.terminal_depth.losing_continuation = child->move_to_get_here;
                                    }
                                    else if (child->terminal_info.terminal_depth.losing < parent->terminal_info.terminal_depth.losing)
                                    {
                                        parent->terminal_info.terminal_depth.losing = child->terminal_info.terminal_depth.losing;
                                        parent->terminal_info.terminal_depth.losing_continuation = child->move_to_get_here;
                                    }
                                }
                                result = terminal_type_to_update;
                            }
                        }
                        else if (this->terminal_info.terminal_depth.losing < parent->terminal_info.terminal_depth.losing)
                        {
                            parent->terminal_info.terminal_depth.losing = this->terminal_info.terminal_depth.losing;
                            parent->terminal_info.terminal_depth.losing_continuation = this->move_to_get_here;

                            result = terminal_type_to_update;
                        }
                    } break ;
                    default: UNREACHABLE_CODE;
                }
            }
        } break ;
        case TerminalType::NEUTRAL: {
            assert(this->terminal_info.terminal_depth.neutral > 0 && "child's neutral terminal depth hasn't been initialized");
            if (parent->terminal_info.terminal_depth.neutral == 0)
            {
                // NOTE(david): if neutral terminal depth hasn't been initialize yet, initialize it
                parent->terminal_info.terminal_depth.neutral = this->terminal_info.terminal_depth.neutral;
                parent->terminal_info.terminal_depth.neutral_continuation = this->move_to_get_here;

                // NOTE(david): need to update the grandparent's neutral terminal depth as well
                result = terminal_type_to_update;
            }
            else
            {
                assert(parent->terminal_info.terminal_depth.neutral_continuation.IsValid() && "if neutral continuation isn't initialized, handle it in separate condition");
                switch (parent->controlled_type)
                {
                    case ControlledType::CONTROLLED: {
                        if (this->move_to_get_here == parent->terminal_info.terminal_depth.neutral_continuation)
                        {
                            // NOTE(david): neutral terminal depth is from the same child

                            if (this->terminal_info.terminal_depth.neutral > parent->terminal_info.terminal_depth.neutral)
                            {
                                parent->terminal_info.terminal_depth.neutral = this->terminal_info.terminal_depth.neutral;
                                // NOTE(david): improved neutral terminal depth, need to update grandparent
                                result = terminal_type_to_update;
                            }
                            else if (this->terminal_info.terminal_depth.neutral < parent->terminal_info.terminal_depth.neutral)
                            {
                                // NOTE(david): previously best continuation now has worse neutral terminal depth -> recheck parent's children for best neutral continuation
                                NodePool::ChildrenTables *children_nodes = node_pool.GetChildren(parent);
                                for (u32 child_index = 0; child_index < children_nodes->number_of_children; ++child_index)
                                {
                                    Node *child = children_nodes->children[child_index];
                                    if (child->terminal_info.terminal_depth.neutral == 0)
                                    {
                                        continue ;
                                    }
                                    if (child_index == 0)
                                    {
                                        parent->terminal_info.terminal_depth.neutral = child->terminal_info.terminal_depth.neutral;
                                        parent->terminal_info.terminal_depth.neutral_continuation = child->move_to_get_here;
                                    }
                                    else if (child->terminal_info.terminal_depth.neutral > parent->terminal_info.terminal_depth.neutral)
                                    {
                                        parent->terminal_info.terminal_depth.neutral = child->terminal_info.terminal_depth.neutral;
                                        parent->terminal_info.terminal_depth.neutral_continuation = child->move_to_get_here;
                                    }
                                }
                                result = terminal_type_to_update;
                            }
                        }
                        else if (this->terminal_info.terminal_depth.neutral > parent->terminal_info.terminal_depth.neutral)
                        {
                            parent->terminal_info.terminal_depth.neutral = this->terminal_info.terminal_depth.neutral;
                            parent->terminal_info.terminal_depth.neutral_continuation = this->move_to_get_here;

                            result = terminal_type_to_update;
                        }
                    } break ;
                    case ControlledType::UNCONTROLLED: {
                        if (this->move_to_get_here == parent->terminal_info.terminal_depth.neutral_continuation)
                        {
                            // NOTE(david): neutral terminal depth is from the same child

                            if (this->terminal_info.terminal_depth.neutral > parent->terminal_info.terminal_depth.neutral)
                            {
                                parent->terminal_info.terminal_depth.neutral = this->terminal_info.terminal_depth.neutral;
                                // NOTE(david): improved neutral terminal depth, need to update grandparent
                                result = terminal_type_to_update;
                            }
                            else if (this->terminal_info.terminal_depth.neutral < parent->terminal_info.terminal_depth.neutral)
                            {
                                // NOTE(david): previously best continuation now has worse neutral terminal depth -> recheck parent's children for best neutral continuation
                                NodePool::ChildrenTables *children_nodes = node_pool.GetChildren(parent);
                                for (u32 child_index = 0; child_index < children_nodes->number_of_children; ++child_index)
                                {
                                    Node *child = children_nodes->children[child_index];
                                    if (child->terminal_info.terminal_depth.neutral == 0)
                                    {
                                        continue ;
                                    }
                                    if (child_index == 0)
                                    {
                                        parent->terminal_info.terminal_depth.neutral = child->terminal_info.terminal_depth.neutral;
                                        parent->terminal_info.terminal_depth.neutral_continuation = child->move_to_get_here;
                                    }
                                    else if (child->terminal_info.terminal_depth.neutral > parent->terminal_info.terminal_depth.neutral)
                                    {
                                        parent->terminal_info.terminal_depth.neutral = child->terminal_info.terminal_depth.neutral;
                                        parent->terminal_info.terminal_depth.neutral_continuation = child->move_to_get_here;
                                    }
                                }
                                result = terminal_type_to_update;
                            }
                        }
                        else if (this->terminal_info.terminal_depth.neutral > parent->terminal_info.terminal_depth.neutral)
                        {
                            parent->terminal_info.terminal_depth.neutral = this->terminal_info.terminal_depth.neutral;
                            parent->terminal_info.terminal_depth.neutral_continuation = this->move_to_get_here;

                            result = terminal_type_to_update;
                        }
                    } break ;
                    default: UNREACHABLE_CODE;
                }
            }
        } break ;
        default: UNREACHABLE_CODE;
    }

    return result;
}

void MCST::PruneNode(Node *node_to_prune, NodePool &node_pool)
{
    // NOTE(david): when pruning nodes, there is no need to update terminal depth, as we never prune the better terminal node, could even assert that here, but that's a bit expensive to do to iterate over the children and check that the pruned node doesn't have the best terminal depth (or at least a second one has the same terminal depth as well)
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

void MCST::_BackPropagate(Node *simulated_node, NodePool &node_pool, SimulationResult simulation_result)
{
    assert(simulated_node != _root_node && "root node is not a valid move so it couldn't have been simulated");
    assert(_root_node->terminal_info.terminal_type == TerminalType::NOT_TERMINAL);

    assert(simulated_node->terminal_info.terminal_type != TerminalType::NOT_TERMINAL || simulated_node->num_simulations == 1 && "TODO: current implementation: simulate once, I need to reintroduce this number to the simulation result maybe");

    /*
        NOTE(david): don't backpropagate if the node needs to be pruned
        Criterias under which the node needs to be pruned:
            // NOTE(david): implemented this, but didn't find it to be necessary, the mean value is enough? - with high certainty (narrow confidence interval) the value is below a certain threshold
            // TODO(david): - understand and maybe implement this: if the confidence interval doesn't overlap with the parent's confidence interval (? because the outcome of the node isn't very different therefore it's not worth exploring further)
    */

    TerminalType should_update_parent_terminal_depth_from_its_children = TerminalType::NOT_TERMINAL;
    if (simulated_node->terminal_info.terminal_type != TerminalType::NOT_TERMINAL)
    {
        Node *parent_node = simulated_node->parent;
        assert(parent_node != nullptr && "node can't be root to propagate back from, as if it was terminal we should have already returned an evaluation result");

        should_update_parent_terminal_depth_from_its_children = simulated_node->UpdateTerminalDepthForParentNode(simulated_node->terminal_info.terminal_type, node_pool);

        // TODO(david): whenever a node's terminal type is set, also set it's terminal depth -> move this to a centralized place
        switch (parent_node->controlled_type)
        {
            case ControlledType::CONTROLLED: {
                if (simulated_node->terminal_info.terminal_type == TerminalType::WINNING)
                {
                    parent_node->terminal_info.terminal_type = TerminalType::WINNING;
                    // parent_node->terminal_info.terminal_depth.winning = parent_node->depth;
                }
            } break ;
            case ControlledType::UNCONTROLLED: {
                if (simulated_node->terminal_info.terminal_type == TerminalType::LOSING)
                {
                    parent_node->terminal_info.terminal_type = TerminalType::LOSING;
                    // parent_node->terminal_info.terminal_depth.losing = parent_node->depth;
                }
            } break ;
            default: UNREACHABLE_CODE;
        }
    }

    Node *cur_node = simulated_node->parent;
    while (cur_node != nullptr)
    {
        Node *parent_node = cur_node->parent;

        // TODO(david): add backpropagating rules terminal rules here as above? in which case start with simulated_node and don't add simulation_result to the node in the simulation itself
        if (should_update_parent_terminal_depth_from_its_children != TerminalType::NOT_TERMINAL)
        {
            should_update_parent_terminal_depth_from_its_children = cur_node->UpdateTerminalDepthForParentNode(should_update_parent_terminal_depth_from_its_children, node_pool);
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
                    assert(false);
                    // NOTE(david): force node to be terminal based on its mean after enough simulations
                    // TODO(david): similar logic, using the same backpropagating rules as above multiple times

                    ControlledType controlling_type = cur_node->controlled_type == ControlledType::CONTROLLED ? ControlledType::UNCONTROLLED : ControlledType::CONTROLLED;
                    switch (controlling_type)
                    {
                        case ControlledType::CONTROLLED: {
                        } break ;
                        case ControlledType::UNCONTROLLED: {
                            if (parent_node)
                            {
                                parent_node->terminal_info.terminal_type = TerminalType::LOSING;
                                parent_node->terminal_info.terminal_depth.losing = parent_node->depth;
                                cur_node->UpdateTerminalDepthForParentNode(TerminalType::LOSING, node_pool);
                            }
                        } break ;
                        default: UNREACHABLE_CODE;
                    }
                    // NOTE(david): haven't finished backpropagation of the simulation, so account for this fact
                    cur_node->num_simulations -= simulation_result.num_simulations;
                    cur_node->value -= simulation_result.value;
                    PruneNode(cur_node, node_pool);
                    return ;
                }
                else if (mean >= upper_mean_prune_treshold)
                {
                    assert(false);
                    ControlledType controlling_type = cur_node->controlled_type == ControlledType::CONTROLLED ? ControlledType::UNCONTROLLED : ControlledType::CONTROLLED;
                    switch (controlling_type)
                    {
                        case ControlledType::CONTROLLED: {
                            if (parent_node)
                            {
                                parent_node->terminal_info.terminal_type = TerminalType::WINNING;
                                parent_node->terminal_info.terminal_depth.winning = parent_node->depth;
                                cur_node->UpdateTerminalDepthForParentNode(TerminalType::WINNING, node_pool);
                            }
                        } break ;
                        case ControlledType::UNCONTROLLED: {
                        } break ;
                        default: UNREACHABLE_CODE;
                    }
                    // NOTE(david): haven't finished backpropagation of the simulation, so account for this fact
                    cur_node->num_simulations -= simulation_result.num_simulations;
                    cur_node->value -= simulation_result.value;
                    PruneNode(cur_node, node_pool);
                    return ;
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
