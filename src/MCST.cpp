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
    double number_of_branches_weight = 1.0;
    double weighted_exploration_factor = EXPLORATION_FACTOR / (number_of_branches * number_of_branches_weight + depth * depth_weight);
    result = (double)node->value / (double)node->num_simulations +
             weighted_exploration_factor * sqrt(log((double)node->parent->num_simulations) / (double)node->num_simulations);

    return result;
}

static Move _EvaluateBasedOnMostSimulated(Node *node_to_evaluate_from, const MoveSet &legal_moveset_at_node_to_evaluate_from, unsigned int depth)
{
    // NOTE(david): for the other algorithm it makes sense to use depth, but not in this one, not good design
    (void)depth;
    bool is_most_simulated_move_initialized = false;
    pair<unsigned int, Move> most_simulated_move = {};
    for (const auto &move : legal_moveset_at_node_to_evaluate_from)
    {
        auto it = node_to_evaluate_from->children.find(move);
        if (it == node_to_evaluate_from->children.end())
        {
            continue;
        }
        if (is_most_simulated_move_initialized == false || it->second->num_simulations > most_simulated_move.first)
        {
            most_simulated_move = {it->second->num_simulations, move};
        }
        is_most_simulated_move_initialized = true;
    }
    if (is_most_simulated_move_initialized == false)
    {
        throw runtime_error("Need at least 1 simulation");
    }

    return most_simulated_move.second;
}

static Move _EvaluateBasedOnValue(Node *node_to_evaluate_from, const MoveSet &legal_moveset_at_node_to_evaluate_from, unsigned int depth)
{
    double highest_average_value = 0.0;
    Move highest_average_move = Move::NONE;
    for (const auto &move : legal_moveset_at_node_to_evaluate_from)
    {
        if (node_to_evaluate_from->children.count(move) == 0)
        {
            // this move is either pruned out or not simulated yet
            continue;
        }
        Node *child_node = node_to_evaluate_from->children[move];
        double average_value = child_node->value / (double)child_node->num_simulations;
        if (highest_average_move == Move::NONE || average_value > highest_average_value)
        {
            highest_average_value = average_value;
            highest_average_move = move;
        }
    }

    if (highest_average_move == Move::NONE)
    {
        throw runtime_error("either no simulations are run yet or all moves are pruned out");
    }

    return highest_average_move;
}

MCST::MCST()
{
    _root = _AllocateNode();
    winning_move_selection_strategy_fn = &_EvaluateBasedOnValue;
}

static void _delete_node(Node *current_node)
{
    if (current_node)
    {
        for (auto child : current_node->children)
        {
            _delete_node(child.second);
        }
    }
    delete current_node;
}

MCST::~MCST()
{
    _delete_node(_root);
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
       << "depth: " << depth << ", value: " << from_node->value << ", num_simulations: " << from_node->num_simulations << ", move: " << MoveToWord(from_move) << ")" << endl;

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

Move MCST::Evaluate(const MoveSet &legal_moveset_at_root_node, TerminationPredicate termination_predicate, SimulateFromState simulation_from_state, MoveProcessor move_processor, UtilityEstimationFromState utility_estimation_from_state)
{
    if (legal_moveset_at_root_node.size() == 0)
    {
        termination_predicate(true);
        return Move::NONE;
    }

    while (termination_predicate(false) == false)
    {
        SelectionResult selection_result = _Selection(legal_moveset_at_root_node, move_processor, utility_estimation_from_state);

        SimulationResult simulation_result = simulation_from_state(selection_result.movechain_from_state);

        _BackPropagate(selection_result.selected_node, simulation_result);
    }

    DebugPrintDecisionTree(_root, legal_moveset_at_root_node, g_move_counter);

    Move result_move = winning_move_selection_strategy_fn(_root, legal_moveset_at_root_node, 0);

    return result_move;
}

static pair<Move, Node *> _SelectChild(Node *from_node, const MoveSet &moveset_from_node, UtilityEstimationFromState utility_estimation_from_state, const MoveChain &movechain_from_state, unsigned int depth)
{
    // Find the maximum UCT value and its corresponding legal move
    Move selected_legal_move = Move::NONE;
    Node *selected_node = nullptr;
    Node *highest_pruned_node = nullptr;
    Move highest_pruned_move = Move::NONE;
    double highest_pruned_node_utility = 0.0;
    for (const auto &move : moveset_from_node)
    {
        UtilityEstimationResult utility_estimation_result = utility_estimation_from_state(movechain_from_state);
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

        if (from_node->children.count(move) == 0)
        {
            selected_node = nullptr;
            selected_legal_move = move;
            break;
        }
        Node *child_node = from_node->children[move];
        double uct = UCT(child_node, moveset_from_node.size(), depth);
        if (selected_legal_move == Move::NONE || uct > UCT(from_node->children[selected_legal_move], moveset_from_node.size(), depth))
        {
            selected_legal_move = move;
            selected_node = child_node;
        }
    }

    if (selected_legal_move == Move::NONE)
    {
        if (highest_pruned_move == Move::NONE)
        {
            throw runtime_error("no selected move and also no pruned moves");
        }
        return make_pair(highest_pruned_move, highest_pruned_node);
    }

    return make_pair(selected_legal_move, selected_node);
}

// TODO(david): use transposition table to speed up the selection, which would store previous searches, allowing to avoid re-exploring parts of the tree that have already been searched
MCST::SelectionResult MCST::_Selection(const MoveSet &legal_moveset_at_root_node, MoveProcessor move_processor, UtilityEstimationFromState utility_estimation_from_state)
{
    assert(legal_moveset_at_root_node.empty() == false);

    SelectionResult selection_result = {};

    Node *current_node = _root;
    MoveSet current_legal_moves = legal_moveset_at_root_node;
    unsigned int depth = 0;
    while (1)
    {
        if (current_legal_moves.size() == 0)
        {
            // no more legal moves -> reached a terminal node
            break;
        }

        // Find the maximum UCT value and its corresponding legal move
        auto [selected_legal_move, selected_node] = _SelectChild(current_node, current_legal_moves, utility_estimation_from_state, selection_result.movechain_from_state, depth);
        if (selected_node == nullptr)
        {
            // If not all children have been simulated, expand
            selected_node = _Expansion(current_node);
            current_node->children.emplace(pair<Move, Node *>(selected_legal_move, selected_node));
            selection_result.selected_node = selected_node;
            selection_result.movechain_from_state.push_back(selected_legal_move);
            return selection_result;
        }
        assert(selected_node != nullptr);

        selection_result.selected_node = selected_node;
        selection_result.movechain_from_state.push_back(selected_legal_move);
        move_processor(current_legal_moves, selected_legal_move);
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

void MCST::_BackPropagate(Node *from_node, SimulationResult simulation_result)
{
    while (from_node != nullptr)
    {
        from_node->num_simulations += simulation_result.num_simulations;
        from_node->value += simulation_result.total_value;
        from_node = from_node->parent;
    }
}

Node *MCST::_AllocateNode(void)
{
    Node *result = new Node();

    result->value = 0.0;
    result->num_simulations = 0;
    result->parent = nullptr;

    return result;
}

unsigned int MCST::NumberOfSimulationsRan(void)
{
    return _root->num_simulations;
}
