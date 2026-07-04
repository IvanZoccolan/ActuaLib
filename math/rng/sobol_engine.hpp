#pragma once

#include "rng_engine.hpp"
#include "sobol_joe_kuo_6_21201.hpp"
#include "../stats/normal.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace ActuaLib {

class sobol_engine : public rng_engine {
    static constexpr std::size_t kWordBits = 32;
    static constexpr double kOneOver2Pow32 = 1.0 / 4294967296.0;

    std::size_t dim_ = 0;
    std::vector<std::uint32_t> state_;
    std::vector<std::uint32_t> direction_numbers_;
    unsigned index_ = 0;

    static double to_unit_interval(const std::uint32_t x) {
        double u = kOneOver2Pow32 * static_cast<double>(x);
        if (u <= 0.0) {
            u = std::numeric_limits<double>::min();
        }
        return u;
    }

    void reset_state() {
        std::fill(state_.begin(), state_.end(), 0U);
        index_ = 0;
    }

    void build_direction_numbers() {
        direction_numbers_.assign(kWordBits * dim_, 0U);

        for (std::size_t k = 0; k < dim_; ++k) {
            const std::size_t sobol_dim = k + 1;

            std::uint32_t v[kWordBits] = {};
            if (sobol_dim == 1) {
                for (std::size_t i = 0; i < kWordBits; ++i) {
                    v[i] = static_cast<std::uint32_t>(1ULL << (31ULL - i));
                }
            } else {
                const std::size_t row = sobol_dim - 2;
                const std::uint32_t degree = sobol_joe_kuo_6_21201::kDegrees[row];
                const std::uint32_t a = sobol_joe_kuo_6_21201::kAs[row];
                const std::uint32_t offset = sobol_joe_kuo_6_21201::kOffsets[row];

                for (std::uint32_t i = 1; i <= degree; ++i) {
                    const std::uint32_t m_i = sobol_joe_kuo_6_21201::kMs[offset + i - 1U];
                    v[i - 1U] = m_i << (kWordBits - i);
                }

                for (std::uint32_t i = degree + 1U; i <= kWordBits; ++i) {
                    std::uint32_t value = v[i - degree - 1U] ^ (v[i - degree - 1U] >> degree);
                    for (std::uint32_t j = 1U; j <= degree - 1U; ++j) {
                        if ((a >> (degree - 1U - j)) & 1U) {
                            value ^= v[i - j - 1U];
                        }
                    }
                    v[i - 1U] = value;
                }
            }

            for (std::size_t i = 0; i < kWordBits; ++i) {
                direction_numbers_[i * dim_ + k] = v[i];
            }
        }
    }

    void next_point() {
        unsigned n = index_;
        std::size_t j = 0;
        while (n & 1U) {
            n >>= 1U;
            ++j;
        }

        const std::uint32_t* dir = direction_numbers_.data() + (j * dim_);
        for (std::size_t k = 0; k < dim_; ++k) {
            state_[k] ^= dir[k];
        }
        ++index_;
    }

public:
    void init(const std::size_t sim_dim) override {
        if (sim_dim == 0) {
            throw std::invalid_argument("sobol_engine::init requires sim_dim > 0");
        }
        if (sim_dim > sobol_joe_kuo_6_21201::kMaxDimension) {
            throw std::invalid_argument("sobol_engine::init sim_dim exceeds supported Sobol dimensions");
        }

        dim_ = sim_dim;
        state_.assign(dim_, 0U);
        build_direction_numbers();
        reset_state();
    }

    std::size_t sim_dim() const override {
        return dim_;
    }

    void next_uniform(std::vector<double>& out) override {
        if (out.size() != dim_) {
            out.resize(dim_);
        }
        next_point();
        for (std::size_t k = 0; k < dim_; ++k) {
            out[k] = to_unit_interval(state_[k]);
        }
    }

    void next_gaussian(std::vector<double>& out) override {
        if (out.size() != dim_) {
            out.resize(dim_);
        }
        next_point();
        for (std::size_t k = 0; k < dim_; ++k) {
            out[k] = ActuaLib::invNormalCdf(to_unit_interval(state_[k]));
        }
    }

    void skip_to(const unsigned index) override {
        reset_state();
        if (index == 0U) {
            return;
        }

        const std::uint64_t target = static_cast<std::uint64_t>(index);
        std::uint64_t two_i = 1ULL;
        std::uint64_t two_i_plus_one = 2ULL;
        std::size_t i = 0;

        while (two_i <= target && i < kWordBits) {
            if (((target + two_i) / two_i_plus_one) & 1ULL) {
                const std::uint32_t* dir = direction_numbers_.data() + (i * dim_);
                for (std::size_t k = 0; k < dim_; ++k) {
                    state_[k] ^= dir[k];
                }
            }

            two_i <<= 1ULL;
            two_i_plus_one <<= 1ULL;
            ++i;
        }

        index_ = index;
    }

    std::unique_ptr<rng_engine> clone() const override {
        return std::make_unique<sobol_engine>(*this);
    }
};

} // namespace ActuaLib
