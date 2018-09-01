#include "co_local_storage.h"

namespace co {


CLSMap* GetThreadLocalCLSMap() {
    static thread_local CLSMap tlm;
    return &tlm;
}

} //namespace co
