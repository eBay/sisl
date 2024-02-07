#include <sisl/grpc/rpc_server.hpp>

int main() { auto server = sisl::GrpcServer::make("127.0.0.1", nullptr, 1, "", ""); }
