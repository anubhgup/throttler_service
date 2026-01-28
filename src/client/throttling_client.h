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
  /// Constructs a throttling client.
  ///
  /// @param server_address Server address (e.g., "localhost:50051").
  /// @param client_id Unique identifier for this client.
  /// @param heartbeat_interval How often to send heartbeats (default: 10s).
  ThrottlingClient(const std::string& server_address,
                   const std::string& client_id,
                   std::chrono::seconds heartbeat_interval = std::chrono::seconds(10));

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
  std::string server_address_;
  std::string client_id_;
  std::chrono::seconds heartbeat_interval_;

  // gRPC
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<ThrottlingService::Stub> stub_;

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
