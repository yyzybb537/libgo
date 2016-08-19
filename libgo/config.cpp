#include "config.h"
#include "context.h"

namespace co
{
    CoroutineOptions::CoroutineOptions()
        : protect_stack_page(StackAllocator::get_protect_stack_page()),
        stack_malloc_fn(StackAllocator::get_malloc_fn()),
        stack_free_fn(StackAllocator::get_free_fn())
    {
    }
} //namespace co
