// Token Bucket Unit Tests
//
// Tests cover:
// - Construction and initialization
// - Basic consume operations
// - Refill behavior over time (using FakeClock)
// - Dynamic rate changes
// - Dynamic burst size changes
// - Edge cases (zero rate, zero burst, negative inputs)
// - Thread safety

#include "token_bucket.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace throttling {
namespace {

using namespace std::chrono_literals;

// =============================================================================
// Test Fixture with FakeClock
// =============================================================================

class TokenBucketTest : public ::testing::Test {
 protected:
  void SetUp() override {
    clock_ = std::make_shared<FakeClock>();
  }

  std::shared_ptr<FakeClock> clock_;
};

// =============================================================================
// Construction Tests
// =============================================================================

TEST_F(TokenBucketTest, ConstructWithRateOnly) {
  TokenBucket bucket(10.0, -1.0, clock_);

  EXPECT_DOUBLE_EQ(bucket.GetRate(), 10.0);
  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 10.0);  // Defaults to rate
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 10.0);  // Starts full
}

TEST_F(TokenBucketTest, ConstructWithRateAndBurstSize) {
  TokenBucket bucket(10.0, 20.0, clock_);

  EXPECT_DOUBLE_EQ(bucket.GetRate(), 10.0);
  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 20.0);
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 20.0);  // Starts full
}

TEST_F(TokenBucketTest, ConstructWithZeroRate) {
  TokenBucket bucket(0.0, 5.0, clock_);

  EXPECT_DOUBLE_EQ(bucket.GetRate(), 0.0);
  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 5.0);
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 5.0);
}

TEST_F(TokenBucketTest, ConstructWithNegativeRate) {
  TokenBucket bucket(-10.0, 5.0, clock_);

  EXPECT_DOUBLE_EQ(bucket.GetRate(), 0.0);  // Clamped to 0
  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 5.0);
}

TEST_F(TokenBucketTest, ConstructWithNegativeBurstSize) {
  TokenBucket bucket(10.0, -5.0, clock_);

  // Negative burst_size means "use rate as default"
  EXPECT_DOUBLE_EQ(bucket.GetRate(), 10.0);
  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 10.0);
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 10.0);
}

TEST_F(TokenBucketTest, ConstructWithZeroBurstSize) {
  TokenBucket bucket(10.0, 0.0, clock_);

  EXPECT_DOUBLE_EQ(bucket.GetRate(), 10.0);
  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 0.0);
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 0.0);
}

TEST_F(TokenBucketTest, ConstructWithNullClockUsesRealClock) {
  // Just verify it doesn't crash and works
  TokenBucket bucket(10.0);
  EXPECT_TRUE(bucket.TryConsume(1.0));
}

// =============================================================================
// Basic Consume Tests
// =============================================================================

TEST_F(TokenBucketTest, ConsumeSucceedsWithAvailableTokens) {
  TokenBucket bucket(10.0, 10.0, clock_);

  EXPECT_TRUE(bucket.TryConsume(1.0));
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 9.0);
}

TEST_F(TokenBucketTest, ConsumeDefaultsToOne) {
  TokenBucket bucket(10.0, 10.0, clock_);

  EXPECT_TRUE(bucket.TryConsume());  // Default count = 1
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 9.0);
}

TEST_F(TokenBucketTest, ConsumeMultipleTokens) {
  TokenBucket bucket(10.0, 10.0, clock_);

  EXPECT_TRUE(bucket.TryConsume(5.0));
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 5.0);
}

TEST_F(TokenBucketTest, ConsumeAllTokens) {
  TokenBucket bucket(10.0, 10.0, clock_);

  EXPECT_TRUE(bucket.TryConsume(10.0));
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 0.0);
}

TEST_F(TokenBucketTest, ConsumeFailsWhenInsufficientTokens) {
  TokenBucket bucket(10.0, 10.0, clock_);

  EXPECT_FALSE(bucket.TryConsume(11.0));  // More than available
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 10.0);  // Unchanged
}

TEST_F(TokenBucketTest, ConsumeFailsWhenEmpty) {
  TokenBucket bucket(10.0, 10.0, clock_);

  EXPECT_TRUE(bucket.TryConsume(10.0));  // Drain bucket
  EXPECT_FALSE(bucket.TryConsume(1.0));  // Fail - empty
}

TEST_F(TokenBucketTest, ConsumeZeroTokensAlwaysSucceeds) {
  TokenBucket bucket(10.0, 10.0, clock_);
  bucket.TryConsume(10.0);  // Drain bucket

  EXPECT_TRUE(bucket.TryConsume(0.0));  // Zero always succeeds
}

TEST_F(TokenBucketTest, ConsumeNegativeTokensAlwaysSucceeds) {
  TokenBucket bucket(10.0, 10.0, clock_);
  bucket.TryConsume(10.0);  // Drain bucket

  EXPECT_TRUE(bucket.TryConsume(-5.0));  // Negative always succeeds
}

// =============================================================================
// Refill Tests (using FakeClock - no sleep!)
// =============================================================================

TEST_F(TokenBucketTest, RefillOverTime) {
  TokenBucket bucket(100.0, 100.0, clock_);  // 100 tokens/sec

  bucket.TryConsume(100.0);  // Drain bucket
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 0.0);

  clock_->Advance(50ms);  // 50ms = 5 tokens

  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 5.0);
}

TEST_F(TokenBucketTest, RefillExactlyOneSecond) {
  TokenBucket bucket(10.0, 10.0, clock_);

  bucket.TryConsume(10.0);  // Drain bucket

  clock_->Advance(1s);  // 1 second = 10 tokens

  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 10.0);
}

TEST_F(TokenBucketTest, RefillCappedAtBurstSize) {
  TokenBucket bucket(100.0, 10.0, clock_);  // 100 tokens/sec, max 10

  bucket.TryConsume(10.0);  // Drain bucket

  clock_->Advance(200ms);  // 200ms = 20 tokens would be added, but capped at 10

  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 10.0);  // Capped at burst
}

TEST_F(TokenBucketTest, NoRefillWithZeroRate) {
  TokenBucket bucket(0.0, 10.0, clock_);  // Zero rate

  bucket.TryConsume(10.0);  // Drain bucket

  clock_->Advance(1s);  // Wait - no refill should happen

  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 0.0);  // Still empty
}

TEST_F(TokenBucketTest, PartialRefill) {
  TokenBucket bucket(10.0, 10.0, clock_);

  bucket.TryConsume(5.0);  // 5 tokens left

  clock_->Advance(200ms);  // 0.2s * 10 = 2 tokens

  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 7.0);
}

TEST_F(TokenBucketTest, RefillWithFractionalRate) {
  TokenBucket bucket(0.5, 1.0, clock_);  // 0.5 tokens/sec

  bucket.TryConsume(1.0);  // Drain

  clock_->Advance(1s);  // 1 sec = 0.5 tokens

  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 0.5);
}

// =============================================================================
// SetRate Tests
// =============================================================================

TEST_F(TokenBucketTest, SetRateUpdatesRate) {
  TokenBucket bucket(10.0, 10.0, clock_);

  bucket.SetRate(20.0);

  EXPECT_DOUBLE_EQ(bucket.GetRate(), 20.0);
}

TEST_F(TokenBucketTest, SetRatePreservesTokens) {
  TokenBucket bucket(10.0, 20.0, clock_);

  bucket.TryConsume(5.0);  // 15 tokens left
  bucket.SetRate(100.0);

  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 15.0);
}

TEST_F(TokenBucketTest, SetRateToZero) {
  TokenBucket bucket(10.0, 10.0, clock_);

  bucket.TryConsume(5.0);  // 5 tokens left
  bucket.SetRate(0.0);

  clock_->Advance(1s);  // Wait - no refill with zero rate

  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 5.0);  // Preserved, no refill
}

TEST_F(TokenBucketTest, SetRateNegativeClampsToZero) {
  TokenBucket bucket(10.0, 10.0, clock_);

  bucket.SetRate(-50.0);

  EXPECT_DOUBLE_EQ(bucket.GetRate(), 0.0);
}

TEST_F(TokenBucketTest, SetRateAffectsRefill) {
  TokenBucket bucket(10.0, 100.0, clock_);

  bucket.TryConsume(100.0);  // Drain
  bucket.SetRate(200.0);     // 200 tokens/sec

  clock_->Advance(50ms);  // 50ms = 10 tokens at new rate

  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 10.0);
}

TEST_F(TokenBucketTest, SetRateRefillsWithOldRateFirst) {
  TokenBucket bucket(10.0, 100.0, clock_);

  bucket.TryConsume(100.0);  // Drain: 0 tokens

  clock_->Advance(500ms);  // 0.5s at 10 tokens/sec = 5 tokens earned

  bucket.SetRate(100.0);  // Change rate (refills first with old rate)

  // Should have 5 tokens from old rate
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 5.0);

  clock_->Advance(100ms);  // 0.1s at new rate (100/sec) = 10 more tokens

  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 15.0);
}

// =============================================================================
// SetBurstSize Tests
// =============================================================================

TEST_F(TokenBucketTest, SetBurstSizeUpdates) {
  TokenBucket bucket(10.0, 10.0, clock_);

  bucket.SetBurstSize(20.0);

  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 20.0);
}

TEST_F(TokenBucketTest, SetBurstSizeCapsTokens) {
  TokenBucket bucket(10.0, 20.0, clock_);  // 20 tokens available

  bucket.SetBurstSize(5.0);  // Cap to 5

  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 5.0);
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 5.0);  // Capped
}

TEST_F(TokenBucketTest, SetBurstSizeIncrease) {
  TokenBucket bucket(10.0, 10.0, clock_);

  bucket.TryConsume(5.0);   // 5 tokens
  bucket.SetBurstSize(20.0);

  // Tokens stay at 5 (don't automatically fill up)
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 5.0);
}

TEST_F(TokenBucketTest, SetBurstSizeToZero) {
  TokenBucket bucket(10.0, 10.0, clock_);

  bucket.SetBurstSize(0.0);

  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 0.0);
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 0.0);
  EXPECT_FALSE(bucket.TryConsume(1.0));  // Always fails
}

TEST_F(TokenBucketTest, SetBurstSizeNegativeClampsToZero) {
  TokenBucket bucket(10.0, 10.0, clock_);

  bucket.SetBurstSize(-10.0);

  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 0.0);
}

TEST_F(TokenBucketTest, SetBurstSizeAffectsRefillCap) {
  TokenBucket bucket(100.0, 10.0, clock_);

  bucket.TryConsume(10.0);  // Drain

  bucket.SetBurstSize(5.0);  // Lower cap

  clock_->Advance(1s);  // Would add 100 tokens, but cap is 5

  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 5.0);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(TokenBucketTest, ZeroBurstSizeRejectsAll) {
  TokenBucket bucket(10.0, 0.0, clock_);

  EXPECT_FALSE(bucket.TryConsume(1.0));
  EXPECT_FALSE(bucket.TryConsume(0.001));
}

TEST_F(TokenBucketTest, FractionalTokens) {
  TokenBucket bucket(1.0, 1.0, clock_);  // 1 token/sec

  EXPECT_TRUE(bucket.TryConsume(0.5));
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 0.5);

  EXPECT_TRUE(bucket.TryConsume(0.25));
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 0.25);

  EXPECT_TRUE(bucket.TryConsume(0.25));
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 0.0);

  EXPECT_FALSE(bucket.TryConsume(0.01));  // Should fail, 0 tokens left
}

TEST_F(TokenBucketTest, RapidConsumeThenRefill) {
  TokenBucket bucket(10.0, 10.0, clock_);

  // Rapid consume - all should succeed
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(bucket.TryConsume());
  }

  // Next should fail
  EXPECT_FALSE(bucket.TryConsume());

  // Advance time for refill
  clock_->Advance(150ms);  // 1.5 tokens

  EXPECT_TRUE(bucket.TryConsume());
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 0.5);
}

TEST_F(TokenBucketTest, VeryLongTimeElapsed) {
  TokenBucket bucket(10.0, 10.0, clock_);

  bucket.TryConsume(10.0);  // Drain

  clock_->Advance(std::chrono::hours(24));  // 24 hours

  // Should be capped at burst_size, not overflow
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 10.0);
}

// =============================================================================
// Thread Safety Tests
// =============================================================================

TEST_F(TokenBucketTest, ConcurrentConsume) {
  // Use real clock for concurrency test
  TokenBucket bucket(1000.0, 1000.0);  // 1000 tokens

  std::atomic<int> success_count{0};
  std::vector<std::thread> threads;

  // 10 threads, each trying to consume 100 times
  for (int t = 0; t < 10; ++t) {
    threads.emplace_back([&bucket, &success_count]() {
      for (int i = 0; i < 100; ++i) {
        if (bucket.TryConsume(1.0)) {
          success_count.fetch_add(1);
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Exactly 1000 should succeed (bucket started with 1000 tokens)
  // Allow small variance due to refill during test
  EXPECT_GE(success_count.load(), 1000);
  EXPECT_LE(success_count.load(), 1010);
}

TEST_F(TokenBucketTest, ConcurrentSetRateAndConsume) {
  // Use real clock for concurrency test
  TokenBucket bucket(100.0, 100.0);

  std::atomic<bool> stop{false};
  std::atomic<int> consume_count{0};

  // Consumer thread
  std::thread consumer([&]() {
    while (!stop.load()) {
      if (bucket.TryConsume()) {
        consume_count.fetch_add(1);
      }
      std::this_thread::yield();
    }
  });

  // Rate changer thread
  std::thread rate_changer([&]() {
    for (int i = 0; i < 100; ++i) {
      bucket.SetRate(50.0 + (i % 100));
      std::this_thread::yield();
    }
  });

  rate_changer.join();
  stop.store(true);
  consumer.join();

  // Just verify no crashes and some consumes happened
  EXPECT_GT(consume_count.load(), 0);
}

TEST_F(TokenBucketTest, ConcurrentAccessToFakeClock) {
  // Verify FakeClock is also thread-safe
  std::vector<std::thread> threads;

  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([this]() {
      for (int i = 0; i < 100; ++i) {
        clock_->Now();
        clock_->Advance(1ms);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Just verify no crashes - time should have advanced ~400ms total
  // (not exact due to race conditions, but that's okay for this test)
}

}  // namespace
}  // namespace throttling
