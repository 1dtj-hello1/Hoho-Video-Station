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

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <list>
#include <string>
#include <thread>
#include <vector>
#include <memory>

#include "database/Database.hpp"
#include "handlers/LoginHandler.hpp"
#include "handlers/RegisterHandler.hpp"
#include "handlers/UploadHandler.hpp"
#include "handlers/VideoHandler.hpp"
#include "core/TaskGroup.hpp"
#include "utils/MimeType.hpp"
#include "utils/PathCat.hpp"

#if defined(BOOST_ASIO_HAS_CO_AWAIT)

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;

using tcp = boost::asio::ip::tcp;
using executor_type = net::strand<net::io_context::executor_type>;
using stream_type = typename beast::tcp_stream::rebind_executor<executor_type>::other;
using acceptor_type = typename net::ip::tcp::acceptor::rebind_executor<executor_type>::other;

// 验证 session
bool verify_session(http::request<http::string_body>& req) {
    auto cookie = req[http::field::cookie];
    std::string cookie_str(cookie.data(), cookie.size());
    auto pos = cookie_str.find("session_id=");
    if (pos != std::string::npos) {
        return true;
    }
    return false;
}

// 请求处理函数
template<class Body, class Allocator>
http::message_generator
handle_request(
    beast::string_view doc_root,
    http::request<Body, http::basic_fields<Allocator>>&& req)
{
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

    std::cout << "method=" << req.method_string()
        << " target=" << req.target()
        << std::endl;

    // POST 请求路由
    if (req.method() == http::verb::post) {
        std::string target = std::string(req.target());
        if (target == "/login")
            return handle_login(doc_root, std::move(req));
        if (target == "/register")
            return handle_register(doc_root, std::move(req));
        if (target == "/upload")
            return handle_upload(doc_root, std::move(req));
        return bad_request("Invalid upload endpoint");
    }

    // GET 请求 - API 路由
    if (req.method() == http::verb::get && req.target() == "/api/videos") {
        return handle_get_videos(doc_root, std::move(req));
    }

    // 确保只能处理 GET/HEAD
    if (req.method() != http::verb::get && req.method() != http::verb::head)
        return bad_request("Unknown HTTP-method");

    // 安全检查
    if (req.target().empty() || req.target()[0] != '/' ||
        req.target().find("..") != beast::string_view::npos)
        return bad_request("Illegal request-target");

    std::string path = path_cat(doc_root, req.target());
    if (req.target().back() == '/') {
        path.append("index.html");
    }

    // 打开文件
    beast::error_code ec;
    http::file_body::value_type body;
    body.open(path.c_str(), beast::file_mode::scan, ec);

    if (ec == beast::errc::no_such_file_or_directory)
        return not_found(req.target());
    if (ec)
        return server_error(ec.message());

    auto const size = body.size();

    // HEAD 请求
    if (req.method() == http::verb::head) {
        http::response<http::empty_body> res{ http::status::ok, req.version() };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, mime_type(path));
        res.content_length(size);
        res.keep_alive(req.keep_alive());
        return res;
    }

    // GET 请求
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

// WebSocket 会话
template<typename Stream>
net::awaitable<void, executor_type>
run_websocket_session(
    Stream& stream,
    beast::flat_buffer& buffer,
    http::request<http::string_body> req)
{
    auto cs = co_await net::this_coro::cancellation_state;
    auto ws = websocket::stream<Stream&>{ stream };

    ws.set_option(
        websocket::stream_base::timeout::suggested(beast::role_type::server));

    ws.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res)
        {
            res.set(
                http::field::server,
                std::string(BOOST_BEAST_VERSION_STRING) +
                " advanced-server-flex");
        }));

    co_await ws.async_accept(req);

    while (!cs.cancelled()) {
        auto [ec, _] = co_await ws.async_read(buffer, net::as_tuple);

        if (ec == websocket::error::closed || ec == ssl::error::stream_truncated)
            co_return;
        if (ec)
            throw boost::system::system_error{ ec };

        ws.text(ws.got_text());
        co_await ws.async_write(buffer.data());
        buffer.consume(buffer.size());
    }

    auto [ec] = co_await ws.async_close(
        websocket::close_code::service_restart, net::as_tuple);
    if (ec && ec != ssl::error::stream_truncated)
        throw boost::system::system_error{ ec };
}

// HTTP/WebSocket 会话
template<typename Stream>
net::awaitable<void, executor_type>
run_session(
    Stream& stream,
    beast::flat_buffer& buffer,
    beast::string_view doc_root)
{
    auto cs = co_await net::this_coro::cancellation_state;

    while (!cs.cancelled()) {
        http::request_parser<http::string_body> parser;
        parser.body_limit(10000);

        auto [ec, _] =
            co_await http::async_read(stream, buffer, parser, net::as_tuple);

        if (ec == http::error::end_of_stream)
            co_return;

        if (websocket::is_upgrade(parser.get())) {
            beast::get_lowest_layer(stream).expires_never();
            co_await run_websocket_session(stream, buffer, parser.release());
            co_return;
        }

        auto res = handle_request(doc_root, parser.release());
        if (!res.keep_alive()) {
            co_await beast::async_write(stream, std::move(res));
            co_return;
        }

        co_await beast::async_write(stream, std::move(res));
    }
}

// 检测 SSL 会话
net::awaitable<void, executor_type>
detect_session(
    stream_type stream,
    ssl::context& ctx,
    beast::string_view doc_root)
{
    beast::flat_buffer buffer;

    co_await net::this_coro::reset_cancellation_state(
        net::enable_total_cancellation(), net::enable_terminal_cancellation());
    co_await net::this_coro::throw_if_cancelled(false);

    stream.expires_after(std::chrono::seconds(30));

    if (co_await beast::async_detect_ssl(stream, buffer)) {
        ssl::stream<stream_type> ssl_stream{ std::move(stream), ctx };
        auto bytes_transferred = co_await ssl_stream.async_handshake(
            ssl::stream_base::server, buffer.data());
        buffer.consume(bytes_transferred);
        co_await run_session(ssl_stream, buffer, doc_root);

        if (!ssl_stream.lowest_layer().is_open())
            co_return;

        auto [ec] = co_await ssl_stream.async_shutdown(net::as_tuple);
        if (ec && ec != ssl::error::stream_truncated)
            throw boost::system::system_error{ ec };
    }
    else {
        co_await run_session(stream, buffer, doc_root);
        if (!stream.socket().is_open())
            co_return;
        stream.socket().shutdown(net::ip::tcp::socket::shutdown_send);
    }
}

// 监听协程
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

    co_await net::this_coro::reset_cancellation_state(
        net::enable_total_cancellation());

    while (!cs.cancelled()) {
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
                    if (e) {
                        try {
                            std::rethrow_exception(e);
                        }
                        catch (std::exception& e) {
                            std::cerr << "Error in session: " << e.what() << "\n";
                        }
                    }
                }));
    }
}

// 信号处理
net::awaitable<void, executor_type>
handle_signals(task_group& task_group)
{
    auto executor = co_await net::this_coro::executor;
    auto signal_set = net::signal_set{ executor, SIGINT, SIGTERM };
    auto sig = co_await signal_set.async_wait();

    if (sig == SIGINT) {
        std::cout << "Gracefully cancelling child tasks...\n";
        task_group.emit(net::cancellation_type::total);

        auto [ec] = co_await task_group.async_wait(
            net::as_tuple(net::cancel_after(std::chrono::seconds{ 10 })));

        if (ec == net::error::operation_aborted) {
            std::cout << "Sending a terminal cancellation signal...\n";
            task_group.emit(net::cancellation_type::terminal);
            co_await task_group.async_wait();
        }

        std::cout << "Child tasks completed.\n";
    }
    else {
        net::query(
            executor.get_inner_executor(),
            net::execution::context).stop();
    }
}

// 主函数
int main(int argc, char* argv[])
{
    if (argc != 5) {
        std::cerr << "Usage: advanced-server-flex-awaitable <address> <port> <doc_root> <threads>\n"
            << "Example:\n"
            << "    advanced-server-flex-awaitable 0.0.0.0 8080 . 1\n";
        return EXIT_FAILURE;
    }

    auto const address = net::ip::make_address(argv[1]);
    auto const port = static_cast<unsigned short>(std::atoi(argv[2]));
    auto const endpoint = net::ip::tcp::endpoint{ address, port };

    std::string doc_root_str;
    if (std::string(argv[3]) == ".") {
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        std::string exe_path(buffer);
        doc_root_str = exe_path.substr(0, exe_path.find_last_of("\\/"));
    }
    else {
        doc_root_str = argv[3];
    }
    auto const doc_root = beast::string_view{ doc_root_str };
    auto const threads = std::max<int>(1, std::atoi(argv[4]));

    net::io_context ioc{ threads };

    if (!g_db.connect("127.0.0.1", 3306, "root", "123456", "niwudile")) {
        std::cerr << "警告: 数据库连接失败，将使用硬编码登录" << std::endl;
    }

    ssl::context ctx{ ssl::context::tlsv12 };
    load_server_certificate(ctx);

    task_group task_group{ ioc.get_executor() };

    net::co_spawn(
        net::make_strand(ioc),
        listen(task_group, ctx, endpoint, doc_root),
        task_group.adapt(
            [](std::exception_ptr e) {
                if (e) {
                    try {
                        std::rethrow_exception(e);
                    }
                    catch (std::exception& e) {
                        std::cerr << "Error in listener: " << e.what() << "\n";
                    }
                }
            }));

    net::co_spawn(
        net::make_strand(ioc), handle_signals(task_group), net::detached);

    std::vector<std::thread> v;
    v.reserve(threads - 1);
    for (auto i = threads - 1; i > 0; --i)
        v.emplace_back([&ioc] { ioc.run(); });
    ioc.run();

    for (auto& t : v)
        t.join();

    return EXIT_SUCCESS;
}

#else

int main(int, char* [])
{
    std::printf("awaitables require C++20\n");
    return EXIT_FAILURE;
}

#endif