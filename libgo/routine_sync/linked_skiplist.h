#pragma once
#include <atomic>
#include <thread>
#include <algorithm>

namespace libgo
{

template <typename K, typename V, uint8_t MaxHeight = 12>
struct LinkedSkipList
{
public:
    typedef LinkedSkipList<K, V, MaxHeight> this_type;

    struct Node;

    struct PointPair
    {
        Node* prev = nullptr;
        Node* next = nullptr;
    };

    struct Node
    {
        PointPair links[MaxHeight];
        uint8_t height = 0;
        K key;
        V value;
    };

    class Random
    {
    private:
        enum : uint32_t {
           M = 2147483647L  // 2^31-1
        };
        enum : uint64_t {
           A = 16807  // bits 14, 8, 7, 5, 2, 1, 0
        };

        uint32_t seed_;

        static uint32_t GoodSeed(uint32_t s) { return (s & M) != 0 ? (s & M) : 1; }

    public:
        // This is the largest value that can be returned from Next()
        enum : uint32_t { kMaxNext = M };

        explicit Random(uint32_t s) : seed_(GoodSeed(s)) {}

        uint32_t Next() {
            // We are computing
            //       seed_ = (seed_ * A) % M,    where M = 2^31-1
            //
            // seed_ must not be zero or M, or else all subsequent computed values
            // will be zero or M respectively.  For all other values, seed_ will end
            // up cycling through every number in [1,M-1]
            uint64_t product = seed_ * A;

            // Compute (product % M) using the fact that ((x << 31) % M) == x.
            seed_ = static_cast<uint32_t>((product >> 31) + (product & M));
            // The first reduction may overflow by 1 bit, so we may need to
            // repeat.  mod == M is not possible; using > allows the faster
            // sign-bit-based test.
            if (seed_ > M) {
                seed_ -= M;
            }
            return seed_;
        }

        static Random* getTLSInstance()
        {
            static thread_local Random* instance = createTLSInstance();
            return instance;
        }

    private:
        static Random* createTLSInstance()
        {
            size_t seed = std::hash<std::thread::id>()(std::this_thread::get_id());
            return new Random((uint32_t)seed);
        }
    };

public:
    LinkedSkipList() {
        setBranchingFactor(4);
        clear();
    }

    LinkedSkipList(LinkedSkipList const&) = delete;
    LinkedSkipList& operator=(LinkedSkipList const&) = delete;

    void clear()
    {
        head_ = &dummy_;
        head_->height = 1;
        memset(&head_->links, 0, sizeof(head_->links));
    }

    // 设置增加节点高度的概率: 1/branching_factor
    void setBranchingFactor(int32_t branching_factor)
    {
        scaledInverseBranching_ = ((uint64_t)Random::kMaxNext + 1) / branching_factor;
    }

    // 随机构造height, 不用加锁
    void buildNode(Node* node)
    {
        Random* rnd = Random::getTLSInstance();
        uint8_t height = 1;

        while (height < MaxHeight && rnd->Next() < scaledInverseBranching_)
            ++height;

        node->height = height;
    }

    void insert(Node* node)
    {
        if (!node->height) {
            buildNode(node);
        }

        // 更新最大高度
        if (head_->height < node->height) {
            head_->height = node->height;
        }

        Node* prevs[MaxHeight] = {};
        lower_bound(node, prevs);

        for (uint8_t i = 0; i < node->height; i++)
        {
            Node* prev = prevs[i];
            if (!prev) {
                prev = head_;
            }

            Node* next = prev->links[i].next;

            node->links[i].next = next;
            if (next)
                next->links[i].prev = node;

            node->links[i].prev = prev;
            prev->links[i].next = node;
        }
    }

    Node* front() const
    {
        return head_->links[0].next;
    }

    bool empty() const
    {
        return !front();
    }

    bool erase(Node* node, bool clearHeight = true)
    {
        if (this_type::unlink(node, clearHeight)) {
            shrink_height();
            return true;
        }

        return false;
    }

    int height() const
    {
        return head_->height;
    }

    void shrink_height()
    {
        while (head_->height > 1) {
            int i = head_->height - 1;
            if (head_->links[i].next)
                return ;

            -- head_->height;
        }
    }

    static bool unlink(Node* node, bool clearHeight = true)
    {
        bool unlinked = false;
        for (uint8_t i = 0; i < node->height; ++i)
        {
            PointPair & pp = node->links[i];
            if (pp.prev) {
                pp.prev->links[i].next = pp.next;
                unlinked = true;
            }
            if (pp.next) {
                pp.next->links[i].prev = pp.prev;
                unlinked = true;
            }
            pp.next = pp.prev = nullptr;
        }
        if (clearHeight) {
            node->height = 0;
        }
        return unlinked;
    }

private:
    // @prevs: Node* prevs[MaxHeight] = {};
    // @return: 找到的Node*值是否和value相等
    // @找到的Node*在prevs[0]
    void lower_bound(Node* node, Node** prevs)
    {
        uint8_t maxHeight = head_->height;
        Node* last = head_;
        for (uint8_t x = 0; x < maxHeight; x++)
        {
            uint8_t i = maxHeight - 1 - x;
            Node* pos = last;
            while (pos->links[i].next && pos->links[i].next->key < node->key)
            {
                pos = pos->links[i].next;
            }
            prevs[i] = pos;
            last = pos;
        }
    }

private:
    Node* head_;
    Node dummy_;
    uint32_t scaledInverseBranching_;
};

} // namespace libgo
