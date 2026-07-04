#pragma once

#include <future>

namespace ActuaLib {

using task_handle = std::future<bool>;
using TaskHandle = task_handle;

} // namespace ActuaLib
