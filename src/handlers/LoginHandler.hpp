#pragma once
#include <boost/beast/http.hpp>

namespace beast = boost::beast;
namespace http = beast::http;

template<class Body, class Allocator>
http::message_generator
handle_login(beast::string_view doc_root,
    http::request<Body, http::basic_fields<Allocator>>&& req);