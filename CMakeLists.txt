message(STATUS "Build main")
cmake_minimum_required( VERSION 2.8)
project(webserver)
add_subdirectory(http_connect)
add_subdirectory(thread_pool)
add_executable(app main.cpp thread_pool/threadPool.hpp)
target_link_libraries(app PUBLIC
                      -Wl,--start-group
                      pthread
                      http_conn
                      locker
                      -Wl,--end-group
                    )