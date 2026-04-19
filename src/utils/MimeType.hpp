#pragma once
#include <boost/beast/core.hpp>

namespace beast = boost::beast;

beast::string_view mime_type(beast::string_view path);