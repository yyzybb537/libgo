#pragma once
#include "../common/config.h"

namespace co {

template <typename T>
class RingBuffer
{
    T* buf_;
    size_t capacity_;
    size_t write_;
    size_t read_;

public:
    explicit RingBuffer(size_t cap) {
        capacity_ = cap;
        buf_ = (T*)malloc(sizeof(T) * (capacity_ + 1));
        write_ = 0;
        read_ = 0;
    }
    ~RingBuffer() {
        T t;
        while (pop(t));
        free(buf_);
    }

    template <typename Arg>
    bool push(Arg && arg) {
        if (full())
            return false;

        T* ptr = buf_ + pwrite();
        new (ptr) T(std::forward<Arg>(arg));
        if (++write_ == 0) {
            write_ = capacity_ + 1;
            read_ += capacity_ + 1;
        }
        return true;
    }

    template <typename Arg>
    bool pop(Arg & arg) {
        if (empty())
            return false;

        T* ptr = buf_ + pread();
        arg = std::move(*ptr);
        ptr->~T();
        ++read_;
        return true;
    }

    inline size_t size() {
        return write_ - read_;
    }

    inline size_t empty() {
        return size() == 0;
    }

    inline size_t full() {
        return size() == capacity();
    }

    inline size_t capacity() {
        return capacity_;
    }

private:
    inline size_t pwrite() {
        return write_ % (capacity_ + 1);
    }

    inline size_t pread() {
        return read_ % (capacity_ + 1);
    }
};

} // namespace co
