#pragma once
#include "../common/config.h"
#include "../task/task.h"
#include "../common/util.h"

namespace co
{

TaskRefDefine(bool, Affinity)
TaskRefDefine(SourceLocation, Location)
TaskRefDefine(std::string, DebugInfo)
TaskRefDefine(atomic_t<uint64_t>, SuspendId)

inline const char* TaskDebugInfo(Task *tk)
{
    std::string& info = TaskRefDebugInfo(tk);
    if (info.empty()) {
        char buf[128] = {};
        SourceLocation& loc = TaskRefLocation(tk);
        int len = snprintf(buf, sizeof(buf) - 1, "id:%lu, file:%s, line:%d", (unsigned long)tk->id_, loc.file_, loc.lineno_);
        info.assign(buf, len);
    }
    return info.c_str();
}

} // namespace co
