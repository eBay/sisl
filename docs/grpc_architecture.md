# SISL gRPC Architecture & Thread Management

## Overview
SISL provides a C++ wrapper around gRPC for building async RPC servers and clients. This document captures the architecture, thread model, and known issues.

## Code Structure

### Server Side
**Location**: `sisl/src/grpc/rpc_server.cpp`, `sisl/include/sisl/grpc/rpc_server.hpp`

**Key Classes**:
- `GrpcServer`: Main server wrapper class
- `RpcTag`: Base class for RPC state management using intrusive reference counting

**Server Initialization** (`rpc_server.cpp:32-79`):
```cpp
GrpcServer::GrpcServer(
    const std::string& listen_addr,
    uint32_t threads,  // Number of CompletionQueue worker threads
    int max_receive_msg_size,
    int max_send_msg_size,
    const std::string& ssl_key,
    const std::string& ssl_cert,
    GrpcTokenVerifier* auth_mgr
)
```

**Configuration** (Lines 44-77):
- Message size limits: `SetMaxReceiveMessageSize()`, `SetMaxSendMessageSize()`
- SSL/TLS setup with certificates
- Authentication via `GrpcTokenVerifier`
- ⚠️ **Missing**: `grpc::ResourceQuota` for thread pool limits

**Thread Model** (`rpc_server.cpp:101-134`):
- Creates one `grpc::CompletionQueue` per thread (Lines 74-77)
- Spawns worker threads in `run()` (Lines 106-115):
  ```cpp
  for (uint32_t i = 0; i < m_num_threads; ++i) {
      auto t = std::make_shared<std::thread>(&GrpcServer::handle_rpcs, this, i, ...);
      pthread_setname_np(t->native_handle(), "grpc_server");
      m_threads.push_back(t);
  }
  ```
- Each thread runs `handle_rpcs()` which calls `CompletionQueue::Next()` in a loop

**Shutdown** (`rpc_server.cpp:136-153`):
- Properly shuts down server and completion queues
- Joins all worker threads (Line 148): `if (thr->joinable()) thr->join();`
- ✅ No thread leaks from application-level threads

### Client Side
**Location**: `sisl/src/grpc/rpc_client.cpp`, `sisl/include/sisl/grpc/rpc_client.hpp`

**Key Classes**:
- `GrpcBaseClient`: Base client for channel management
- `GrpcAsyncClient`: Async RPC client
- `GrpcAsyncClientWorker`: Named worker pool with dedicated threads

**Client Initialization** (`rpc_client.cpp:22-60`):
- Creates `grpc::Channel` with SSL or insecure credentials
- Configures message size limits via `ChannelArguments`

**Worker Thread Pool** (`rpc_client.cpp:89-125`):
- Static registry of named workers
- Each worker owns a `CompletionQueue` and thread pool
- Factory method: `GrpcAsyncClientWorker::create_worker(name, num_threads)`
- Worker threads run `client_loop()` processing responses from CQ

## Thread Types in Production

### Application-Level Threads (Configured)
Based on typical configuration:
- **ctrl_svc**: 1 thread (`SM_CONFIG(ctrl_svc_threads)`)
- **data_svc**: 2 threads (`SM_CONFIG(data_svc_threads)`)
- **nuraft_mesg server**: 2 threads (`NURAFT_MESG_CONFIG(grpc_server_thread_cnt)`)
- **grpc_raft_client**: 1 thread (`NURAFT_MESG_CONFIG(grpc_raft_client_thread_cnt)`)
- **grpc_data_client**: 1 thread (`NURAFT_MESG_CONFIG(grpc_data_client_thread_cnt)`)

**Total Application Threads**: ~7 threads

### gRPC Internal Threads (NOT Configured by SISL)

⚠️ **Critical**: These are gRPC library internals, NOT created by application code:

1. **grpc_executor** (`grpc_core::Executor::ThreadMain`):
   - **Purpose**: Global thread pool for async operations (DNS, connection management, callbacks)
   - **Default**: Auto-scales based on load, no upper limit
   - **Observed**: Can grow to 200+ threads in production
   - **Control**: `GRPC_MAX_THREADS` env var OR `grpc::ResourceQuota::SetMaxThreads()`

2. **grpc_threadpool** (`grpc_event_engine::experimental::ThreadPool::ThreadFunc`):
   - **Purpose**: General-purpose thread pool for gRPC operations
   - **Observed**: ~32 threads typical

3. **grpc_epoll_worker** (`ev_epoll1_linux.cc:begin_worker`):
   - **Purpose**: Epoll-based event loop workers
   - **Observed**: 6-8 threads typical

## Known Issues

### Issue #1: Unbounded gRPC Executor Thread Pool

**Symptom**: Process can accumulate 200+ idle threads in `grpc_core::Executor::ThreadMain`

**Root Cause**:
- SISL does not set `grpc::ResourceQuota` limits
- gRPC's executor pool auto-scales with load but never shrinks
- High concurrent RPC activity or connection churn causes growth

**Evidence**: Thread dump from SH testing (541 total threads):
```
209 threads: grpc_core::Executor::ThreadMain (all waiting on condition variables)
246 threads: Empty stack 0x0000000000000000 (separate issue, likely OS/library)
 32 threads: grpc_threadpool
 54 threads: Application threads (normal)
```

**Code Location**: `sisl/src/grpc/rpc_server.cpp:44-79` - ResourceQuota NOT set

**Workaround**:
```bash
export GRPC_MAX_THREADS=64
```

**Proper Fix** (not implemented yet):
```cpp
// Add to GrpcServer constructor
grpc::ResourceQuota quota;
quota.SetMaxThreads(max_executor_threads);  // Value needs load testing
m_builder.SetResourceQuota(quota);
```

**Why Not Fixed**:
- Root cause of growth unclear (needs investigation)
- Optimal limit value unknown
- Impact minimal (pod auto-restarts)

## Usage Patterns

### Storage Manager (storage_mgr)
**Location**: `storage_mgr/src/lib/sm_lib.cpp`

**Server Creation** (Lines 204-233):
```cpp
// Control plane services
auto ctrl_svc = std::make_shared<CtrlServices>(
    server_address,
    SM_CONFIG(ctrl_svc_threads),  // Default: 1
    create_pg_svc(...),
    create_shard_svc(...)
);

// Data plane services
auto data_svc = std::make_shared<DataService>(
    data_server_addr,
    SM_CONFIG(data_svc_threads),  // Default: 2
    create_data_svc(...)
);
```

**Restart Logic** (Lines 301-336):
- `restart_svcs()` properly calls `shutdown()` and waits for threads
- No application-level thread leaks from restarts
- Triggered by SSL cert changes via file watcher

### NuRaft Messaging (nuraft_mesg)
**Location**: `nuraft_mesg/src/lib/manager_impl.cpp`

**Configuration** (`nuraft_mesg/src/lib/nuraft_mesg_config.fbs`):
```
grpc_raft_client_thread_cnt: 1
grpc_data_client_thread_cnt: 1
grpc_server_thread_cnt: 2
raft_scheduler_thread_cnt: 2
raft_append_entries_thread_cnt: 2
```

**Server Creation** (Lines 109-112):
```cpp
sisl::GrpcServer::make(
    listen_address,
    NURAFT_MESG_CONFIG(grpc_server_thread_cnt),
    ssl_key, ssl_cert,
    max_receive_msg_size, max_send_msg_size
);
```

**Client Workers** (Lines 69-70):
```cpp
engine_factory(
    NURAFT_MESG_CONFIG(grpc_raft_client_thread_cnt),
    NURAFT_MESG_CONFIG(grpc_data_client_thread_cnt),
    ...
);
```

## Best Practices

### Thread Configuration
- **Server threads**: Match expected concurrent RPC handlers (typically 1-4)
- **Client workers**: One worker per service type with 1-2 threads
- **Avoid**: Creating too many application threads (gRPC handles concurrency internally)

### Resource Management
- Always call `shutdown()` before destroying `GrpcServer`
- Join all worker threads in destructors
- Use named client workers to share connections

### Missing Features
- ❌ `grpc::ResourceQuota` configuration
- ❌ Configurable thread pool limits
- ❌ Metrics/monitoring for thread pool size

## References
- gRPC Documentation: https://grpc.github.io/grpc/cpp/
- ResourceQuota API: https://grpc.github.io/grpc/cpp/classgrpc_1_1_resource_quota.html
- Thread dump analysis: `/Users/yawzhang/test/debug/threads` (SH testing, Jan 2026)

---
*Document created from AI-assisted code analysis. Last updated: 2026-01-15*
