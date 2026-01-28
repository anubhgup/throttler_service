// Token Bucket Rate Limiter - Implementation
//
// See token_bucket.h for the Story and algorithm description.

#include "token_bucket.h"

#include <algorithm>
#include <cassert>

namespace throttling {

TokenBucket::TokenBucket(double rate, double burst_size)
    : rate_(std::max(0.0, rate)),
      burst_size_(burst_size > 0.0 ? burst_size : rate_),
      tokens_(burst_size_),
      last_refill_time_(std::chrono::steady_clock::now()) {
  // Ensure burst_size_ is non-negative even if rate was negative
  burst_size_ = std::max(0.0, burst_size_);
  tokens_ = burst_size_;
}

bool TokenBucket::TryConsume(double count) {
  if (count <= 0.0) {
    return true;  // Consuming 0 or negative tokens always succeeds
  }

  std::lock_guard<std::mutex> lock(mutex_);
  RefillLocked();

  if (tokens_ >= count) {
    tokens_ -= count;
    return true;
  }
  return false;
}

void TokenBucket::SetRate(double new_rate) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Refill with old rate first to preserve earned tokens
  RefillLocked();

  // Apply new rate (non-negative)
  rate_ = std::max(0.0, new_rate);
}

void TokenBucket::SetBurstSize(double new_burst_size) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Refill first
  RefillLocked();

  // Apply new burst size (non-negative)
  burst_size_ = std::max(0.0, new_burst_size);

  // Cap tokens if they exceed new burst size
  tokens_ = std::min(tokens_, burst_size_);
}

double TokenBucket::GetRate() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return rate_;
}

double TokenBucket::GetBurstSize() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return burst_size_;
}

double TokenBucket::GetAvailableTokens() {
  std::lock_guard<std::mutex> lock(mutex_);
  RefillLocked();
  return tokens_;
}

void TokenBucket::RefillLocked() {
  auto now = std::chrono::steady_clock::now();

  // Calculate elapsed time in seconds
  auto elapsed = std::chrono::duration<double>(now - last_refill_time_);

  // Handle clock anomalies (should not happen with steady_clock, but be safe)
  if (elapsed.count() < 0.0) {
    last_refill_time_ = now;
    return;
  }

  // Calculate tokens to add
  double tokens_to_add = elapsed.count() * rate_;

  // Add tokens, capped at burst_size
  tokens_ = std::min(tokens_ + tokens_to_add, burst_size_);

  // Update timestamp
  last_refill_time_ = now;
}

}  // namespace throttling
