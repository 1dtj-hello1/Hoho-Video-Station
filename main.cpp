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

class SimpleDatabase {
private:
	asio::io_context ioc_;
	mysql::any_connection conn_;  // 使用 any_connection
	bool connected_ = false;

public:
	SimpleDatabase() : conn_(ioc_) {}
	bool is_connected() const { return connected_; }
	bool connect(const std::string& host,
		unsigned int port,
		const std::string& user,
		const std::string& pass,
		const std::string& db) {
		try {
			// 新版 API：使用 connect_params 结构体
			mysql::connect_params params;
			params.server_address = mysql::host_and_port(host, port);
			params.username = user;
			params.password = pass;
			params.database = db;
			params.ssl = mysql::ssl_mode::require;

			conn_.connect(params);
			connected_ = true;
			printf("[数据库] 连接成功\n");
			return true;
		}
		catch (const std::exception& e) {
			printf("[数据库] 连接失败: %s\n", e.what());
			return false;
		}
	}

	mysql::results query(const std::string& sql) {
		mysql::results result;
		conn_.execute(sql, result);
		return result;
	}
	template<typename... Args>
	mysql::results query_params(const std::string& sql, Args&&... args) {
		mysql::results result;
		if (connected_) {
			conn_.execute(mysql::with_params(sql, std::forward<Args>(args)...), result);
		}
		return result;
	}

	// 支持多个参数的参数化查询（使用可变模板）
	template<typename... Args>
	mysql::results query_prepared(const std::string& sql, Args&&... args) {
		mysql::results result;
		if (connected_) {
			// 使用 stringstream 构建 SQL
			std::stringstream ss;
			size_t start = 0;
			size_t pos = 0;
			size_t arg_index = 0;

			// 将参数放入 tuple 以便遍历
			auto params = std::make_tuple(std::forward<Args>(args)...);

			// 遍历 SQL，替换 {} 为参数值
			while ((pos = sql.find("?", start)) != std::string::npos) {
				ss << sql.substr(start, pos - start);

				// 获取第 arg_index 个参数并转换为字符串
				if (arg_index < sizeof...(Args)) {
					std::string param_str = get_param_string(params, arg_index);
					ss << "'" << param_str << "'";
					arg_index++;
				}

				start = pos + 1;
			}
			ss << sql.substr(start);

			conn_.execute(ss.str(), result);
		}
		return result;
	}

private:
	template<size_t I = 0, typename... Tp>
	typename std::enable_if<I == sizeof...(Tp), std::string>::type
		get_param_string(const std::tuple<Tp...>&, int) {
		return "";
	}

	template<size_t I = 0, typename... Tp>
	typename std::enable_if < I < sizeof...(Tp), std::string>::type
		get_param_string(const std::tuple<Tp...>& t, int) {
		std::stringstream ss;
		ss << std::get<I>(t);
		return ss.str();
	}
};

// 全局数据库对象
SimpleDatabase g_db;
// 全局数据库对象

beast::string_view
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

template<class Body, class Allocator>
http::message_generator
handle_get_videos(
	beast::string_view doc_root,
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
			video_obj["url"] += "/videos/";
			video_obj["url"] += result.rows()[i][1].as_string();
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
// 处理登录请求
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
	printf( "path=%s\n",path.c_str());
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
	if (req.target() == "/api/videos" && req.method() == http::verb::get) {
		return handle_get_videos(doc_root, std::move(req));
	}
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
	// Check command line arguments.
	if (argc != 5)
	{
		std::cerr << "Usage: advanced-server-flex-awaitable <address> <port> <doc_root> <threads>\n"
			<< "Example:\n"
			<< "dtj    advanced-server-flex-awaitable 0.0.0.0 8080 . 1\n";
		return EXIT_FAILURE;
	}
	auto const address = net::ip::make_address(argv[1]);
	auto const port = static_cast<unsigned short>(std::atoi(argv[2]));
	auto const endpoint = net::ip::tcp::endpoint{ address, port };
	auto const doc_root = argv[3];
	printf("服务器根目录: %s\n", doc_root);
	auto const threads = std::max<int>(1, std::atoi(argv[4]));

	// The io_context is required for all I/O
	net::io_context ioc{ threads };
	if (!g_db.connect("127.0.0.1", 3306, "root", "123456", "niwudile")) {
		printf("数据库连接失败，无法启动服务器\n");
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
