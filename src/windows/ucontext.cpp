#include "ucontext.h"
#include <Windows.h>
#include <system_error>
#include "error.h"

namespace co {
	
ucontext_t::~ucontext_t()
{
	if (native) {
		DeleteFiber(native);
		native = NULL;
	}
}

static VOID WINAPI FiberFunc(LPVOID param)
{
	ucontext_t *ucp = (ucontext_t*)param;
	ucp->fn(ucp->arg);
}

void makecontext(ucontext_t *ucp, void (*func)(), int argc, void* argv)
{
	ucp->fn = (void(*)(void*))func;
	ucp->arg = argv;
	ucp->native = CreateFiberEx(1024, ucp->uc_stack.ss_size, FIBER_FLAG_FLOAT_SWITCH, (LPFIBER_START_ROUTINE)FiberFunc, ucp);
	if (!ucp->native) {
		ThrowError(eCoErrorCode::ec_makecontext_failed);
	}
}

int swapcontext(ucontext_t *oucp, ucontext_t *ucp)
{
	SwitchToFiber(ucp->native);
    return 0;
}

int getcontext(ucontext_t *ucp)
{
    return 0;
}

} //namespace co
