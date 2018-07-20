
//          Copyright Oliver Kowalke 2014.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_CONTEXT_EXECUTION_CONTEXT_H
#define BOOST_CONTEXT_EXECUTION_CONTEXT_H

#include <boost/context/detail/config.hpp>

#if ! defined(BOOST_CONTEXT_NO_EXECUTION_CONTEXT)

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <tuple>
#include <utility>

#include <boost/assert.hpp>
#include <boost/config.hpp>
#include <boost/intrusive_ptr.hpp>

#include <boost/context/detail/invoke.hpp>
#include <boost/context/fixedsize_stack.hpp>
#include <boost/context/stack_context.hpp>

#ifdef BOOST_HAS_ABI_HEADERS
# include BOOST_ABI_PREFIX
#endif

namespace boost {
namespace context {
namespace detail {

struct activation_record {
    typedef boost::intrusive_ptr< activation_record >    ptr_t;

    enum flag_t {
        flag_main_ctx   = 1 << 1,
        flag_preserve_fpu = 1 << 2,
        flag_segmented_stack = 1 << 3
    };

    thread_local static ptr_t                   current_rec;

    std::atomic< std::size_t >  use_count;
    LPVOID                      fiber;
    stack_context               sctx;
    void                    *   data;
    int                         flags;

    // used for toplevel-context
    // (e.g. main context, thread-entry context)
    activation_record() noexcept :
        use_count( 0),
        fiber( nullptr),
        sctx(),
        flags( flag_main_ctx
# if defined(BOOST_USE_SEGMENTED_STACKS)
            | flag_segmented_stack
# endif
        ) {
    } 

    activation_record( stack_context sctx_, bool use_segmented_stack) noexcept :
        use_count( 0),
        fiber( nullptr),
        sctx( sctx_),
        data( nullptr),
        flags( use_segmented_stack ? flag_segmented_stack : 0) {
    } 

    virtual ~activation_record() noexcept = default;

    void * resume( void * vp, bool fpu) noexcept {
        // store current activation record in local variable
        activation_record * from = current_rec.get();
        // store `this` in static, thread local pointer
        // `this` will become the active (running) context
        // returned by execution_context::current()
        current_rec = this;
        // context switch from parent context to `this`-context
#if ( _WIN32_WINNT > 0x0600)
        if ( ::IsThreadAFiber() ) {
            from->fiber = ::GetCurrentFiber();
        } else {
            from->fiber = ::ConvertThreadToFiber( nullptr);
        }
#else
        from->fiber = ::ConvertThreadToFiber( nullptr);
        if ( nullptr == from->fiber) {
            DWORD err = ::GetLastError();
            BOOST_ASSERT( ERROR_ALREADY_FIBER == err);
            from->fiber = ::GetCurrentFiber(); 
            BOOST_ASSERT( nullptr != from->fiber);
            BOOST_ASSERT( reinterpret_cast< LPVOID >( 0x1E00) != from->fiber);
        }
#endif
        // store passed argument (void pointer)
        data = vp;
        // context switch
        ::SwitchToFiber( fiber);
        // access the activation-record of the current fiber
        activation_record * ar = static_cast< activation_record * >( GetFiberData() );
        return nullptr != ar ? ar->data : nullptr;
    }

    virtual void deallocate() {
        delete this;
    }

    friend void intrusive_ptr_add_ref( activation_record * ar) {
        ++ar->use_count;
    }

    friend void intrusive_ptr_release( activation_record * ar) {
        BOOST_ASSERT( nullptr != ar);

        if ( 0 == --ar->use_count) {
            ar->deallocate();
        }
    }
};

struct activation_record_initializer {
    activation_record_initializer();
    ~activation_record_initializer();
};

template< typename Fn, typename Tpl, typename StackAlloc >
class capture_record : public activation_record {
private:
    StackAlloc      salloc_;
    Fn              fn_;
    Tpl             tpl_;
    activation_record   *   caller_;

    static void destroy( capture_record * p) {
        StackAlloc salloc( p->salloc_);
        stack_context sctx( p->sctx);
        // deallocate activation record
        p->~capture_record();
        // destroy stack with stack allocator
        salloc.deallocate( sctx);
    }

public:
    explicit capture_record(
            stack_context sctx, StackAlloc const& salloc,
            Fn && fn, Tpl && tpl,
            activation_record * caller,
            bool use_segmented_stack) noexcept :
        activation_record( sctx, use_segmented_stack),
        salloc_( salloc),
        fn_( std::forward< Fn >( fn) ),
        tpl_( std::forward< Tpl >( tpl) ),
        caller_( caller) {
    }

    void deallocate() override final {
        destroy( this);
    }

    void run() noexcept {
        try {
            void * vp = caller_->resume( caller_, true);
            do_invoke( fn_, std::tuple_cat( tpl_, std::tie( vp) ) );
        } catch (...) {
            std::terminate();
        }
        BOOST_ASSERT( 0 == (flags & flag_main_ctx) );
    }
};

}

struct preallocated {
    void        *   sp;
    std::size_t     size;
    stack_context   sctx;

    preallocated( void * sp_, std::size_t size_, stack_context sctx_) noexcept :
        sp( sp_), size( size_), sctx( sctx_) {
    }
};

class BOOST_CONTEXT_DECL execution_context {
private:
    // tampoline function
    // entered if the execution context
    // is resumed for the first time
    template< typename AR >
    static VOID WINAPI entry_func( LPVOID p) {
        BOOST_ASSERT( 0 != p);

        AR * ar( reinterpret_cast< AR * >( p) );
        // start execution of toplevel context-function
        ar->run();
        //ctx->fn_(ctx->param_);
        ::DeleteFiber( ar->fiber);
    }

    typedef boost::intrusive_ptr< detail::activation_record >    ptr_t;

    ptr_t   ptr_;

    template< typename StackAlloc, typename Fn ,typename Tpl >
    static detail::activation_record * create_context(
            StackAlloc salloc,
            Fn && fn, Tpl && tpl,
            bool use_segmented_stack) {
        typedef detail::capture_record< Fn, Tpl, StackAlloc >  capture_t;

        // hackish
        std::size_t fsize = salloc.size_;
        // protected_fixedsize_stack needs at least 2*page-size
        salloc.size_ = ( std::max)( sizeof( capture_t), 2 * stack_traits::page_size() );

        stack_context sctx( salloc.allocate() );
        // reserve space for control structure
        void * sp = static_cast< char * >( sctx.sp) - sizeof( capture_t);
        // get current activation record
        ptr_t curr = execution_context::current().ptr_;
        // placement new for control structure on fast-context stack
        capture_t * cr = new ( sp) capture_t(
                sctx, salloc, std::forward< Fn >( fn), std::forward< Tpl >( tpl), curr.get(), use_segmented_stack);
        // create fiber
        // use default stacksize
        cr->fiber = ::CreateFiber( fsize, execution_context::entry_func< capture_t >, cr);
        BOOST_ASSERT( nullptr != cr->fiber);
        return cr;
    }

    template< typename StackAlloc, typename Fn , typename Tpl >
    static detail::activation_record * create_context(
            preallocated palloc, StackAlloc salloc,
            Fn && fn, Tpl && tpl,
            bool use_segmented_stack) {
        typedef detail::capture_record< Fn, Tpl, StackAlloc >  capture_t;

        // hackish
        std::size_t fsize = salloc.size_;
        // protected_fixedsize_stack needs at least 2*page-size
        salloc.size_ = ( std::max)( sizeof( capture_t), 2 * stack_traits::page_size() );

        // reserve space for control structure
        void * sp = static_cast< char * >( palloc.sp) - sizeof( capture_t);
        // get current activation record
        ptr_t curr = execution_context::current().ptr_;
        // placement new for control structure on fast-context stack
        capture_t * cr = new ( sp) capture_t(
                palloc.sctx, salloc, std::forward< Fn >( fn), std::forward< Tpl >( tpl), curr.get(), use_segmented_stack);
        // create fiber
        // use default stacksize
        cr->fiber = ::CreateFiber( fsize, execution_context::entry_func< capture_t >, cr);
        BOOST_ASSERT( nullptr != cr->fiber);
        return cr;
    }

    execution_context() :
        // default constructed with current activation_record
        ptr_( detail::activation_record::current_rec) {
    }

public:
    static execution_context current() noexcept;

    template< typename Fn, typename ... Args >
    explicit execution_context( Fn && fn, Args && ... args) :
        // deferred execution of fn and its arguments
        // arguments are stored in std::tuple<>
        // non-type template parameter pack via std::index_sequence_for<>
        // preserves the number of arguments
        // used to extract the function arguments from std::tuple<>
        ptr_( create_context( fixedsize_stack(),
                              std::forward< Fn >( fn),
                              std::make_tuple( std::forward< Args >( args) ...),
                              false) ) {
        ptr_->resume( ptr_.get(), true);
    }

    template< typename StackAlloc, typename Fn, typename ... Args >
    explicit execution_context( std::allocator_arg_t, StackAlloc salloc, Fn && fn, Args && ... args) :
        // deferred execution of fn and its arguments
        // arguments are stored in std::tuple<>
        // non-type template parameter pack via std::index_sequence_for<>
        // preserves the number of arguments
        // used to extract the function arguments from std::tuple<>
        ptr_( create_context( salloc,
                              std::forward< Fn >( fn),
                              std::make_tuple( std::forward< Args >( args) ...),
                              false) ) {
        ptr_->resume( ptr_.get(), true);
    }

    template< typename StackAlloc, typename Fn, typename ... Args >
    explicit execution_context( std::allocator_arg_t, preallocated palloc, StackAlloc salloc, Fn && fn, Args && ... args) :
        // deferred execution of fn and its arguments
        // arguments are stored in std::tuple<>
        // non-type template parameter pack via std::index_sequence_for<>
        // preserves the number of arguments
        // used to extract the function arguments from std::tuple<>
        ptr_( create_context( palloc, salloc,
                              std::forward< Fn >( fn),
                              std::make_tuple( std::forward< Args >( args) ...),
                              false) ) {
        ptr_->resume( ptr_.get(), true);
    }

    execution_context( execution_context const& other) noexcept :
        ptr_( other.ptr_) {
    }

    execution_context( execution_context && other) noexcept :
        ptr_( other.ptr_) {
        other.ptr_.reset();
    }

    execution_context & operator=( execution_context const& other) noexcept {
        if ( this != & other) {
            ptr_ = other.ptr_;
        }
        return * this;
    }

    execution_context & operator=( execution_context && other) noexcept {
        if ( this != & other) {
            ptr_ = other.ptr_;
            other.ptr_.reset();
        }
        return * this;
    }

    explicit operator bool() const noexcept {
        return nullptr != ptr_.get();
    }

    bool operator!() const noexcept {
        return nullptr == ptr_.get();
    }

    void * operator()( void * vp = nullptr, bool preserve_fpu = false) noexcept {
        return ptr_->resume( vp, preserve_fpu);
    }
};

}}

# ifdef BOOST_HAS_ABI_HEADERS
# include BOOST_ABI_SUFFIX
# endif

#endif

#endif // BOOST_CONTEXT_EXECUTION_CONTEXT_H
