cmake_minimum_required(VERSION 3.2)
project(vplay)

#set(CMAKE_CXX_COMPILER  /usr/bin/clang++)
#set(CMAKE_C_COMPILER    /usr/bin/clang)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y -fsanitize=address -ggdb3 \
-O0")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DJABS_USE_LIBUNWIND")

include_directories(${PROJECT_SOURCE_DIR}/src
                    ${PROJECT_SOURCE_DIR}/third_party/libwebm
                    ${PROJECT_SOURCE_DIR}/third_party/libwebm/webm_parser/include
                    )

add_subdirectory(third_party/libwebm)
#add_subdirectory(third_party/libvpx)

add_executable(vplay src/vplay.cpp)
target_link_libraries(vplay PUBLIC webm)

