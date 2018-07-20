#pragma once
#include "../common/config.h"

namespace co {

typedef void* fcontext_t;
typedef void (*fn_t)(intptr_t);

struct StackTraits
{
    static stack_malloc_fn_t& MallocFunc();

    static stack_free_fn_t& FreeFunc();

    static int & GetProtectStackPageSize();

    static bool ProtectStack(void* stack, std::size_t size, int pageSize);

    static void UnprotectStack(void* stack, int pageSize);
};

} // namespace co

extern "C"
{

intptr_t jump_fcontext(fcontext_t * ofc, fcontext_t nfc,
        intptr_t vp, bool preserve_fpu = false);

fcontext_t make_fcontext(void* stack, std::size_t size, fn_t fn);

} // extern "C"
