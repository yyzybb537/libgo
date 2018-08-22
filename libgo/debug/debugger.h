#pragma once
#include "../common/config.h"
#include "../common/spinlock.h"
#include "../common/util.h"
#include <deque>
#include <mutex>
#include <map>
#include <vector>
#include <unordered_set>

#if defined(__GNUC__)
#include <cxxabi.h>
#include <stdlib.h>
#endif 

namespace co
{

// libgo调试工具
class CoDebugger
{
public:
    template <typename T>
    class DebuggerBase
#if ENABLE_DEBUGGER
        : public ObjectCounter<T>
    {
    public:
        typedef DebuggerBase<T> this_type;
        typedef std::unordered_set<this_type*> DbgMap;

    protected:
        DebuggerBase() {
            std::unique_lock<std::mutex> lock(GetLock());
            GetDbgMap().insert(this);
        }

        virtual ~DebuggerBase() {
            std::unique_lock<std::mutex> lock(GetLock());
            GetDbgMap().erase(this);
        }

        static DbgMap& GetDbgMap() {
            static DbgMap obj;
            return obj;
        }

        static std::mutex& GetLock() {
            static std::mutex obj;
            return obj;
        }

    public:
        static std::shared_ptr<DbgMap> SafeGetDbgMap() {
            GetLock().lock();
            return std::shared_ptr<DbgMap>(&GetDbgMap(), [](DbgMap*){
                        GetLock().unlock();
                    });
        }
    };
#else
    {
        ALWAYS_INLINE void Initialize() {}
    };
#endif

public:
    static CoDebugger& getInstance();

    // 获取当前所有信息
    std::string GetAllInfo();

    // 当前协程总数量
    int TaskCount();

    // 当前协程ID, ID从1开始（不在协程中则返回0）
    unsigned long GetCurrentTaskID();

    // 当前协程切换的次数
    unsigned long GetCurrentTaskYieldCount();

    // 设置当前协程调试信息, 打印调试信息时将回显
    void SetCurrentTaskDebugInfo(std::string const& info);

    // 获取当前协程的调试信息, 返回的内容包括用户自定义的信息和协程ID
    const char* GetCurrentTaskDebugInfo();

private:
    CoDebugger() = default;
    ~CoDebugger() = default;
    CoDebugger(CoDebugger const&) = delete;
    CoDebugger& operator=(CoDebugger const&) = delete;
};

template <typename T>
struct real_typename_helper {};

template <typename T>
std::string real_typename()
{
#if defined(__GNUC__)
    /// gcc.
    int s;
    char * realname = abi::__cxa_demangle(typeid(real_typename_helper<T>).name(), 0, 0, &s);
    std::string result(realname);
    free(realname);
#else
    std::string result(typeid(real_typename_helper<T>).name());
#endif 
    std::size_t start = result.find_first_of('<') + 1;
    std::size_t end = result.find_last_of('>');
    return result.substr(start, end - start);
}

} //namespace co
