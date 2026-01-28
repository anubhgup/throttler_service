// Token Bucket Rate Limiter
//
// Story:
// This module implements the token bucket algorithm for rate limiting. The bucket
// refills at a constant rate and allows bursts up to burst_size. Rate and burst
// size can be changed dynamically to support rebalancing when clients join/leave.
//
// Algorithm:
// 1. Bucket starts full (tokens = burst_size)
// 2. On each TryConsume: refill based on elapsed time, then consume if possible
// 3. SetRate/SetBurstSize: refill first with old values, then apply new values
//
// Thread Safety:
// All public methods are thread-safe (protected by mutex).

#pragma once

#include <chrono>
#include <mutex>

namespace throttling {

/// Thread-safe token bucket rate limiter.
///
/// The bucket refills at a constant rate and allows bursts up to burst_size.
/// Rate and burst size can be changed dynamically.
///
/// Example:
///   TokenBucket bucket(10.0);  // 10 tokens/sec, burst=10
///   if (bucket.TryConsume()) {
///     // Request allowed
///   } else {
///     // Request denied (rate limited)
///   }
class TokenBucket {
 public:
  /// Constructs a token bucket with the given rate and burst size.
  ///
  /// @param rate Tokens added per second. Must be >= 0.
  /// @param burst_size Maximum tokens the bucket can hold. If <= 0, defaults to rate.
  ///
  /// The bucket starts full (tokens = burst_size).
  explicit TokenBucket(double rate, double burst_size = 0.0);

  // Non-copyable, non-movable (due to mutex)
  TokenBucket(const TokenBucket&) = delete;
  TokenBucket& operator=(const TokenBucket&) = delete;
  TokenBucket(TokenBucket&&) = delete;
  TokenBucket& operator=(TokenBucket&&) = delete;

  ~TokenBucket() = default;

  /// Attempts to consume the specified number of tokens.
  ///
  /// @param count Number of tokens to consume. Defaults to 1.
  /// @return true if tokens were consumed, false if insufficient tokens.
  bool TryConsume(double count = 1.0);

  /// Updates the rate (tokens per second).
  ///
  /// Refills with the old rate first, then applies the new rate.
  /// This ensures tokens earned at the old rate are preserved.
  ///
  /// @param new_rate New tokens per second. Must be >= 0.
  void SetRate(double new_rate);

  /// Updates the burst size (maximum tokens).
  ///
  /// If current tokens exceed new burst size, tokens are capped.
  ///
  /// @param new_burst_size New maximum tokens. Must be >= 0.
  void SetBurstSize(double new_burst_size);

  /// Returns the current rate (tokens per second).
  double GetRate() const;

  /// Returns the current burst size.
  double GetBurstSize() const;

  /// Returns the current available tokens (after refill).
  ///
  /// Useful for monitoring/debugging. Note that this triggers a refill.
  double GetAvailableTokens();

 private:
  /// Refills the bucket based on elapsed time.
  /// Must be called with mutex_ held.
  void RefillLocked();

  // Configuration
  double rate_;        // Tokens per second
  double burst_size_;  // Maximum tokens

  // Runtime state
  double tokens_;  // Current available tokens
  std::chrono::steady_clock::time_point last_refill_time_;

  // Synchronization
  mutable std::mutex mutex_;
};

}  // namespace throttling
