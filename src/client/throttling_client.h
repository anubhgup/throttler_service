// Throttling Client Library
//
// Story:
// Client-side SDK for the throttling service. Handles server communication,
// automatic heartbeats, and local token bucket rate limiting. The Acquire()
// method queues requests and executes callbacks when tokens become available.
//
// Algorithm:
// 1. Start() registers with server and starts heartbeat + worker threads
// 2. Heartbeats send resource interests, receive allocations
// 3. Allocations update local token buckets
// 4. Acquire() tries immediately; if blocked, queues request for worker thread
// 5. Worker thread processes queue, executing callbacks as tokens refill
//
// Thread Safety:
// All public methods are thread-safe.

#pragma once

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "throttling_service.grpc.pb.h"
#include "token_bucket.h"

namespace throttling {

// ============================================================================
// Stub Interface for Dependency Injection
// ============================================================================

/// Interface for throttling service RPC calls.
/// Allows mocking for unit tests.
class ThrottlingStubInterface {
 public:
  virtual ~ThrottlingStubInterface() = default;

  virtual grpc::Status RegisterClient(const RegisterClientRequest& request,
                                      RegisterClientResponse* response) = 0;

  virtual grpc::Status UnregisterClient(const UnregisterClientRequest& request,
                                        UnregisterClientResponse* response) = 0;

  virtual grpc::Status Heartbeat(const HeartbeatRequest& request,
                                 HeartbeatResponse* response) = 0;

  virtual grpc::Status SetResourceLimit(const SetResourceLimitRequest& request,
                                        SetResourceLimitResponse* response) = 0;
};

/// Real stub implementation that wraps the gRPC-generated stub.
class RealThrottlingStub : public ThrottlingStubInterface {
 public:
  explicit RealThrottlingStub(const std::string& server_address);

  grpc::Status RegisterClient(const RegisterClientRequest& request,
                              RegisterClientResponse* response) override;

  grpc::Status UnregisterClient(const UnregisterClientRequest& request,
                                UnregisterClientResponse* response) override;

  grpc::Status Heartbeat(const HeartbeatRequest& request,
                         HeartbeatResponse* response) override;

  grpc::Status SetResourceLimit(const SetResourceLimitRequest& request,
                                SetResourceLimitResponse* response) override;

 private:
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<ThrottlingService::Stub> stub_;
};

/// Fake stub for testing - configurable responses.
class FakeThrottlingStub : public ThrottlingStubInterface {
 public:
  FakeThrottlingStub() = default;

  grpc::Status RegisterClient(const RegisterClientRequest& request,
                              RegisterClientResponse* response) override;

  grpc::Status UnregisterClient(const UnregisterClientRequest& request,
                                UnregisterClientResponse* response) override;

  grpc::Status Heartbeat(const HeartbeatRequest& request,
                         HeartbeatResponse* response) override;

  grpc::Status SetResourceLimit(const SetResourceLimitRequest& request,
                                SetResourceLimitResponse* response) override;

  // Configuration for test scenarios
  void SetRegisterResult(grpc::Status status);
  void SetUnregisterResult(grpc::Status status);
  void SetHeartbeatResult(grpc::Status status);
  void SetSetResourceLimitResult(grpc::Status status);
  void SetAllocations(const std::map<int64_t, double>& allocations);

  // Inspection for tests
  int GetRegisterCallCount() const { return register_call_count_; }
  int GetUnregisterCallCount() const { return unregister_call_count_; }
  int GetHeartbeatCallCount() const { return heartbeat_call_count_; }
  int GetSetResourceLimitCallCount() const { return set_resource_limit_call_count_; }
  std::string GetLastClientId() const { return last_client_id_; }
  std::set<int64_t> GetLastResourceIds() const { return last_resource_ids_; }
  int64_t GetLastSetResourceId() const { return last_set_resource_id_; }
  double GetLastSetRateLimit() const { return last_set_rate_limit_; }

 private:
  grpc::Status register_status_ = grpc::Status::OK;
  grpc::Status unregister_status_ = grpc::Status::OK;
  grpc::Status heartbeat_status_ = grpc::Status::OK;
  grpc::Status set_resource_limit_status_ = grpc::Status::OK;
  std::map<int64_t, double> allocations_;

  int register_call_count_ = 0;
  int unregister_call_count_ = 0;
  int heartbeat_call_count_ = 0;
  int set_resource_limit_call_count_ = 0;
  std::string last_client_id_;
  std::set<int64_t> last_resource_ids_;
  int64_t last_set_resource_id_ = 0;
  double last_set_rate_limit_ = 0.0;
};

// ============================================================================
// ThrottlingClient
// ============================================================================

/// Client library for the throttling service.
///
/// Handles server communication, automatic heartbeats, and local rate limiting.
/// The Acquire() method uses local token buckets for rate limiting.
///
/// Example:
///   ThrottlingClient client("localhost:50051", "my-client");
///   client.SetResourceInterests({1, 2});
///   if (!client.Start()) {
///     // Handle error
///   }
///
///   // Rate-limited execution - callback always executes eventually
///   client.Acquire(1, 1.0, []() {
///     ProcessRequest();  // Executes when rate limit allows
///   });
///
///   client.Stop();
class ThrottlingClient {
 public:
  /// Constructs a throttling client with server address.
  ///
  /// @param server_address Server address (e.g., "localhost:50051").
  /// @param client_id Unique identifier for this client.
  /// @param heartbeat_interval How often to send heartbeats (default: 10s).
  ThrottlingClient(const std::string& server_address,
                   const std::string& client_id,
                   std::chrono::milliseconds heartbeat_interval = std::chrono::seconds(10));

  /// Constructs a throttling client with injected stub (for testing).
  ///
  /// @param stub Stub implementation (takes ownership).
  /// @param client_id Unique identifier for this client.
  /// @param heartbeat_interval How often to send heartbeats (default: 10s).
  ThrottlingClient(std::unique_ptr<ThrottlingStubInterface> stub,
                   const std::string& client_id,
                   std::chrono::milliseconds heartbeat_interval = std::chrono::seconds(10));

  /// Destructor. Calls Stop() if running.
  ~ThrottlingClient();

  // Non-copyable, non-movable
  ThrottlingClient(const ThrottlingClient&) = delete;
  ThrottlingClient& operator=(const ThrottlingClient&) = delete;

  /// Starts the client.
  ///
  /// Registers with the server and starts the heartbeat thread.
  ///
  /// @return true on success, false if registration fails.
  bool Start();

  /// Stops the client.
  ///
  /// Stops the heartbeat thread and unregisters from the server.
  /// Pending queued callbacks are discarded.
  void Stop();

  /// Sets the resources this client is interested in.
  ///
  /// Takes effect on the next heartbeat. The server will allocate
  /// a fair share of each resource's rate limit to this client.
  ///
  /// @param resource_ids Set of resource IDs to track.
  void SetResourceInterests(const std::set<int64_t>& resource_ids);

  /// Acquires tokens for a resource and executes the callback.
  ///
  /// If tokens are immediately available, consumes them and executes the
  /// callback synchronously. Otherwise, queues the request and the callback
  /// will be executed asynchronously when tokens become available.
  ///
  /// @param resource_id The resource to acquire tokens for.
  /// @param count Number of tokens to acquire.
  /// @param callback Function to execute when tokens are acquired.
  void Acquire(int64_t resource_id, double count, std::function<void()> callback);

  /// Returns the current allocation rate for a resource.
  ///
  /// @param resource_id The resource to query.
  /// @return Allocation in requests per second, or 0 if not allocated.
  double GetAllocation(int64_t resource_id) const;

  /// Sets the rate limit for a resource on the server.
  ///
  /// This is an admin operation that configures the total rate limit
  /// for a resource. The server will fairly distribute this limit
  /// among all interested clients.
  ///
  /// @param resource_id The resource to configure.
  /// @param rate_limit Total requests per second for this resource.
  /// @return true on success, false on error.
  bool SetResourceLimit(int64_t resource_id, double rate_limit);

  /// Returns whether the client is running.
  bool IsRunning() const;

  /// Returns the number of pending queued requests.
  size_t GetPendingCount() const;

 private:
  /// Pending acquire request.
  struct PendingRequest {
    int64_t resource_id;
    double count;
    std::function<void()> callback;
  };

  /// Heartbeat thread function.
  void HeartbeatLoop();

  /// Worker thread function - processes pending requests.
  void WorkerLoop();

  /// Sends a single heartbeat and updates token buckets.
  /// @return true if heartbeat succeeded, false on error.
  bool SendHeartbeat();

  /// Updates token buckets based on allocations from server.
  void UpdateTokenBuckets(const std::map<int64_t, double>& allocations);

  // Configuration
  std::string client_id_;
  std::chrono::milliseconds heartbeat_interval_;

  // gRPC stub (injectable for testing)
  std::unique_ptr<ThrottlingStubInterface> stub_;

  // Resource interests (protected by mutex_)
  std::set<int64_t> resource_interests_;

  // Token buckets: resource_id -> TokenBucket (protected by mutex_)
  std::map<int64_t, std::unique_ptr<TokenBucket>> token_buckets_;

  // Pending request queue (protected by queue_mutex_)
  std::deque<PendingRequest> pending_queue_;
  mutable std::mutex queue_mutex_;
  std::condition_variable queue_cv_;

  // Thread control
  std::thread heartbeat_thread_;
  std::thread worker_thread_;
  std::atomic<bool> running_{false};
  mutable std::mutex mutex_;
  std::condition_variable cv_;
};

}  // namespace throttling
