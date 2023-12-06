/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
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

[[maybe_unused]] static void serialize_to_byte_buffer(io_blob_list_t const& cli_buf, grpc::ByteBuffer& cli_byte_buf) {
    folly::small_vector< grpc::Slice, 4 > slices;
    for (auto const& blob : cli_buf) {
        slices.emplace_back(blob.cbytes(), blob.size(), grpc::Slice::STATIC_SLICE);
    }
    cli_byte_buf.Clear();
    grpc::ByteBuffer tmp(slices.data(), cli_buf.size());
    cli_byte_buf.Swap(&tmp);
}

[[maybe_unused]] static grpc::Status try_deserialize_from_byte_buffer(grpc::ByteBuffer const& cli_byte_buf,
                                                                      io_blob& cli_buf) {
    grpc::Slice slice;
    auto status = cli_byte_buf.TrySingleSlice(&slice);
    if (status.ok()) {
        cli_buf.set_bytes(slice.begin());
        cli_buf.set_size(slice.size());
    }
    return status;
}

[[maybe_unused]] static grpc::Status deserialize_from_byte_buffer(grpc::ByteBuffer const& cli_byte_buf,
                                                                  io_blob& cli_buf) {
    grpc::Slice slice;
    auto status = cli_byte_buf.DumpToSingleSlice(&slice);
    if (status.ok()) {
        cli_buf.buf_alloc(slice.size());
        std::memcpy(voidptr_cast(cli_buf.bytes()), c_voidptr_cast(slice.begin()), slice.size());
    }
    return status;
}

[[maybe_unused]] static sisl::io_blob_safe deserialize_from_byte_buffer(grpc::ByteBuffer const& cli_byte_buf) {
    grpc::Slice slice;
    auto status = cli_byte_buf.TrySingleSlice(&slice);
    if (status.ok()) { return sisl::io_blob_safe(slice.begin(), slice.size(), false /* is alligned*/); }
    auto cli_buf = sisl::io_blob_safe(slice.size());
    std::memcpy(voidptr_cast(cli_buf.bytes()), c_voidptr_cast(slice.begin()), slice.size());
    return cli_buf;
}

} // namespace sisl
