#include <boost/context/all.hpp>

#if BOOST_EXECUTION_CONTEXT == 2  // boost version >= 1.61
# include "context_v2.h"
#elif BOOST_EXECUTION_CONTEXT == 1  // boost version <= 1.60
# include "context_v1.h"
#else
# error "Unsupport boost version"
#endif

