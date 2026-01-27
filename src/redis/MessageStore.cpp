#include "MessageStore.hpp"
#include "../util/Logger.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>

#ifdef ENABLE_REDIS
#include "RedisClient.hpp"
#endif

namespace chat {

// ==========================================
// InMemoryMessageStore Implementation
// ==========================================

InMemoryMessageStore::InMemoryMessageStore(int max_messages_per_channel)
    : max_messages_per_channel_(max_messages_per_channel)
{
    LOG_INFO("InMemoryMessageStore initialized (max_per_channel={})", max_messages_per_channel_);
}

bool InMemoryMessageStore::saveMessage(const std::string& channel_id, const ChatMessage& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto& channel_messages = channels_[channel_id];
    channel_messages.push_back(message);

    // Trim if exceeds max
    while (static_cast<int>(channel_messages.size()) > max_messages_per_channel_) {
        channel_messages.pop_front();
    }

    return true;
}

std::vector<ChatMessage> InMemoryMessageStore::getRecentMessages(
    const std::string& channel_id,
    int count,
    bool reverse
) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = channels_.find(channel_id);
    if (it == channels_.end()) {
        return {};
    }

    const auto& messages = it->second;
    int actual_count = std::min(count, static_cast<int>(messages.size()));

    std::vector<ChatMessage> result;
    result.reserve(actual_count);

    if (reverse) {
        // Most recent first
        auto start = messages.rbegin();
        for (int i = 0; i < actual_count && start != messages.rend(); ++i, ++start) {
            result.push_back(*start);
        }
    } else {
        // Oldest first (last N messages)
        auto start = messages.end() - actual_count;
        for (auto iter = start; iter != messages.end(); ++iter) {
            result.push_back(*iter);
        }
    }

    return result;
}

std::vector<ChatMessage> InMemoryMessageStore::getMessagesByTimeRange(
    const std::string& channel_id,
    int64_t start_timestamp,
    int64_t end_timestamp,
    int limit
) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = channels_.find(channel_id);
    if (it == channels_.end()) {
        return {};
    }

    std::vector<ChatMessage> result;
    const auto& messages = it->second;

    for (const auto& msg : messages) {
        if (msg.timestamp >= start_timestamp && msg.timestamp <= end_timestamp) {
            result.push_back(msg);
            if (static_cast<int>(result.size()) >= limit) {
                break;
            }
        }
    }

    return result;
}

int64_t InMemoryMessageStore::getMessageCount(const std::string& channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = channels_.find(channel_id);
    return it != channels_.end() ? static_cast<int64_t>(it->second.size()) : 0;
}

bool InMemoryMessageStore::clearChannel(const std::string& channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    channels_.erase(channel_id);
    return true;
}

#ifdef ENABLE_REDIS

// ==========================================
// RedisMessageStore Implementation
// ==========================================

RedisMessageStore::RedisMessageStore(std::shared_ptr<RedisClient> redis, const Config& config)
    : redis_(std::move(redis))
    , config_(config)
{
    LOG_INFO("RedisMessageStore initialized (prefix={}, max={}, ttl={}s)",
             config_.key_prefix, config_.max_messages_per_channel, config_.message_ttl_seconds);
}

std::string RedisMessageStore::makeKey(const std::string& channel_id) const {
    return config_.key_prefix + channel_id;
}

std::string RedisMessageStore::serializeMessage(const ChatMessage& message) const {
    nlohmann::json j;
    j["message_id"] = message.message_id;
    j["channel_id"] = message.channel_id;
    j["sender_id"] = message.sender_id;
    j["sender_nickname"] = message.sender_nickname;
    j["sender_profile_image"] = message.sender_profile_image;
    j["sender_frame_image"] = message.sender_frame_image;
    j["sender_extra_data"] = message.sender_extra_data;
    j["content"] = message.content;
    j["timestamp"] = message.timestamp;
    j["message_type"] = message.message_type;
    return j.dump();
}

ChatMessage RedisMessageStore::deserializeMessage(const std::string& json) const {
    ChatMessage message;
    try {
        auto j = nlohmann::json::parse(json);
        message.message_id = j.value("message_id", "");
        message.channel_id = j.value("channel_id", "");
        message.sender_id = j.value("sender_id", "");
        message.sender_nickname = j.value("sender_nickname", "");
        message.sender_profile_image = j.value("sender_profile_image", "");
        message.sender_frame_image = j.value("sender_frame_image", "");
        message.sender_extra_data = j.value("sender_extra_data", "");
        message.content = j.value("content", "");
        message.timestamp = j.value("timestamp", 0LL);
        message.message_type = j.value("message_type", 0);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to deserialize message: {}", e.what());
    }
    return message;
}

bool RedisMessageStore::saveMessage(const std::string& channel_id, const ChatMessage& message) {
    if (!redis_ || !redis_->isConnected()) {
        return false;
    }

    std::string key = makeKey(channel_id);
    std::string json = serializeMessage(message);

    // Use timestamp as score for sorted set
    bool success = redis_->zadd(key, static_cast<double>(message.timestamp), json);

    if (success) {
        // Cleanup old messages if enabled
        if (config_.auto_cleanup) {
            cleanupOldMessages(channel_id);
        }

        // Set TTL on the key
        if (config_.message_ttl_seconds > 0) {
            redis_->expire(key, config_.message_ttl_seconds);
        }
    }

    return success;
}

std::vector<ChatMessage> RedisMessageStore::getRecentMessages(
    const std::string& channel_id,
    int count,
    bool reverse
) {
    if (!redis_ || !redis_->isConnected()) {
        return {};
    }

    std::string key = makeKey(channel_id);
    std::vector<std::string> json_messages;

    if (reverse) {
        // Most recent first (highest scores first)
        json_messages = redis_->zrevrange(key, 0, count - 1);
    } else {
        // Oldest first among recent messages
        int64_t total = redis_->zcard(key);
        int start = std::max(0LL, total - count);
        json_messages = redis_->zrange(key, static_cast<int>(start), -1);
    }

    std::vector<ChatMessage> result;
    result.reserve(json_messages.size());

    for (const auto& json : json_messages) {
        result.push_back(deserializeMessage(json));
    }

    return result;
}

std::vector<ChatMessage> RedisMessageStore::getMessagesByTimeRange(
    const std::string& channel_id,
    int64_t start_timestamp,
    int64_t end_timestamp,
    int limit
) {
    if (!redis_ || !redis_->isConnected()) {
        return {};
    }

    // Note: This would require ZRANGEBYSCORE which is not in our simple RedisClient
    // For now, fetch all and filter
    std::string key = makeKey(channel_id);
    auto all_messages = redis_->zrange(key, 0, -1);

    std::vector<ChatMessage> result;
    for (const auto& json : all_messages) {
        ChatMessage msg = deserializeMessage(json);
        if (msg.timestamp >= start_timestamp && msg.timestamp <= end_timestamp) {
            result.push_back(msg);
            if (static_cast<int>(result.size()) >= limit) {
                break;
            }
        }
    }

    return result;
}

int64_t RedisMessageStore::getMessageCount(const std::string& channel_id) {
    if (!redis_ || !redis_->isConnected()) {
        return 0;
    }

    return redis_->zcard(makeKey(channel_id));
}

bool RedisMessageStore::clearChannel(const std::string& channel_id) {
    if (!redis_ || !redis_->isConnected()) {
        return false;
    }

    return redis_->del(makeKey(channel_id));
}

bool RedisMessageStore::isAvailable() const {
    return redis_ && redis_->isConnected();
}

void RedisMessageStore::cleanupOldMessages(const std::string& channel_id) {
    if (!redis_ || !redis_->isConnected()) {
        return;
    }

    std::string key = makeKey(channel_id);
    int64_t count = redis_->zcard(key);

    if (count > config_.max_messages_per_channel) {
        // Remove oldest messages (lowest scores)
        int to_remove = static_cast<int>(count - config_.max_messages_per_channel);
        redis_->zremrangebyrank(key, 0, to_remove - 1);
    }
}

// ==========================================
// HybridMessageStore Implementation
// ==========================================

HybridMessageStore::HybridMessageStore(
    std::shared_ptr<RedisClient> redis,
    const RedisMessageStore::Config& redis_config,
    int memory_max_messages
) {
    if (redis) {
        redis_store_ = std::make_unique<RedisMessageStore>(redis, redis_config);
    }
    memory_store_ = std::make_unique<InMemoryMessageStore>(memory_max_messages);

    LOG_INFO("HybridMessageStore initialized (redis={}, memory_fallback=true)",
             redis_store_ ? "enabled" : "disabled");
}

bool HybridMessageStore::saveMessage(const std::string& channel_id, const ChatMessage& message) {
    bool saved_to_redis = false;

    // Try Redis first
    if (redis_store_ && redis_store_->isAvailable()) {
        saved_to_redis = redis_store_->saveMessage(channel_id, message);
    }

    // Always save to memory as well (for fast access and fallback)
    memory_store_->saveMessage(channel_id, message);

    return saved_to_redis || true;  // Success if saved to at least memory
}

std::vector<ChatMessage> HybridMessageStore::getRecentMessages(
    const std::string& channel_id,
    int count,
    bool reverse
) {
    // Try Redis first
    if (redis_store_ && redis_store_->isAvailable()) {
        auto messages = redis_store_->getRecentMessages(channel_id, count, reverse);
        if (!messages.empty()) {
            return messages;
        }
    }

    // Fallback to memory
    return memory_store_->getRecentMessages(channel_id, count, reverse);
}

std::vector<ChatMessage> HybridMessageStore::getMessagesByTimeRange(
    const std::string& channel_id,
    int64_t start_timestamp,
    int64_t end_timestamp,
    int limit
) {
    // Try Redis first
    if (redis_store_ && redis_store_->isAvailable()) {
        auto messages = redis_store_->getMessagesByTimeRange(
            channel_id, start_timestamp, end_timestamp, limit);
        if (!messages.empty()) {
            return messages;
        }
    }

    // Fallback to memory
    return memory_store_->getMessagesByTimeRange(
        channel_id, start_timestamp, end_timestamp, limit);
}

int64_t HybridMessageStore::getMessageCount(const std::string& channel_id) {
    // Prefer Redis count as it has more history
    if (redis_store_ && redis_store_->isAvailable()) {
        return redis_store_->getMessageCount(channel_id);
    }

    return memory_store_->getMessageCount(channel_id);
}

bool HybridMessageStore::clearChannel(const std::string& channel_id) {
    bool redis_cleared = true;
    bool memory_cleared = true;

    if (redis_store_) {
        redis_cleared = redis_store_->clearChannel(channel_id);
    }

    memory_cleared = memory_store_->clearChannel(channel_id);

    return redis_cleared && memory_cleared;
}

bool HybridMessageStore::isAvailable() const {
    // Hybrid store is always available (memory fallback)
    return true;
}

bool HybridMessageStore::isUsingRedis() const {
    return redis_store_ && redis_store_->isAvailable();
}

#endif // ENABLE_REDIS

} // namespace chat
