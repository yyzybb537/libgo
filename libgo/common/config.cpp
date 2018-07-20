#include "config.h"
#include "../context/fcontext.h"
#include <string.h>

namespace co
{
    CoroutineOptions::CoroutineOptions()
        : protect_stack_page(StackTraits::GetProtectStackPageSize()),
        stack_malloc_fn(StackTraits::MallocFunc()),
        stack_free_fn(StackTraits::FreeFunc())
    {
    }

    const char* BaseFile(const char* file)
    {
        const char* p = strrchr(file, '/');
        if (p) return p + 1;

        p = strrchr(file, '\\');
        if (p) return p + 1;

        return file;
    }
} //namespace co
