#pragma once
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
// 处理登录请求
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/filesystem.hpp>
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
		// 硬编码验证（后续接入数据库）
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
				printf("查询数据库成功，验证密码中...\n");
				std::string stored_password = result.rows()[0][1].as_string();
				printf("数据库中存储的密码: %s\n", stored_password.c_str());
				printf("用户输入的密码: %s\n", password.c_str());
				if (password == stored_password) {
					for (int i = 0; i < 100; i++)
						printf("用数据库了!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
					std::string session_id = "session_" + std::to_string(time(nullptr));
					res.set(http::field::set_cookie, "session_id=" + session_id + "; Path=/; HttpOnly");
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
handle_upload(
	beast::string_view doc_root,
	http::request<Body, http::basic_fields<Allocator>>&& req)
{
	// 1. 解析文件名（从 URL 参数或自定义头获取）
	// 这里假设前端通过 /upload?filename=xxx.mp4 传递文件名
	std::string filename;
	auto target = req.target();
	auto pos = target.find("filename=");
	if (pos != std::string::npos) {
		filename = target.substr(pos + 9);
		// URL 解码（处理空格、中文等）
		// 简单处理：替换 %20 为空格
		auto space_pos = filename.find("%20");
		while (space_pos != std::string::npos) {
			filename.replace(space_pos, 3, " ");
			space_pos = filename.find("%20");
		}
	}

	// 如果没有提供文件名，生成一个唯一文件名
	if (filename.empty()) {
		filename = std::to_string(time(nullptr)) + ".mp4";
	}

	// 2. 构建保存路径
	std::string upload_dir = path_cat(std::string(doc_root), "videos");
	std::cout << upload_dir << std::endl;
	std::string filepath = path_cat(upload_dir, filename);

	// 3. 确保 uploads 目录存在
	boost::system::error_code ec;
	if (!boost::filesystem::exists(upload_dir)) {
		boost::filesystem::create_directories(upload_dir, ec);
		if (ec) {
			http::response<http::string_body> res{ http::status::internal_server_error, req.version() };
			res.set(http::field::content_type, "application/json");
			res.body() = R"({"success": false, "message": "无法创建上传目录"})";
			res.prepare_payload();
			return res;
		}
	}

	// 4. 保存文件
	FILE* fp = fopen(filepath.c_str(), "wb");
	if (!fp) {
		http::response<http::string_body> res{ http::status::internal_server_error, req.version() };
		res.set(http::field::content_type, "application/json");
		res.body() = R"({"success": false, "message": "无法打开文件"})";
		res.prepare_payload();
		return res;
	}

	fwrite(req.body().data(), 1, req.body().size(), fp);
	fclose(fp);


	// 5. 获取文件大小
	auto file_size = boost::filesystem::file_size(filepath, ec);

	// 6. 将视频信息存入数据库（可选）
	try {
		// 获取当前登录用户的 session_id（需要从 cookie 中解析）
		// 这里简化处理，暂时用 1 作为用户 ID
		int user_id = 1;

		g_db.query_prepared(
			"INSERT INTO videos (user_id, filename, filepath, size, created_at) VALUES (?, ?, ?, ?, NOW())",
			user_id, filename, filepath, file_size
		);
	}
	catch (const std::exception& e) {
		std::cerr << "数据库插入失败: " << e.what() << std::endl;
		// 文件已保存，但数据库记录失败，仍然返回成功但带警告
	}

	// 7. 返回成功响应
	http::response<http::string_body> res{ http::status::ok, req.version() };
	res.set(http::field::content_type, "application/json");

	// 生成视频访问 URL
	std::string video_url = "/videos/" + filename;

	std::string response_body = R"({
        "success": true,
        "message": "上传成功",
        "filename": ")" + filename + R"(",
        "url": ")" + video_url + R"(",
        "size": )" + std::to_string(file_size) + R"(
    })";

	res.body() = response_body;
	res.prepare_payload();
	return res;
}
// Return a response for the given request.
//
// The concrete type of the response message (which depends on the
// request), is type-erased in message_generator.
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
	if (req.method() == http::verb::post)
	{
		std::string target = std::string(req.target());

		if (target == "/login")
			return handle_login(doc_root, std::move(req));

		if (target == "/register")
			return handle_register(doc_root, std::move(req));

		// 分片上传初始化
		if (target == "/upload/init")
			return handle_upload_init(doc_root, std::move(req));

		// 分片上传 - 关键：这里要能匹配带参数的 URL
		if (target.find("/upload/chunk") == 0)  // ← 这个条件
			return handle_upload_chunk(doc_root, std::move(req));

		if (target == "/upload/complete")
			return handle_upload_complete(doc_root, std::move(req));
	}

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
		//		path = path_cat(path, "web");
		//		path = path_cat(path, "index.html");
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

	// Build the path to the requested file
	//if (req.target() == "/api/videos" && req.method() == http::verb::get) {
	//	return handle_get_videos(doc_root, std::move(req));
	//}
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
