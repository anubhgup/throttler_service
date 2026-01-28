// Throttling Client Library - Implementation
//
// See throttling_client.h for the Story and algorithm description.

#include "throttling_client.h"

#include <iostream>
#include <utility>

namespace throttling {

ThrottlingClient::ThrottlingClient(const std::string& server_address,
                                   const std::string& client_id,
                                   std::chrono::seconds heartbeat_interval)
    : server_address_(server_address),
      client_id_(client_id),
      heartbeat_interval_(heartbeat_interval) {
  // Create gRPC channel and stub
  channel_ = grpc::CreateChannel(server_address_, grpc::InsecureChannelCredentials());
  stub_ = ThrottlingService::NewStub(channel_);
}

ThrottlingClient::~ThrottlingClient() {
  if (running_.load()) {
    Stop();
  }
}

bool ThrottlingClient::Start() {
  if (running_.load()) {
    return true;  // Already running
  }

  // Register with server
  RegisterClientRequest request;
  request.set_client_id(client_id_);

  RegisterClientResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub_->RegisterClient(&context, request, &response);

  if (!status.ok()) {
    std::cerr << "Failed to register client: " << status.error_message()
              << std::endl;
    return false;
  }

  // Start threads
  running_.store(true);
  heartbeat_thread_ = std::thread(&ThrottlingClient::HeartbeatLoop, this);
  worker_thread_ = std::thread(&ThrottlingClient::WorkerLoop, this);

  // Send initial heartbeat to get allocations
  SendHeartbeat();

  return true;
}

void ThrottlingClient::Stop() {
  if (!running_.load()) {
    return;  // Already stopped
  }

  // Signal threads to stop
  running_.store(false);
  cv_.notify_all();
  queue_cv_.notify_all();

  // Wait for threads to finish
  if (heartbeat_thread_.joinable()) {
    heartbeat_thread_.join();
  }
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }

  // Unregister from server
  UnregisterClientRequest request;
  request.set_client_id(client_id_);

  UnregisterClientResponse response;
  grpc::ClientContext context;
  stub_->UnregisterClient(&context, request, &response);

  // Clear state
  {
    std::lock_guard<std::mutex> lock(mutex_);
    token_buckets_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    pending_queue_.clear();
  }
}

void ThrottlingClient::SetResourceInterests(const std::set<int64_t>& resource_ids) {
  std::lock_guard<std::mutex> lock(mutex_);
  resource_interests_ = resource_ids;
}

void ThrottlingClient::Acquire(int64_t resource_id, double count,
                               std::function<void()> callback) {
  // Try to acquire immediately
  TokenBucket* bucket = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = token_buckets_.find(resource_id);
    if (it != token_buckets_.end()) {
      bucket = it->second.get();
    }
  }

  // If bucket exists and has tokens, execute immediately
  if (bucket != nullptr && bucket->TryConsume(count)) {
    callback();
    return;
  }

  // Queue request for later execution
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    pending_queue_.push_back({resource_id, count, std::move(callback)});
  }
  queue_cv_.notify_one();
}

double ThrottlingClient::GetAllocation(int64_t resource_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = token_buckets_.find(resource_id);
  if (it == token_buckets_.end()) {
    return 0.0;
  }
  return it->second->GetRate();
}

bool ThrottlingClient::IsRunning() const {
  return running_.load();
}

size_t ThrottlingClient::GetPendingCount() const {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  return pending_queue_.size();
}

void ThrottlingClient::HeartbeatLoop() {
  while (running_.load()) {
    // Wait for heartbeat interval or stop signal
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, heartbeat_interval_, [this]() {
      return !running_.load();
    });

    if (!running_.load()) {
      break;
    }

    lock.unlock();
    SendHeartbeat();
  }
}

void ThrottlingClient::WorkerLoop() {
  while (running_.load()) {
    PendingRequest req;
    bool have_request = false;

    // Get next pending request
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait_for(lock, std::chrono::milliseconds(10), [this]() {
        return !running_.load() || !pending_queue_.empty();
      });

      if (!running_.load() && pending_queue_.empty()) {
        break;
      }

      if (!pending_queue_.empty()) {
        req = std::move(pending_queue_.front());
        pending_queue_.pop_front();
        have_request = true;
      }
    }

    if (!have_request) {
      continue;
    }

    // Try to consume tokens
    TokenBucket* bucket = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = token_buckets_.find(req.resource_id);
      if (it != token_buckets_.end()) {
        bucket = it->second.get();
      }
    }

    if (bucket != nullptr && bucket->TryConsume(req.count)) {
      // Success - execute callback
      req.callback();
    } else {
      // Re-queue request (put at front to maintain order)
      std::lock_guard<std::mutex> lock(queue_mutex_);
      pending_queue_.push_front(std::move(req));
    }
  }
}

bool ThrottlingClient::SendHeartbeat() {
  // Get current resource interests
  std::set<int64_t> interests;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    interests = resource_interests_;
  }

  // Build heartbeat request
  HeartbeatRequest request;
  request.set_client_id(client_id_);
  for (int64_t resource_id : interests) {
    request.add_resource_ids(resource_id);
  }

  // Send heartbeat
  HeartbeatResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub_->Heartbeat(&context, request, &response);

  if (!status.ok()) {
    std::cerr << "Heartbeat failed: " << status.error_message() << std::endl;
    return false;
  }

  // Update token buckets with new allocations
  std::map<int64_t, double> allocations(response.allocations().begin(),
                                        response.allocations().end());
  UpdateTokenBuckets(allocations);

  return true;
}

void ThrottlingClient::UpdateTokenBuckets(
    const std::map<int64_t, double>& allocations) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Update or create token buckets for each allocation
  for (const auto& [resource_id, rate] : allocations) {
    auto it = token_buckets_.find(resource_id);
    if (it != token_buckets_.end()) {
      // Update existing bucket's rate
      it->second->SetRate(rate);
    } else {
      // Create new bucket with rate (burst = rate = 1 second of tokens)
      token_buckets_[resource_id] = std::make_unique<TokenBucket>(rate);
    }
  }

  // Remove buckets for resources no longer allocated
  for (auto it = token_buckets_.begin(); it != token_buckets_.end();) {
    if (allocations.find(it->first) == allocations.end()) {
      it = token_buckets_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace throttling
