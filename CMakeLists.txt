message(STATUS "Build main")
cmake_minimum_required( VERSION 2.8)
project(webserver)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
set(CMAKE_BUILD_TYPE Debug)
add_subdirectory(timer)
add_subdirectory(http_connect)
add_subdirectory(thread_pool)
add_executable(app main.cpp thread_pool/threadPool.hpp)
target_link_libraries(app PUBLIC
                      listTimer
                      pthread
                      http_conn
                      locker
                    )