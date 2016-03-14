#pragma once
#include <system_error>
#include <string>

namespace co
{

enum class eCoErrorCode : int
{
    ec_ok = 0,
    ec_mutex_double_unlock,
    ec_block_object_locked,
    ec_block_object_waiting,
    ec_yield_failed,
    ec_swapcontext_failed,
	ec_makecontext_failed,
    ec_iocpinit_failed,
};

class co_error_category
    : public std::error_category
{
public:
    virtual const char* name() const throw();

    virtual std::string message(int) const;
};

const std::error_category& GetCoErrorCategory();

std::error_code MakeCoErrorCode(eCoErrorCode code);

void ThrowError(eCoErrorCode code);

} //namespace co
