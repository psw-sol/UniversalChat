#include "RedisPubSub.hpp"
#include "RedisClient.hpp"
#include "../util/Logger.hpp"
#include <chrono>

namespace chat {

RedisPubSub::RedisPubSub(const Config& config)
    : config_(config)
{
    if (config_.server_id.empty()) {
        // Generate a unique server ID if not provided
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        config_.server_id = "server-" + std::to_string(now);
    }
    LOG_INFO("RedisPubSub initialized (server_id={})", config_.server_id);
}

RedisPubSub::~RedisPubSub() {
    stopListening();
    disconnect();
}

#ifdef ENABLE_REDIS

// ============================================================================
// Connection Management
// ============================================================================

bool RedisPubSub::connect() {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    return connectInternal();
}

bool RedisPubSub::connectInternal() {
    if (connected_.load()) {
        return true;
    }

    // Connect subscribe context
    struct timeval timeout = {
        config_.connect_timeout_ms / 1000,
        (config_.connect_timeout_ms % 1000) * 1000
    };

    subscribe_context_ = redisConnectWithTimeout(
        config_.host.c_str(),
        config_.port,
        timeout
    );

    if (!subscribe_context_ || subscribe_context_->err) {
        last_error_ = subscribe_context_ ? subscribe_context_->errstr : "Failed to allocate redis context";
        LOG_ERROR("RedisPubSub subscribe connection failed: {}", last_error_);
        if (subscribe_context_) {
            redisFree(subscribe_context_);
            subscribe_context_ = nullptr;
        }
        return false;
    }

    // Authenticate if needed
    if (!config_.password.empty()) {
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(subscribe_context_, "AUTH %s", config_.password.c_str())
        );
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            last_error_ = reply ? reply->str : "AUTH failed";
            LOG_ERROR("RedisPubSub AUTH failed: {}", last_error_);
            if (reply) freeReplyObject(reply);
            redisFree(subscribe_context_);
            subscribe_context_ = nullptr;
            return false;
        }
        freeReplyObject(reply);
    }

    // Select database if not default
    if (config_.db != 0) {
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(subscribe_context_, "SELECT %d", config_.db)
        );
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            last_error_ = reply ? reply->str : "SELECT failed";
            LOG_ERROR("RedisPubSub SELECT failed: {}", last_error_);
            if (reply) freeReplyObject(reply);
            redisFree(subscribe_context_);
            subscribe_context_ = nullptr;
            return false;
        }
        freeReplyObject(reply);
    }

    // Create publish context if no shared client
    if (!publish_client_) {
        publish_context_ = redisConnectWithTimeout(
            config_.host.c_str(),
            config_.port,
            timeout
        );

        if (!publish_context_ || publish_context_->err) {
            last_error_ = publish_context_ ? publish_context_->errstr : "Failed to create publish context";
            LOG_ERROR("RedisPubSub publish connection failed: {}", last_error_);
            if (publish_context_) {
                redisFree(publish_context_);
                publish_context_ = nullptr;
            }
            redisFree(subscribe_context_);
            subscribe_context_ = nullptr;
            return false;
        }

        // Authenticate publish context
        if (!config_.password.empty()) {
            redisReply* reply = static_cast<redisReply*>(
                redisCommand(publish_context_, "AUTH %s", config_.password.c_str())
            );
            if (!reply || reply->type == REDIS_REPLY_ERROR) {
                if (reply) freeReplyObject(reply);
                redisFree(publish_context_);
                publish_context_ = nullptr;
                redisFree(subscribe_context_);
                subscribe_context_ = nullptr;
                return false;
            }
            freeReplyObject(reply);
        }

        if (config_.db != 0) {
            redisReply* reply = static_cast<redisReply*>(
                redisCommand(publish_context_, "SELECT %d", config_.db)
            );
            if (!reply || reply->type == REDIS_REPLY_ERROR) {
                if (reply) freeReplyObject(reply);
                redisFree(publish_context_);
                publish_context_ = nullptr;
                redisFree(subscribe_context_);
                subscribe_context_ = nullptr;
                return false;
            }
            freeReplyObject(reply);
        }
    }

    connected_.store(true);
    LOG_INFO("RedisPubSub connected to {}:{}", config_.host, config_.port);

    // Notify connection handler
    if (connection_handler_) {
        connection_handler_(true);
    }

    return true;
}

void RedisPubSub::disconnect() {
    stopListening();

    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    disconnectInternal();
}

void RedisPubSub::disconnectInternal() {
    connected_.store(false);

    if (subscribe_context_) {
        redisFree(subscribe_context_);
        subscribe_context_ = nullptr;
    }

    if (publish_context_) {
        redisFree(publish_context_);
        publish_context_ = nullptr;
    }

    // Clear subscriptions
    {
        std::lock_guard<std::mutex> sub_lock(subscriptions_mutex_);
        subscribed_channels_.clear();
        subscribed_patterns_.clear();
    }

    LOG_INFO("RedisPubSub disconnected");

    // Notify connection handler
    if (connection_handler_) {
        connection_handler_(false);
    }
}

bool RedisPubSub::isConnected() const {
    return connected_.load();
}

void RedisPubSub::setPublishClient(std::shared_ptr<RedisClient> client) {
    publish_client_ = std::move(client);
}

bool RedisPubSub::reconnect() {
    LOG_INFO("RedisPubSub attempting reconnection...");

    // Save current subscriptions
    std::set<std::string> channels;
    std::set<std::string> patterns;
    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        channels = subscribed_channels_;
        patterns = subscribed_patterns_;
    }

    // Disconnect and reconnect with lock
    {
        std::unique_lock<std::shared_mutex> lock(state_mutex_);

        // Disconnect internal (no lock needed, we have it)
        disconnectInternal();

        // Wait before reconnecting
        // Release lock during sleep to avoid blocking other operations
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.reconnect_interval_ms));
        lock.lock();

        // Reconnect
        if (!connectInternal()) {
            return false;
        }

        // Re-subscribe to channels (while holding lock, use direct command)
        for (const auto& channel : channels) {
            if (!sendSubscribeCommandInternal("SUBSCRIBE", channel)) {
                LOG_WARN("Failed to re-subscribe to channel: {}", channel);
            }
        }

        // Re-subscribe to patterns
        for (const auto& pattern : patterns) {
            if (!sendSubscribeCommandInternal("PSUBSCRIBE", pattern)) {
                LOG_WARN("Failed to re-subscribe to pattern: {}", pattern);
            }
        }
    }

    // Restore subscription tracking
    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscribed_channels_ = channels;
        subscribed_patterns_ = patterns;
    }

    // Update stats
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.reconnect_count++;
    }

    LOG_INFO("RedisPubSub reconnected successfully");
    return true;
}

bool RedisPubSub::sendSubscribeCommandInternal(const std::string& command, const std::string& arg) {
    // Internal version - assumes state_mutex_ is already held
    if (!subscribe_context_) {
        last_error_ = "Subscribe context not available";
        return false;
    }

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(subscribe_context_, "%s %s", command.c_str(), arg.c_str())
    );

    if (!reply) {
        last_error_ = subscribe_context_->errstr;
        LOG_ERROR("RedisPubSub {} failed: {}", command, last_error_);
        return false;
    }

    // Subscribe commands return array: [type, channel, count]
    bool success = (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 1);
    freeReplyObject(reply);
    return success;
}

// ============================================================================
// Publishing
// ============================================================================

bool RedisPubSub::publish(const std::string& channel, const PubSubMessage& message) {
    // Ensure origin_server_id is set
    PubSubMessage msg = message;
    if (msg.origin_server_id.empty()) {
        msg.origin_server_id = config_.server_id;
    }

    return publishRaw(channel, msg.serialize());
}

bool RedisPubSub::publishRaw(const std::string& channel, const std::string& data) {
    // Use shared client if available
    if (publish_client_ && publish_client_->isConnected()) {
        // Use RPUSH for a simple approach, but PUBLISH is what we need
        // Since RedisClient doesn't have publish, we need to use direct
        return publishDirect(channel, data);
    }

    return publishDirect(channel, data);
}

bool RedisPubSub::publishDirect(const std::string& channel, const std::string& data) {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);

    if (!publish_context_) {
        last_error_ = "Publish context not available";
        return false;
    }

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(publish_context_, "PUBLISH %s %b", channel.c_str(), data.c_str(), data.size())
    );

    if (!reply) {
        last_error_ = publish_context_->errstr;
        LOG_ERROR("RedisPubSub PUBLISH failed: {}", last_error_);
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.publish_errors++;
        }
        return false;
    }

    bool success = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);

    if (success) {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.messages_published++;
    } else {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.publish_errors++;
    }

    return success;
}

// ============================================================================
// Subscription Management
// ============================================================================

bool RedisPubSub::subscribe(const std::string& channel) {
    if (!connected_.load()) {
        last_error_ = "Not connected";
        return false;
    }

    if (sendSubscribeCommand("SUBSCRIBE", channel)) {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscribed_channels_.insert(channel);
        LOG_DEBUG("Subscribed to channel: {}", channel);
        return true;
    }
    return false;
}

bool RedisPubSub::psubscribe(const std::string& pattern) {
    if (!connected_.load()) {
        last_error_ = "Not connected";
        return false;
    }

    if (sendSubscribeCommand("PSUBSCRIBE", pattern)) {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscribed_patterns_.insert(pattern);
        LOG_DEBUG("Subscribed to pattern: {}", pattern);
        return true;
    }
    return false;
}

bool RedisPubSub::unsubscribe(const std::string& channel) {
    if (!connected_.load()) {
        return false;
    }

    if (sendSubscribeCommand("UNSUBSCRIBE", channel)) {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscribed_channels_.erase(channel);
        LOG_DEBUG("Unsubscribed from channel: {}", channel);
        return true;
    }
    return false;
}

bool RedisPubSub::punsubscribe(const std::string& pattern) {
    if (!connected_.load()) {
        return false;
    }

    if (sendSubscribeCommand("PUNSUBSCRIBE", pattern)) {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscribed_patterns_.erase(pattern);
        LOG_DEBUG("Unsubscribed from pattern: {}", pattern);
        return true;
    }
    return false;
}

bool RedisPubSub::sendSubscribeCommand(const std::string& command, const std::string& arg) {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);

    if (!subscribe_context_) {
        last_error_ = "Subscribe context not available";
        return false;
    }

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(subscribe_context_, "%s %s", command.c_str(), arg.c_str())
    );

    if (!reply) {
        last_error_ = subscribe_context_->errstr;
        LOG_ERROR("RedisPubSub {} failed: {}", command, last_error_);
        return false;
    }

    // Subscribe commands return array: [type, channel, count]
    bool success = (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 1);
    freeReplyObject(reply);
    return success;
}

std::set<std::string> RedisPubSub::getSubscribedChannels() const {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    return subscribed_channels_;
}

std::set<std::string> RedisPubSub::getSubscribedPatterns() const {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    return subscribed_patterns_;
}

// ============================================================================
// Listener Management
// ============================================================================

void RedisPubSub::startListening() {
    if (listening_.load()) {
        return;
    }

    if (!connected_.load()) {
        LOG_WARN("Cannot start listening: not connected");
        return;
    }

    should_stop_.store(false);
    listening_.store(true);

    listen_thread_ = std::thread(&RedisPubSub::listenLoop, this);
    LOG_INFO("RedisPubSub listener started");
}

void RedisPubSub::stopListening() {
    if (!listening_.load()) {
        return;
    }

    should_stop_.store(true);
    listening_.store(false);

    // Signal the condition variable
    {
        std::lock_guard<std::mutex> lock(stop_mutex_);
        stop_cv_.notify_all();
    }

    // Wait for thread to finish
    if (listen_thread_.joinable()) {
        listen_thread_.join();
    }

    LOG_INFO("RedisPubSub listener stopped");
}

void RedisPubSub::listenLoop() {
    LOG_DEBUG("RedisPubSub listen loop started");

    int reconnect_attempts = 0;

    while (!should_stop_.load()) {
        // Check connection
        bool needs_reconnect = false;
        {
            std::shared_lock<std::shared_mutex> lock(state_mutex_);
            needs_reconnect = (!subscribe_context_ || subscribe_context_->err);
        }

        if (needs_reconnect) {
            LOG_WARN("RedisPubSub connection lost, attempting reconnect...");

            if (config_.max_reconnect_attempts > 0 &&
                reconnect_attempts >= config_.max_reconnect_attempts) {
                LOG_ERROR("RedisPubSub max reconnect attempts reached, stopping");
                break;
            }

            connected_.store(false);
            // reconnect() handles its own locking
            if (reconnect()) {
                reconnect_attempts = 0;
            } else {
                reconnect_attempts++;
                continue;
            }
        }

        // Wait for message with timeout
        redisReply* reply = nullptr;
        {
            std::shared_lock<std::shared_mutex> lock(state_mutex_);
            if (!subscribe_context_) {
                continue;
            }

            // Set read timeout
            struct timeval tv = {1, 0};  // 1 second timeout
            redisSetTimeout(subscribe_context_, tv);

            // Blocking read
            int result = redisGetReply(subscribe_context_, reinterpret_cast<void**>(&reply));

            if (result == REDIS_ERR) {
                if (subscribe_context_->err == REDIS_ERR_IO &&
                    (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    // Timeout, just continue
                    continue;
                }
                LOG_WARN("RedisPubSub redisGetReply error: {}", subscribe_context_->errstr);
                connected_.store(false);
                continue;
            }
        }

        if (reply) {
            processReply(reply);
            freeReplyObject(reply);
        }
    }

    LOG_DEBUG("RedisPubSub listen loop ended");
}

void RedisPubSub::processReply(redisReply* reply) {
    if (!reply || reply->type != REDIS_REPLY_ARRAY || reply->elements < 3) {
        return;
    }

    // Message format: ["message", channel, data] or ["pmessage", pattern, channel, data]
    std::string type(reply->element[0]->str, reply->element[0]->len);

    if (type == "message") {
        std::string channel(reply->element[1]->str, reply->element[1]->len);
        std::string data(reply->element[2]->str, reply->element[2]->len);
        handleMessage(channel, data);
    }
    else if (type == "pmessage" && reply->elements >= 4) {
        std::string pattern(reply->element[1]->str, reply->element[1]->len);
        std::string channel(reply->element[2]->str, reply->element[2]->len);
        std::string data(reply->element[3]->str, reply->element[3]->len);
        handlePMessage(pattern, channel, data);
    }
}

void RedisPubSub::handleMessage(const std::string& channel, const std::string& data) {
    // Update stats
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_received++;
        stats_.last_message_received_at = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Parse message
    auto msg_opt = PubSubMessage::tryDeserialize(data);
    if (!msg_opt) {
        LOG_WARN("RedisPubSub failed to deserialize message from channel: {}", channel);
        return;
    }

    const auto& msg = *msg_opt;

    // Skip messages from self
    if (msg.isFromServer(config_.server_id)) {
        LOG_DEBUG("RedisPubSub skipping self-originated message on channel: {}", channel);
        return;
    }

    // Invoke handler
    if (message_handler_) {
        try {
            message_handler_(channel, msg);
        } catch (const std::exception& e) {
            LOG_ERROR("RedisPubSub message handler exception: {}", e.what());
        }
    }
}

void RedisPubSub::handlePMessage(const std::string& pattern, const std::string& channel, const std::string& data) {
    // Same logic as handleMessage, but we pass the actual channel (not pattern)
    handleMessage(channel, data);
}

// ============================================================================
// Handler Registration
// ============================================================================

void RedisPubSub::setMessageHandler(MessageHandler handler) {
    message_handler_ = std::move(handler);
}

void RedisPubSub::setConnectionHandler(ConnectionHandler handler) {
    connection_handler_ = std::move(handler);
}

// ============================================================================
// Information
// ============================================================================

RedisPubSub::Stats RedisPubSub::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    Stats s = stats_;
    s.connected = connected_.load();
    return s;
}

std::string RedisPubSub::lastError() const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    return last_error_;
}

#else // !ENABLE_REDIS - Stub implementation

bool RedisPubSub::connect() {
    LOG_WARN("Redis support is disabled. Rebuild with -DENABLE_REDIS=ON");
    return false;
}

void RedisPubSub::disconnect() {}

bool RedisPubSub::isConnected() const { return false; }

void RedisPubSub::setPublishClient(std::shared_ptr<RedisClient>) {}

bool RedisPubSub::publish(const std::string&, const PubSubMessage&) { return false; }
bool RedisPubSub::publishRaw(const std::string&, const std::string&) { return false; }

bool RedisPubSub::subscribe(const std::string&) { return false; }
bool RedisPubSub::psubscribe(const std::string&) { return false; }
bool RedisPubSub::unsubscribe(const std::string&) { return false; }
bool RedisPubSub::punsubscribe(const std::string&) { return false; }

std::set<std::string> RedisPubSub::getSubscribedChannels() const { return {}; }
std::set<std::string> RedisPubSub::getSubscribedPatterns() const { return {}; }

void RedisPubSub::startListening() {}
void RedisPubSub::stopListening() {}

void RedisPubSub::setMessageHandler(MessageHandler) {}
void RedisPubSub::setConnectionHandler(ConnectionHandler) {}

RedisPubSub::Stats RedisPubSub::getStats() const { return {}; }
std::string RedisPubSub::lastError() const { return "Redis support disabled"; }

#endif // ENABLE_REDIS

} // namespace chat
