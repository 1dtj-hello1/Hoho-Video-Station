#include "LoginHandler.hpp"
#include "../database/Database.hpp"
#include <nlohmann/json.hpp>
#include <ctime>
#include <iostream>

using json = nlohmann::json;
extern SimpleDatabase g_db;

template<class Body, class Allocator>
http::message_generator
handle_login(beast::string_view doc_root,
    http::request<Body, http::basic_fields<Allocator>>&& req)
{
    http::response<http::string_body> res{ http::status::ok, req.version() };
    res.set(http::field::content_type, "application/json; charset=utf-8");

    try {
        auto json_body = json::parse(req.body());
        std::string username = json_body.value("username", "");
        std::string password = json_body.value("password", "");
        printf("Received login request: username=%s, password=%s\n", username.c_str(), password.c_str());

        if (g_db.is_connected()) {
            auto result = g_db.query_prepared(
                "SELECT id, password FROM users WHERE username = {}",
                username
            );
            if (result.rows().empty()) {
                res.body() = R"({"success": false, "message": "用户名不存在"})";
            }
            else {
                std::string stored_password = result.rows()[0][1].as_string();
                if (password == stored_password) {
                    std::string session_id = "session_" + std::to_string(time(nullptr));
                    res.set(http::field::set_cookie, "session_id=" + session_id + "; Path=/; HttpOnly");
                    res.body() = R"({"success": true, "message": "登录成功"})";
                }
                else {
                    res.body() = R"({"success": false, "message": "密码错误"})";
                }
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
template http::message_generator handle_login<boost::beast::http::string_body, std::allocator<char>>(
    beast::string_view,
    http::request<boost::beast::http::string_body, boost::beast::http::basic_fields<std::allocator<char>>>&&);