//
// upload_videos.cpp
//

#include "upload_videos.hpp"
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <cstdio>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <random>

// 全局变量定义
std::unordered_map<std::string, UploadSession> g_upload_sessions;
std::mutex g_upload_mutex;

// 外部函数声明（来自 main.cpp）
extern std::string path_cat(beast::string_view base, beast::string_view path);

// 生成唯一的上传ID
std::string generate_upload_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    const char* hex = "0123456789abcdef";
    std::string id = "upload_";
    for (int i = 0; i < 32; i++) {
        id += hex[dis(gen)];
    }
    id += "_" + std::to_string(time(nullptr));
    return id;
}

// URL 解码
std::string url_decode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int value;
            std::istringstream iss(str.substr(i + 1, 2));
            if (iss >> std::hex >> value) {
                result += static_cast<char>(value);
                i += 2;
            }
            else {
                result += str[i];
            }
        }
        else if (str[i] == '+') {
            result += ' ';
        }
        else {
            result += str[i];
        }
    }
    return result;
}

// 从查询字符串中提取参数
std::string get_query_param(const std::string& target, const std::string& key) {
    // 找到 ? 的位置
    size_t qmark = target.find('?');
    if (qmark == std::string::npos) {
        std::cerr << "[get_query_param] No '?' in target: " << target << std::endl;
        return "";
    }
    
    // 提取查询字符串部分
    std::string query = target.substr(qmark + 1);
    
    // 构建要查找的字符串 "key="
    std::string search = key + "=";
    
    // 在查询字符串中查找
    size_t start = query.find(search);
    if (start == std::string::npos) {
        std::cerr << "[get_query_param] Key not found: " << key << " in " << query << std::endl;
        return "";
    }
    
    start += search.length();
    size_t end = query.find('&', start);
    if (end == std::string::npos) {
        end = query.length();
    }
    
    std::string value = query.substr(start, end - start);
    
    // URL 解码
    std::string decoded;
    for (size_t i = 0; i < value.length(); i++) {
        if (value[i] == '%' && i + 2 < value.length()) {
            int hex;
            std::stringstream ss;
            ss << std::hex << value.substr(i + 1, 2);
            ss >> hex;
            decoded += static_cast<char>(hex);
            i += 2;
        } else if (value[i] == '+') {
            decoded += ' ';
        } else {
            decoded += value[i];
        }
    }
    
    std::cout << "[get_query_param] " << key << " = " << decoded << std::endl;
    return decoded.empty() ? value : decoded;
}

// 从 Cookie 获取用户 ID（简化版）
int get_user_id_from_cookie(const http::request<http::string_body>& req) {
    // 实际项目中应该验证 session
    return 1;
}

// 清理过期会话（30分钟未活动）
void clean_expired_sessions() {
    time_t now = time(nullptr);
    std::lock_guard<std::mutex> lock(g_upload_mutex);

    for (auto it = g_upload_sessions.begin(); it != g_upload_sessions.end();) {
        if (now - it->second.last_update > 1800) {
            std::cout << "[清理] 删除过期上传会话: " << it->first << std::endl;
            it = g_upload_sessions.erase(it);
        }
        else {
            ++it;
        }
    }
}

// 初始化上传会话
http::message_generator
handle_upload_init(
    beast::string_view doc_root,
    http::request<http::string_body>&& req)
{
    http::response<http::string_body> res{ http::status::ok, req.version() };
    res.set(http::field::content_type, "application/json");
    res.set(http::field::access_control_allow_origin, "*");

    try {
        auto json_body = json::parse(req.body());
        std::string filename = json_body.value("filename", "");
        int total_chunks = json_body.value("total_chunks", 0);

        if (filename.empty()) {
            throw std::runtime_error("filename is required");
        }
        if (total_chunks <= 0) {
            throw std::runtime_error("total_chunks must be positive");
        }

        std::string upload_id = generate_upload_id();

        {
            std::lock_guard<std::mutex> lock(g_upload_mutex);
            UploadSession session;
            session.filename = filename;
            session.total_chunks = total_chunks;
            session.received_chunks = 0;
            session.last_update = time(nullptr);
            g_upload_sessions[upload_id] = std::move(session);
        }

        json response = {
            {"success", true},
            {"uploadId", upload_id},
            {"message", "Upload session initialized"}
        };
        res.body() = response.dump();

    }
    catch (const std::exception& e) {
        res.result(http::status::bad_request);
        res.body() = json{
            {"success", false},
            {"message", e.what()}
        }.dump();
    }

    res.prepare_payload();
    return res;
}

// 处理分片上传
http::message_generator
handle_upload_chunk(
    beast::string_view doc_root,
    http::request<http::string_body>&& req)
{
    http::response<http::string_body> res{ http::status::ok, req.version() };
    res.set(http::field::content_type, "application/json");
    res.set(http::field::access_control_allow_origin, "*");

    try {
        std::string target(req.target());
        std::cout << "[分片上传] 完整 target: " << target << std::endl;

        // 手动解析 URL 参数
        std::string upload_id;
        std::string chunk_idx_str;

        // 查找 uploadId
        size_t upload_pos = target.find("uploadId=");
        if (upload_pos != std::string::npos) {
            size_t start = upload_pos + 9;
            size_t end = target.find("&", start);
            if (end == std::string::npos) end = target.length();
            upload_id = target.substr(start, end - start);
        }

        // 查找 chunkIndex
        size_t chunk_pos = target.find("chunkIndex=");
        if (chunk_pos != std::string::npos) {
            size_t start = chunk_pos + 11;
            size_t end = target.find("&", start);
            if (end == std::string::npos) end = target.length();
            chunk_idx_str = target.substr(start, end - start);
        }

        std::cout << "[分片上传] uploadId=" << upload_id << std::endl;
        std::cout << "[分片上传] chunkIndex=" << chunk_idx_str << std::endl;
        std::cout << "[分片上传] body size=" << req.body().size() << std::endl;

        if (upload_id.empty()) {
            throw std::runtime_error("uploadId is required");
        }
        if (chunk_idx_str.empty()) {
            throw std::runtime_error("chunkIndex is required");
        }

        int chunk_index = std::stoi(chunk_idx_str);
        std::string chunk_data = req.body();

        {
            std::lock_guard<std::mutex> lock(g_upload_mutex);

            // 检查会话是否存在
            auto it = g_upload_sessions.find(upload_id);
            if (it == g_upload_sessions.end()) {
                std::cerr << "[分片上传] 会话不存在: " << upload_id << std::endl;
                throw std::runtime_error("Upload session not found. Please call /upload/init first.");
            }

            auto& session = it->second;

            // 检查分片是否已存在
            if (session.chunks.find(chunk_index) == session.chunks.end()) {
                session.received_chunks++;
            }

            session.chunks[chunk_index] = chunk_data;
            session.last_update = time(nullptr);

            std::cout << "[分片上传] 进度: " << session.received_chunks
                << "/" << session.total_chunks << std::endl;
        }

        json response = {
            {"success", true},
            {"chunkIndex", chunk_index},
            {"message", "Chunk received"}
        };
        res.body() = response.dump();

    }
    catch (const std::exception& e) {
        std::cerr << "[分片上传错误] " << e.what() << std::endl;
        res.result(http::status::bad_request);
        res.body() = json{
            {"success", false},
            {"message", e.what()}
        }.dump();
    }

    res.prepare_payload();
    return res;
}

// 处理合并请求
http::message_generator
handle_upload_complete(
    beast::string_view doc_root,
    http::request<http::string_body>&& req)
{
    http::response<http::string_body> res{ http::status::ok, req.version() };
    res.set(http::field::content_type, "application/json");
    res.set(http::field::access_control_allow_origin, "*");

    try {
        auto json_body = json::parse(req.body());
        std::string upload_id = json_body.value("uploadId", "");

        if (upload_id.empty()) {
            throw std::runtime_error("uploadId is required");
        }

        UploadSession session;
        {
            std::lock_guard<std::mutex> lock(g_upload_mutex);
            auto it = g_upload_sessions.find(upload_id);
            if (it == g_upload_sessions.end()) {
                throw std::runtime_error("Upload session not found");
            }
            session = std::move(it->second);
            g_upload_sessions.erase(it);
        }

        // 检查是否收齐
        if (session.received_chunks != session.total_chunks) {
            throw std::runtime_error("Missing chunks. Received " +
                std::to_string(session.received_chunks) + "/" +
                std::to_string(session.total_chunks));
        }

        // 构建保存路径
        printf("保存路径: %s\n", doc_root.data());
        std::string upload_dir = path_cat(std::string(doc_root), "videos");
        printf("保存路径: %s\n", upload_dir.c_str());
        std::string filepath = path_cat(upload_dir, session.filename);

        // 确保目录存在
        boost::system::error_code ec;
        if (!boost::filesystem::exists(upload_dir)) {
            boost::filesystem::create_directories(upload_dir, ec);
            if (ec) {
                throw std::runtime_error("Cannot create directory: " + ec.message());
            }
        }
		printf("保存路径: %s\n", filepath.c_str());
        // 合并文件
        FILE* file = fopen(filepath.c_str(), "wb");
        if (!file) {
            throw std::runtime_error("Cannot create file: " + filepath);
        }

        size_t total_bytes = 0;
        for (int i = 0; i < session.total_chunks; i++) {
            auto it = session.chunks.find(i);
            if (it == session.chunks.end() || it->second.empty()) {
                fclose(file);
                throw std::runtime_error("Missing chunk " + std::to_string(i));
            }
            size_t written = fwrite(it->second.data(), 1, it->second.size(), file);
            if (written != it->second.size()) {
                fclose(file);
                throw std::runtime_error("Failed to write chunk " + std::to_string(i));
            }
            total_bytes += written;
        }
        fclose(file);

        std::cout << "[合并完成] " << session.filename
            << ", " << total_bytes << " bytes" << std::endl;

        uintmax_t file_size = boost::filesystem::file_size(filepath);

        json response = {
            {"success", true},
            {"message", "Upload completed"},
            {"filename", session.filename},
            {"url", "/videos/" + session.filename},
            {"size", (long long)file_size}
        };
        res.body() = response.dump();

    }
    catch (const std::exception& e) {
        std::cerr << "[合并错误] " << e.what() << std::endl;
        res.result(http::status::bad_request);
        res.body() = json{
            {"success", false},
            {"message", e.what()}
        }.dump();
    }
    
    res.prepare_payload();
    return res;
}

// 获取上传状态
http::message_generator
handle_upload_status(
    beast::string_view doc_root,
    http::request<http::string_body>&& req)
{
    http::response<http::string_body> res{ http::status::ok, req.version() };
    res.set(http::field::content_type, "application/json");
    res.set(http::field::access_control_allow_origin, "*");

    try {
        std::string target(req.target());
        std::string upload_id = get_query_param(target, "uploadId");

        if (upload_id.empty()) {
            throw std::runtime_error("uploadId is required");
        }

        std::lock_guard<std::mutex> lock(g_upload_mutex);
        auto it = g_upload_sessions.find(upload_id);
        if (it == g_upload_sessions.end()) {
            throw std::runtime_error("Upload session not found");
        }

        auto& session = it->second;

        json response = {
            {"success", true},
            {"uploadId", upload_id},
            {"filename", session.filename},
            {"totalChunks", session.total_chunks},
            {"receivedChunks", session.received_chunks}
        };
        res.body() = response.dump();

    }
    catch (const std::exception& e) {
        res.result(http::status::bad_request);
        res.body() = json{
            {"success", false},
            {"message", e.what()}
        }.dump();
    }

    res.prepare_payload();
    return res;
}

// 启动清理定时器
void start_cleanup_timer(net::io_context& ioc) {
    auto timer = std::make_shared<net::steady_timer>(ioc);
    std::function<void()> cleanup = [timer, &cleanup, &ioc]() {
        clean_expired_sessions();
        timer->expires_after(std::chrono::minutes(10));
        timer->async_wait([&cleanup](boost::system::error_code ec) {
            if (!ec) cleanup();
            });
        };
    cleanup();
}