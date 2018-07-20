
//          Copyright Oliver Kowalke 2014.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_CONTEXT_DETAIL_INVOKE_H
#define BOOST_CONTEXT_DETAIL_INVOKE_H

#include <functional>
#include <type_traits>
#include <utility>

#include <boost/config.hpp>

#ifdef BOOST_HAS_ABI_HEADERS
# include BOOST_ABI_PREFIX
#endif

namespace boost {
namespace context {
namespace detail {

# if _MSC_VER > 1800 
using std::invoke;
# else
template< typename Fn, typename ... Args >
typename std::enable_if<
    ( ! std::is_member_pointer< Fn >::value &&
      ! std::is_function< Fn >::value &&
      ! std::is_function< typename std::remove_pointer< Fn >::type >::value
    ),
    typename std::result_of< Fn( Args ... ) >::type
>::type
invoke( Fn & fn, Args && ... args) {
    return fn( std::forward< Args >( args) ... );
}

template< typename Fn, typename ... Args >
typename std::enable_if<
    ( std::is_member_pointer< Fn >::value &&
      ! std::is_function< Fn >::value &&
      ! std::is_function< typename std::remove_pointer< Fn >::type >::value
    ),
    typename std::result_of< Fn( Args ... ) >::type
>::type
invoke( Fn & fn, Args && ... args) {
    return std::mem_fn( fn)( std::forward< Args >( args) ... );
}

template< typename Fn, typename ... Args >
typename std::enable_if<
    ( std::is_pointer< Fn >::value &&
      std::is_function< typename std::remove_pointer< Fn >::type >::value
    ),
    typename std::result_of< Fn( Args ... ) >::type
>::type
invoke( Fn fn, Args && ... args) {
    return fn( std::forward< Args >( args) ... );
}
# endif

template< typename Fn, typename Tpl, std::size_t... I >
decltype( auto) do_invoke( Fn && fn, Tpl && tpl, std::index_sequence< I ... >) {
    return invoke( fn,
                    // std::tuple_element<> does not perfect forwarding
                    std::forward< decltype( std::get< I >( std::declval< typename std::decay< Tpl >::type >() ) ) >(
                        std::get< I >( std::forward< typename std::decay< Tpl >::type >( tpl) ) ) ... );
}


template< typename Fn, typename Tpl >
decltype( auto) do_invoke( Fn && fn, Tpl && tpl) {
    constexpr auto Size = std::tuple_size< typename std::decay< Tpl >::type >::value;
    return do_invoke( std::forward< Fn >( fn),
                      std::forward< Tpl >( tpl),
                      std::make_index_sequence< Size >{});
}

}}}

#ifdef BOOST_HAS_ABI_HEADERS
#include BOOST_ABI_SUFFIX
#endif

#endif // BOOST_CONTEXT_DETAIL_INVOKE_H
