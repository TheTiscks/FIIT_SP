cmake_minimum_required(VERSION 4.2)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
)
FetchContent_MakeAvailable(googletest)

# add_subdirectory(b_plus_tree)
# add_subdirectory(b_star_plus_tree)
# add_subdirectory(b_star_tree)
add_subdirectory(b_tree)