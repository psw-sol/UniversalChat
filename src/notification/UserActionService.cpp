#include "UserActionService.hpp"
#include "../core/Session.hpp"
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

UserActionService::UserActionService(SessionManager& session_manager,
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

    LOG_INFO("UserActionService initialized with {} admin tokens", admin_tokens_.size());
}

#ifdef ENABLE_REDIS
void UserActionService::setRedisPubSub(std::shared_ptr<RedisPubSub> pubsub) {
    pubsub_ = std::move(pubsub);
    LOG_INFO("UserActionService: Redis Pub/Sub set (available={})",
             pubsub_ ? "true" : "false");
}
#endif

int UserActionService::processNotification(const std::string& notification_id,
                                           int action_type,
                                           const std::string& actor_user_id,
                                           const std::string& actor_nickname,
                                           const std::string& actor_profile_image,
                                           const std::string& actor_frame_image,
                                           const std::string& title,
                                           const std::string& content,
                                           const std::string& icon_id,
                                           const std::string& extra_data,
                                           const std::string& target_channel,
                                           bool exclude_actor,
                                           const std::string& admin_token) {
    // Validate admin token
    if (!validateAdminToken(admin_token)) {
        LOG_WARN("UserActionService: Invalid admin token for notification {}", notification_id);
        return -1;  // Invalid token
    }

    // Check deduplication
    if (isAlreadyProcessed(notification_id)) {
        LOG_DEBUG("UserActionService: Notification {} already processed", notification_id);
        return 0;
    }

    // Mark as processed
    markAsProcessed(notification_id);

    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    int local_count = 0;
    std::string exclude_user_id = exclude_actor ? actor_user_id : "";

#ifdef ENABLE_REDIS
    // If Redis is available, publish to Pub/Sub for cross-server broadcasting
    if (pubsub_ && pubsub_->isConnected()) {
        UserActionPayload payload;
        payload.notification_id = notification_id;
        payload.action_type = action_type;
        payload.actor_user_id = actor_user_id;
        payload.actor_nickname = actor_nickname;
        payload.actor_profile_image = actor_profile_image;
        payload.actor_frame_image = actor_frame_image;
        payload.title = title;
        payload.content = content;
        payload.icon_id = icon_id;
        payload.extra_data = extra_data;
        payload.target_channel = target_channel;
        payload.exclude_actor = exclude_actor;
        payload.timestamp = timestamp;

        PubSubMessage msg;
        msg.type = PubSubMessageType::UserActionNotification;
        msg.origin_server_id = pubsub_->serverId();
        msg.channel_id = target_channel;  // Target channel or empty for all
        msg.sender_user_id = actor_user_id;
        msg.payload = payload.serialize();

        // Publish to user action notification channel
        std::string pub_channel = "chat:useraction:global";
        if (!target_channel.empty()) {
            pub_channel = "chat:useraction:" + target_channel;
        }

        if (pubsub_->publish(pub_channel, msg)) {
            LOG_INFO("UserActionService: Published notification {} to Pub/Sub channel {}",
                     notification_id, pub_channel);
        } else {
            LOG_ERROR("UserActionService: Failed to publish notification {} to Pub/Sub",
                      notification_id);
        }
    }
#endif

    // Broadcast to local sessions
    local_count = broadcastLocal(notification_id, action_type, actor_user_id, actor_nickname,
                                  actor_profile_image, actor_frame_image, title, content,
                                  icon_id, extra_data, target_channel, exclude_user_id, timestamp);

    LOG_INFO("UserActionService: Processed notification {} (type={}, actor={}, local_count={})",
             notification_id, action_type, actor_nickname, local_count);

    return local_count;
}

int UserActionService::broadcastLocal(const std::string& notification_id,
                                      int action_type,
                                      const std::string& actor_user_id,
                                      const std::string& actor_nickname,
                                      const std::string& actor_profile_image,
                                      const std::string& actor_frame_image,
                                      const std::string& title,
                                      const std::string& content,
                                      const std::string& icon_id,
                                      const std::string& extra_data,
                                      const std::string& target_channel,
                                      const std::string& exclude_user_id,
                                      int64_t timestamp) {
    // Build UserActionNotificationReceive packet
    protocol::UserActionNotificationReceive notification;
    notification.set_notification_id(notification_id);
    notification.set_action_type(static_cast<protocol::UserActionType>(action_type));
    notification.set_actor_user_id(actor_user_id);
    notification.set_actor_nickname(actor_nickname);
    notification.set_actor_profile_image(actor_profile_image);
    notification.set_actor_frame_image(actor_frame_image);
    notification.set_title(title);
    notification.set_content(content);
    notification.set_icon_id(icon_id);
    notification.set_extra_data(extra_data);
    notification.set_timestamp(timestamp);

    Packet packet;
    packet.type = PacketType::UserActionNotificationReceive;
    std::string data;
    notification.SerializeToString(&data);
    packet.data = std::vector<char>(data.begin(), data.end());

    int delivered_count = 0;

    if (target_channel.empty()) {
        // Broadcast to all connected sessions (optionally excluding actor)
        if (exclude_user_id.empty()) {
            session_manager_.broadcast(packet);
            delivered_count = static_cast<int>(session_manager_.count());
        } else {
            // Broadcast with exclusion - iterate through all authenticated sessions
            session_manager_.forEachAuthenticated([&](const std::shared_ptr<Session>& session) {
                if (session && session->userId() != exclude_user_id) {
                    session->send(packet);
                    delivered_count++;
                }
            });
        }
        LOG_DEBUG("UserActionService: Broadcast to all sessions (count={}, excluded={})",
                  delivered_count, exclude_user_id);
    } else {
        // Broadcast to specific channel
        auto* channel = channel_manager_.getChannel(target_channel);
        if (channel) {
            if (exclude_user_id.empty()) {
                channel->broadcast(packet);
                delivered_count = static_cast<int>(channel->memberCount());
            } else {
                // Broadcast with exclusion - iterate through channel members
                auto members = channel->getMembers();
                for (const auto& member : members) {
                    if (member && member->userId() != exclude_user_id) {
                        member->send(packet);
                        delivered_count++;
                    }
                }
            }
            LOG_DEBUG("UserActionService: Broadcast to channel {} (count={}, excluded={})",
                      target_channel, delivered_count, exclude_user_id);
        } else {
            LOG_WARN("UserActionService: Target channel {} not found", target_channel);
        }
    }

    return delivered_count;
}

bool UserActionService::validateAdminToken(const std::string& token) const {
    if (token.empty()) {
        return false;
    }

    // Check against configured admin tokens
    if (admin_tokens_.find(token) != admin_tokens_.end()) {
        return true;
    }

    // If no admin tokens configured, allow any non-empty token (dev mode)
    if (admin_tokens_.empty()) {
        LOG_WARN("UserActionService: No admin tokens configured, allowing any token (dev mode)");
        return true;
    }

    return false;
}

bool UserActionService::isAlreadyProcessed(const std::string& notification_id) {
    std::lock_guard<std::mutex> lock(processed_mutex_);
    return processed_notifications_.find(notification_id) != processed_notifications_.end();
}

void UserActionService::markAsProcessed(const std::string& notification_id) {
    std::lock_guard<std::mutex> lock(processed_mutex_);

    // Clear old entries if cache is full (simple LRU-like behavior)
    if (processed_notifications_.size() >= MAX_PROCESSED_CACHE) {
        LOG_DEBUG("UserActionService: Clearing processed cache (size={})", processed_notifications_.size());
        processed_notifications_.clear();
    }

    processed_notifications_.insert(notification_id);
}

} // namespace chat
