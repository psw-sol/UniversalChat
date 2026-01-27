#pragma once

#include "PubSubMessage.hpp"
#include <string>
#include <vector>
#include <set>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <thread>
#include <condition_variable>

#ifdef ENABLE_REDIS
#include <hiredis/hiredis.h>
#endif

namespace chat {

class RedisClient;

/**
 * RedisPubSub
 *
 * Redis Pub/Sub client for cross-server communication.
 *
 * Key design decisions:
 * - Uses a separate Redis connection for subscriptions (hiredis requirement)
 * - Publishes via the main RedisClient (shared connection)
 * - Runs a background thread for receiving subscription messages
 * - Supports both exact channel subscriptions and pattern subscriptions
 * - Implements automatic reconnection on connection loss
 *
 * Topic naming conventions:
 * - chat:channel:{channel_id}   - Channel messages
 * - chat:whisper:{user_id}      - Private messages (whisper)
 * - chat:system                  - System-wide broadcasts
 * - chat:presence               - User presence updates
 */
class RedisPubSub {
public:
    /**
     * Message handler callback
     * @param channel The channel/pattern that received the message
     * @param message The deserialized PubSubMessage
     */
    using MessageHandler = std::function<void(const std::string& channel,
                                               const PubSubMessage& message)>;

    /**
     * Connection state change callback
     * @param connected True if connected, false if disconnected
     */
    using ConnectionHandler = std::function<void(bool connected)>;

    /**
     * Configuration for RedisPubSub
     */
    struct Config {
        std::string host = "127.0.0.1";
        int port = 6379;
        std::string password;
        int db = 0;
        int connect_timeout_ms = 5000;
        int reconnect_interval_ms = 5000;
        int max_reconnect_attempts = 0;  // 0 = unlimited
        std::string server_id;           // Unique identifier for this server instance
    };

    /**
     * Connection statistics
     */
    struct Stats {
        uint64_t messages_published = 0;
        uint64_t messages_received = 0;
        uint64_t publish_errors = 0;
        uint64_t reconnect_count = 0;
        int64_t last_message_received_at = 0;
        bool connected = false;
    };

    explicit RedisPubSub(const Config& config);
    ~RedisPubSub();

    // Non-copyable, non-movable
    RedisPubSub(const RedisPubSub&) = delete;
    RedisPubSub& operator=(const RedisPubSub&) = delete;
    RedisPubSub(RedisPubSub&&) = delete;
    RedisPubSub& operator=(RedisPubSub&&) = delete;

    // === Connection Management ===

    /**
     * Connect to Redis (subscribe connection)
     * @return true if connected successfully
     */
    bool connect();

    /**
     * Disconnect from Redis
     */
    void disconnect();

    /**
     * Check if connected
     */
    bool isConnected() const;

    /**
     * Set the RedisClient to use for publishing
     * This allows sharing the main Redis connection for publish operations
     */
    void setPublishClient(std::shared_ptr<RedisClient> client);

    // === Publishing ===

    /**
     * Publish a message to a channel
     * @param channel The Redis channel to publish to
     * @param message The message to publish
     * @return true if published successfully
     */
    bool publish(const std::string& channel, const PubSubMessage& message);

    /**
     * Publish raw string to a channel (for advanced use)
     * @param channel The Redis channel
     * @param data Raw string data
     * @return true if published successfully
     */
    bool publishRaw(const std::string& channel, const std::string& data);

    // === Subscription Management ===

    /**
     * Subscribe to an exact channel
     * @param channel Channel name (e.g., "chat:channel:world-1")
     * @return true if subscription request was sent
     */
    bool subscribe(const std::string& channel);

    /**
     * Subscribe to a channel pattern
     * @param pattern Pattern with wildcards (e.g., "chat:channel:*")
     * @return true if subscription request was sent
     */
    bool psubscribe(const std::string& pattern);

    /**
     * Unsubscribe from an exact channel
     * @param channel Channel name
     * @return true if unsubscription request was sent
     */
    bool unsubscribe(const std::string& channel);

    /**
     * Unsubscribe from a channel pattern
     * @param pattern Pattern
     * @return true if unsubscription request was sent
     */
    bool punsubscribe(const std::string& pattern);

    /**
     * Get list of subscribed channels
     */
    std::set<std::string> getSubscribedChannels() const;

    /**
     * Get list of subscribed patterns
     */
    std::set<std::string> getSubscribedPatterns() const;

    // === Listener Management ===

    /**
     * Start the background listening thread
     * Must be called after connect() and after setting up subscriptions
     */
    void startListening();

    /**
     * Stop the background listening thread
     */
    void stopListening();

    /**
     * Check if listener is running
     */
    bool isListening() const { return listening_.load(); }

    // === Handler Registration ===

    /**
     * Set the message handler callback
     * Called for each received Pub/Sub message
     */
    void setMessageHandler(MessageHandler handler);

    /**
     * Set the connection state change handler
     */
    void setConnectionHandler(ConnectionHandler handler);

    // === Information ===

    /**
     * Get the server ID of this instance
     */
    const std::string& serverId() const { return config_.server_id; }

    /**
     * Get current statistics
     */
    Stats getStats() const;

    /**
     * Get last error message
     */
    std::string lastError() const;

private:
#ifdef ENABLE_REDIS
    // Internal connection management
    bool connectInternal();
    void disconnectInternal();
    bool reconnect();

    // Subscription helpers
    bool sendSubscribeCommand(const std::string& command, const std::string& arg);

    // Listener thread function
    void listenLoop();

    // Process received Redis reply
    void processReply(redisReply* reply);
    void handleMessage(const std::string& channel, const std::string& data);
    void handlePMessage(const std::string& pattern, const std::string& channel, const std::string& data);

    // Publish via dedicated connection (when publish_client_ not set)
    bool publishDirect(const std::string& channel, const std::string& data);

    // Subscribe context (dedicated connection for subscriptions)
    redisContext* subscribe_context_ = nullptr;

    // Publish context (used if publish_client_ not set)
    redisContext* publish_context_ = nullptr;
#endif

    Config config_;

    // Optional shared publish client
    std::shared_ptr<RedisClient> publish_client_;

    // Connection state
    mutable std::shared_mutex state_mutex_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> listening_{false};
    std::atomic<bool> should_stop_{false};
    std::string last_error_;

    // Subscription tracking
    mutable std::mutex subscriptions_mutex_;
    std::set<std::string> subscribed_channels_;
    std::set<std::string> subscribed_patterns_;

    // Listener thread
    std::thread listen_thread_;
    std::condition_variable stop_cv_;
    std::mutex stop_mutex_;

    // Callbacks
    MessageHandler message_handler_;
    ConnectionHandler connection_handler_;

    // Statistics
    mutable std::mutex stats_mutex_;
    Stats stats_;
};

} // namespace chat
