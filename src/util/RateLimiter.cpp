#include "RateLimiter.hpp"
#include "Logger.hpp"

namespace chat {

RateLimiter::RateLimiter(const Config& config)
    : config_(config)
{
    LOG_INFO("RateLimiter initialized (bucket={}, rate={}/sec)",
             config_.bucket_size, config_.refill_rate);
}

bool RateLimiter::tryConsume(const std::string& session_id, int cost) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = buckets_.find(session_id);
    if (it == buckets_.end()) {
        auto [inserted_it, success] = buckets_.emplace(
            session_id, Bucket(config_.bucket_size));
        it = inserted_it;
    }

    refillBucket(it->second);

    if (it->second.tokens >= cost) {
        it->second.tokens -= cost;
        return true;
    }

    LOG_DEBUG("Rate limited session {}", session_id);
    return false;
}

void RateLimiter::refillBucket(Bucket& bucket) {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(
        now - bucket.last_refill).count();

    bucket.tokens = std::min(
        static_cast<double>(config_.bucket_size),
        bucket.tokens + elapsed * config_.refill_rate
    );
    bucket.last_refill = now;
}

void RateLimiter::removeSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    buckets_.erase(session_id);
}

int RateLimiter::getTokens(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buckets_.find(session_id);
    return it != buckets_.end()
           ? static_cast<int>(it->second.tokens)
           : config_.bucket_size;
}

size_t RateLimiter::sessionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buckets_.size();
}

} // namespace chat
