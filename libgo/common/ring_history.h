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
        : capacity_(reCapacity(capacity))
        , write_{0}
    {
        buffer_ = new T[capacity_];
    }

    ~LockFreeRingQueue() {
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

    void PopAll(std::vector<T> & vec) {
        uint_t writen = write_.load();
        if (writen < capacity_) {
            vec.reverse(writen);
            for (int i = 0; i < writen; i++)
                vec.push_back(buffer_[i]);
        } else {
            vec.reverse(capacity_);
            uint_t idx = writen + 1;
            for (int i = 0; i < capacity_; i++) {
                vec.push_back(buffer_[(idx + i) % capacity_]);
            }
        }
    }

private:
    size_t capacity_;
    T* buffer_;
    atomic_t write_;
};

} // namespace co
