#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

#include "sisl/grpc/rpc_client.hpp"

SISL_LOGGING_INIT(grpc_server)
SISL_OPTIONS_ENABLE(logging)

namespace sisltesting {
using namespace sisl;
using namespace ::grpc;

static void SerializeToByteBuffer(ByteBuffer& buffer, std::string const& msg) {
    buffer.Clear();
    Slice slice(msg);
    ByteBuffer tmp(&slice, 1);
    buffer.Swap(&tmp);
}

static std::string DeserializeFromBuffer(ByteBuffer const& buffer) {
    std::vector< grpc::Slice > slices;
    (void)buffer.Dump(&slices);
    std::string buf;
    buf.reserve(buffer.Length());
    for (auto s = slices.begin(); s != slices.end(); s++) {
        buf.append(reinterpret_cast< const char* >(s->begin()), s->size());
    }
    return buf;
}

TEST(GenericClientResponseTest, movability_test) {
    std::string msg("Hello");
    ByteBuffer buffer;
    SerializeToByteBuffer(buffer, msg);
    GenericClientResponse resp1(buffer);
    auto& b1 = resp1.response_blob();
    auto buf1 = resp1.response_buf();
    EXPECT_EQ(msg, DeserializeFromBuffer(buf1));
    EXPECT_EQ(msg, std::string(reinterpret_cast< const char* >(b1.cbytes()), b1.size()));

    // move construction
    GenericClientResponse resp2(std::move(resp1));
    auto& b2 = resp2.response_blob();
    EXPECT_EQ(msg, std::string(b2.bytes(), b2.bytes() + b2.size()));
    EXPECT_TRUE(resp2.response_buf().Valid());
    EXPECT_EQ(b1.size(), 0);
    EXPECT_EQ(b1.bytes(), nullptr);
    EXPECT_FALSE(resp1.response_buf().Valid());
    EXPECT_EQ(resp1.response_blob().size(), 0);
    EXPECT_EQ(resp1.response_blob().bytes(), nullptr);

    // move assignment
    GenericClientResponse resp3 = std::move(resp2);
    auto b3 = resp3.response_blob();
    EXPECT_EQ(msg, std::string(b3.bytes(), b3.bytes() + b3.size()));
    EXPECT_TRUE(resp3.response_buf().Valid());
    EXPECT_EQ(b2.size(), 0);
    EXPECT_EQ(b2.bytes(), nullptr);
    EXPECT_FALSE(resp2.response_buf().Valid());
    EXPECT_EQ(resp2.response_blob().size(), 0);
    EXPECT_EQ(resp2.response_blob().bytes(), nullptr);
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