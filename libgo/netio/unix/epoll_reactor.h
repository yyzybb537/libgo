#pragma once
#include "../../common/config.h"

#if defined(LIBGO_SYS_Linux)
#include "reactor.h"

namespace co {

class EpollReactor : public Reactor
{
public:
    EpollReactor();

    void Run() override;

    bool AddEvent(int fd, short int addEvent, short int promiseEvent) override;

    bool DelEvent(int fd, short int delEvent, short int promiseEvent) override;

private:
    int epfd_;
};

} // namespace co
#endif
