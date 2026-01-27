#pragma once

#include "Channel.hpp"
#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <vector>
#include <optional>
#include <universalchat/Types.hpp>

namespace chat {

class Session;
class Config;
class IMessageStore;

#ifdef ENABLE_REDIS
class RedisClient;
class RedisPubSub;
class ChannelRegistry;
struct PubSubMessage;
#endif

/**
 * Channel Information for listing
 */
struct ChannelInfo {
    std::string channel_id;
    std::string channel_name;
    size_t member_count;
    int max_members;
    bool is_system;
    bool has_password;
};

/**
 * ChannelManager
 *
 * Manages all channels in the server, including:
 * - Creating/deleting channels
 * - User join/leave operations
 * - Broadcasting messages
 */
class ChannelManager {
public:
    using SessionPtr = std::shared_ptr<Session>;

    explicit ChannelManager(const Config& config);
    ~ChannelManager() = default;

    // Initialize message store (call after construction with Redis client if available)
#ifdef ENABLE_REDIS
    void initializeMessageStore(std::shared_ptr<RedisClient> redis_client = nullptr);

    /**
     * Initialize Pub/Sub for cross-server message distribution
     * @param pubsub Shared pointer to RedisPubSub client
     */
    void initializePubSub(std::shared_ptr<RedisPubSub> pubsub);

    /**
     * Initialize ChannelRegistry for global channel membership
     * @param channel_registry Shared pointer to ChannelRegistry
     */
    void initializeChannelRegistry(std::shared_ptr<ChannelRegistry> channel_registry);

    /**
     * Get the ChannelRegistry instance
     * @return Shared pointer to ChannelRegistry, or nullptr if not initialized
     */
    std::shared_ptr<ChannelRegistry> getChannelRegistry() const { return channel_registry_; }
#else
    void initializeMessageStore(std::nullptr_t redis_client = nullptr);
#endif

    // Non-copyable
    ChannelManager(const ChannelManager&) = delete;
    ChannelManager& operator=(const ChannelManager&) = delete;

    // === Channel Management ===
    Channel* createChannel(const ChannelConfig& config);
    Channel* getChannel(const std::string& channel_id);
    const Channel* getChannel(const std::string& channel_id) const;
    bool deleteChannel(const std::string& channel_id);
    bool channelExists(const std::string& channel_id) const;

    // === Channel Listing ===
    std::vector<ChannelInfo> getChannelList() const;
    size_t channelCount() const;

    // === User-Channel Operations ===
    Channel::JoinResult joinChannel(const std::string& channel_id,
                                    SessionPtr session,
                                    const std::string& password = "");
    bool leaveChannel(const std::string& channel_id, SessionPtr session);
    void leaveAllChannels(SessionPtr session);

    // === Broadcasting ===
    void broadcastToChannel(const std::string& channel_id,
                           const Packet& packet,
                           const std::string& exclude_session = "");

    // === Session Cleanup ===
    void onSessionDisconnect(SessionPtr session);

private:
#ifdef ENABLE_REDIS
    /**
     * Handle incoming Pub/Sub message and route to appropriate channel
     * @param channel Redis channel name (e.g., "chat:channel:world-1")
     * @param message The Pub/Sub message
     */
    void onPubSubMessage(const std::string& channel, const PubSubMessage& message);

    /**
     * Subscribe to all existing channels' Pub/Sub topics
     */
    void subscribeToChannels();
#endif

    const Config& config_;

    mutable std::shared_mutex channels_mutex_;
    std::unordered_map<std::string, std::unique_ptr<Channel>> channels_;

    // Shared message store for all channels
    std::shared_ptr<IMessageStore> message_store_;

#ifdef ENABLE_REDIS
    // Redis Pub/Sub for cross-server message distribution
    std::shared_ptr<RedisPubSub> pubsub_;

    // Redis-based global channel membership registry
    std::shared_ptr<ChannelRegistry> channel_registry_;
#endif
};

} // namespace chat
