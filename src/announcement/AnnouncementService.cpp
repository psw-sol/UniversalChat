#include "AnnouncementService.hpp"
#include "../core/SessionManager.hpp"
#include "../channel/ChannelManager.hpp"
#include "../channel/Channel.hpp"
#include "../util/Config.hpp"
#include "../util/Logger.hpp"
#include "../protocol/PacketCodec.hpp"

#ifdef ENABLE_REDIS
#include "../redis/RedisPubSub.hpp"
#include "../redis/PubSubMessage.hpp"
#endif

#include "chat.pb.h"
#include <chrono>

namespace chat {

AnnouncementService::AnnouncementService(SessionManager& session_manager,
                                         ChannelManager& channel_manager,
                                         const Config& config)
    : session_manager_(session_manager)
    , channel_manager_(channel_manager)
    , config_(config)
{
    // Load admin tokens from config
    for (const auto& token : config.getAdminTokens()) {
        admin_tokens_.insert(token);
    }

    LOG_INFO("AnnouncementService initialized with {} admin tokens", admin_tokens_.size());
}

#ifdef ENABLE_REDIS
void AnnouncementService::setRedisPubSub(std::shared_ptr<RedisPubSub> pubsub) {
    pubsub_ = std::move(pubsub);
    LOG_INFO("AnnouncementService: Redis Pub/Sub set (available={})",
             pubsub_ ? "true" : "false");
}
#endif

int AnnouncementService::processAnnouncement(const std::string& announcement_id,
                                             const std::string& content,
                                             int type,
                                             const std::string& sender_name,
                                             int duration_seconds,
                                             const std::string& target_channel,
                                             const std::string& admin_token,
                                             const std::string& extra_data) {
    // Validate admin token
    if (!validateAdminToken(admin_token)) {
        LOG_WARN("AnnouncementService: Invalid admin token for announcement {}", announcement_id);
        return -1;  // Invalid token
    }

    // Check deduplication
    if (isAlreadyProcessed(announcement_id)) {
        LOG_DEBUG("AnnouncementService: Announcement {} already processed", announcement_id);
        return 0;
    }

    // Mark as processed
    markAsProcessed(announcement_id);

    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    int local_count = 0;

#ifdef ENABLE_REDIS
    // If Redis is available, publish to Pub/Sub for cross-server broadcasting
    if (pubsub_ && pubsub_->isConnected()) {
        AnnouncementPayload payload;
        payload.announcement_id = announcement_id;
        payload.content = content;
        payload.type = type;
        payload.sender_name = sender_name;
        payload.duration_seconds = duration_seconds;
        payload.target_channel = target_channel;
        payload.extra_data = extra_data;
        payload.timestamp = timestamp;

        PubSubMessage msg;
        msg.type = PubSubMessageType::SystemBroadcast;
        msg.origin_server_id = pubsub_->serverId();
        msg.channel_id = target_channel;  // Target channel or empty for all
        msg.payload = payload.serialize();

        // Publish to announcement channel
        std::string pub_channel = "chat:announcement:global";
        if (!target_channel.empty()) {
            pub_channel = "chat:announcement:" + target_channel;
        }

        if (pubsub_->publish(pub_channel, msg)) {
            LOG_INFO("AnnouncementService: Published announcement {} to Pub/Sub channel {}",
                     announcement_id, pub_channel);
        } else {
            LOG_ERROR("AnnouncementService: Failed to publish announcement {} to Pub/Sub",
                      announcement_id);
        }
    }
#endif

    // Broadcast to local sessions
    local_count = broadcastLocal(announcement_id, content, type, sender_name,
                                  duration_seconds, target_channel, extra_data, timestamp);

    LOG_INFO("AnnouncementService: Processed announcement {} (type={}, local_count={})",
             announcement_id, type, local_count);

    return local_count;
}

int AnnouncementService::broadcastLocal(const std::string& announcement_id,
                                        const std::string& content,
                                        int type,
                                        const std::string& sender_name,
                                        int duration_seconds,
                                        const std::string& target_channel,
                                        const std::string& extra_data,
                                        int64_t timestamp) {
    // Build AnnouncementReceive packet
    protocol::AnnouncementReceive announcement;
    announcement.set_announcement_id(announcement_id);
    announcement.set_content(content);
    announcement.set_type(static_cast<protocol::AnnouncementType>(type));
    announcement.set_sender_name(sender_name);
    announcement.set_duration_seconds(duration_seconds);
    announcement.set_timestamp(timestamp);
    announcement.set_extra_data(extra_data);

    Packet packet;
    packet.type = PacketType::AnnouncementReceive;
    std::string data;
    announcement.SerializeToString(&data);
    packet.data = std::vector<char>(data.begin(), data.end());

    int delivered_count = 0;

    if (target_channel.empty()) {
        // Broadcast to all connected sessions
        session_manager_.broadcast(packet);
        delivered_count = static_cast<int>(session_manager_.count());
        LOG_DEBUG("AnnouncementService: Broadcast to all sessions (count={})", delivered_count);
    } else {
        // Broadcast to specific channel
        auto* channel = channel_manager_.getChannel(target_channel);
        if (channel) {
            channel->broadcast(packet);
            delivered_count = static_cast<int>(channel->memberCount());
            LOG_DEBUG("AnnouncementService: Broadcast to channel {} (count={})",
                      target_channel, delivered_count);
        } else {
            LOG_WARN("AnnouncementService: Target channel {} not found", target_channel);
        }
    }

    return delivered_count;
}

bool AnnouncementService::validateAdminToken(const std::string& token) const {
    if (token.empty()) {
        return false;
    }

    // Check against configured admin tokens
    if (admin_tokens_.find(token) != admin_tokens_.end()) {
        return true;
    }

    // If no admin tokens configured, allow any non-empty token (dev mode)
    if (admin_tokens_.empty()) {
        LOG_WARN("AnnouncementService: No admin tokens configured, allowing any token (dev mode)");
        return true;
    }

    return false;
}

bool AnnouncementService::isAlreadyProcessed(const std::string& announcement_id) {
    std::lock_guard<std::mutex> lock(processed_mutex_);
    return processed_announcements_.find(announcement_id) != processed_announcements_.end();
}

void AnnouncementService::markAsProcessed(const std::string& announcement_id) {
    std::lock_guard<std::mutex> lock(processed_mutex_);

    // Clear old entries if cache is full (simple LRU-like behavior)
    if (processed_announcements_.size() >= MAX_PROCESSED_CACHE) {
        // Clear half of the cache (oldest entries first would require ordered container)
        // For simplicity, we just clear everything when it gets too full
        LOG_DEBUG("AnnouncementService: Clearing processed cache (size={})", processed_announcements_.size());
        processed_announcements_.clear();
    }

    processed_announcements_.insert(announcement_id);
}

} // namespace chat
