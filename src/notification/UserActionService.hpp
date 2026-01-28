#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_set>
#include <mutex>
#include "../protocol/PacketTypes.hpp"

namespace chat {

class SessionManager;
class ChannelManager;
class Config;

#ifdef ENABLE_REDIS
class RedisPubSub;
#endif

/**
 * UserActionService
 *
 * Handles user action notifications (item acquisition, ranking breakthroughs, etc.)
 * Supports both local broadcasting and cross-server communication via Redis Pub/Sub.
 */
class UserActionService {
public:
    UserActionService(SessionManager& session_manager,
                      ChannelManager& channel_manager,
                      const Config& config);

    ~UserActionService() = default;

    // Non-copyable
    UserActionService(const UserActionService&) = delete;
    UserActionService& operator=(const UserActionService&) = delete;

#ifdef ENABLE_REDIS
    /**
     * Set Redis Pub/Sub for cross-server broadcasting
     */
    void setRedisPubSub(std::shared_ptr<RedisPubSub> pubsub);
#endif

    /**
     * Process a user action notification request from game server
     * @param notification_id Unique notification ID (for deduplication)
     * @param action_type Action type (0=ITEM_ACQUIRE, 1=RANKING, 2=MISSION, 3=ACHIEVEMENT, 99=CUSTOM)
     * @param actor_user_id User ID who performed the action
     * @param actor_nickname Nickname of the user
     * @param actor_profile_image Profile image URL or ID
     * @param actor_frame_image Frame image URL or ID
     * @param title Notification title (for popup)
     * @param content Notification content (for chat message)
     * @param icon_id Icon ID (item icon, etc.)
     * @param extra_data Additional JSON data
     * @param target_channel Target channel ID (empty = all users)
     * @param exclude_actor Whether to exclude the actor from notification
     * @param admin_token Admin authentication token
     * @return Number of users the notification was delivered to (local only)
     */
    int processNotification(const std::string& notification_id,
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
                            const std::string& admin_token);

    /**
     * Broadcast a notification to local sessions
     * Called by processNotification or by Redis Pub/Sub handler
     * @param exclude_user_id User ID to exclude from broadcast (empty = none)
     * @return Number of users delivered to
     */
    int broadcastLocal(const std::string& notification_id,
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
                       int64_t timestamp);

    /**
     * Validate admin token
     * @param token Admin authentication token
     * @return true if valid
     */
    bool validateAdminToken(const std::string& token) const;

    /**
     * Check if notification was already processed (deduplication)
     * @param notification_id Unique notification ID
     * @return true if already processed
     */
    bool isAlreadyProcessed(const std::string& notification_id);

    /**
     * Mark notification as processed
     * @param notification_id Unique notification ID
     */
    void markAsProcessed(const std::string& notification_id);

private:
    SessionManager& session_manager_;
    ChannelManager& channel_manager_;
    const Config& config_;

    // Deduplication: processed notification IDs
    std::unordered_set<std::string> processed_notifications_;
    std::mutex processed_mutex_;

    // Max notifications to keep for deduplication (prevent memory leak)
    static constexpr size_t MAX_PROCESSED_CACHE = 10000;

    // Admin tokens (loaded from config)
    std::unordered_set<std::string> admin_tokens_;

#ifdef ENABLE_REDIS
    std::shared_ptr<RedisPubSub> pubsub_;
#endif
};

} // namespace chat
