#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <string>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class Config {
public:
    // 单例模式
    static Config& get() {
        static Config instance;
        return instance;
    }

    // 加载配置
    bool load(const std::string& path = "config.json") {
        try {
			FILE* fp = std::fopen(path.c_str(), "r");
            if (!fp) {
                std::cout << "[Config] Using default config\n";
                return false;
            }

            fseek(fp, 0, SEEK_END);
            long size = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            // 读取整个文件到字符串
            std::string content;
            content.resize(size);
            fread(&content[0], 1, size, fp);
            std::fclose(fp);
            json j = json::parse(content);
            parse_json(j);
            std::cout << "[Config] Loaded from " << path << "\n";
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[Config] Load failed: " << e.what() << "\n";
            return false;
        }
    }

    // 配置项（只读访问）
    struct Server {
        std::string host = "0.0.0.0";
        int port = 8080;
        int threads = 4;
		std::string doc_root = "E:/Tiny/niwudile/web";
    } server;

    struct Database {
        std::string host = "127.0.0.1";
        int port = 3306;
        std::string user = "root";
        std::string password = "123456";
        std::string name = "niwudile";
        int pool_size = 10;
    } database;

    struct Upload {
        size_t max_file_mb = 100;
        size_t chunk_size_mb = 5;
        std::string dir = "videos";
    } upload;

private:
    Config() = default;

    void parse_json(const json& j) {
        if (j.contains("server")) {
            auto& s = j["server"];
            if (s.contains("host")) server.host = s["host"];
            if (s.contains("port")) server.port = s["port"];
            if (s.contains("threads")) server.threads = s["threads"];
            if (s.contains("doc_root")) server.doc_root = s["doc_root"];
        }

        if (j.contains("database")) {
            auto& db = j["database"];
            if (db.contains("host")) database.host = db["host"];
            if (db.contains("port")) database.port = db["port"];
            if (db.contains("user")) database.user = db["user"];
            if (db.contains("password")) database.password = db["password"];
            if (db.contains("name")) database.name = db["name"];
            if (db.contains("pool_size")) database.pool_size = db["pool_size"];
        }

        if (j.contains("upload")) {
            auto& up = j["upload"];
            if (up.contains("max_file_mb")) upload.max_file_mb = up["max_file_mb"];
            if (up.contains("chunk_size_mb")) upload.chunk_size_mb = up["chunk_size_mb"];
            if (up.contains("dir")) upload.dir = up["dir"];
        }
    }
};