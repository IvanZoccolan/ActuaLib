#pragma once

#include "genericrng.hpp"
#include "../rng/sobol_engine.hpp"

#include <memory>
#include <vector>

namespace ActuaLib {

class Sobol : public RNG {
    sobol_engine engine_;

public:
    std::unique_ptr<RNG> clone() const override {
        return std::make_unique<Sobol>(*this);
    }

    void init(const size_t simDim) override {
        engine_.init(simDim);
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

    size_t simDim() const override {
        return engine_.sim_dim();
    }
};

} // namespace ActuaLib
