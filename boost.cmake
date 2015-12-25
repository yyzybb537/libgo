
if (NOT Boost_FOUND)
    if (WIN32)
        set(Boost_USE_STATIC_LIBS        ON)
        set(Boost_USE_MULTITHREADED      ON)
        set(Boost_USE_STATIC_RUNTIME     ON)
    endif()

    find_package(Boost REQUIRED coroutine context thread system date_time chrono regex)
    if (Boost_FOUND)
        include_directories(${Boost_INCLUDE_DIRS})
    endif()
endif()

