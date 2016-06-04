#include <boost/context/all.hpp>

#if BOOST_EXECUTION_CONTEXT == 2  // boost version >= 1.61
# include "context_v2.h"
#else  // boost version <= 1.60
# include "context_v1.h"
#endif

