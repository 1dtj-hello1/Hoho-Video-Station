#include "VideoHandler.hpp"
#include "../database/Database.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;
extern SimpleDatabase g_db;

template<class Body, class Allocator>
http::message_generator
handle_get_videos(beast::string_view doc_root,
    http::request<Body, http::basic_fields<Allocator>>&& req)
{
    http::response<http::string_body> res{ http::status::ok, req.version() };
    res.set(http::field::content_type, "application/json");

    try {
        auto result = g_db.query("SELECT id, filename, filepath, size, created_at FROM videos ORDER BY created_at DESC");

        json videos_array = json::array();
        for (size_t i = 0; i < result.rows().size(); ++i) {
            json video_obj;
            video_obj["id"] = result.rows()[i][0].as_int64();
            video_obj["filename"] = result.rows()[i][1].as_string();
            // 修复：先将字符串转换为 std::string
            std::string filename = result.rows()[i][1].as_string();
            video_obj["url"] = "/videos/" + filename;
            video_obj["size"] = result.rows()[i][3].as_int64();
            video_obj["created_at"] = result.rows()[i][4].as_string();
            videos_array.push_back(video_obj);
        }

        res.body() = videos_array.dump();
    }
    catch (const std::exception& e) {
        res.body() = R"({"error": "查询失败"})";
    }

    res.prepare_payload();
    return res;
}

// 显式实例化
template http::message_generator handle_get_videos<boost::beast::http::string_body, std::allocator<char>>(
    beast::string_view,
    http::request<boost::beast::http::string_body, boost::beast::http::basic_fields<std::allocator<char>>>&&);