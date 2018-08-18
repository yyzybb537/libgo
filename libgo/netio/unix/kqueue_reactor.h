#pragma once
#include "../../common/config.h"

#if defined(LIBGO_SYS_FreeBSD)
#include "reactor.h"

namespace co {

class KqueueReactor : public Reactor
{
public:
    KqueueReactor();

    void Run() override;

    bool AddEvent(int fd, short int addEvent, short int promiseEvent) override;

    bool DelEvent(int fd, short int delEvent, short int promiseEvent) override;

private:
    int kq_;
};

} // namespace co
#endif
