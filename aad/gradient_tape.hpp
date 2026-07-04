/*
 * Adapted from: A. Savine, "Modern Computational Finance: AAD and Parallel
 * Simulations", Wiley, 2018. Used and modified under the book's license.
 * Internal design replaced: flat slot-index op_record vs pointer-based Node.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

#include "op_record.hpp"
#include "tape_arena.hpp"

namespace ActuaLib {

class gradient_tape {
public:
    static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

    std::size_t record_leaf() {
        op_record rec;
        rec.arity = 0;
        const std::size_t slot = arena_.push(rec);
        adjoints_.push_back(0.0);
        return slot;
    }

    std::size_t record_unary(const std::size_t parent, const double partial_parent) {
        op_record rec;
        rec.arity = 1;
        rec.parents[0] = parent;
        rec.partials[0] = partial_parent;
        const std::size_t slot = arena_.push(rec);
        adjoints_.push_back(0.0);
        return slot;
    }

    std::size_t record_binary(const std::size_t left_parent, const std::size_t right_parent,
        const double partial_left, const double partial_right) {
        op_record rec;
        rec.arity = 2;
        rec.parents[0] = left_parent;
        rec.parents[1] = right_parent;
        rec.partials[0] = partial_left;
        rec.partials[1] = partial_right;
        const std::size_t slot = arena_.push(rec);
        adjoints_.push_back(0.0);
        return slot;
    }

    void seed(const std::size_t slot, const double value = 1.0) {
        if (slot < adjoints_.size()) {
            adjoints_[slot] = value;
        }
    }

    void reverse_from(const std::size_t root_slot) {
        if (root_slot == npos || root_slot >= arena_.size()) {
            return;
        }

        std::fill(adjoints_.begin(), adjoints_.end(), 0.0);
        seed(root_slot, 1.0);
        propagate_range_inclusive(root_slot, 0);
    }

    void mark() {
        mark_slot_ = arena_.size();
    }

    void rewindToMark() {
        if (mark_slot_ == npos) {
            clear();
            return;
        }
        arena_.truncate(mark_slot_);
        adjoints_.resize(mark_slot_);
    }

    void reverse_to_mark(const std::size_t root_slot) {
        if (root_slot == npos || root_slot >= arena_.size()) {
            return;
        }

        const std::size_t begin = mark_slot_ == npos ? 0 : mark_slot_;
        for (std::size_t i = begin; i < adjoints_.size(); ++i) {
            adjoints_[i] = 0.0;
        }
        seed(root_slot, 1.0);
        propagate_range_inclusive(root_slot, begin);
    }

    void reverse_mark_to_start() {
        if (mark_slot_ == npos || mark_slot_ == 0 || arena_.size() == 0) {
            return;
        }
        propagate_range_inclusive(mark_slot_ - 1, 0);
    }

    double adjoint(const std::size_t slot) const {
        return slot < adjoints_.size() ? adjoints_[slot] : 0.0;
    }

    double& adjoint(const std::size_t slot) {
        return adjoints_[slot];
    }

    std::size_t size() const {
        return arena_.size();
    }

    void clear() {
        arena_.clear();
        adjoints_.clear();
        mark_slot_ = npos;
    }

    void rewind() {
        clear();
    }

private:
    void propagate_range_inclusive(const std::size_t from, const std::size_t to) {
        if (arena_.size() == 0 || from >= arena_.size() || to > from) {
            return;
        }

        for (std::size_t i = from + 1; i-- > to;) {
            const auto& rec = arena_.at(i);
            const double a = adjoints_[i];
            if (a == 0.0 || rec.arity == 0) {
                continue;
            }

            if (rec.arity >= 1 && rec.parents[0] < adjoints_.size()) {
                adjoints_[rec.parents[0]] += rec.partials[0] * a;
            }
            if (rec.arity >= 2 && rec.parents[1] < adjoints_.size()) {
                adjoints_[rec.parents[1]] += rec.partials[1] * a;
            }
        }
    }

    tape_arena arena_;
    std::vector<double> adjoints_;
    std::size_t mark_slot_ = npos;
};

} // namespace ActuaLib
