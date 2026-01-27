#include "Channel.hpp"
#include "../core/Session.hpp"
#include "../protocol/PacketCodec.hpp"
#include "../util/Logger.hpp"
#include "../util/PasswordHasher.hpp"
#include "../redis/MessageStore.hpp"

#ifdef ENABLE_REDIS
#include "../redis/RedisPubSub.hpp"
#include "../redis/PubSubMessage.hpp"
#endif

namespace chat {

Channel::Channel(const ChannelConfig& config, std::shared_ptr<IMessageStore> message_store)
    : config_(config)
    , message_store_(std::move(message_store))
    , use_local_history_(message_store_ == nullptr)
{
    LOG_INFO("Channel created: {} ({}) [storage={}]",
             config_.channel_id, config_.channel_name,
             use_local_history_ ? "local" : "external");
}

Channel::JoinResult Channel::addMember(SessionPtr session, const std::string& password) {
    // Check password if required (config_.password stores salted hash)
    if (hasPassword()) {
        if (password.empty()) {
            return JoinResult::PasswordRequired;
        }
        if (!PasswordHasher::verifyWithSalt(password, config_.password)) {
            return JoinResult::PasswordIncorrect;
        }
    }

    std::unique_lock<std::shared_mutex> lock(members_mutex_);

    // Check if already joined
    if (members_.count(session->sessionId()) > 0) {
        return JoinResult::AlreadyJoined;
    }

    // Check capacity
    if (static_cast<int>(members_.size()) >= config_.max_members) {
        return JoinResult::ChannelFull;
    }

    // Add member
    members_[session->sessionId()] = session;

    LOG_DEBUG("Session {} joined channel {} (members={})",
             session->sessionId(), config_.channel_id, members_.size());

    return JoinResult::Success;
}

bool Channel::removeMember(const std::string& session_id) {
    std::unique_lock<std::shared_mutex> lock(members_mutex_);

    auto it = members_.find(session_id);
    if (it == members_.end()) {
        return false;
    }

    members_.erase(it);

    LOG_DEBUG("Session {} left channel {} (members={})",
             session_id, config_.channel_id, members_.size());

    return true;
}

bool Channel::hasMember(const std::string& session_id) const {
    std::shared_lock<std::shared_mutex> lock(members_mutex_);
    return members_.count(session_id) > 0;
}

size_t Channel::memberCount() const {
    std::shared_lock<std::shared_mutex> lock(members_mutex_);
    return members_.size();
}

bool Channel::isFull() const {
    std::shared_lock<std::shared_mutex> lock(members_mutex_);
    return static_cast<int>(members_.size()) >= config_.max_members;
}

std::vector<Channel::SessionPtr> Channel::getMembers() const {
    std::shared_lock<std::shared_mutex> lock(members_mutex_);

    std::vector<SessionPtr> result;
    result.reserve(members_.size());

    for (const auto& [id, session] : members_) {
        result.push_back(session);
    }

    return result;
}

Channel::SessionPtr Channel::getMember(const std::string& session_id) const {
    std::shared_lock<std::shared_mutex> lock(members_mutex_);

    auto it = members_.find(session_id);
    return (it != members_.end()) ? it->second : nullptr;
}

void Channel::broadcast(const Packet& packet, const std::string& exclude_session, bool publish_to_redis) {
    auto data = PacketCodec::encode(packet);

    // 1. Broadcast to local members
    broadcastRaw(data, exclude_session);

#ifdef ENABLE_REDIS
    // 2. Publish to Redis Pub/Sub for other servers
    if (publish_to_redis && pubsub_) {
        PubSubMessage msg;
        msg.type = PubSubMessageType::ChannelMessage;
        msg.origin_server_id = pubsub_->serverId();
        msg.channel_id = config_.channel_id;
        msg.sender_session_id = exclude_session;

        // Create payload with packet type and data
        ChannelMessagePayload payload;
        payload.message_id = "";  // Will be set by caller if needed
        payload.content = packet.dataAsString();
        payload.message_type = static_cast<int>(packet.type);
        msg.payload = payload.serialize();

        std::string topic = "chat:channel:" + config_.channel_id;
        if (!pubsub_->publish(topic, msg)) {
            LOG_WARN("Failed to publish message to Redis for channel {}", config_.channel_id);
        }
    }
#else
    (void)publish_to_redis;  // Suppress unused parameter warning
#endif
}

void Channel::broadcastRaw(const std::vector<char>& data, const std::string& exclude_session) {
    std::shared_lock<std::shared_mutex> lock(members_mutex_);

    for (const auto& [id, session] : members_) {
        if (id != exclude_session && session->isConnected()) {
            session->sendRaw(data);
        }
    }
}

void Channel::addToHistory(const ChatMessage& message) {
    // Try external store first
    if (message_store_ && message_store_->isAvailable()) {
        message_store_->saveMessage(config_.channel_id, message);
    }

    // Always keep local history as well (for fast access)
    if (use_local_history_ || !message_store_ || !message_store_->isAvailable()) {
        std::lock_guard<std::mutex> lock(history_mutex_);

        local_history_.push_back(message);

        // Trim history if needed
        while (static_cast<int>(local_history_.size()) > config_.message_history_size) {
            local_history_.pop_front();
        }
    }
}

std::vector<ChatMessage> Channel::getRecentMessages(int count) const {
    // Try external store first
    if (message_store_ && message_store_->isAvailable()) {
        auto messages = message_store_->getRecentMessages(config_.channel_id, count, false);
        if (!messages.empty()) {
            return messages;
        }
    }

    // Fallback to local history
    std::lock_guard<std::mutex> lock(history_mutex_);

    std::vector<ChatMessage> result;

    int actual_count = std::min(count, static_cast<int>(local_history_.size()));
    result.reserve(actual_count);

    // Get last 'count' messages
    auto start = local_history_.end() - actual_count;
    for (auto it = start; it != local_history_.end(); ++it) {
        result.push_back(*it);
    }

    return result;
}

void Channel::clearHistory() {
    // Clear external store
    if (message_store_) {
        message_store_->clearChannel(config_.channel_id);
    }

    // Clear local history
    std::lock_guard<std::mutex> lock(history_mutex_);
    local_history_.clear();
}

#ifdef ENABLE_REDIS
void Channel::setPubSub(std::shared_ptr<RedisPubSub> pubsub) {
    pubsub_ = std::move(pubsub);
    LOG_DEBUG("Channel {} Pub/Sub enabled (server_id={})",
              config_.channel_id, pubsub_ ? pubsub_->serverId() : "null");
}

void Channel::onPubSubMessage(const PubSubMessage& msg) {
    // Ignore messages from this server (already broadcast locally)
    if (pubsub_ && msg.isFromServer(pubsub_->serverId())) {
        return;
    }

    // Deserialize the payload
    auto payload_opt = ChannelMessagePayload::tryDeserialize(msg.payload);
    if (!payload_opt) {
        LOG_WARN("Failed to deserialize Pub/Sub payload for channel {}", config_.channel_id);
        return;
    }

    const auto& payload = *payload_opt;

    // Reconstruct packet from payload
    Packet packet;
    packet.type = static_cast<PacketType>(payload.message_type);
    packet.data = std::vector<char>(payload.content.begin(), payload.content.end());

    // Broadcast to local members only (don't republish to Redis)
    broadcast(packet, msg.sender_session_id, false);

    LOG_DEBUG("Channel {} received Pub/Sub message from server {} (type={})",
              config_.channel_id, msg.origin_server_id,
              pubSubMessageTypeToString(msg.type));
}
#endif

} // namespace chat
