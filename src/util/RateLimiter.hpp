#pragma once

#include <chrono>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <string>

namespace chat {

/**
 * RateLimiter
 *
 * Token Bucket algorithm implementation for rate limiting.
 * Each session gets its own bucket that refills over time.
 */
class RateLimiter {
public:
    struct Config {
        int bucket_size = 20;      // Maximum tokens per bucket
        int refill_rate = 10;      // Tokens per second
    };

    explicit RateLimiter(const Config& config = Config{});
    ~RateLimiter() = default;

    // Non-copyable
    RateLimiter(const RateLimiter&) = delete;
    RateLimiter& operator=(const RateLimiter&) = delete;

    /**
     * Try to consume tokens from a session's bucket
     * @param session_id Session identifier
     * @param cost Number of tokens to consume (default: 1)
     * @return true if tokens were consumed, false if rate limited
     */
    bool tryConsume(const std::string& session_id, int cost = 1);

    /**
     * Remove a session's bucket (call on disconnect)
     * @param session_id Session identifier
     */
    void removeSession(const std::string& session_id);

    /**
     * Get current token count for a session
     * @param session_id Session identifier
     * @return Current token count
     */
    int getTokens(const std::string& session_id) const;

    /**
     * Get total number of tracked sessions
     */
    size_t sessionCount() const;

private:
    struct Bucket {
        double tokens;
        std::chrono::steady_clock::time_point last_refill;

        explicit Bucket(int initial)
            : tokens(initial)
            , last_refill(std::chrono::steady_clock::now())
        {}
    };

    void refillBucket(Bucket& bucket);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
    Config config_;
};

} // namespace chat
