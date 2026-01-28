// Token Bucket Unit Tests
//
// Tests cover:
// - Construction and initialization
// - Basic consume operations
// - Refill behavior over time
// - Dynamic rate changes
// - Dynamic burst size changes
// - Edge cases (zero rate, zero burst, negative inputs)
// - Thread safety

#include "token_bucket.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

namespace throttling {
namespace {

// Helper to sleep for a duration
void SleepMs(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// =============================================================================
// Construction Tests
// =============================================================================

TEST(TokenBucketTest, ConstructWithRateOnly) {
  TokenBucket bucket(10.0);

  EXPECT_DOUBLE_EQ(bucket.GetRate(), 10.0);
  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 10.0);  // Defaults to rate
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 10.0);  // Starts full
}

TEST(TokenBucketTest, ConstructWithRateAndBurstSize) {
  TokenBucket bucket(10.0, 20.0);

  EXPECT_DOUBLE_EQ(bucket.GetRate(), 10.0);
  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 20.0);
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 20.0);  // Starts full
}

TEST(TokenBucketTest, ConstructWithZeroRate) {
  TokenBucket bucket(0.0, 5.0);

  EXPECT_DOUBLE_EQ(bucket.GetRate(), 0.0);
  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 5.0);
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 5.0);
}

TEST(TokenBucketTest, ConstructWithNegativeRate) {
  TokenBucket bucket(-10.0, 5.0);

  EXPECT_DOUBLE_EQ(bucket.GetRate(), 0.0);  // Clamped to 0
  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 5.0);
}

TEST(TokenBucketTest, ConstructWithNegativeBurstSize) {
  TokenBucket bucket(10.0, -5.0);

  EXPECT_DOUBLE_EQ(bucket.GetRate(), 10.0);
  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 0.0);  // Clamped to 0
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 0.0);
}

// =============================================================================
// Basic Consume Tests
// =============================================================================

TEST(TokenBucketTest, ConsumeSucceedsWithAvailableTokens) {
  TokenBucket bucket(10.0, 10.0);

  EXPECT_TRUE(bucket.TryConsume(1.0));
  EXPECT_NEAR(bucket.GetAvailableTokens(), 9.0, 0.1);
}

TEST(TokenBucketTest, ConsumeDefaultsToOne) {
  TokenBucket bucket(10.0, 10.0);

  EXPECT_TRUE(bucket.TryConsume());  // Default count = 1
  EXPECT_NEAR(bucket.GetAvailableTokens(), 9.0, 0.1);
}

TEST(TokenBucketTest, ConsumeMultipleTokens) {
  TokenBucket bucket(10.0, 10.0);

  EXPECT_TRUE(bucket.TryConsume(5.0));
  EXPECT_NEAR(bucket.GetAvailableTokens(), 5.0, 0.1);
}

TEST(TokenBucketTest, ConsumeAllTokens) {
  TokenBucket bucket(10.0, 10.0);

  EXPECT_TRUE(bucket.TryConsume(10.0));
  EXPECT_NEAR(bucket.GetAvailableTokens(), 0.0, 0.1);
}

TEST(TokenBucketTest, ConsumeFailsWhenInsufficientTokens) {
  TokenBucket bucket(10.0, 10.0);

  EXPECT_FALSE(bucket.TryConsume(11.0));  // More than available
  EXPECT_NEAR(bucket.GetAvailableTokens(), 10.0, 0.1);  // Unchanged
}

TEST(TokenBucketTest, ConsumeFailsWhenEmpty) {
  TokenBucket bucket(10.0, 10.0);

  EXPECT_TRUE(bucket.TryConsume(10.0));  // Drain bucket
  EXPECT_FALSE(bucket.TryConsume(1.0));  // Fail - empty
}

TEST(TokenBucketTest, ConsumeZeroTokensAlwaysSucceeds) {
  TokenBucket bucket(10.0, 10.0);
  bucket.TryConsume(10.0);  // Drain bucket

  EXPECT_TRUE(bucket.TryConsume(0.0));  // Zero always succeeds
}

TEST(TokenBucketTest, ConsumeNegativeTokensAlwaysSucceeds) {
  TokenBucket bucket(10.0, 10.0);
  bucket.TryConsume(10.0);  // Drain bucket

  EXPECT_TRUE(bucket.TryConsume(-5.0));  // Negative always succeeds
}

// =============================================================================
// Refill Tests
// =============================================================================

TEST(TokenBucketTest, RefillOverTime) {
  TokenBucket bucket(100.0, 100.0);  // 100 tokens/sec

  bucket.TryConsume(100.0);  // Drain bucket
  EXPECT_NEAR(bucket.GetAvailableTokens(), 0.0, 1.0);

  SleepMs(50);  // Wait 50ms = 5 tokens expected

  double tokens = bucket.GetAvailableTokens();
  EXPECT_GE(tokens, 4.0);   // At least 4 tokens (accounting for timing)
  EXPECT_LE(tokens, 10.0);  // Not more than 10 (with some slack)
}

TEST(TokenBucketTest, RefillCappedAtBurstSize) {
  TokenBucket bucket(100.0, 10.0);  // 100 tokens/sec, max 10

  bucket.TryConsume(10.0);  // Drain bucket

  SleepMs(200);  // Wait 200ms = 20 tokens would be added, but capped at 10

  EXPECT_NEAR(bucket.GetAvailableTokens(), 10.0, 0.5);  // Capped at burst
}

TEST(TokenBucketTest, NoRefillWithZeroRate) {
  TokenBucket bucket(0.0, 10.0);  // Zero rate

  bucket.TryConsume(10.0);  // Drain bucket

  SleepMs(100);  // Wait - no refill should happen

  EXPECT_NEAR(bucket.GetAvailableTokens(), 0.0, 0.1);  // Still empty
}

// =============================================================================
// SetRate Tests
// =============================================================================

TEST(TokenBucketTest, SetRateUpdatesRate) {
  TokenBucket bucket(10.0, 10.0);

  bucket.SetRate(20.0);

  EXPECT_DOUBLE_EQ(bucket.GetRate(), 20.0);
}

TEST(TokenBucketTest, SetRatePreservesTokens) {
  TokenBucket bucket(10.0, 20.0);

  bucket.TryConsume(5.0);  // 15 tokens left
  bucket.SetRate(100.0);

  // Tokens should be preserved (approximately, with small refill)
  EXPECT_GE(bucket.GetAvailableTokens(), 14.0);
  EXPECT_LE(bucket.GetAvailableTokens(), 20.0);
}

TEST(TokenBucketTest, SetRateToZero) {
  TokenBucket bucket(10.0, 10.0);

  bucket.TryConsume(5.0);  // 5 tokens left
  bucket.SetRate(0.0);

  SleepMs(100);  // Wait - no refill with zero rate

  EXPECT_NEAR(bucket.GetAvailableTokens(), 5.0, 0.5);  // Preserved, no refill
}

TEST(TokenBucketTest, SetRateNegativeClampsToZero) {
  TokenBucket bucket(10.0, 10.0);

  bucket.SetRate(-50.0);

  EXPECT_DOUBLE_EQ(bucket.GetRate(), 0.0);
}

TEST(TokenBucketTest, SetRateAffectsRefill) {
  TokenBucket bucket(10.0, 100.0);

  bucket.TryConsume(100.0);  // Drain
  bucket.SetRate(200.0);     // 200 tokens/sec

  SleepMs(50);  // 50ms = 10 tokens at new rate

  double tokens = bucket.GetAvailableTokens();
  EXPECT_GE(tokens, 8.0);
  EXPECT_LE(tokens, 15.0);
}

// =============================================================================
// SetBurstSize Tests
// =============================================================================

TEST(TokenBucketTest, SetBurstSizeUpdates) {
  TokenBucket bucket(10.0, 10.0);

  bucket.SetBurstSize(20.0);

  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 20.0);
}

TEST(TokenBucketTest, SetBurstSizeCapsTokens) {
  TokenBucket bucket(10.0, 20.0);  // 20 tokens available

  bucket.SetBurstSize(5.0);  // Cap to 5

  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 5.0);
  EXPECT_NEAR(bucket.GetAvailableTokens(), 5.0, 0.1);  // Capped
}

TEST(TokenBucketTest, SetBurstSizeIncrease) {
  TokenBucket bucket(10.0, 10.0);

  bucket.TryConsume(5.0);   // 5 tokens
  bucket.SetBurstSize(20.0);

  // Tokens stay at ~5 (don't automatically fill up)
  EXPECT_GE(bucket.GetAvailableTokens(), 4.5);
  EXPECT_LE(bucket.GetAvailableTokens(), 6.0);
}

TEST(TokenBucketTest, SetBurstSizeToZero) {
  TokenBucket bucket(10.0, 10.0);

  bucket.SetBurstSize(0.0);

  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 0.0);
  EXPECT_DOUBLE_EQ(bucket.GetAvailableTokens(), 0.0);
  EXPECT_FALSE(bucket.TryConsume(1.0));  // Always fails
}

TEST(TokenBucketTest, SetBurstSizeNegativeClampsToZero) {
  TokenBucket bucket(10.0, 10.0);

  bucket.SetBurstSize(-10.0);

  EXPECT_DOUBLE_EQ(bucket.GetBurstSize(), 0.0);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST(TokenBucketTest, ZeroBurstSizeRejectsAll) {
  TokenBucket bucket(10.0, 0.0);

  EXPECT_FALSE(bucket.TryConsume(1.0));
  EXPECT_FALSE(bucket.TryConsume(0.001));
}

TEST(TokenBucketTest, FractionalTokens) {
  TokenBucket bucket(1.0, 1.0);  // 1 token/sec

  EXPECT_TRUE(bucket.TryConsume(0.5));
  EXPECT_TRUE(bucket.TryConsume(0.25));
  EXPECT_TRUE(bucket.TryConsume(0.25));
  EXPECT_FALSE(bucket.TryConsume(0.01));  // Should fail, ~0 tokens left
}

TEST(TokenBucketTest, FractionalRate) {
  TokenBucket bucket(0.5, 1.0);  // 0.5 tokens/sec = 1 token per 2 seconds

  bucket.TryConsume(1.0);  // Drain

  SleepMs(100);  // 0.1 sec = 0.05 tokens

  EXPECT_LT(bucket.GetAvailableTokens(), 0.1);
}

TEST(TokenBucketTest, RapidConsumeThenWait) {
  TokenBucket bucket(10.0, 10.0);

  // Rapid consume - all should succeed
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(bucket.TryConsume());
  }

  // Next should fail
  EXPECT_FALSE(bucket.TryConsume());

  // Wait for refill
  SleepMs(150);  // ~1.5 tokens

  EXPECT_TRUE(bucket.TryConsume());
}

// =============================================================================
// Thread Safety Tests
// =============================================================================

TEST(TokenBucketTest, ConcurrentConsume) {
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
  EXPECT_GE(success_count.load(), 990);
  EXPECT_LE(success_count.load(), 1010);
}

TEST(TokenBucketTest, ConcurrentSetRateAndConsume) {
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
  SleepMs(10);
  stop.store(true);
  consumer.join();

  // Just verify no crashes and some consumes happened
  EXPECT_GT(consume_count.load(), 0);
}

}  // namespace
}  // namespace throttling
