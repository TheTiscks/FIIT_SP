cmake_minimum_required(VERSION 4.2)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
)
FetchContent_MakeAvailable(googletest)

add_subdirectory(../common common)
add_subdirectory(allocator)
add_subdirectory(allocator_boundary_tags)
# add_subdirectory(allocator_buddies_system)
add_subdirectory(allocator_global_heap)
# add_subdirectory(allocator_red_black_tree)
add_subdirectory(allocator_sorted_list)