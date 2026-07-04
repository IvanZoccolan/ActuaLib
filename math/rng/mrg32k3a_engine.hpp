#pragma once

#include "rng_engine.hpp"
#include "../stats/normal.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace ActuaLib {

class mrg32k3a_engine : public rng_engine {
    uint64_t seed_a_;
    uint64_t seed_b_;
    std::size_t dim_ = 0;

    uint64_t x0_ = 1;
    uint64_t x1_ = 1;
    uint64_t x2_ = 1;
    uint64_t y0_ = 1;
    uint64_t y1_ = 1;
    uint64_t y2_ = 1;

    bool use_antithetic_cache_ = false;
    std::vector<double> cached_uniforms_;
    std::vector<double> cached_gaussians_;

    static constexpr uint64_t M1 = 4294967087ULL;
    static constexpr uint64_t M2 = 4294944443ULL;
    static constexpr uint64_t A12 = 1403580ULL;
    static constexpr uint64_t A13 = 810728ULL;
    static constexpr uint64_t A21 = 527612ULL;
    static constexpr uint64_t A23 = 1370589ULL;
    static constexpr double INV_M1P1 = 1.0 / 4294967088.0;

    static uint64_t mod_subtract(const uint64_t lhs, const uint64_t rhs, const uint64_t mod) {
        return (lhs >= rhs) ? (lhs - rhs) : (lhs + mod - rhs);
    }

    double next_uniform_raw() {
        const uint64_t x_new = mod_subtract((A12 * x1_) % M1, (A13 * x2_) % M1, M1);
        x2_ = x1_;
        x1_ = x0_;
        x0_ = x_new;

        const uint64_t y_new = mod_subtract((A21 * y0_) % M2, (A23 * y2_) % M2, M2);
        y2_ = y1_;
        y1_ = y0_;
        y0_ = y_new;

        const uint64_t z = (x_new > y_new) ? (x_new - y_new) : (x_new + M1 - y_new);
        return static_cast<double>(z) * INV_M1P1;
    }

    void skip_numbers(const unsigned count) {
        for (unsigned i = 0; i < count; ++i) {
            (void)next_uniform_raw();
        }
    }

public:
    explicit mrg32k3a_engine(const unsigned a = 12345, const unsigned b = 12346)
        : seed_a_(a == 0 ? 1U : a), seed_b_(b == 0 ? 1U : b) {
        reset_state();
    }

    void init(const std::size_t sim_dim) override {
        dim_ = sim_dim;
        cached_uniforms_.assign(dim_, 0.0);
        cached_gaussians_.assign(dim_, 0.0);
    }

    std::size_t sim_dim() const override {
        return dim_;
    }

    void next_uniform(std::vector<double>& out) override {
        if (out.size() != dim_) {
            out.resize(dim_);
        }

        if (use_antithetic_cache_) {
            std::transform(cached_uniforms_.begin(), cached_uniforms_.end(), out.begin(),
                [](const double u) { return 1.0 - u; });
            use_antithetic_cache_ = false;
        } else {
            std::generate(cached_uniforms_.begin(), cached_uniforms_.end(),
                [this]() { return next_uniform_raw(); });
            std::copy(cached_uniforms_.begin(), cached_uniforms_.end(), out.begin());
            use_antithetic_cache_ = true;
        }
    }

    void next_gaussian(std::vector<double>& out) override {
        if (out.size() != dim_) {
            out.resize(dim_);
        }

        if (use_antithetic_cache_) {
            std::transform(cached_gaussians_.begin(), cached_gaussians_.end(), out.begin(),
                [](const double n) { return -n; });
            use_antithetic_cache_ = false;
        } else {
            std::generate(cached_gaussians_.begin(), cached_gaussians_.end(),
                [this]() { return ActuaLib::invNormalCdf(next_uniform_raw()); });
            std::copy(cached_gaussians_.begin(), cached_gaussians_.end(), out.begin());
            use_antithetic_cache_ = true;
        }
    }

    void skip_to(const unsigned index) override {
        reset_state();
        const uint64_t skip = static_cast<uint64_t>(index) * static_cast<uint64_t>(dim_);
        const unsigned capped = static_cast<unsigned>(std::min<uint64_t>(
            skip,
            static_cast<uint64_t>(std::numeric_limits<unsigned>::max())));
        skip_numbers(capped);
        use_antithetic_cache_ = false;
    }

    std::unique_ptr<rng_engine> clone() const override {
        return std::make_unique<mrg32k3a_engine>(*this);
    }

private:
    void reset_state() {
        x0_ = x1_ = x2_ = seed_a_ % M1;
        y0_ = y1_ = y2_ = seed_b_ % M2;

        if (x0_ == 0) x0_ = x1_ = x2_ = 1;
        if (y0_ == 0) y0_ = y1_ = y2_ = 1;

        use_antithetic_cache_ = false;
    }
};

} // namespace ActuaLib
