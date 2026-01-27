#pragma once

#include <string>
#include <vector>
#include <memory>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <universalchat/Types.hpp>

namespace chat {

#ifdef ENABLE_REDIS
class RedisClient;
#endif

/**
 * IMessageStore
 *
 * Abstract interface for message storage.
 * Allows different storage backends (memory, Redis, database).
 */
class IMessageStore {
public:
    virtual ~IMessageStore() = default;

    // Store a message
    virtual bool saveMessage(const std::string& channel_id, const ChatMessage& message) = 0;

    // Get recent messages (most recent first if reverse=true)
    virtual std::vector<ChatMessage> getRecentMessages(
        const std::string& channel_id,
        int count,
        bool reverse = true
    ) = 0;

    // Get messages in a time range
    virtual std::vector<ChatMessage> getMessagesByTimeRange(
        const std::string& channel_id,
        int64_t start_timestamp,
        int64_t end_timestamp,
        int limit = 100
    ) = 0;

    // Get message count
    virtual int64_t getMessageCount(const std::string& channel_id) = 0;

    // Clear all messages in a channel
    virtual bool clearChannel(const std::string& channel_id) = 0;

    // Check if store is available
    virtual bool isAvailable() const = 0;
};

/**
 * InMemoryMessageStore
 *
 * Simple in-memory message storage using deque.
 * Used as fallback when Redis is not available.
 */
class InMemoryMessageStore : public IMessageStore {
public:
    explicit InMemoryMessageStore(int max_messages_per_channel = 100);

    bool saveMessage(const std::string& channel_id, const ChatMessage& message) override;

    std::vector<ChatMessage> getRecentMessages(
        const std::string& channel_id,
        int count,
        bool reverse = true
    ) override;

    std::vector<ChatMessage> getMessagesByTimeRange(
        const std::string& channel_id,
        int64_t start_timestamp,
        int64_t end_timestamp,
        int limit = 100
    ) override;

    int64_t getMessageCount(const std::string& channel_id) override;
    bool clearChannel(const std::string& channel_id) override;
    bool isAvailable() const override { return true; }

private:
    int max_messages_per_channel_;
    std::unordered_map<std::string, std::deque<ChatMessage>> channels_;
    mutable std::mutex mutex_;
};

#ifdef ENABLE_REDIS

/**
 * RedisMessageStore
 *
 * Redis-backed message storage using sorted sets.
 * Messages are stored with timestamp as score for efficient time-based queries.
 *
 * Key format: chat:messages:{channel_id}
 * Score: message timestamp
 * Value: JSON serialized ChatMessage
 */
class RedisMessageStore : public IMessageStore {
public:
    struct Config {
        std::string key_prefix = "chat:messages:";
        int max_messages_per_channel = 1000;
        int message_ttl_seconds = 86400 * 7;  // 7 days default
        bool auto_cleanup = true;
    };

    RedisMessageStore(std::shared_ptr<RedisClient> redis, const Config& config = Config{});

    bool saveMessage(const std::string& channel_id, const ChatMessage& message) override;

    std::vector<ChatMessage> getRecentMessages(
        const std::string& channel_id,
        int count,
        bool reverse = true
    ) override;

    std::vector<ChatMessage> getMessagesByTimeRange(
        const std::string& channel_id,
        int64_t start_timestamp,
        int64_t end_timestamp,
        int limit = 100
    ) override;

    int64_t getMessageCount(const std::string& channel_id) override;
    bool clearChannel(const std::string& channel_id) override;
    bool isAvailable() const override;

    // Redis-specific methods
    void setMessageTTL(int seconds) { config_.message_ttl_seconds = seconds; }
    void setMaxMessages(int max) { config_.max_messages_per_channel = max; }

private:
    std::string makeKey(const std::string& channel_id) const;
    std::string serializeMessage(const ChatMessage& message) const;
    ChatMessage deserializeMessage(const std::string& json) const;
    void cleanupOldMessages(const std::string& channel_id);

    std::shared_ptr<RedisClient> redis_;
    Config config_;
};

/**
 * HybridMessageStore
 *
 * Combines Redis and in-memory storage.
 * Uses Redis as primary, falls back to memory if Redis unavailable.
 */
class HybridMessageStore : public IMessageStore {
public:
    HybridMessageStore(
        std::shared_ptr<RedisClient> redis,
        const RedisMessageStore::Config& redis_config = RedisMessageStore::Config{},
        int memory_max_messages = 100
    );

    bool saveMessage(const std::string& channel_id, const ChatMessage& message) override;

    std::vector<ChatMessage> getRecentMessages(
        const std::string& channel_id,
        int count,
        bool reverse = true
    ) override;

    std::vector<ChatMessage> getMessagesByTimeRange(
        const std::string& channel_id,
        int64_t start_timestamp,
        int64_t end_timestamp,
        int limit = 100
    ) override;

    int64_t getMessageCount(const std::string& channel_id) override;
    bool clearChannel(const std::string& channel_id) override;
    bool isAvailable() const override;

    // Check which store is currently active
    bool isUsingRedis() const;

private:
    std::unique_ptr<RedisMessageStore> redis_store_;
    std::unique_ptr<InMemoryMessageStore> memory_store_;
};

#endif // ENABLE_REDIS

} // namespace chat
