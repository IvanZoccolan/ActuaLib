#include "AAD.hpp"

namespace ActuaLib {

size_t Node::numAdj = 1;
Tape globalTape;
thread_local Tape* Number::tape = &globalTape;

}
