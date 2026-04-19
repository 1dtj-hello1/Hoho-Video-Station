#include "Database.hpp"

SimpleDatabase::SimpleDatabase() : conn_(ioc_) {}

bool SimpleDatabase::connect(const std::string& host,
    unsigned int port,
    const std::string& user,
    const std::string& pass,
    const std::string& db) {
    try {
        mysql::connect_params params;
        params.server_address = mysql::host_and_port(host, port);
        params.username = user;
        params.password = pass;
        params.database = db;
        params.ssl = mysql::ssl_mode::require;

        conn_.connect(params);
        connected_ = true;
        std::cout << "[数据库] 连接成功" << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[数据库] 连接失败: " << e.what() << std::endl;
        return false;
    }
}

mysql::results SimpleDatabase::query(const std::string& sql) {
    mysql::results result;
    conn_.execute(sql, result);
    return result;
}

void SimpleDatabase::execute(const std::string& sql) {
    mysql::results result;
    conn_.execute(sql, result);
}

bool SimpleDatabase::is_connected() const { return connected_; }

SimpleDatabase g_db;