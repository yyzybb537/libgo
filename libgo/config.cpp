#include <libgo/config.h>
#include <libgo/context.h>
#include <string.h>

namespace co
{
    CoroutineOptions::CoroutineOptions()
        : protect_stack_page(StackAllocator::get_protect_stack_page()),
        stack_malloc_fn(StackAllocator::get_malloc_fn()),
        stack_free_fn(StackAllocator::get_free_fn())
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
