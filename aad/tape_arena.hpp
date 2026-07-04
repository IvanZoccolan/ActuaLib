#pragma once

#include <algorithm>
#include <cstddef>
#include <new>

#include "op_record.hpp"

namespace ActuaLib {

class tape_arena {
public:
    static constexpr std::size_t block_size = 4096;

    tape_arena() = default;

    ~tape_arena() {
        clear();
        delete[] block_table_;
    }

    tape_arena(const tape_arena&) = delete;
    tape_arena& operator=(const tape_arena&) = delete;

    tape_arena(tape_arena&& other) noexcept {
        swap(other);
    }

    tape_arena& operator=(tape_arena&& other) noexcept {
        if (this != &other) {
            clear();
            delete[] block_table_;
            block_table_ = nullptr;
            block_table_capacity_ = 0;
            swap(other);
        }
        return *this;
    }

    std::size_t push(const op_record& rec) {
        if (size_ == allocated_slots()) {
            allocate_block();
        }
        const std::size_t slot = size_;
        at(slot) = rec;
        ++size_;
        return slot;
    }

    void clear() {
        for (std::size_t i = 0; i < block_count_; ++i) {
            delete block_table_[i];
        }
        block_count_ = 0;
        size_ = 0;
    }

    void rewind() {
        clear();
    }

    void truncate(const std::size_t size) {
        if (size >= size_) {
            return;
        }
        size_ = size;
    }

    std::size_t size() const {
        return size_;
    }

    std::size_t block_count() const {
        return block_count_;
    }

    std::size_t allocated_slots() const {
        return block_count_ * block_size;
    }

    std::size_t memory_bytes() const {
        return block_count_ * sizeof(block) + block_table_capacity_ * sizeof(block*);
    }

    op_record& at(const std::size_t idx) {
        const std::size_t bi = idx / block_size;
        const std::size_t oi = idx % block_size;
        return block_table_[bi]->data[oi];
    }

    const op_record& at(const std::size_t idx) const {
        const std::size_t bi = idx / block_size;
        const std::size_t oi = idx % block_size;
        return block_table_[bi]->data[oi];
    }

private:
    struct block {
        op_record data[block_size];
    };

    void swap(tape_arena& other) noexcept {
        std::swap(block_table_, other.block_table_);
        std::swap(block_table_capacity_, other.block_table_capacity_);
        std::swap(block_count_, other.block_count_);
        std::swap(size_, other.size_);
    }

    void ensure_block_table_capacity(const std::size_t required) {
        if (required <= block_table_capacity_) {
            return;
        }

        std::size_t next_capacity = std::max<std::size_t>(8, block_table_capacity_ * 2);
        while (next_capacity < required) {
            next_capacity *= 2;
        }

        auto** next = new block*[next_capacity];
        for (std::size_t i = 0; i < block_count_; ++i) {
            next[i] = block_table_[i];
        }
        delete[] block_table_;
        block_table_ = next;
        block_table_capacity_ = next_capacity;
    }

    void allocate_block() {
        ensure_block_table_capacity(block_count_ + 1);
        block_table_[block_count_] = new block();
        ++block_count_;
    }

    block** block_table_ = nullptr;
    std::size_t block_table_capacity_ = 0;
    std::size_t block_count_ = 0;
    std::size_t size_ = 0;
};

} // namespace ActuaLib
