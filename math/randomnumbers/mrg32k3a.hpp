#pragma once

#include "genericrng.hpp"
#include "../rng/mrg32k3a_engine.hpp"
#include <vector>

namespace ActuaLib {

class mrg32k3a : public RNG {
    mrg32k3a_engine engine_;

public:
    mrg32k3a(const unsigned a = 12345, const unsigned b = 12346)
        : engine_(a, b) {}

    std::unique_ptr<RNG> clone() const override {
        return std::make_unique<mrg32k3a>(*this);
    }

    void init(const size_t simDim) override {
        engine_.init(simDim);
    }

    size_t simDim() const override {
        return engine_.sim_dim();
    }

    void nextU(std::vector<double>& uVec) override {
        engine_.next_uniform(uVec);
    }

    void nextG(std::vector<double>& gaussVec) override {
        engine_.next_gaussian(gaussVec);
    }

    void skipTo(const unsigned b) override {
        engine_.skip_to(b);
    }
};

} // namespace ActuaLib
