#include <libgo/co_local_storage.h>

namespace co {

static thread_local CLSMap tlm;

CLSMap* GetThreadLocalCLSMap() {
    return &tlm;
}

} //namespace co
