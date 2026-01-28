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
//
// Testability:
// A Clock interface is injected for deterministic testing without sleeps.

#pragma once

#include <chrono>
#include <memory>
#include <mutex>

namespace throttling {

// =============================================================================
// Clock Interface
// =============================================================================

/// Abstract interface for obtaining the current time.
/// Allows dependency injection for testing.
class Clock {
 public:
  virtual ~Clock() = default;

  /// Returns the current time point.
  virtual std::chrono::steady_clock::time_point Now() const = 0;
};

/// Real clock implementation using std::chrono::steady_clock.
/// Use this in production code.
class RealClock : public Clock {
 public:
  std::chrono::steady_clock::time_point Now() const override {
    return std::chrono::steady_clock::now();
  }
};

/// Fake clock for testing. Time is manually controlled via Advance().
/// Starts at time_point{} (epoch).
class FakeClock : public Clock {
 public:
  std::chrono::steady_clock::time_point Now() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_time_;
  }

  /// Advances the clock by the specified duration.
  void Advance(std::chrono::steady_clock::duration duration) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_time_ += duration;
  }

  /// Sets the clock to a specific time point.
  void SetTime(std::chrono::steady_clock::time_point time) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_time_ = time;
  }

 private:
  mutable std::mutex mutex_;
  std::chrono::steady_clock::time_point current_time_{};
};

// =============================================================================
// Token Bucket
// =============================================================================

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
  /// @param burst_size Maximum tokens the bucket can hold.
  ///                   If < 0, defaults to rate. If 0, bucket starts empty and stays empty.
  /// @param clock Clock implementation for time. If nullptr, uses RealClock.
  ///
  /// The bucket starts full (tokens = burst_size).
  explicit TokenBucket(double rate, double burst_size = -1.0,
                       std::shared_ptr<Clock> clock = nullptr);

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

  // Clock
  std::shared_ptr<Clock> clock_;

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
