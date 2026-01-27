#include "ChannelManager.hpp"
#include "../core/Session.hpp"
#include "../util/Config.hpp"
#include "../util/Logger.hpp"
#include "../util/PasswordHasher.hpp"
#include "../redis/MessageStore.hpp"

#ifdef ENABLE_REDIS
#include "../redis/RedisClient.hpp"
#include "../redis/RedisPubSub.hpp"
#include "../redis/PubSubMessage.hpp"
#include "../redis/ChannelRegistry.hpp"
#endif

namespace chat {

ChannelManager::ChannelManager(const Config& config)
    : config_(config)
{
    LOG_INFO("ChannelManager initialized");
}

#ifdef ENABLE_REDIS
void ChannelManager::initializeMessageStore(std::shared_ptr<RedisClient> redis_client) {
    if (config_.message_persistence_enabled && redis_client) {
        // Create hybrid store (Redis with memory fallback)
        RedisMessageStore::Config store_config;
        store_config.key_prefix = config_.message_store_key_prefix;
        store_config.max_messages_per_channel = config_.message_store_max_per_channel;
        store_config.message_ttl_seconds = config_.message_store_ttl_seconds;

        message_store_ = std::make_shared<HybridMessageStore>(
            redis_client,
            store_config,
            config_.message_history_size
        );

        LOG_INFO("Message store initialized with Redis (ttl={}s, max={})",
                 store_config.message_ttl_seconds, store_config.max_messages_per_channel);
    } else if (config_.message_persistence_enabled) {
        // Redis not available, use in-memory store
        message_store_ = std::make_shared<InMemoryMessageStore>(config_.message_history_size);
        LOG_WARN("Redis not available, using in-memory message store");
    } else {
        // No persistence configured
        LOG_INFO("Message persistence disabled, channels will use local history");
    }
}
#else
void ChannelManager::initializeMessageStore(std::nullptr_t) {
    if (config_.message_persistence_enabled) {
        // Redis not compiled in, use in-memory store
        message_store_ = std::make_shared<InMemoryMessageStore>(config_.message_history_size);
        LOG_INFO("Message store initialized with in-memory storage (Redis not compiled)");
    } else {
        // No persistence configured
        LOG_INFO("Message persistence disabled, channels will use local history");
    }
}
#endif

Channel* ChannelManager::createChannel(const ChannelConfig& config) {
    std::unique_lock<std::shared_mutex> lock(channels_mutex_);

    // Check if channel already exists
    if (channels_.count(config.channel_id) > 0) {
        LOG_WARN("Channel {} already exists", config.channel_id);
        return channels_[config.channel_id].get();
    }

    // Hash password if provided (create modified config)
    ChannelConfig hashedConfig = config;
    if (!config.password.empty()) {
        // Check if already hashed (contains $ delimiter for salt$hash format)
        if (config.password.find('$') == std::string::npos) {
            std::string salt = PasswordHasher::generateSalt();
            hashedConfig.password = PasswordHasher::hashWithSalt(config.password, salt);
            LOG_DEBUG("Channel {} password hashed", config.channel_id);
        }
    }

    // Create new channel with hashed password and message store
    auto channel = std::make_unique<Channel>(hashedConfig, message_store_);
    auto* ptr = channel.get();

#ifdef ENABLE_REDIS
    // Set Pub/Sub for cross-server distribution
    if (pubsub_) {
        channel->setPubSub(pubsub_);
    }
#endif

    channels_[config.channel_id] = std::move(channel);

    LOG_INFO("Channel created: {} ({})", config.channel_id, config.channel_name);

    return ptr;
}

Channel* ChannelManager::getChannel(const std::string& channel_id) {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);

    auto it = channels_.find(channel_id);
    return (it != channels_.end()) ? it->second.get() : nullptr;
}

const Channel* ChannelManager::getChannel(const std::string& channel_id) const {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);

    auto it = channels_.find(channel_id);
    return (it != channels_.end()) ? it->second.get() : nullptr;
}

bool ChannelManager::deleteChannel(const std::string& channel_id) {
    std::unique_lock<std::shared_mutex> lock(channels_mutex_);

    auto it = channels_.find(channel_id);
    if (it == channels_.end()) {
        return false;
    }

    // Cannot delete system channels
    if (it->second->isSystem()) {
        LOG_WARN("Cannot delete system channel: {}", channel_id);
        return false;
    }

    channels_.erase(it);
    LOG_INFO("Channel deleted: {}", channel_id);

    return true;
}

bool ChannelManager::channelExists(const std::string& channel_id) const {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    return channels_.count(channel_id) > 0;
}

std::vector<ChannelInfo> ChannelManager::getChannelList() const {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);

    std::vector<ChannelInfo> list;
    list.reserve(channels_.size());

    for (const auto& [id, channel] : channels_) {
        ChannelInfo info;
        info.channel_id = channel->id();
        info.channel_name = channel->name();
        info.member_count = channel->memberCount();
        info.max_members = channel->maxMembers();
        info.is_system = channel->isSystem();
        info.has_password = channel->hasPassword();
        list.push_back(info);
    }

    return list;
}

size_t ChannelManager::channelCount() const {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    return channels_.size();
}

Channel::JoinResult ChannelManager::joinChannel(const std::string& channel_id,
                                                 SessionPtr session,
                                                 const std::string& password) {
    Channel* channel = getChannel(channel_id);
    if (!channel) {
        // Channel doesn't exist - could auto-create non-system channels here
        LOG_WARN("Join failed: channel {} not found", channel_id);
        return Channel::JoinResult::Forbidden;
    }

    auto result = channel->addMember(session, password);

    if (result == Channel::JoinResult::Success) {
        session->addChannel(channel_id);

#ifdef ENABLE_REDIS
        // Update global channel membership in Redis
        if (channel_registry_ && channel_registry_->isAvailable()) {
            channel_registry_->addMember(channel_id, session->userId());
        }

        // Publish join notification to other servers
        if (pubsub_) {
            PubSubMessage msg;
            msg.type = PubSubMessageType::ChannelJoin;
            msg.origin_server_id = pubsub_->serverId();
            msg.channel_id = channel_id;
            msg.sender_session_id = session->sessionId();
            msg.sender_user_id = session->userId();

            ChannelMemberPayload payload;
            payload.user_id = session->userId();
            payload.session_id = session->sessionId();
            payload.nickname = session->nickname();
            payload.profile_image = session->profileImage();
            payload.frame_image = session->frameImage();
            msg.payload = payload.serialize();

            std::string topic = "chat:channel:" + channel_id;
            pubsub_->publish(topic, msg);
        }
#endif
    }

    return result;
}

bool ChannelManager::leaveChannel(const std::string& channel_id, SessionPtr session) {
    Channel* channel = getChannel(channel_id);
    if (!channel) {
        return false;
    }

    bool removed = channel->removeMember(session->sessionId());

    if (removed) {
        session->removeChannel(channel_id);

#ifdef ENABLE_REDIS
        // Update global channel membership in Redis
        if (channel_registry_ && channel_registry_->isAvailable()) {
            channel_registry_->removeMember(channel_id, session->userId());
        }

        // Publish leave notification to other servers
        if (pubsub_) {
            PubSubMessage msg;
            msg.type = PubSubMessageType::ChannelLeave;
            msg.origin_server_id = pubsub_->serverId();
            msg.channel_id = channel_id;
            msg.sender_session_id = session->sessionId();
            msg.sender_user_id = session->userId();

            ChannelMemberPayload payload;
            payload.user_id = session->userId();
            payload.session_id = session->sessionId();
            payload.nickname = session->nickname();
            msg.payload = payload.serialize();

            std::string topic = "chat:channel:" + channel_id;
            pubsub_->publish(topic, msg);
        }
#endif
    }

    return removed;
}

void ChannelManager::leaveAllChannels(SessionPtr session) {
    auto channels = session->getJoinedChannels();

    for (const auto& channel_id : channels) {
        leaveChannel(channel_id, session);
    }
}

void ChannelManager::broadcastToChannel(const std::string& channel_id,
                                        const Packet& packet,
                                        const std::string& exclude_session) {
    Channel* channel = getChannel(channel_id);
    if (channel) {
        channel->broadcast(packet, exclude_session);
    }
}

void ChannelManager::onSessionDisconnect(SessionPtr session) {
    leaveAllChannels(session);

#ifdef ENABLE_REDIS
    // Clean up global channel membership in Redis
    if (channel_registry_ && channel_registry_->isAvailable()) {
        int removed = channel_registry_->removeUserFromAllChannels(session->userId());
        if (removed > 0) {
            LOG_DEBUG("Removed user {} from {} channels in global registry",
                      session->userId(), removed);
        }
    }
#endif
}

#ifdef ENABLE_REDIS
void ChannelManager::initializeChannelRegistry(std::shared_ptr<ChannelRegistry> channel_registry) {
    channel_registry_ = std::move(channel_registry);

    if (channel_registry_ && channel_registry_->isAvailable()) {
        // Register metadata for all existing channels
        std::shared_lock<std::shared_mutex> lock(channels_mutex_);
        for (const auto& [id, channel] : channels_) {
            ChannelConfig config;
            config.channel_id = id;
            config.channel_name = channel->name();
            config.max_members = channel->maxMembers();
            config.is_system = channel->isSystem();
            config.is_persistent = channel->isPersistent();
            channel_registry_->setChannelMetadata(id, config);
        }
        LOG_INFO("ChannelRegistry initialized with {} existing channels", channels_.size());
    } else {
        LOG_INFO("ChannelRegistry not available");
    }
}

void ChannelManager::initializePubSub(std::shared_ptr<RedisPubSub> pubsub) {
    pubsub_ = std::move(pubsub);

    if (!pubsub_) {
        LOG_INFO("ChannelManager Pub/Sub not initialized (null)");
        return;
    }

    // Set up message handler
    pubsub_->setMessageHandler([this](const std::string& channel, const PubSubMessage& message) {
        onPubSubMessage(channel, message);
    });

    // Set Pub/Sub for all existing channels
    {
        std::shared_lock<std::shared_mutex> lock(channels_mutex_);
        for (auto& [id, channel] : channels_) {
            channel->setPubSub(pubsub_);
        }
    }

    // Subscribe to channel pattern
    pubsub_->psubscribe("chat:channel:*");

    LOG_INFO("ChannelManager Pub/Sub initialized (server_id={})", pubsub_->serverId());
}

void ChannelManager::onPubSubMessage(const std::string& redis_channel, const PubSubMessage& message) {
    // Extract channel_id from Redis channel name (e.g., "chat:channel:world-1" -> "world-1")
    const std::string prefix = "chat:channel:";
    if (redis_channel.find(prefix) != 0) {
        LOG_WARN("Unexpected Redis channel format: {}", redis_channel);
        return;
    }

    std::string channel_id = redis_channel.substr(prefix.length());

    // Route message to appropriate channel
    Channel* channel = getChannel(channel_id);
    if (channel) {
        switch (message.type) {
            case PubSubMessageType::ChannelMessage:
                channel->onPubSubMessage(message);
                break;

            case PubSubMessageType::ChannelJoin:
            case PubSubMessageType::ChannelLeave:
                // Handle join/leave notifications (member count sync etc.)
                // These don't need to be rebroadcast as messages
                LOG_DEBUG("Channel {} received {} from server {}",
                          channel_id, pubSubMessageTypeToString(message.type),
                          message.origin_server_id);
                break;

            default:
                LOG_DEBUG("Channel {} received unhandled message type: {}",
                          channel_id, pubSubMessageTypeToString(message.type));
                break;
        }
    } else {
        LOG_DEBUG("Received Pub/Sub message for unknown channel: {}", channel_id);
    }
}

void ChannelManager::subscribeToChannels() {
    if (!pubsub_) return;

    // Subscribe to pattern for all channels
    pubsub_->psubscribe("chat:channel:*");

    LOG_DEBUG("Subscribed to channel pattern: chat:channel:*");
}
#endif

} // namespace chat
