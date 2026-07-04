#pragma once

#include <cstddef>

namespace ActuaLib {

class multi_output_scope {
public:
    explicit multi_output_scope(std::size_t outputs) : outputs_(outputs) {}
    std::size_t outputs() const { return outputs_; }

private:
    std::size_t outputs_;
};

} // namespace ActuaLib
