#include "MCST.hpp"
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <string>

static double UCT(Node *node, unsigned int number_of_branches, unsigned int depth)
{
    assert(node != nullptr);
    assert(node->parent != nullptr);
    assert(node->num_simulations != 0);

    double result = 0.0;

    double depth_weight = 0.0;
    double number_of_branches_weight = pow(1.05, number_of_branches);
    double weighted_exploration_factor = EXPLORATION_FACTOR / (number_of_branches_weight + depth * depth_weight);
    result = (double)node->value / (double)node->num_simulations +
             weighted_exploration_factor * sqrt(log((double)node->parent->num_simulations) / (double)node->num_simulations);

    return result;
}

static Move _EvaluateBasedOnMostSimulated(Node *node_to_evaluate_from, const MoveSet &legal_moveset_at_node_to_evaluate_from)
{
    unsigned int max_num_simulation = 0;
    Move max_num_simulation_move = Move::NONE;
    for (const auto &move : legal_moveset_at_node_to_evaluate_from)
    {
        auto it = node_to_evaluate_from->children.find(move);
        if (it == node_to_evaluate_from->children.end())
        {
            // this move is either pruned out or not simulated yet
            continue;
        }
        if (max_num_simulation_move == Move::NONE || it->second->num_simulations > max_num_simulation)
        {
            max_num_simulation_move = move;
            max_num_simulation = it->second->num_simulations;
        }
    }
    if (max_num_simulation_move == Move::NONE)
    {
        throw runtime_error("Need at least 1 simulation");
    }

    return max_num_simulation_move;
}

static Move _EvaluateBasedOnUCT(Node *node_to_evaluate_from, const MoveSet &legal_moveset_at_node_to_evaluate_from)
{
    double highest_uct = 0.0;
    Move highest_uct_move = Move::NONE;
    for (const auto &move : legal_moveset_at_node_to_evaluate_from)
    {
        auto it = node_to_evaluate_from->children.find(move);
        if (it == node_to_evaluate_from->children.end())
        {
            // this move is either pruned out or not simulated yet
            continue;
        }
        double uct = UCT(it->second, legal_moveset_at_node_to_evaluate_from.size(), 0);
        if (highest_uct_move == Move::NONE || uct > highest_uct)
        {
            highest_uct = uct;
            highest_uct_move = move;
        }
    }

    if (highest_uct_move == Move::NONE)
    {
        throw runtime_error("either no simulations are run yet or all moves are pruned out");
    }

    return highest_uct_move;
}

MCST::MCST()
{
    _root = _AllocateNode();
    winning_move_selection_strategy_fn = &_EvaluateBasedOnUCT;
}

void MCST::_DeleteNode(Node *node)
{
    if (node)
    {
        for (auto child : node->children)
        {
            _DeleteNode(child.second);
        }
    }
    delete node;
}

MCST::~MCST()
{
    _DeleteNode(_root);
}

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

static void DebugPrintDecisionTreeHelper(Node *from_node, const MoveSet &legal_moveset_from_node, unsigned int depth, ofstream &fs, Move from_move)
{
    fs << string(depth * 4, ' ') << "("
       << "depth: " << depth << ", value: " << from_node->value << ", num_simulations: " << from_node->num_simulations << ", move: " << MoveToWord(from_move) << ", pruned: " << (from_node->is_pruned ? "yes" : "no") << ")" << endl;

    for (auto move : legal_moveset_from_node)
    {
        MoveSet moveset_from_node = legal_moveset_from_node;
        moveset_from_node.erase(move);
        if (from_node->children.count(move) == 0)
        {
            // isn't simulated yet
            continue;
        }
        Node *child_node = from_node->children[move];
        DebugPrintDecisionTreeHelper(child_node, moveset_from_node, depth + 1, fs, move);
    }
}

static void DebugPrintDecisionTree(Node *from_node, const MoveSet &legal_moveset_from_node, unsigned int move_counter)
{
    ofstream fs("debug/tree" + to_string(move_counter));
    DebugPrintDecisionTreeHelper(from_node, legal_moveset_from_node, 0, fs, Move::NONE);
}

static unsigned int g_move_counter;

Move MCST::Evaluate(const MoveSet &legal_moveset_at_root_node, TerminationPredicate termination_predicate, SimulateFromState simulation_from_state, MoveProcessor move_processor, UtilityEstimationFromState utility_estimation_from_state, double prune_treshhold_for_node)
{
    if (legal_moveset_at_root_node.size() == 0)
    {
        termination_predicate();
        return Move::NONE;
    }

    while (termination_predicate() == false)
    {
        SelectionResult selection_result = _Selection(legal_moveset_at_root_node, move_processor, utility_estimation_from_state);

        SimulationResult simulation_result = simulation_from_state(selection_result.movechain_from_state);

        _BackPropagate(selection_result.selected_node, simulation_result, prune_treshhold_for_node);
    }

#if defined(DEBUG_PRINT_OUT)
    DebugPrintDecisionTree(_root, legal_moveset_at_root_node, g_move_counter);
#endif

    Move result_move = winning_move_selection_strategy_fn(_root, legal_moveset_at_root_node);

    return result_move;
}

static pair<Move, Node *> _SelectChild(Node *from_node, const MoveSet &legal_moves_from_node, UtilityEstimationFromState utility_estimation_from_state, const MoveSequence &movechain_from_state, unsigned int depth)
{
    // Find the maximum UCT value and its corresponding legal move
    Node *selected_node = nullptr;
    Move selected_legal_move = Move::NONE;

    // prune nodes that don't pass the heuristic utility estimate
    Node *highest_pruned_node = nullptr;
    Move highest_pruned_move = Move::NONE;
    double highest_pruned_node_utility = 0.0;
    for (const auto &move : legal_moves_from_node)
    {
        UtilityEstimationResult utility_estimation_result = utility_estimation_from_state(movechain_from_state);
        // TODO(david): test pruning
        assert(utility_estimation_result.should_prune == false);
        if (utility_estimation_result.should_prune)
        {
            if (highest_pruned_move == Move::NONE || utility_estimation_result.utility > highest_pruned_node_utility)
            {
                if (from_node->children.count(move) == 0)
                {
                    highest_pruned_node = nullptr;
                }
                else
                {
                    highest_pruned_node = from_node->children[move];
                }
                highest_pruned_move = move;
                highest_pruned_node_utility = utility_estimation_result.utility;
            }
            continue;
        }

        auto child_node_it = from_node->children.find(move);
        if (child_node_it == from_node->children.end())
        {
            // found a move/node that hasn't been simulated yet -> uct is infinite
            selected_node = nullptr;
            selected_legal_move = move;
            break ;
        }
        Node *child_node = child_node_it->second;
        if (child_node->is_pruned == true)
        {
            continue ;
        }
        double uct = UCT(child_node, legal_moves_from_node.size() - 1, depth);
        if (selected_legal_move == Move::NONE || uct > UCT(from_node->children[selected_legal_move], legal_moves_from_node.size() - 1, depth))
        {
            selected_legal_move = move;
            selected_node = child_node;
        }
    }

    if (selected_legal_move == Move::NONE)
    {
        if (highest_pruned_move == Move::NONE)
        {
            // NOTE(david): prune from_node
            from_node->is_pruned = true;
        }
        return make_pair(highest_pruned_move, highest_pruned_node);
    }

    return make_pair(selected_legal_move, selected_node);
}

// TODO(david): use transposition table to speed up the selection, which would store previous searches, allowing to avoid re-exploring parts of the tree that have already been searched
// TODO(david): LRU?
MCST::SelectionResult MCST::_Selection(const MoveSet &legal_moveset_at_root_node, MoveProcessor move_processor, UtilityEstimationFromState utility_estimation_from_state)
{
    assert(legal_moveset_at_root_node.empty() == false);
    // TODO(david): if there is no move that passes the the treshhold value, we have no good moves, so at this point we should just return with our move selection strategy and end the Evaluation
    assert(_root->is_pruned == false);

    SelectionResult selection_result = {};

    Node *current_node = _root;
    MoveSet current_legal_moves = legal_moveset_at_root_node;
    unsigned int depth = 0;
    while (1)
    {
        if (current_legal_moves.size() == 0)
        {
            // no more legal moves, reached a terminal node
            break ;
        }

        // Select a child node and its corresponding legal move based on maximum UCT value and some other heuristic
        auto [selected_move, selected_node] = _SelectChild(current_node, current_legal_moves, utility_estimation_from_state, selection_result.movechain_from_state, depth);
        if (current_node->is_pruned == true)
        {
            // NOTE(david): if all children is pruned of the current node, start the selection again from the beginning
            return _Selection(legal_moveset_at_root_node, move_processor, utility_estimation_from_state);
        }
        if (selected_node == nullptr)
        {
            // found a child that hasn't been simulated yet -> expand
            selected_node = _Expansion(current_node);
            current_node->children.insert({selected_move, selected_node});
            selection_result.selected_node = selected_node;
            selection_result.movechain_from_state.push_back(selected_move);
            move_processor(current_legal_moves, selected_move);
            return selection_result;
        }
        assert(selected_node != nullptr);

        selection_result.selected_node = selected_node;
        selection_result.movechain_from_state.push_back(selected_move);
        move_processor(current_legal_moves, selected_move);
        current_node = selected_node;
        ++depth;
    }

    return selection_result;
}

Node *MCST::_Expansion(Node *from_node)
{
    Node *result = _AllocateNode();
    result->parent = from_node;

    return result;
}

void MCST::_BackPropagate(Node *from_node, SimulationResult simulation_result, double prune_treshhold_for_node)
{
    // NOTE(david): if the simulation result was from a terminal node, based on a node value threshhold,
    // prune the node
    if (simulation_result.is_terminal_simulation == true)
    {
        from_node->is_pruned = true;
        return ;
    }
    Node *cur_node = from_node;
    while (cur_node != nullptr)
    {
        cur_node->num_simulations += simulation_result.num_simulations;
        cur_node->value += simulation_result.total_value;
        cur_node = cur_node->parent;
    }
}

Node *MCST::_AllocateNode(void)
{
    Node *result = new Node();

    result->value = 0.0;
    result->num_simulations = 0;
    result->parent = nullptr;
    result->is_pruned = false;

    return result;
}

unsigned int MCST::NumberOfSimulationsRan(void)
{
    return _root->num_simulations;
}
