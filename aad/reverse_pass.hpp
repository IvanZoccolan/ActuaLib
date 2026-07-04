#pragma once

#include <cstddef>

#include "gradient_tape.hpp"

namespace ActuaLib {

inline void run_reverse_pass(gradient_tape& tape, std::size_t root_slot) {
	tape.reverse_from(root_slot);
}

inline void run_reverse_pass(gradient_tape& tape) {
	if (tape.size() == 0) {
		return;
	}
	tape.reverse_from(tape.size() - 1);
}

} // namespace ActuaLib
