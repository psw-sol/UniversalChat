#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "util/RateLimiter.hpp"

using namespace chat;

class RateLimiterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Default config: bucket_size=20, refill_rate=10
    }
};

// Basic consumption test
TEST_F(RateLimiterTest, BasicConsumption) {
    RateLimiter limiter;

    // First consumption should succeed
    EXPECT_TRUE(limiter.tryConsume("session1", 1));

    // Check remaining tokens (should be 19)
    EXPECT_EQ(limiter.getTokens("session1"), 19);
}

// Consume all tokens
TEST_F(RateLimiterTest, ConsumeAllTokens) {
    RateLimiter limiter;

    // Consume all 20 tokens
    for (int i = 0; i < 20; ++i) {
        EXPECT_TRUE(limiter.tryConsume("session1", 1));
    }

    // 21st should fail (no tokens left)
    EXPECT_FALSE(limiter.tryConsume("session1", 1));
    EXPECT_EQ(limiter.getTokens("session1"), 0);
}

// Multiple sessions are independent
TEST_F(RateLimiterTest, MultipleSessions) {
    RateLimiter limiter;

    // Consume tokens from session1
    for (int i = 0; i < 15; ++i) {
        EXPECT_TRUE(limiter.tryConsume("session1", 1));
    }

    // session2 should still have full tokens
    EXPECT_EQ(limiter.getTokens("session2"), 20);  // New session gets full bucket
    EXPECT_TRUE(limiter.tryConsume("session2", 1));

    // session1 should have 5 tokens left
    EXPECT_EQ(limiter.getTokens("session1"), 5);
}

// Token refill over time
TEST_F(RateLimiterTest, TokenRefill) {
    RateLimiter limiter;

    // Consume all tokens
    for (int i = 0; i < 20; ++i) {
        limiter.tryConsume("session1", 1);
    }

    // Verify all consumed (tryConsume should fail)
    EXPECT_FALSE(limiter.tryConsume("session1", 1));

    // Wait 150ms - should refill ~1.5 tokens (refill_rate=10/sec)
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Should be able to consume at least 1 token now
    // (tryConsume triggers refill internally)
    EXPECT_TRUE(limiter.tryConsume("session1", 1));
}

// Custom configuration
TEST_F(RateLimiterTest, CustomConfig) {
    RateLimiter::Config config;
    config.bucket_size = 5;
    config.refill_rate = 2;

    RateLimiter limiter(config);

    // Consume 5 tokens (bucket_size)
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(limiter.tryConsume("session1", 1));
    }

    // 6th should fail
    EXPECT_FALSE(limiter.tryConsume("session1", 1));
}

// Cost parameter test
TEST_F(RateLimiterTest, CostParameter) {
    RateLimiter limiter;

    // Consume 5 tokens at once
    EXPECT_TRUE(limiter.tryConsume("session1", 5));
    EXPECT_EQ(limiter.getTokens("session1"), 15);

    // Consume 10 more
    EXPECT_TRUE(limiter.tryConsume("session1", 10));
    EXPECT_EQ(limiter.getTokens("session1"), 5);

    // Try to consume 10 (should fail - only 5 left)
    EXPECT_FALSE(limiter.tryConsume("session1", 10));
    EXPECT_EQ(limiter.getTokens("session1"), 5);  // Unchanged
}

// Remove session test
TEST_F(RateLimiterTest, RemoveSession) {
    RateLimiter limiter;

    // Consume some tokens
    limiter.tryConsume("session1", 10);
    EXPECT_EQ(limiter.getTokens("session1"), 10);
    EXPECT_EQ(limiter.sessionCount(), 1);

    // Remove session
    limiter.removeSession("session1");
    EXPECT_EQ(limiter.sessionCount(), 0);

    // New access should have full bucket
    EXPECT_EQ(limiter.getTokens("session1"), 20);
}

// Token cap at bucket_size (no overflow)
TEST_F(RateLimiterTest, TokenCapAtBucketSize) {
    RateLimiter limiter;

    // Consume 5 tokens
    limiter.tryConsume("session1", 5);
    EXPECT_EQ(limiter.getTokens("session1"), 15);

    // Wait long enough for more than 5 tokens to refill
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // Should not exceed bucket_size (20)
    EXPECT_LE(limiter.getTokens("session1"), 20);
}

// Concurrent access (thread safety)
TEST_F(RateLimiterTest, ConcurrentAccess) {
    RateLimiter limiter;
    std::atomic<int> success_count{0};

    // Launch multiple threads consuming from same session
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&limiter, &success_count]() {
            for (int j = 0; j < 5; ++j) {
                if (limiter.tryConsume("session1", 1)) {
                    success_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Total successful consumptions should be <= 20 (bucket_size)
    EXPECT_LE(success_count.load(), 20);
}
