/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Yaming Kuang
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#pragma once
#include <span>
#include <snappy-c.h>

namespace sisl {

class Compress {
public:
    static size_t max_compress_len(size_t size) { return snappy_max_compressed_length(size); }

    static int compress(std::span< const char > src, std::span< char > dst, size_t& bytes_written) {
        bytes_written = dst.size();
        auto ret = snappy_compress(src.data(), src.size(), dst.data(), &bytes_written);
        return (ret == SNAPPY_OK) ? 0 : ret;
    }

    static int decompress(std::span< const char > src, std::span< char > dst, size_t& bytes_written) {
        bytes_written = dst.size();
        auto ret = snappy_uncompress(src.data(), src.size(), dst.data(), &bytes_written);
        return (ret == SNAPPY_OK) ? 0 : ret;
    }
};

} // namespace sisl
