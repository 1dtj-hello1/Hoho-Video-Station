//
// upload_videos.hpp
//

#ifndef UPLOAD_VIDEOS_HPP
#define UPLOAD_VIDEOS_HPP

#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <map>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <memory>
#include <ctime>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

using json = nlohmann::json;

// 前置声明
class SimpleDatabase;
extern SimpleDatabase g_db;

// 上传会话结构体
struct UploadSession {
    std::string filename;
    std::map<int, std::string> chunks;  // chunk_index -> data
    int total_chunks = 0;
    int received_chunks = 0;  // 已接收的分片数
    time_t last_update = 0;

    UploadSession() : last_update(time(nullptr)) {}
};

// 全局变量声明
extern std::unordered_map<std::string, UploadSession> g_upload_sessions;
extern std::mutex g_upload_mutex;

// 函数声明
std::string generate_upload_id();
std::string url_decode(const std::string& str);
std::string get_query_param(const std::string& target, const std::string& key);
int get_user_id_from_cookie(const http::request<http::string_body>& req);
void clean_expired_sessions();
void start_cleanup_timer(net::io_context& ioc);

// HTTP 处理函数
http::message_generator
handle_upload_init(
    beast::string_view doc_root,
    http::request<http::string_body>&& req);

http::message_generator
handle_upload_chunk(
    beast::string_view doc_root,
    http::request<http::string_body>&& req);

http::message_generator
handle_upload_complete(
    beast::string_view doc_root,
    http::request<http::string_body>&& req);

http::message_generator
handle_upload_status(
    beast::string_view doc_root,
    http::request<http::string_body>&& req);

#endif // UPLOAD_VIDEOS_HPP