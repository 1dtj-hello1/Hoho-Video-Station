cmake_minimum_required(VERSION 3.16)
project(niwudile)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(_WIN32_WINNT=0x0A00)
add_compile_definitions(BOOST_ASIO_HAS_CO_AWAIT)

# Debug 模式：生成完整调试符号
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
# 查找依赖
find_package(Boost REQUIRED COMPONENTS system thread charconv filesystem)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)  # header-only，不需要链接

add_executable(niwudile main.cpp  "upload_videos.cpp" "upload_videos.hpp")

# 编译选项
target_compile_options(niwudile PRIVATE
    /bigobj
    /Zm2000
)

# 包含目录
target_include_directories(niwudile PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接库
target_link_libraries(niwudile PRIVATE
    Boost::system
    Boost::thread
    Boost::charconv
    Boost::filesystem      # ⭐ 添加这一行
    OpenSSL::SSL
    OpenSSL::Crypto
    ws2_32
)