// MySQLPool.hpp
#pragma once

#include <boost/mysql.hpp>
#include <boost/asio.hpp>
#include <memory>
#include <iostream>

namespace mysql = boost::mysql;
namespace asio = boost::asio;

class MySQLPool {
public:
    // 构造函数：接收外部的 io_context
    MySQLPool(asio::io_context& ioc,
        const std::string& host,
        unsigned short port,
        const std::string& user,
        const std::string& pass,
        const std::string& db)
        : ioc_(ioc)
        , pool_(create_pool(ioc, host, port, user, pass, db))
    {
        pool_.async_run(asio::detached);
        std::cout << "[MySQLPool] 连接池已启动" << std::endl;
    }

    ~MySQLPool() = default;

    // 禁止拷贝
    MySQLPool(const MySQLPool&) = delete;
    MySQLPool& operator=(const MySQLPool&) = delete;

    // 获取连接（协程版本）
    asio::awaitable<mysql::pooled_connection> get_connection() {
        co_return co_await pool_.async_get_connection(
            asio::cancel_after(std::chrono::seconds(5))
        );
    }

    // 执行查询（快捷方法）
    asio::awaitable<mysql::results> query(const std::string& sql) {
        auto conn = co_await get_connection();
        mysql::results result;
        co_await conn->async_execute(sql, result);
        co_return result;
    }

    // 参数化查询（防 SQL 注入）
    template<typename... Args>
    asio::awaitable<mysql::results> query_params(const std::string& sql, Args&&... args) {
        auto conn = co_await get_connection();
        mysql::results result;
        co_await conn->async_execute(
            mysql::with_params(sql, std::forward<Args>(args)...),
            result
        );
        co_return result;
    }

private:
    asio::io_context& ioc_;           // 引用外部 io_context，不自己创建
    mysql::connection_pool pool_;

    static mysql::connection_pool create_pool(asio::io_context& ioc,
        const std::string& host,
        unsigned short port,
        const std::string& user,
        const std::string& pass,
        const std::string& db) {
        mysql::pool_params params;
        params.server_address.emplace_host_and_port(host, port);
        params.username = user;
        params.password = pass;
        params.database = db;
        params.initial_size = 2;
        params.max_size = 10;
        return mysql::connection_pool(ioc, std::move(params));
    }
};