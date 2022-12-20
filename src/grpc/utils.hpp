#pragma once

#include <string>
#include <fstream>

namespace sisl {

static bool get_file_contents(const std::string& file_name, std::string& contents) {
    try {
        std::ifstream f(file_name);
        std::string buffer(std::istreambuf_iterator< char >{f}, std::istreambuf_iterator< char >{});
        contents = buffer;
        return !contents.empty();
    } catch (...) {}
    return false;
}

} // namespace sisl::grpc
