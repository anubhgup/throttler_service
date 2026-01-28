# Throttling Service

## Project Overview

A distributed rate limiting service where:
- **Server**: Tracks active clients via heartbeats and fairly distributes rate limit quota among them
- **Clients**: Run the token bucket algorithm locally with their allocated share of the rate limit

### Inputs
- Client registrations and heartbeats (gRPC)
- Resource rate limit configurations (requests per second)
- Resource acquisition requests from client applications

### Outputs
- Fair allocation of rate limits to active clients
- Allow/deny decisions for resource acquisition (client-side)

### Constraints
- Single instance standalone server (no clustering)
- In-memory only (no persistence across restarts)
- Scale to 100s of concurrent clients
- C++ implementation with CMake build system
- gRPC for client-server communication

---

## Module Dependency Diagram

```
                    ┌─────────────────┐
                    │  Proto/API (1)  │
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │              │
              ▼              ▼              ▼
    ┌─────────────────┐ ┌─────────┐ ┌─────────────────┐
    │ Client Registry │ │  Token  │ │ Resource Manager│
    │       (3)       │ │Bucket(2)│ │       (4)       │
    └────────┬────────┘ └────┬────┘ └────────┬────────┘
             │               │               │
             └───────┬───────┴───────┬───────┘
                     │               │
                     ▼               ▼
              ┌────────────┐  ┌─────────────────┐
              │gRPC Server │  │ Client Library  │
              │    (5)     │  │      (6)        │
              └─────┬──────┘  └─────────────────┘
                    │
                    ▼
              ┌────────────┐
              │    Main    │
              │    (7)     │
              └────────────┘
```

---

## Module Dependency Table

| Module | Depends On |
|--------|------------|
| 1. Proto/API | (none) |
| 2. Token Bucket | (none) |
| 3. Client Registry | Proto/API |
| 4. Resource Manager | Proto/API, Client Registry |
| 5. gRPC Server | Proto/API, Client Registry, Resource Manager |
| 6. Client Library | Proto/API, Token Bucket |
| 7. Main Application | gRPC Server |

---

## Implementation Order

1. Proto/API (foundation - defines all interfaces)
2. Token Bucket (standalone, no dependencies)
3. Client Registry (depends on Proto for types)
4. Resource Manager (depends on Proto, Client Registry)
5. gRPC Server (integrates 3 & 4)
6. Main Application (wires up server)
7. Client Library (uses Proto, Token Bucket)

---

## Module Definitions

### Module 1: Proto/API Definition

**Responsibility**: Define the gRPC service contract

**Files**:
- `proto/throttling_service.proto`

**API Operations**:
- `RegisterClient(client_id)` - Client announces itself
- `Heartbeat(client_id, resource_ids)` - Keep-alive + declare interest in resources
- `GetAllocation(client_id, resource_id)` - Get allocated rate for a resource
- `SetResourceLimit(resource_id, rate_limit)` - Admin: configure resource limits
- `UnregisterClient(client_id)` - Client gracefully disconnects

**Story**: _To be written_

**Status**: Not started

---

### Module 2: Token Bucket

**Responsibility**: Rate limiting algorithm implementation

**Files**:
- `src/token_bucket/token_bucket.h`
- `src/token_bucket/token_bucket.cc`
- `tests/token_bucket_test.cc`

**Key Components**:
- `TokenBucket` class with configurable rate and burst size
- `TryConsume(tokens)` - Attempt to consume tokens (returns bool)
- `Refill()` - Add tokens based on elapsed time
- Thread-safe for concurrent use

**Story**: _To be written_

**Status**: Not started

---

### Module 3: Client Registry

**Responsibility**: Track connected clients and their liveness

**Files**:
- `src/client_registry/client_registry.h`
- `src/client_registry/client_registry.cc`
- `tests/client_registry_test.cc`

**Key Components**:
- Register/unregister clients
- Track last heartbeat timestamp per client
- Expire stale clients (missed heartbeats)
- Track which resources each client is interested in
- Thread-safe with mutex protection

**Story**: _To be written_

**Status**: Not started

---

### Module 4: Resource Manager

**Responsibility**: Manage resources and fair allocation

**Files**:
- `src/resource_manager/resource_manager.h`
- `src/resource_manager/resource_manager.cc`
- `tests/resource_manager_test.cc`

**Key Components**:
- Store rate limits per `resource_id`
- Calculate fair share: `rate_limit / num_active_clients`
- Notify/rebalance when clients join/leave
- CRUD operations for resource limits

**Story**: _To be written_

**Status**: Not started

---

### Module 5: gRPC Server

**Responsibility**: Service implementation

**Files**:
- `src/server/throttling_server.h`
- `src/server/throttling_server.cc`
- `tests/throttling_server_test.cc`

**Key Components**:
- Implement gRPC service handlers
- Wire together Client Registry and Resource Manager
- Handle concurrent requests
- Graceful shutdown

**Story**: _To be written_

**Status**: Not started

---

### Module 6: Client Library

**Responsibility**: Client-side SDK for applications

**Files**:
- `src/client/throttling_client.h`
- `src/client/throttling_client.cc`
- `tests/throttling_client_test.cc`

**Key Components**:
- Automatic heartbeat thread
- Local `TokenBucket` per resource
- `Acquire(resource_id)` - Check local bucket
- Handle allocation updates from server
- Reconnection logic

**Story**: _To be written_

**Status**: Not started

---

### Module 7: Main Application

**Responsibility**: Server entry point

**Files**:
- `src/main.cc`

**Key Components**:
- Configuration parsing (port, heartbeat timeout, etc.)
- Signal handling (SIGTERM, SIGINT)
- Logging setup
- Server lifecycle management

**Story**: _To be written_

**Status**: Not started

---

## File Structure

```
throttling_service/
├── CMakeLists.txt
├── README.md
├── project.md
├── AGENTS.md
├── proto/
│   └── throttling_service.proto
├── src/
│   ├── token_bucket/
│   │   ├── token_bucket.h
│   │   └── token_bucket.cc
│   ├── client_registry/
│   │   ├── client_registry.h
│   │   └── client_registry.cc
│   ├── resource_manager/
│   │   ├── resource_manager.h
│   │   └── resource_manager.cc
│   ├── server/
│   │   ├── throttling_server.h
│   │   └── throttling_server.cc
│   ├── client/
│   │   ├── throttling_client.h
│   │   └── throttling_client.cc
│   └── main.cc
├── tests/
│   ├── token_bucket_test.cc
│   ├── client_registry_test.cc
│   ├── resource_manager_test.cc
│   ├── throttling_server_test.cc
│   └── throttling_client_test.cc
└── build/
    └── (generated)
```

---

## Design Decisions

1. **Fair Share Algorithm**: Simple equal division (`rate_limit / num_clients`). Future enhancement could support weighted allocation.

2. **Heartbeat Timeout**: Configurable, default 30 seconds. Clients sending heartbeats more frequently (e.g., every 10 seconds) have margin for network hiccups.

3. **Token Bucket Parameters**:
   - Rate: Allocated requests per second
   - Burst: Defaults to 1 second worth of tokens (configurable)

4. **Client Identification**: Clients provide their own unique `client_id` string.

5. **Resource Interest**: Clients declare which `resource_id`s they're interested in via heartbeats. Only interested clients count toward fair share calculation.

---

## Progress Log

| Date | Module | Progress |
|------|--------|----------|
| | | |
