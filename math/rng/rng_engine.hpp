#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace ActuaLib {

class rng_engine {
public:
    virtual ~rng_engine() = default;
    virtual void init(std::size_t sim_dim) = 0;
    virtual std::size_t sim_dim() const = 0;
    virtual void next_uniform(std::vector<double>& out) = 0;
    virtual void next_gaussian(std::vector<double>& out) = 0;
    virtual void skip_to(unsigned index) = 0;
    virtual std::unique_ptr<rng_engine> clone() const = 0;
};

} // namespace ActuaLib
