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

    static int GetReactorThreadCount();

public:
    typedef ReactorElement::Entry Entry;

    Reactor();

    bool Add(int fd, short int pollEvent, Entry const& entry);

    virtual void Run() = 0;

    // ---------- call by element
    virtual bool AddEvent(int fd, short int addEvent, short int promiseEvent) = 0;

    virtual bool DelEvent(int fd, short int delEvent, short int promiseEvent) = 0;

protected:
    void InitLoopThread();

private:
    static std::vector<Reactor*> sReactors_;
    static std::atomic<uint8_t> sReactorCount_;
};

} // namespace co
