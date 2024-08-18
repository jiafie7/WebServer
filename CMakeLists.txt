cmake_minimum_required(VERSION 3.10)

project(WebServer CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DEBUG 1)

if(DEBUG)
    set(CMAKE_BUILD_TYPE Debug)
else()
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g)
else()
    add_compile_options(-O2)
endif()

add_executable(WebServer
    main.cpp
    ./src/timer/timer_list.cpp
    ./src/http/http_conn.cpp
    ./src/log/log.cpp
    ./src/mysql/connection_pool.cpp
    ./src/webserver/webserver.cpp
    ./src/config/config.cpp
)

target_link_libraries(WebServer pthread mysqlclient)


