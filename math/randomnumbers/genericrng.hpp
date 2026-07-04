#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

#include "../rng/rng_engine.hpp"

namespace ActuaLib {

class RNG {
public:
    virtual ~RNG() = default;

    virtual void init(const size_t simDim) = 0;
    virtual size_t simDim() const = 0;

    virtual void nextU(std::vector<double>& uVec) = 0;
    virtual void nextG(std::vector<double>& gaussVec) = 0;

    // Skip ahead
    virtual void skipTo(const unsigned b) = 0;

    virtual std::unique_ptr<RNG> clone() const = 0;

    // Naming aliases for engine compatibility
    virtual void next_uniform(std::vector<double>& out) {
        nextU(out);
    }

    virtual void next_gaussian(std::vector<double>& out) {
        nextG(out);
    }

    virtual void skip_to(const unsigned index) {
        skipTo(index);
    }
};

} // namespace ActuaLib
