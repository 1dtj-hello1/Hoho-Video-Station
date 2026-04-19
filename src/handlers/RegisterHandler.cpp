#include "RegisterHandler.hpp"
#include "../database/Database.hpp"
#include <nlohmann/json.hpp>
#include <ctime>
#include <iostream>

using json = nlohmann::json;
extern SimpleDatabase g_db;

template<class Body, class Allocator>
http::message_generator
handle_register(beast::string_view doc_root,
    http::request<Body, http::basic_fields<Allocator>>&& req)
{
    http::response<http::string_body> res{ http::status::ok, req.version() };
    res.set(http::field::content_type, "application/json; charset=utf-8");

    try {
        auto json_body = json::parse(req.body());
        std::string username = json_body.value("username", "");
        std::string password = json_body.value("password", "");

        if (g_db.is_connected()) {
            auto result = g_db.query_prepared(
                "SELECT id, password FROM users WHERE username = {}",
                username
            );
            if (result.rows().empty()) {
                g_db.query_prepared(
                    "INSERT INTO users (username, password) VALUES ({}, {})",
                    username, password
                );
                std::string session_id = "session_" + std::to_string(time(nullptr));
                res.set(http::field::set_cookie, "session_id=" + session_id + "; Path=/; HttpOnly");
                res.body() = R"({"success": true, "message": "注册成功"})";
            }
            else {
                res.body() = R"({"success": false, "message": "用户名存在"})";
            }
        }
        else {
            res.body() = R"({"success": false, "message": "数据库连接失败"})";
        }
    }
    catch (const std::exception& e) {
        res.body() = R"({"success": false, "message": "请求格式错误"})";
    }

    res.prepare_payload();
    return res;
}

// 显式实例化
template http::message_generator handle_register<boost::beast::http::string_body, std::allocator<char>>(
    beast::string_view,
    http::request<boost::beast::http::string_body, boost::beast::http::basic_fields<std::allocator<char>>>&&);