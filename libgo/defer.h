#pragma once

#include <utility>
#include "pp.h"

namespace co
{
    template<typename Func>
    class __defer
    {
    public:
        explicit __defer(Func && on_exit_scope)
            : on_exit_scope_(on_exit_scope), dismiss_(false) {}

        __defer(__defer && other)
            : on_exit_scope_(std::forward<Func>(other.on_exit_scope_))
        {
            dismiss_ = other.dismiss_;
            other.dismiss_ = true;
        }

        __defer& operator=(__defer && other)
        {
            on_exit_scope_ = std::forward<Func>(other.on_exit_scope_);
            dismiss_ = other.dismiss_;
            other.dismiss_ = true;
        }

        ~__defer()
        {
            if (!dismiss_)
                on_exit_scope_();
        }

        bool dismiss()
        {
            if (!dismiss_) {
                dismiss_ = true;
                return true;
            } else
                return false;
        }

    private:
        Func on_exit_scope_;
        bool dismiss_;

    private: // noncopyable
        __defer(__defer const &) = delete;
        __defer& operator=(__defer const &) = delete;
    };

    struct __defer_op
    {
        template <typename Func>
        __defer<Func> operator-(Func && func)
        {
            return __defer<Func>(std::forward<Func>(func));
        }
    };
} // namespace co

