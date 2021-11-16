#pragma once
#include <atomic>

#if DEBUG_ROUTINE_SYNC_IN_LIBGO
# include "../common/config.h"
#endif

#if !defined(OPEN_ROUTINE_SYNC_DEBUG)
# define OPEN_ROUTINE_SYNC_DEBUG 0
#endif

namespace libgo
{

#if OPEN_ROUTINE_SYNC_DEBUG

# define RS_DBG DebugPrint
    using co::dbg_channel;
    using co::dbg_rutex;
    using co::dbg_mutex;
    using co::dbg_cond_v;

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

# define RS_DBG(...)
    static const uint64_t dbg_channel           = 0x1 << 16;
    static const uint64_t dbg_rutex             = 0x1 << 18;
    static const uint64_t dbg_mutex             = 0x1 << 19;
    static const uint64_t dbg_cond_v            = 0x1 << 20;

    template <typename T>
    struct DebuggerId
    {
        inline long id() const { return 0; }
    };
#endif

} //namespace libgo