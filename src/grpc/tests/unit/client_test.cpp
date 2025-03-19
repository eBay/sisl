#include <random>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

#include "sisl/grpc/rpc_client.hpp"

SISL_OPTIONS_ENABLE(logging)

namespace sisltesting {
using namespace sisl;
using namespace ::grpc;

[[maybe_unused]] static void SerializeToByteBuffer(ByteBuffer& buffer, std::string const& msg) {
    buffer.Clear();
    Slice slice(msg);
    ByteBuffer tmp(&slice, 1);
    buffer.Swap(&tmp);
}

[[maybe_unused]] static std::string DeserializeFromBuffer(ByteBuffer const& buffer) {
    std::vector< grpc::Slice > slices;
    (void)buffer.Dump(&slices);
    std::string buf;
    buf.reserve(buffer.Length());
    for (auto s = slices.begin(); s != slices.end(); s++) {
        buf.append(reinterpret_cast< const char* >(s->begin()), s->size());
    }
    return buf;
}

static std::random_device g_rd{};
static std::default_random_engine g_re{g_rd()};

static constexpr std::array< const char, 62 > alphanum{
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K',
    'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z'};

static std::string gen_random_string(size_t len) {
    std::string str;
    std::uniform_int_distribution< size_t > rand_char{0, alphanum.size() - 1};
    for (size_t i{0}; i < len; ++i) {
        str += alphanum[rand_char(g_re)];
    }
    str += '\0';
    return str;
}

static std::pair< std::string, grpc::ByteBuffer > create_test_byte_buffer(uint32_t num_slices, uint64_t total_size) {
    std::vector< grpc::Slice > slices;
    std::string concat_str;

    uint64_t size_per_slice = (total_size - 1) / num_slices + 1;
    for (uint32_t i = 0; i < num_slices; i++) {
        std::string msg = gen_random_string(size_per_slice);
        concat_str += msg;
        slices.push_back(grpc::Slice(msg));
    }
    return std::pair{concat_str, grpc::ByteBuffer{slices.data(), slices.size()}};
}

static std::string blob_to_string(io_blob const& b) { return std::string(c_charptr_cast(b.cbytes()), b.size()); }

static void do_test(std::string const& msg, grpc::ByteBuffer& bbuf) {
    GenericClientResponse resp1(bbuf);
    {
        // EXPECT_EQ(msg, DeserializeFromBuffer(rbuf1));
        EXPECT_EQ(msg, blob_to_string(resp1.response_blob()));
    }

    // move construction
    GenericClientResponse resp2(std::move(resp1));
    {
        EXPECT_EQ(msg, blob_to_string(resp2.response_blob()));
        auto blb1 = resp1.response_blob();
        EXPECT_EQ(blb1.size(), 0);
        EXPECT_EQ(blb1.cbytes(), nullptr);
    }

    // move assignment
    GenericClientResponse resp3;
    resp3 = std::move(resp2);
    {
        EXPECT_EQ(msg, blob_to_string(resp3.response_blob()));
        auto blb2 = resp2.response_blob();
        EXPECT_EQ(blb2.size(), 0);
        EXPECT_EQ(blb2.cbytes(), nullptr);
    }

    // copy construction
    {
        GenericClientResponse resp4(resp3);
        EXPECT_EQ(msg, blob_to_string(resp4.response_blob()));
        EXPECT_EQ(msg, blob_to_string(resp3.response_blob()));
    }

    // copy assignment
    {
        GenericClientResponse resp5;
        resp5 = resp3;
        EXPECT_EQ(msg, blob_to_string(resp5.response_blob()));
        EXPECT_EQ(msg, blob_to_string(resp3.response_blob()));
    }
}

TEST(GenericClientResponseTest, inline_single_slice_test) {
    auto [msg, bbuf] = create_test_byte_buffer(1u, 128);
    do_test(msg, bbuf);
}

TEST(GenericClientResponseTest, inline_multi_slice_test) {
    auto [msg, bbuf] = create_test_byte_buffer(2u, 128);
    do_test(msg, bbuf);
}

TEST(GenericClientResponseTest, refcounted_single_slice_test) {
    auto [msg, bbuf] = create_test_byte_buffer(1u, 8192);
    do_test(msg, bbuf);
}

TEST(GenericClientResponseTest, refcounted_multi_slice_test) {
    auto [msg, bbuf] = create_test_byte_buffer(2u, 10000);
    do_test(msg, bbuf);
}
} // namespace sisltesting

int main(int argc, char* argv[]) {
    ::testing::InitGoogleMock(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging)
    sisl::logging::SetLogger("auth_test");
    int ret{RUN_ALL_TESTS()};
    sisl::GrpcAsyncClientWorker::shutdown_all();
    return ret;
}