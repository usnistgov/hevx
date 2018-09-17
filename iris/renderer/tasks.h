#ifndef HEV_IRIS_RENDERER_TASKS_H_
#define HEV_IRIS_RENDERER_TASKS_H_

#include "protos.h"
#include "tbb/concurrent_queue.h"
#include <system_error>
#include <variant>

namespace iris::Renderer {

using TaskResult = std::variant<std::error_code, Control::Control>;

extern tbb::concurrent_queue<TaskResult> sTasksResultsQueue;

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_TASKS_H_

