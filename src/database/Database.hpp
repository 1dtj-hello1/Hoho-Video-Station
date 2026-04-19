#pragma once
#include <boost/mysql.hpp>
#include <iostream>
#include <string>

namespace mysql = boost::mysql;
namespace asio = boost::asio;

class SimpleDatabase {
private:
    asio::io_context ioc_;
    mysql::any_connection conn_;
    bool connected_ = false;

public:
    SimpleDatabase();

    bool connect(const std::string& host,
        unsigned int port,
        const std::string& user,
        const std::string& pass,
        const std::string& db);

    mysql::results query(const std::string& sql);

    template<typename... Args>
    mysql::results query_params(const std::string& sql, Args&&... args) {
        mysql::results result;
        if (connected_) {
            conn_.execute(mysql::with_params(sql, std::forward<Args>(args)...), result);
        }
        return result;
    }

    template<typename... Args>
    mysql::results query_prepared(const std::string& sql, Args&&... args) {
        mysql::results result;
        if (connected_) {
            // 先构造参数元组，再传递
            auto params = std::make_tuple(std::forward<Args>(args)...);
            std::apply([&](auto&&... p) {
                conn_.execute(mysql::with_params(sql, p...), result);
                }, params);
        }
        return result;
    }
    void execute(const std::string& sql);
    bool is_connected() const;
};

extern SimpleDatabase g_db;