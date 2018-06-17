#pragma once
#include "../common/config.h"
#include "../task/task.h"
#include "../common/util.h"

namespace co
{

TaskRefDefine(bool, Affinity)
TaskRefDefine(uint64_t, YieldCount)
TaskRefDefine(SourceLocation, Location)
TaskRefDefine(std::string, DebugInfo)

inline const char* TaskDebugInfo(Task *tk)
{
    std::string& info = TaskRefDebugInfo(tk);
    if (info.empty()) {
        char buf[128] = {};
        SourceLocation& loc = TaskRefLocation(tk);
        info = snprintf(buf, sizeof(buf) - 1, "id:%lu, file:%s, line:%d", tk->id_, loc.file_, loc.lineno_);
    }
    return info.c_str();
}

} // namespace co
