//
// Copyright (c) 2022 Klemens D. Morgenstern (klemens dot morgenstern at gmx dot net)
// Copyright (c) 2024 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

//------------------------------------------------------------------------------
//
// Example: Advanced server, flex (plain + SSL)
//
//------------------------------------------------------------------------------
#include "example/common/server_certificate.hpp"
#include "upload_videos.hpp"
#include "MySQLPool.hpp"
#include "config.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/filesystem.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/mysql.hpp>
#include <memory>
#include <nlohmann/json.hpp>

#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <list>
#include <string>
#include <unordered_map>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <ctime>

#if defined(BOOST_ASIO_HAS_CO_AWAIT)
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace mysql = boost::mysql;
namespace asio = boost::asio;

using tcp = boost::asio::ip::tcp;
using executor_type = net::strand<net::io_context::executor_type>;
using stream_type = typename beast::tcp_stream::rebind_executor<executor_type>::other;
using acceptor_type = typename net::ip::tcp::acceptor::rebind_executor<executor_type>::other;
using json = nlohmann::json;
// Return a reasonable mime type based on the extension of a file.
//现在的数据库连接类，支持连接池和参数化查询

SimpleDatabase g_db;
// 全局数据库对象

beast::string_view
//根据文件扩展名，返回对应的 HTTP Content-Type 响应头。
mime_type(beast::string_view path)
{
	using beast::iequals;
	auto const ext = [&path]
		{
			auto const pos = path.rfind(".");
			if (pos == beast::string_view::npos)
				return beast::string_view{};
			return path.substr(pos);
		}();
	if (iequals(ext, ".htm"))  return "text/html";
	if (iequals(ext, ".html")) return "text/html";
	if (iequals(ext, ".php"))  return "text/html";
	if (iequals(ext, ".css"))  return "text/css";
	if (iequals(ext, ".txt"))  return "text/plain";
	if (iequals(ext, ".js"))   return "application/javascript";
	if (iequals(ext, ".json")) return "application/json";
	if (iequals(ext, ".xml"))  return "application/xml";
	if (iequals(ext, ".swf"))  return "application/x-shockwave-flash";
	if (iequals(ext, ".flv"))  return "video/x-flv";
	if (iequals(ext, ".png"))  return "image/png";
	if (iequals(ext, ".jpe"))  return "image/jpeg";
	if (iequals(ext, ".jpeg")) return "image/jpeg";
	if (iequals(ext, ".jpg"))  return "image/jpeg";
	if (iequals(ext, ".gif"))  return "image/gif";
	if (iequals(ext, ".bmp"))  return "image/bmp";
	if (iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
	if (iequals(ext, ".tiff")) return "image/tiff";
	if (iequals(ext, ".tif"))  return "image/tiff";
	if (iequals(ext, ".svg"))  return "image/svg+xml";
	if (iequals(ext, ".svgz")) return "image/svg+xml";
	// 在函数中添加这些判断
	if (iequals(ext, ".mp4"))  return "video/mp4";
	if (iequals(ext, ".webm")) return "video/webm";
	if (iequals(ext, ".ogg"))  return "video/ogg";
	if (iequals(ext, ".mkv"))  return "video/x-matroska";
	if (iequals(ext, ".avi"))  return "video/x-msvideo";
	if (iequals(ext, ".mov"))  return "video/quicktime";
	return "application/text";
}

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
std::string
path_cat(
	beast::string_view base,
	beast::string_view path)
{
	if (base.empty())
		return std::string(path);
	std::string result(base);
#ifdef BOOST_MSVC
	char constexpr path_separator = '\\';
	if(result.back() == path_separator)
		result.resize(result.size() - 1);
	if (result.back() != path_separator)
		result.push_back(path_separator);
	result.append(path.data(), path.size());
	for (auto& c : result)
		if (c == '/')
			c = path_separator;
#else
	char constexpr path_separator = '/';
	if(result.back() == path_separator)
		result.resize(result.size() - 1);
	if (result.back() != path_separator)
		result.push_back(path_separator);
	result.append(path.data(), path.size());
#endif
	return result;
}


// 验证 session（后续可以改用 JWT）
bool verify_session(http::request<http::string_body>& req) {
	// 从 Cookie 头中提取 session_id
	auto cookie = req[http::field::cookie];
	std::string cookie_str(cookie.data(), cookie.size());

	// 简单查找 session_id=
	auto pos = cookie_str.find("session_id=");
	if (pos != std::string::npos) {
		// 验证 session 是否有效（后续接入 Redis 或内存存储）
		return true;  // 暂时总是返回有效
	}
	return false;
}

//template<class Body, class Allocator>
//http::message_generator
//handle_get_videos(
//	beast::string_view doc_root,
//	http::request<Body, http::basic_fields<Allocator>>&& req)
//{
//	http::response<http::string_body> res{ http::status::ok, req.version() };
//	res.set(http::field::content_type, "application/json");
//
//	try {
//		auto result = g_db.query_prepared("SELECT id, filename, filepath, size, created_at FROM videos ORDER BY created_at DESC");
//		json videos_array = json::array();
//		for (size_t i = 0; i < result.rows().size(); ++i) {
//			json video_obj;
//			video_obj["id"] = result.rows()[i][0].as_int64();
//			video_obj["filename"] = result.rows()[i][1].as_string();
//			video_obj["url"] += "/videos/";
//			video_obj["url"] += result.rows()[i][1].as_string();
//			video_obj["size"] = result.rows()[i][3].as_int64();
//			video_obj["created_at"] = result.rows()[i][4].as_string();
//			videos_array.push_back(video_obj);
//		}
//
//		res.body() = videos_array.dump();
//	}
//	catch (const std::exception& e) {
//		res.body() = R"({"error": "查询失败"})";
//	}
//
//	res.prepare_payload();
//	return res;
//}
template<class Body, class Allocator>
http::message_generator
handle_login(
	beast::string_view doc_root,
	http::request<Body, http::basic_fields<Allocator>>&& req)
{
	http::response<http::string_body> res{ http::status::ok, req.version() };
	res.set(http::field::content_type, "application/json; charset=utf-8");

	try {
		// 解析 JSON 请求体
		auto json_body = json::parse(req.body());
		std::string username = json_body.value("username", "");
		std::string password = json_body.value("password", "");
		printf("Received login request: username=%s, password=%s\n", username.c_str(), password.c_str());
		if (g_db.is_connected())
		{
			printf("数据库连接成功，查询用户中...\n");
			auto result = g_db.query_prepared(
				"SELECT id, password FROM users WHERE username = ?",
				username
			);
			if (result.rows().empty())
				res.body() = R"({"success": false, "message": "用户名不存在"})";
			else {
				//printf("查询数据库成功，验证密码中...\n");
				int user_id = result.rows()[0][0].as_int64();
				std::string stored_password = result.rows()[0][1].as_string();
				//printf("数据库中存储的密码: %s\n", stored_password.c_str());
				//printf("用户输入的密码: %s\n", password.c_str());
				if (password == stored_password) {
					for (int i = 0; i < 100; i++)
						printf("用数据库了!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
					boost::uuids::uuid uuid = boost::uuids::random_generator()();
					std::string session_id = "session_" + std::to_string(time(nullptr)) + "_" + boost::uuids::to_string(uuid);
					res.set(http::field::set_cookie, "session_id=" + session_id + "; Path=/; HttpOnly");
					std::cout <<"session_id, user_id" <<session_id << user_id<<std::endl;
					g_db.query_prepared(
						"INSERT INTO sessions (id, user_id, created_at, expires_at) VALUES (?, ?, NOW(), DATE_ADD(NOW(), INTERVAL 1 DAY))",
						session_id, user_id
					);
					res.body() = R"({"success": true, "message": "登录成功"})";
				}
				else {
					printf("密码错误，登录失败\n");
					res.body() = R"({"success": false, "message": "密码错误"})";
				}
			}
		}
		else {
			res.body() = R"({"success": false, "message": "数据库连接失败"})";
		}
	}
	catch (const mysql::error_with_diagnostics& e) {
		std::cerr << "MySQL错误: " << e.what() << std::endl;
		std::cerr << "错误码: " << e.code() << std::endl;
		res.body() = R"({"success": false, "message": "数据库查询失败"})";
	}
	catch (const json::parse_error& e) {
		std::cerr << "JSON解析错误: " << e.what() << std::endl;
		res.body() = R"({"success": false, "message": "请求格式错误"})";
	}
	catch (const std::exception& e) {
		std::cerr << "未知错误: " << e.what() << std::endl;
		res.body() = R"({"success": false, "message": "服务器内部错误"})";
	}
	res.prepare_payload();
	return res;
}
template<class Body, class Allocator>
http::message_generator
handle_register(
	beast::string_view doc_root,
	http::request<Body, http::basic_fields<Allocator>>&& req)
{
	http::response<http::string_body> res{ http::status::ok, req.version() };
	res.set(http::field::content_type, "application/json; charset=utf-8");

	try {
		auto json_body = json::parse(req.body());
		std::string username = json_body.value("username", "");
		std::string password = json_body.value("password", "");

		if (g_db.is_connected())
		{
			auto result = g_db.query_prepared(
				"SELECT id, password FROM users WHERE username = ?",
				username
			);
			if (result.rows().empty())
			{
				g_db.query_prepared(
					"INSERT INTO users (username, password) VALUES (?, ?)",
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
		else
			res.body() = R"({"success": false, "message": "数据库连接失败"})";
	}
	catch (const std::exception& e) {
		res.body() = R"({"success": false, "message": "请求格式错误"})";
	}

	res.prepare_payload();
	return res;
}

template<class Body, class Allocator>
http::message_generator
handle_get_videos(
	beast::string_view doc_root,
	http::request<Body, http::basic_fields<Allocator>>&& req)
{
	http::response<http::string_body> res{ http::status::ok, req.version() };
	res.set(http::field::content_type, "application/json");
	res.set(http::field::access_control_allow_origin, "*");

	try {
		// 获取分页参数
		std::string target(req.target());
		int page = 1;
		int page_size = 12;  // 改成12匹配前端

		std::string page_str = get_query_param(target, "page");
		std::string size_str = get_query_param(target, "size");

		if (!page_str.empty()) page = std::stoi(page_str);
		if (!size_str.empty()) page_size = std::stoi(size_str);

		int offset = (page - 1) * page_size;

		// 查询总数
		auto count_result = g_db.query("SELECT COUNT(*) FROM videos");
		int total = count_result.rows()[0][0].as_int64();

		// 查询视频列表（带分页）
		std::stringstream sql;
		sql << "SELECT v.id, v.filename, v.filepath, v.size, "
			<< "COALESCE(DATE_FORMAT(v.created_at, '%Y-%m-%d %H:%i:%s'), '') as created_at_str, "
			<< "COALESCE(u.username, 'unknown') as username "
			<< "FROM videos v "
			<< "LEFT JOIN users u ON v.user_id = u.id "
			<< "ORDER BY v.created_at DESC "
			<< "LIMIT " << page_size << " OFFSET " << offset;

		auto result = g_db.query(sql.str());

		// 构建 JSON
		json response;
		response["code"] = 200;
		response["message"] = "success";

		json data_array = json::array();
		for (size_t i = 0; i < result.rows().size(); ++i) {
			json video; 
			video["id"] = result.rows()[i][0].as_int64();
			video["title"] = result.rows()[i][1].as_string();
			video["videoUrl"] = "/videos/" + std::string(result.rows()[i][1].as_string());
			video["duration"] = "00:00";
			video["playCount"] = 0;
			video["author"] = result.rows()[i][5].as_string();  // username
			video["publishTime"] = result.rows()[i][4].as_string();
			video["thumbnail"] = "";
			video["size"] = result.rows()[i][3].as_int64();

			data_array.push_back(video);
		}

		response["data"] = data_array;
		response["pagination"] = {
			{"page", page},
			{"page_size", page_size},
			{"total", total},
			{"total_pages", (total + page_size - 1) / page_size}
		};

		res.body() = response.dump();
	}
	catch (const std::exception& e) {
		std::cerr << "查询视频失败: " << e.what() << std::endl;
		res.body() = R"({"code": 500, "message": "查询失败", "data": []})";
	}

	res.prepare_payload();
	return res;
}

template<class Body, class Allocator>
http::message_generator
handle_my_videos(
	beast::string_view doc_root,
	http::request<Body, http::basic_fields<Allocator>>&& req)
{
	http::response<http::string_body> res{ http::status::ok, req.version() };
	res.set(http::field::content_type, "application/json");
	res.set(http::field::access_control_allow_origin, "*");

	try {
		int user_id;
		if (!get_user_id_from_session(req, user_id)) {
			res.body() = R"({"code": 401, "message": "请先登录", "data": []})";
			res.prepare_payload();
			return res;
		}

		std::string target(req.target());
		int page = 1, page_size = 12;
		std::string keyword;

		std::string page_str = get_query_param(target, "page");
		std::string size_str = get_query_param(target, "size");
		keyword = get_query_param(target, "keyword");

		if (!page_str.empty()) page = std::stoi(page_str);
		if (!size_str.empty()) page_size = std::stoi(size_str);

		int offset = (page - 1) * page_size;

		// 查询用户的视频列表，将 created_at 转为字符串
		std::stringstream sql;
		sql << "SELECT id, filename, filepath, size, "
			<< "COALESCE(DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s'), '') as created_at_str "
			<< "FROM videos WHERE user_id = " << user_id;

		if (!keyword.empty()) {
			sql << " AND filename LIKE '%" << keyword << "%'";
		}
		sql << " ORDER BY created_at DESC LIMIT " << page_size << " OFFSET " << offset;

		auto result = g_db.query(sql.str());

		// 查询总数
		std::stringstream count_sql;
		count_sql << "SELECT COUNT(*) FROM videos WHERE user_id = " << user_id;
		if (!keyword.empty()) {
			count_sql << " AND filename LIKE '%" << keyword << "%'";
		}
		auto count_result = g_db.query(count_sql.str());
		int total = count_result.rows()[0][0].as_int64();

		json response;
		response["code"] = 200;
		response["message"] = "success";

		json data_array = json::array();
		for (size_t i = 0; i < result.rows().size(); ++i) {
			json video;
			video["id"] = result.rows()[i][0].as_int64();           // id
			video["title"] = result.rows()[i][1].as_string();       // filename
			video["videoUrl"] = "/videos/" + std::string(result.rows()[i][1].as_string());
			video["duration"] = "00:00";
			video["playCount"] = 0;
			video["author"] = "user_" + std::to_string(user_id);
			video["publishTime"] = result.rows()[i][4].as_string();  // created_at_str (索引4)
			video["thumbnail"] = "";
			video["size"] = result.rows()[i][3].as_int64();          // size (索引3)

			data_array.push_back(video);
		}

		response["data"] = data_array;
		response["pagination"] = {
			{"page", page},
			{"page_size", page_size},
			{"total", total},
			{"total_pages", (total + page_size - 1) / page_size}
		};

		res.body() = response.dump();
	}
	catch (const std::exception& e) {
		std::cerr << "查询我的视频失败: " << e.what() << std::endl;
		res.body() = R"({"code": 500, "message": "查询失败", "data": []})";
	}

	res.prepare_payload();
	return res;
}

template<class Body, class Allocator>
http::message_generator
handle_delete_video(
	beast::string_view doc_root,
	http::request<Body, http::basic_fields<Allocator>>&& req)
{
	http::response<http::string_body> res{ http::status::ok, req.version() };
	res.set(http::field::content_type, "application/json");
	res.set(http::field::access_control_allow_origin, "*");

	try {
		// 1. 获取当前登录用户
		int user_id;
		if (!get_user_id_from_session(req, user_id)) {
			res.body() = R"({"code": 401, "message": "请先登录"})";
			res.prepare_payload();
			return res;
		}

		// 2. 获取视频 ID（从 URL 路径中提取）
		std::string target(req.target());
		// URL 格式: /api/videos/123
		auto pos = target.rfind('/');
		if (pos == std::string::npos || pos == target.length() - 1) {
			throw std::runtime_error("Invalid video id");
		}

		int video_id = std::stoi(target.substr(pos + 1));

		// 3. 查询视频信息（同时验证所有权）
		auto result = g_db.query_prepared(
			"SELECT filepath FROM videos WHERE id = ? AND user_id = ?",
			video_id, user_id
		);

		if (result.rows().empty()) {
			throw std::runtime_error("Video not found or permission denied");
		}

		std::string filepath = result.rows()[0][0].as_string();

		// 4. 删除数据库记录
		g_db.query_prepared("DELETE FROM videos WHERE id = ? AND user_id = ?", video_id, user_id);

		// 5. 删除物理文件
		boost::system::error_code ec;
		if (boost::filesystem::exists(filepath)) {
			boost::filesystem::remove(filepath, ec);
			if (ec) {
				std::cerr << "删除文件失败: " << filepath << ", error: " << ec.message() << std::endl;
			}
		}

		res.body() = R"({"code": 200, "message": "删除成功"})";
	}
	catch (const std::exception& e) {
		std::cerr << "删除视频失败: " << e.what() << std::endl;
		res.body() = json{ {"code", 500}, {"message", e.what()} }.dump();
	}

	res.prepare_payload();
	return res;
}

template<class Body, class Allocator>
http::message_generator
handle_request(
	beast::string_view doc_root,
	http::request<Body, http::basic_fields<Allocator>>&& req)
{
	// Returns a bad request response
	auto const bad_request =
		[&req](beast::string_view why)
		{
			http::response<http::string_body> res{ http::status::bad_request, req.version() };
			res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
			res.set(http::field::content_type, "text/html");
			res.keep_alive(req.keep_alive());
			res.body() = std::string(why);
			res.prepare_payload();
			return res;
		};

	// Returns a not found response
	auto const not_found =
		[&req](beast::string_view target)
		{
			http::response<http::string_body> res{ http::status::not_found, req.version() };
			res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
			res.set(http::field::content_type, "text/html");
			res.keep_alive(req.keep_alive());
			res.body() = "The resource '" + std::string(target) + "' was not found.";
			res.prepare_payload();
			return res;
		};

	// Returns a server error response
	auto const server_error =
		[&req](beast::string_view what)
		{
			http::response<http::string_body> res{ http::status::internal_server_error, req.version() };
			res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
			res.set(http::field::content_type, "text/html");
			res.keep_alive(req.keep_alive());
			res.body() = "An error occurred: '" + std::string(what) + "'";
			res.prepare_payload();
			return res;
		};

	std::cout << "alllmethod=" << req.method_string()
		<< " target=" << req.target()
		<< std::endl;

	// ========== 1. 处理 POST 请求 ==========
	if (req.method() == http::verb::post)
	{
		std::string target = std::string(req.target());

		if (target == "/login")
			return handle_login(doc_root, std::move(req));

		if (target == "/register")
			return handle_register(doc_root, std::move(req));

		if (target == "/upload/init")
			return handle_upload_init(doc_root, std::move(req));

		if (target.find("/upload/chunk") == 0)
			return handle_upload_chunk(doc_root, std::move(req));

		if (target == "/upload/complete")
			return handle_upload_complete(doc_root, std::move(req));
	}

	// ========== 2. 处理 GET API 请求（必须在静态文件之前）==========
	if (req.method() == http::verb::get)
	{
		std::string target = std::string(req.target());

		// 使用 find 匹配，因为带查询参数
		if (target.find("/api/videos") == 0)
		{
			return handle_get_videos(doc_root, std::move(req));
		}

		if (target.find("/api/my-videos") == 0)
		{
			return handle_my_videos(doc_root, std::move(req));
		}
	}

	// DELETE 请求处理
	if (req.method() == http::verb::delete_)
	{
		std::string target = std::string(req.target());
		if (target.find("/api/videos/") == 0)
		{
			return handle_delete_video(doc_root, std::move(req));
		}
	}

	// ========== 3. 处理静态文件（HTML、JS、CSS、视频等）==========
	// Make sure we can handle the method
	if (req.method() != http::verb::get &&
		req.method() != http::verb::head)
		return bad_request("Unknown HTTP-method");

	// Request path must be absolute and not contain "..".
	if (req.target().empty() ||
		req.target()[0] != '/' ||
		req.target().find("..") != beast::string_view::npos)
		return bad_request("Illegal request-target");

	std::string path = path_cat(doc_root, req.target());
	printf("path=%s\n", path.c_str());

	if (req.target().back() == '/') {
		path.append("index.html");
	}
	printf("path=%s\n", path.c_str());

	// Attempt to open the file
	beast::error_code ec;
	http::file_body::value_type body;
	body.open(path.c_str(), beast::file_mode::scan, ec);

	// Handle the case where the file doesn't exist
	if (ec == beast::errc::no_such_file_or_directory)
		return not_found(req.target());

	// Handle an unknown error
	if (ec)
		return server_error(ec.message());

	// Cache the size since we need it after the move
	auto const size = body.size();

	// Respond to HEAD request
	if (req.method() == http::verb::head)
	{
		http::response<http::empty_body> res{ http::status::ok, req.version() };
		res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(http::field::content_type, mime_type(path));
		res.content_length(size);
		res.keep_alive(req.keep_alive());
		return res;
	}

	// Respond to GET request
	if (req.method() == http::verb::get)
	{
		http::response<http::file_body> res{
			std::piecewise_construct,
			std::make_tuple(std::move(body)),
			std::make_tuple(http::status::ok, req.version()) };
		res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(http::field::content_type, mime_type(path));
		res.content_length(size);
		res.keep_alive(req.keep_alive());
		return res;
	}

	// 保底返回值（所有路径都必须有返回值）
	return server_error("Unexpected error");
}

/** A thread-safe task group that tracks child tasks, allows emitting
	cancellation signals to them, and waiting for their completion.
*/
class task_group
{
	std::mutex mtx_;
	net::steady_timer cv_;
	std::list<net::cancellation_signal> css_;

public:
	task_group(net::any_io_executor exec)
		: cv_{ std::move(exec), net::steady_timer::time_point::max() }
	{
	}

	task_group(task_group const&) = delete;
	task_group(task_group&&) = delete;

	/** Adds a cancellation slot and a wrapper object that will remove the child
		task from the list when it completes.

		@param completion_token The completion token that will be adapted.

		@par Thread Safety
		@e Distinct @e objects: Safe.@n
		@e Shared @e objects: Safe.
	*/
	template<typename CompletionToken>
	auto
		adapt(CompletionToken&& completion_token)
	{
		auto lg = std::lock_guard{ mtx_ };
		auto cs = css_.emplace(css_.end());

		class remover
		{
			task_group* tg_;
			decltype(css_)::iterator cs_;

		public:
			remover(
				task_group* tg,
				decltype(css_)::iterator cs)
				: tg_{ tg }
				, cs_{ cs }
			{
			}

			remover(remover&& other) noexcept
				: tg_{ std::exchange(other.tg_, nullptr) }
				, cs_{ other.cs_ }
			{
			}

			~remover()
			{
				if (tg_)
				{
					auto lg = std::lock_guard{ tg_->mtx_ };
					if (tg_->css_.erase(cs_) == tg_->css_.end())
						tg_->cv_.cancel();
				}
			}
		};

		return net::bind_cancellation_slot(
			cs->slot(),
			net::consign(
				std::forward<CompletionToken>(completion_token),
				remover{ this, cs }));
	}

	/** Emits the signal to all child tasks and invokes the slot's
		handler, if any.

		@param type The completion type that will be emitted to child tasks.

		@par Thread Safety
		@e Distinct @e objects: Safe.@n
		@e Shared @e objects: Safe.
	*/
	void
		emit(net::cancellation_type type)
	{
		auto lg = std::lock_guard{ mtx_ };
		for (auto& cs : css_)
			cs.emit(type);
	}

	/** Starts an asynchronous wait on the task_group.

		The completion handler will be called when:

		@li All the child tasks completed.
		@li The operation was cancelled.

		@param completion_token The completion token that will be used to
		produce a completion handler. The function signature of the completion
		handler must be:
		@code
		void handler(
			boost::system::error_code const& error  // result of operation
		);
		@endcode

		@par Thread Safety
		@e Distinct @e objects: Safe.@n
		@e Shared @e objects: Safe.
	*/
	template<
		typename CompletionToken =
		net::default_completion_token_t<net::any_io_executor>>
		auto
		async_wait(
			CompletionToken&& completion_token =
			net::default_completion_token_t<net::any_io_executor>{})
	{
		return net::
			async_compose<CompletionToken, void(boost::system::error_code)>(
				[this, scheduled = false](
					auto&& self, boost::system::error_code ec = {}) mutable
				{
					if (!scheduled)
						self.reset_cancellation_state(
							net::enable_total_cancellation());

					if (!self.cancelled() && ec == net::error::operation_aborted)
						ec = {};

					{
						auto lg = std::lock_guard{ mtx_ };

						if (!css_.empty() && !ec)
						{
							scheduled = true;
							return cv_.async_wait(std::move(self));
						}
					}

					if (!std::exchange(scheduled, true))
						return net::post(net::append(std::move(self), ec));

					self.complete(ec);
				},
				completion_token,
				cv_);
	}
};

template<typename Stream>
net::awaitable<void, executor_type>
run_websocket_session(
	Stream& stream,
	beast::flat_buffer& buffer,
	http::request<http::string_body> req)
{
	auto cs = co_await net::this_coro::cancellation_state;
	auto ws = websocket::stream<Stream&>{ stream };

	// Set suggested timeout settings for the websocket
	ws.set_option(
		websocket::stream_base::timeout::suggested(beast::role_type::server));

	// Set a decorator to change the Server of the handshake
	ws.set_option(websocket::stream_base::decorator(
		[](websocket::response_type& res)
		{
			res.set(
				http::field::server,
				std::string(BOOST_BEAST_VERSION_STRING) +
				" advanced-server-flex");
		}));

	// Accept the websocket handshake
	co_await ws.async_accept(req);

	while (!cs.cancelled())
	{
		// Read a message
		auto [ec, _] = co_await ws.async_read(buffer, net::as_tuple);

		if (ec == websocket::error::closed || ec == ssl::error::stream_truncated)
			co_return;

		if (ec)
			throw boost::system::system_error{ ec };

		// Echo the message back
		ws.text(ws.got_text());
		co_await ws.async_write(buffer.data());

		// Clear the buffer
		buffer.consume(buffer.size());
	}

	// A cancellation has been requested, gracefully close the session.
	auto [ec] = co_await ws.async_close(
		websocket::close_code::service_restart, net::as_tuple);

	if (ec && ec != ssl::error::stream_truncated)
		throw boost::system::system_error{ ec };
}

template<typename Stream>
net::awaitable<void, executor_type>
run_session(
	Stream& stream,
	beast::flat_buffer& buffer,
	beast::string_view doc_root)
{
	auto cs = co_await net::this_coro::cancellation_state;

	while (!cs.cancelled())
	{
		http::request_parser<http::string_body> parser;
		parser.body_limit(100 * 1024 * 1024);

		auto [ec, _] =
			co_await http::async_read(stream, buffer, parser, net::as_tuple);

		if (ec == http::error::end_of_stream)
			co_return;

		if (websocket::is_upgrade(parser.get()))
		{
			// The websocket::stream uses its own timeout settings.
			beast::get_lowest_layer(stream).expires_never();

			co_await run_websocket_session(
				stream, buffer, parser.release());

			co_return;
		}

		auto res = handle_request(doc_root, parser.release());
		if (!res.keep_alive())
		{
			co_await beast::async_write(stream, std::move(res));
			co_return;
		}

		co_await beast::async_write(stream, std::move(res));
	}
}

net::awaitable<void, executor_type>
detect_session(
	stream_type stream,
	ssl::context& ctx,
	beast::string_view doc_root)
{
	beast::flat_buffer buffer;

	// Allow total cancellation to change the cancellation state of this
	// coroutine, but only allow terminal cancellation to propagate to async
	// operations. This setting will be inherited by all child coroutines.
	co_await net::this_coro::reset_cancellation_state(
		net::enable_total_cancellation(), net::enable_terminal_cancellation());

	// We want to be able to continue performing new async operations, such as
	// cleanups, even after the coroutine is cancelled. This setting will be
	// inherited by all child coroutines.
	co_await net::this_coro::throw_if_cancelled(false);

	stream.expires_after(std::chrono::seconds(30));

	if (co_await beast::async_detect_ssl(stream, buffer))
	{
		ssl::stream<stream_type> ssl_stream{ std::move(stream), ctx };

		auto bytes_transferred = co_await ssl_stream.async_handshake(
			ssl::stream_base::server, buffer.data());

		buffer.consume(bytes_transferred);

		co_await run_session(ssl_stream, buffer, doc_root);

		if (!ssl_stream.lowest_layer().is_open())
			co_return;

		// Gracefully close the stream
		auto [ec] = co_await ssl_stream.async_shutdown(net::as_tuple);
		if (ec && ec != ssl::error::stream_truncated)
			throw boost::system::system_error{ ec };
	}
	else
	{
		co_await run_session(stream, buffer, doc_root);

		if (!stream.socket().is_open())
			co_return;

		stream.socket().shutdown(net::ip::tcp::socket::shutdown_send);
	}
}

net::awaitable<void, executor_type>
listen(
	task_group& task_group,
	ssl::context& ctx,
	net::ip::tcp::endpoint endpoint,
	beast::string_view doc_root)
{
	auto cs = co_await net::this_coro::cancellation_state;
	auto executor = co_await net::this_coro::executor;
	auto acceptor = acceptor_type{ executor, endpoint };

	// Allow total cancellation to propagate to async operations.
	co_await net::this_coro::reset_cancellation_state(
		net::enable_total_cancellation());

	while (!cs.cancelled())
	{
		auto socket_executor = net::make_strand(executor.get_inner_executor());
		auto [ec, socket] =
			co_await acceptor.async_accept(socket_executor, net::as_tuple);

		if (ec == net::error::operation_aborted)
			co_return;

		if (ec)
			throw boost::system::system_error{ ec };

		net::co_spawn(
			std::move(socket_executor),
			detect_session(stream_type{ std::move(socket) }, ctx, doc_root),
			task_group.adapt(
				[](std::exception_ptr e)
				{
					if (e)
					{
						try
						{
							std::rethrow_exception(e);
						}
						catch (std::exception& e)
						{
							std::cerr << "Error in session: " << e.what() << "\n";
						}
					}
				}));
	}
}

net::awaitable<void, executor_type>
handle_signals(task_group& task_group)
{
	auto executor = co_await net::this_coro::executor;
	auto signal_set = net::signal_set{ executor, SIGINT, SIGTERM };

	auto sig = co_await signal_set.async_wait();

	if (sig == SIGINT)
	{
		std::cout << "Gracefully cancelling child tasks...\n";
		task_group.emit(net::cancellation_type::total);

		// Wait a limited time for child tasks to gracefully cancell
		auto [ec] = co_await task_group.async_wait(
			net::as_tuple(net::cancel_after(std::chrono::seconds{ 10 })));

		if (ec == net::error::operation_aborted) // Timeout occurred
		{
			std::cout << "Sending a terminal cancellation signal...\n";
			task_group.emit(net::cancellation_type::terminal);
			co_await task_group.async_wait();
		}

		std::cout << "Child tasks completed.\n";
	}
	else // SIGTERM
	{
		net::query(
			executor.get_inner_executor(),
			net::execution::context).stop();
	}
}

int
main(int argc, char* argv[])
{
	Config::get().load("config.json");

	// 获取引用
	auto& cfg = Config::get();
	// Check command line arguments.
	auto const address = net::ip::make_address(cfg.server.host);
	auto const port = static_cast<unsigned short>(cfg.server.port);
	auto const endpoint = net::ip::tcp::endpoint{ address, port };
	auto const doc_root = cfg.server.doc_root;
	auto const threads = cfg.server.threads;
	// The io_context is required for all I/O
	net::io_context ioc{ threads };
	std::cout << "Starting server at " << cfg.server.host << ":" << cfg.server.port << " with doc_root=" << cfg.server.doc_root << "...\n";
	std::cout << "Connecting to database at " << cfg.database.host << ":" << cfg.database.port << " with user=" << cfg.database.user << "...\n";
	if (!g_db.connect(cfg.database.host, cfg.database.port, cfg.database.user, cfg.database.password, cfg.database.name, cfg.database.pool_size)) {
		printf("数据库连接失败，无法启动服务器\n");
		return EXIT_FAILURE; 
	}
	printf("数据库连接成功，启动服务器...\n");
	// The SSL context is required, and holds certificates
	ssl::context ctx{ ssl::context::tlsv12 };

	// This holds the self-signed certificate used by the server
	load_server_certificate(ctx);

	// Track coroutines
	task_group task_group{ ioc.get_executor() };

	// Create and launch a listening coroutine
	net::co_spawn(
		net::make_strand(ioc),
		listen(task_group, ctx, endpoint, doc_root),
		task_group.adapt(
			[](std::exception_ptr e)
			{
				if (e)
				{
					try
					{
						std::rethrow_exception(e);
					}
					catch (std::exception& e)
					{
						std::cerr << "Error in listener: " << e.what() << "\n";
					}
				}
			}));

	// Create and launch a signal handler coroutine
	net::co_spawn(
		net::make_strand(ioc), handle_signals(task_group), net::detached);

	// Run the I/O service on the requested number of threads
	std::vector<std::thread> v;
	v.reserve(threads - 1);
	for (auto i = threads - 1; i > 0; --i)
		v.emplace_back([&ioc] { ioc.run(); });
	ioc.run();

	// Block until all the threads exit
	for (auto& t : v)
		t.join();

	return EXIT_SUCCESS;
}

#else

int
main(int, char* [])
{
	std::printf("awaitables require C++20\n");
	return EXIT_FAILURE;
}

#endif
