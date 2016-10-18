cmake_minimum_required(VERSION 3.2)
project(vplay)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

#set(CMAKE_CXX_COMPILER  /usr/bin/clang++)
#set(CMAKE_C_COMPILER    /usr/bin/clang)

find_package(XCB REQUIRED)
find_package(X11 REQUIRED)


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y -ggdb3 -fconcepts \
-O0 -DVK_USE_PLATFORM_XCB_KHR")

#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DJABS_USE_LIBUNWIND")

include_directories(${PROJECT_SOURCE_DIR}/src
                    ${PROJECT_SOURCE_DIR}/third_party/libwebm
                    ${PROJECT_SOURCE_DIR}/third_party/libwebm/webm_parser/include
                    ${XCB_INCLUDE_DIRS}
                    ${X11_INCLUDE_DIRS}
                    /home/cyberzx/vulkan/VulkanSDK/1.0.26.0/x86_64/include
                    #${VK_INCLUDE_DIRS}
                    )

link_directories(/home/cyberzx/vulkan/VulkanSDK/1.0.26.0/x86_64/lib)

set(ENABLE_WEBM_PARSER true)
add_subdirectory(third_party/libwebm)
#add_subdirectory(third_party/libvpx)

add_executable(vplay src/vrenderer.cpp src/vrenderer.h src/vplay.cpp)
target_link_libraries(vplay webm_parser ${XCB_LIBRARIES} ${X11_LIBRARIES} vulkan png m)

