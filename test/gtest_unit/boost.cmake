
if (NOT Boost_FOUND)
    set(Boost_USE_STATIC_LIBS        ON)
    set(Boost_USE_MULTITHREADED      ON)
    set(Boost_USE_STATIC_RUNTIME     ON)

    find_package(Boost REQUIRED thread system date_time chrono regex)
    if (Boost_FOUND)
        include_directories(${Boost_INCLUDE_DIRS})
		link_directories(${Boost_LIBRARY_DIRS})
    endif()
endif()

