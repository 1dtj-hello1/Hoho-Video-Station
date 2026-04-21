// MySQLPool.hpp
#pragma once

#include <boost/mysql.hpp>
#include <boost/asio.hpp>
#include <type_traits>
#include <queue>
#include <mutex>
#include <iostream>
#include <sstream>

namespace mysql = boost::mysql;
namespace asio = boost::asio;

class SimpleDatabase {
private:
    asio::io_context ioc_;
    std::queue<mysql::any_connection> conn_pool;
    bool connected_ = false;
    mutable std::mutex pool_mutex_;

    // 格式化单个参数（根据类型）
    template<typename T>
    std::string format_single(const T& value) {
        if constexpr (std::is_same_v<std::decay_t<T>, std::string> ||
            std::is_same_v<std::decay_t<T>, const char*> ||
            std::is_same_v<std::decay_t<T>, char*>) {
            // 字符串类型：加引号并转义
            std::string str(value);
            std::string escaped;
            for (char c : str) {
                if (c == '\'') escaped += "\\'";
                else if (c == '\\') escaped += "\\\\";
                else escaped += c;
            }
            return "'" + escaped + "'";
        }
        else if constexpr (std::is_arithmetic_v<std::decay_t<T>>) {
            // 数字类型：直接输出，不加引号
            return std::to_string(value);
        }
        else {
            // 其他类型：转成字符串，加引号
            std::stringstream ss;
            ss << value;
            return "'" + ss.str() + "'";
        }
    }

    // 递归遍历 tuple，找到第 index 个参数并格式化
    template<size_t I = 0, typename... Tp>
    std::string format_param(const std::tuple<Tp...>& t, size_t index) {
        if constexpr (I >= sizeof...(Tp)) {
            return "";
        }
        else if (I == index) {
            return format_single(std::get<I>(t));
        }
        else {
            return format_param<I + 1>(t, index);
        }
    }

public:
    SimpleDatabase() = default;

    bool is_connected() const { return connected_; }

    bool connect(const std::string& host,
        unsigned int port,
        const std::string& user,
        const std::string& pass,
        const std::string& db,
        int num_connections) {
        try {
            mysql::connect_params params;
            params.server_address = mysql::host_and_port(host, port);
            params.username = user;
            params.password = pass;
            params.database = db;
            params.ssl = mysql::ssl_mode::require;

            std::queue<mysql::any_connection> temp_pool;
            for (int i = 0; i < num_connections; i++) {
                mysql::any_connection conn(ioc_);
                conn.connect(params);
                mysql::results result;
                conn.execute("SET NAMES utf8mb4", result);
                temp_pool.emplace(std::move(conn));
            }

            {
                std::lock_guard<std::mutex> lock(pool_mutex_);
                conn_pool = std::move(temp_pool);
            }

            connected_ = true;
            printf("[数据库] 连接池创建成功，共 %d 个连接\n", num_connections);
            return true;
        }
        catch (const std::exception& e) {
            printf("[数据库] 连接失败: %s\n", e.what());
            return false;
        }
    }

    template<typename... Args>
    mysql::results query_prepared(const std::string& sql, Args&&... args) {
        mysql::results result;
        if (!connected_) {
            return result;
        }

        mysql::any_connection conn(ioc_.get_executor());
        {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            if (conn_pool.empty()) {
                return result;
            }
            conn = std::move(conn_pool.front());
            conn_pool.pop();
        }

        try {
            std::stringstream ss;
            size_t start = 0;
            size_t pos = 0;
            size_t arg_index = 0;
            auto params = std::make_tuple(std::forward<Args>(args)...);

            while ((pos = sql.find("?", start)) != std::string::npos) {
                ss << sql.substr(start, pos - start);

                if (arg_index < sizeof...(Args)) {
                    ss << format_param(params, arg_index);
                    arg_index++;
                }

                start = pos + 1;
            }
            ss << sql.substr(start);

            std::cout << "[SQL] " << ss.str() << std::endl;
            conn.execute(ss.str(), result);
        }
        catch (const std::exception& e) {
            printf("[数据库] 查询失败: %s\n", e.what());
        }

        {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            conn_pool.emplace(std::move(conn));
        }

        return result;
    }

    mysql::results query(const std::string& sql) {
        mysql::results result;
        if (!connected_) {
            return result;
        }
        mysql::any_connection conn(ioc_.get_executor());
        {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            if (conn_pool.empty()) {
                return result;
            }
            conn = std::move(conn_pool.front());
            conn_pool.pop();
        }
        try {
            conn.execute(sql, result);
        }
        catch (const std::exception& e) {
            std::cerr << "[DB] Query failed: " << e.what() << std::endl;
            // 连接可能已损坏，不归还，直接丢弃
            return result;
        }
        {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            conn_pool.emplace(std::move(conn));
        }
        return result;
    }
};

// 全局数据库对象声明
extern SimpleDatabase g_db;