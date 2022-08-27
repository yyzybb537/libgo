#pragma once
#include "../common/config.h"
#include "../scheduler/processer.h"
#include <list>
#include <condition_variable>
#include "../routine_sync/condition_variable.h"

namespace co
{

typedef libgo::ConditionVariable ConditionVariable;
typedef ConditionVariable co_condition_variable;

} //namespace co
