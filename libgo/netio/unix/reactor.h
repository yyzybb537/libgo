#pragma once
#include "../../common/config.h"
#include "reactor_element.h"

namespace co {

class Reactor
{
public:
    static Reactor& Select(int fd);

    // @returns: ignore
    static int InitializeReactorCount(uint8_t n);

public:
    typedef ReactorElement::Entry Entry;

    Reactor();

    // @returns: 0 or errno
    bool Add(int fd, short int pollEvent, Entry const& entry);

    void Run();

private:
    int epfd_;

    static std::vector<Reactor*> sReactors_;
    static std::atomic<uint8_t> sReactorCount_;
};

} // namespace co
