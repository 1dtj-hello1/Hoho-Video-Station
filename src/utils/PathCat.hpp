#pragma once
#include <string>
#include <boost/beast/core.hpp>

namespace beast = boost::beast;

std::string path_cat(beast::string_view base, beast::string_view path);