cmake_minimum_required(VERSION 3.15)
project(niwudile)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(_WIN32_WINNT=0x0A00)
add_compile_definitions(BOOST_ASIO_HAS_CO_AWAIT)

set(VCPKG_ROOT "E:/huangjin/vcpkg/installed/x64-windows")

find_package(Boost REQUIRED COMPONENTS system thread charconv)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(niwudile main.cpp)

target_compile_options(niwudile PRIVATE /bigobj)

target_include_directories(niwudile PRIVATE ${VCPKG_ROOT}/include)

target_link_libraries(niwudile PRIVATE 
    ${VCPKG_ROOT}/lib/libcrypto.lib
    ${VCPKG_ROOT}/lib/libssl.lib
    ws2_32
    ${VCPKG_ROOT}/lib/libmysql.lib
)