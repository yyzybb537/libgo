#pragma once
#include <atomic>
#include <limits>

namespace co {

// 无锁环形队列
template <typename T, typename SizeType = size_t>
class LockFreeRingHistory
{
public:
    typedef SizeType uint_t;
    typedef std::atomic<uint_t> atomic_t;

    // 多申请一个typename T的空间, 便于判断full和empty.
    explicit LockFreeRingHistory(uint_t capacity)
        : capacity_(capacity)
        , write_{0}
    {
        buffer_ = new T[capacity_];
    }

    ~LockFreeRingHistory() {
        // destory elements.
        delete[] buffer_;
    }

    template <typename U>
    void Push(U && t) {
        // 1.write_步进1.
        auto idx = write_++ % capacity_;

        // 2.数据写入
        buffer_[idx] = t;
    }

    std::vector<T> PopAll() {
        std::vector<T> vec;
        uint_t writen = write_.load();
        if (writen < capacity_) {
            vec.reserve(writen);
            for (uint_t i = 0; i < writen; i++)
                vec.push_back(buffer_[i]);
        } else {
            vec.reserve(capacity_);
            uint_t idx = writen + 1;
            for (uint_t i = 0; i < capacity_; i++) {
                vec.push_back(buffer_[(idx + i) % capacity_]);
            }
        }
        return vec;
    }

private:
    size_t capacity_;
    T* buffer_;
    atomic_t write_;
};

} // namespace co
