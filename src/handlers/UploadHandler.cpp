#include "UploadHandler.hpp"
#include "../database/Database.hpp"
#include "../utils/PathCat.hpp"
#include <ctime>
#include <iostream>
#include <fstream>

// 正确包含 boost::filesystem
#include <boost/filesystem.hpp>
#include <filesystem> // 使用别名

extern SimpleDatabase g_db;

template<class Body, class Allocator>
http::message_generator
handle_upload(beast::string_view doc_root,
    http::request<Body, http::basic_fields<Allocator>>&& req)
{
    std::string filename;
    auto target = req.target();
    auto pos = target.find("filename=");
    if (pos != std::string::npos) {
        filename = target.substr(pos + 9);
        auto space_pos = filename.find("%20");
        while (space_pos != std::string::npos) {
            filename.replace(space_pos, 3, " ");
            space_pos = filename.find("%20");
        }
    }

    if (filename.empty()) {
        filename = std::to_string(time(nullptr)) + ".mp4";
    }

    std::string upload_dir = path_cat(std::string(doc_root), "videos");
    std::string filepath = path_cat(upload_dir, filename);

    // 使用 fs:: 命名空间
    fs::path upload_path(upload_dir);
    if (!fs::exists(upload_path)) {
        fs::create_directories(upload_path);
    }

    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs.is_open()) {
        http::response<http::string_body> res{ http::status::internal_server_error, req.version() };
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"success": false, "message": "无法打开文件"})";
        res.prepare_payload();
        return res;
    }

    ofs.write(req.body().data(), req.body().size());
    ofs.close();

    auto file_size = fs::file_size(filepath);

    try {
        int user_id = 1;
        g_db.query_prepared(
            "INSERT INTO videos (user_id, filename, filepath, size, created_at) VALUES ({}, {}, {}, {}, NOW())",
            user_id, filename, filepath, file_size
        );
    }
    catch (const std::exception& e) {
        std::cerr << "数据库插入失败: " << e.what() << std::endl;
    }

    http::response<http::string_body> res{ http::status::ok, req.version() };
    res.set(http::field::content_type, "application/json");

    std::string video_url = "/videos/" + filename;

    std::string response_body =
        std::string(R"({"success": true, "message": "上传成功", "filename": ")")
        + filename
        + R"(", "url": ")" + video_url
        + R"(", "size": )" + std::to_string(file_size)
        + R"(})";

    res.body() = response_body;
    res.prepare_payload();
    return res;
}

// 显式实例化
template http::message_generator handle_upload<boost::beast::http::string_body, std::allocator<char>>(
    beast::string_view,
    http::request<boost::beast::http::string_body, boost::beast::http::basic_fields<std::allocator<char>>>&&);