#include <iostream>
#include <chrono>
#include <unistd.h>
#include <sys/socket.h>
#include <string>
#include <atomic>
#include <assert.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mutex>
#include <boost/thread.hpp>
#include <unordered_map>
using std::string;

struct timer
{
    timer(const string& info)
    {
        info_ = info;
        s_ = std::chrono::system_clock::now();
    }
    ~timer()
    {
        std::cout << info_ << " cost " << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now() - s_).count() << " ms" << std::endl;
    }

    std::chrono::time_point<std::chrono::system_clock> s_;
    string info_;
};

int main()
{
    int avoid_optimzed = 0;
    for (int i = 0; i < 10000000; ++i)
        avoid_optimzed ++;

    {
        timer t("increment 100,000,000 times");
        for (int i = 0; i < 100000000; ++i)
            avoid_optimzed += i;
    }

    {
        std::atomic<long> atc;
        timer t("atomic increment 10,000,000 times");
        for (int i = 0; i < 10000000; ++i)
            ++atc;
    }
    
    {
        volatile std::atomic_flag f;
        timer t("atomic lock-unlock 10,000,000 times");
        for (int i = 0; i < 10000000; ++i)
        {
            std::atomic_flag_test_and_set_explicit(&f, std::memory_order_acquire);
            std::atomic_flag_clear_explicit(&f, std::memory_order_release);
        }
    }

    int fds[2];
    socketpair(AF_LOCAL, SOCK_DGRAM, 0, fds);
    int fd = fds[0];
    {
        assert(fd > 0);
        timer t("syscall fstat(socket) 10,000,000 times");
        for (int i = 0; i < 10 * 1000 * 1000; ++i)
        {
            struct stat fd_stat;                                                          
            fstat(fd, &fd_stat);
        }
    }

    int flags = 0;
    {
        timer t("syscall fcntl(fd, F_GETFL, 0) 10,000,000 times");
        for (int i = 0; i < 10 * 1000 * 1000; ++i)
        {
            flags = fcntl(fd, F_GETFL, 0);
        }
    }

    {
        timer t("syscall fcntl(fd, F_SETFL, flags | O_NONBLOCK) 10,000,000 times");
        for (int i = 0; i < 10 * 1000 * 1000; ++i)
        {
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }

    {
        char buf;
        ssize_t n;
        timer t("syscall read(fd) 10,000,000 times");
        for (int i = 0; i < 10 * 1000 * 1000; ++i)
        {
            n += read(fd, &buf, 1);
        }
    }

    {
        char buf;
        ssize_t n = 0;
        timer t("syscall write(fd) 10,000,000 times");
        for (int i = 0; i < 10 * 1000 * 1000; ++i)
        {
            n += write(fd, &buf, 1);
        }
    }

    std::unordered_map<int, int> umap;
    for (int i = 0; i < 10240; ++i)
        umap[i] = i;
    {
        timer t("unordered_map::find() 10,000,000 times");
        for (int i = 0; i < 10 * 1000 * 1000; ++i)
        {
            umap.find(i);
        }
    }

    {
        volatile std::atomic_flag f{0};
        timer t("spinlock unordered_map::find() 10,000,000 times");
        for (int i = 0; i < 10 * 1000 * 1000; ++i)
        {
            while (std::atomic_flag_test_and_set_explicit(&f, std::memory_order_acquire));
            umap.find(i);
            std::atomic_flag_clear_explicit(&f, std::memory_order_release);
        }
    }

    {
        std::mutex mu;
        timer t("syscall mutex::lock-unlock() 10,000,000 times");
        for (int i = 0; i < 10 * 1000 * 1000; ++i)
        {
            std::unique_lock<std::mutex> lock(mu);
            avoid_optimzed ++;
        }
    }

    int thread_c = 4;
    {
        std::mutex mu;
        timer t("syscall multiple-thread mutex::lock-unlock(++i) 1,000,000 times");
        boost::thread *threads = new boost::thread[thread_c];
        for (int i = 0; i < thread_c; ++i)
        {
            boost::thread th([&]{
                for (int i = 0; i < 1 * 1000 * 1000; ++i)
                {
                    std::unique_lock<std::mutex> lock(mu);
                    avoid_optimzed ++;
                }
            });
            threads[i].swap(th);
        }
        for (int i = 0; i < thread_c; ++i)
        {
            threads[i].join();
        }
    }

    {
        volatile std::atomic_flag f { 0 };
        timer t("multiple-thread atomic_flag::lock-unlock(++i) 1,000,000 times");
        boost::thread *threads = new boost::thread[thread_c];
        for (int i = 0; i < thread_c; ++i)
        {
            boost::thread th([&]{
                for (int i = 0; i < 1 * 1000 * 1000; ++i)
                {
                    while (std::atomic_flag_test_and_set_explicit(&f, std::memory_order_acquire));
                    avoid_optimzed ++;
                    std::atomic_flag_clear_explicit(&f, std::memory_order_release);
                }
            });
            threads[i].swap(th);
        }
        for (int i = 0; i < thread_c; ++i)
        {
            threads[i].join();
        }
    }

    {
        std::mutex mu;
        timer t("syscall multiple-thread mutex::lock-unlock(find) 1,000,000 times");
        boost::thread *threads = new boost::thread[thread_c];
        for (int i = 0; i < thread_c; ++i)
        {
            boost::thread th([&]{
                for (int i = 0; i < 1 * 1000 * 1000; ++i)
                {
                    std::unique_lock<std::mutex> lock(mu);
                    umap.find(i);
                }
            });
            threads[i].swap(th);
        }
        for (int i = 0; i < thread_c; ++i)
        {
            threads[i].join();
        }
    }

    {
        volatile std::atomic_flag f{0};
        timer t("multiple-thread atomic_flag::lock-unlock(find) 1,000,000 times");
        boost::thread *threads = new boost::thread[thread_c];
        for (int i = 0; i < thread_c; ++i)
        {
            boost::thread th([&]{
                for (int i = 0; i < 1 * 1000 * 1000; ++i)
                {
                    while (std::atomic_flag_test_and_set_explicit(&f, std::memory_order_acquire));
                    umap.find(i);
                    std::atomic_flag_clear_explicit(&f, std::memory_order_release);
                }
            });
            threads[i].swap(th);
        }
        for (int i = 0; i < thread_c; ++i)
        {
            threads[i].join();
        }
    }

    {
        std::mutex mu;
        timer t("syscall multiple-thread mutex::lock-unlock(sleep(0)) 1,000,000 times");
        boost::thread *threads = new boost::thread[thread_c];
        for (int i = 0; i < thread_c; ++i)
        {
            boost::thread th([&]{
                for (int i = 0; i < 1 * 1000 * 1000; ++i)
                {
                    std::unique_lock<std::mutex> lock(mu);
                    umap.find(i);
                    sleep(0);
                }
            });
            threads[i].swap(th);
        }
        for (int i = 0; i < thread_c; ++i)
        {
            threads[i].join();
        }
    }

    {
        volatile std::atomic_flag f{0};
        timer t("multiple-thread atomic_flag::lock-unlock(sleep(0)) 1,000,000 times");
        boost::thread *threads = new boost::thread[thread_c];
        for (int i = 0; i < thread_c; ++i)
        {
            boost::thread th([&]{
                for (int i = 0; i < 1 * 1000 * 1000; ++i)
                {
                    while (std::atomic_flag_test_and_set_explicit(&f, std::memory_order_acquire));
                    umap.find(i);
                    sleep(0);
                    std::atomic_flag_clear_explicit(&f, std::memory_order_release);
                }
            });
            threads[i].swap(th);
        }
        for (int i = 0; i < thread_c; ++i)
        {
            threads[i].join();
        }
    }

    {
        volatile std::atomic_flag f[1024];
        for (int i = 0; i < 1024; ++i)
            f[i].clear();
        timer t("multiple-thread atomic_flag::lock-unlock(switch find) 1,000,000 times");
        boost::thread *threads = new boost::thread[thread_c];
        for (int i = 0; i < thread_c; ++i)
        {
            boost::thread th([&]{
                for (int i = 0; i < 1 * 1000 * 1000; ++i)
                {
                    while (std::atomic_flag_test_and_set_explicit(&f[i & 1024], std::memory_order_acquire));
                    umap.find(i);
                    std::atomic_flag_clear_explicit(&f[i & 1024], std::memory_order_release);
                }
            });
            threads[i].swap(th);
        }
        for (int i = 0; i < thread_c; ++i)
        {
            threads[i].join();
        }
    }

    std::cout << "avoid integer:" << avoid_optimzed << std::endl;
    return 0;
}
