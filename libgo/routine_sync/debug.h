#pragma once
#include <atomic>
#include <mutex>

#if DEBUG_ROUTINE_SYNC_IN_LIBGO
# include "../common/config.h"
#endif

#if !defined(OPEN_ROUTINE_SYNC_DEBUG)
# define OPEN_ROUTINE_SYNC_DEBUG 0
#endif

#if defined(__linux__)
# include <unistd.h>
# include <sys/syscall.h>
# include <sys/time.h>
# include <string.h>
#endif

namespace libgo
{

#if OPEN_ROUTINE_SYNC_DEBUG
    // ID
    template <typename T>
    struct DebuggerId
    {
        DebuggerId() { id_ = ++counter(); }
        DebuggerId(DebuggerId const&) { id_ = ++counter(); }
        DebuggerId(DebuggerId &&) { id_ = ++counter(); }

        inline long id() const {
            return id_;
        }

    private:
        static std::atomic<long>& counter() {
            static std::atomic<long> c;
            return c;
        }

        long id_;
    };
#else
    template <typename T>
    struct DebuggerId
    {
        inline long id() const { return 0; }
    };
#endif

#if OPEN_ROUTINE_SYNC_DEBUG

# if DEBUG_ROUTINE_SYNC_IN_LIBGO
    using co::dbg_channel;
    using co::dbg_rutex;
    using co::dbg_mutex;
    using co::dbg_cond_v;
#  define RS_DBG DebugPrint
# else // DEBUG_ROUTINE_SYNC_IN_LIBGO 

    static const uint64_t dbg_channel           = 0x1 << 16;
    static const uint64_t dbg_rutex             = 0x1 << 18;
    static const uint64_t dbg_mutex             = 0x1 << 19;
    static const uint64_t dbg_cond_v            = 0x1 << 20;

    inline FILE* & rsDebugOutputFile() {
        static FILE* f = stderr;
        return f;
    }

    inline int64_t & rsDebugMask() {
        static int64_t mask = 0;
        return mask;
    }
    inline std::mutex & rsDebugMtx() {
        static std::mutex mtx;
        return mtx;
    }
    inline int rsGetPid()
    {
#    if defined(__linux__)
        return getpid();
#    else
        return 0;
#    endif 
    }
    inline int rsGetTid()
    {
#    if defined(__linux__)
        return syscall(SYS_gettid);
#    else
        return 0;
#    endif 
    }
    inline std::string rsGetTimeStr()
    {
#    if defined(__linux__)
        struct tm local;
        struct timeval tv;
        gettimeofday(&tv, NULL);
        localtime_r(&tv.tv_sec, &local);
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%06d",
                local.tm_year+1900, local.tm_mon+1, local.tm_mday, 
                local.tm_hour, local.tm_min, local.tm_sec, (int)tv.tv_usec);
        return std::string(buffer);
#    else
        return std::string();
#    endif
    }

    inline const char* rsBaseFile(const char* file)
    {
        const char* p = strrchr(file, '/');
        if (p) return p + 1;

        p = strrchr(file, '\\');
        if (p) return p + 1;

        return file;
    }

    inline int rsGetCid()
    {
        return 0;
    }

#  if !defined(RS_GetCid)
#  define RS_GetCid() ::libgo::rsGetCid()
#  endif

#  define RS_DBG(type, fmt, ...) \
    do { \
        if (rsDebugMask() & (type)) { \
            std::unique_lock<std::mutex> lock(::libgo::rsDebugMtx()); \
            fprintf(::libgo::rsDebugOutputFile(), "[%s][%05d][%04d][%06d]%s:%d:(%s)\t " fmt "\n", \
                    ::libgo::rsGetTimeStr().c_str(),\
                    ::libgo::rsGetPid(), ::libgo::rsGetTid(), RS_GetCid(), \
                    ::libgo::rsBaseFile(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__); \
            fflush(::libgo::rsDebugOutputFile()); \
        } \
    } while(0)
# endif 

#else // OPEN_ROUTINE_SYNC_DEBUG
# define RS_DBG(...)
#endif

} //namespace libgo
