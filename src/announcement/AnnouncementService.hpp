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
 * AnnouncementService
 *
 * Handles system-wide announcements broadcasting.
 * Supports both local broadcasting and cross-server communication via Redis Pub/Sub.
 */
class AnnouncementService {
public:
    AnnouncementService(SessionManager& session_manager,
                        ChannelManager& channel_manager,
                        const Config& config);

    ~AnnouncementService() = default;

    // Non-copyable
    AnnouncementService(const AnnouncementService&) = delete;
    AnnouncementService& operator=(const AnnouncementService&) = delete;

#ifdef ENABLE_REDIS
    /**
     * Set Redis Pub/Sub for cross-server broadcasting
     */
    void setRedisPubSub(std::shared_ptr<RedisPubSub> pubsub);
#endif

    /**
     * Process an announcement request from game server or admin
     * @param announcement_id Unique announcement ID (for deduplication)
     * @param content Announcement content
     * @param type Announcement type (0=NORMAL, 1=URGENT, 2=MAINTENANCE, 3=EVENT)
     * @param sender_name Sender name (e.g., "GM", "System")
     * @param duration_seconds Display duration (0 = unlimited)
     * @param target_channel Target channel ID (empty = all users)
     * @param admin_token Admin authentication token
     * @param extra_data Additional JSON data
     * @return Number of users the announcement was delivered to (local only)
     */
    int processAnnouncement(const std::string& announcement_id,
                            const std::string& content,
                            int type,
                            const std::string& sender_name,
                            int duration_seconds,
                            const std::string& target_channel,
                            const std::string& admin_token,
                            const std::string& extra_data);

    /**
     * Broadcast an announcement to local sessions
     * Called by processAnnouncement or by Redis Pub/Sub handler
     * @return Number of users delivered to
     */
    int broadcastLocal(const std::string& announcement_id,
                       const std::string& content,
                       int type,
                       const std::string& sender_name,
                       int duration_seconds,
                       const std::string& target_channel,
                       const std::string& extra_data,
                       int64_t timestamp);

    /**
     * Validate admin token
     * @param token Admin authentication token
     * @return true if valid
     */
    bool validateAdminToken(const std::string& token) const;

    /**
     * Check if announcement was already processed (deduplication)
     * @param announcement_id Unique announcement ID
     * @return true if already processed
     */
    bool isAlreadyProcessed(const std::string& announcement_id);

    /**
     * Mark announcement as processed
     * @param announcement_id Unique announcement ID
     */
    void markAsProcessed(const std::string& announcement_id);

private:
    SessionManager& session_manager_;
    ChannelManager& channel_manager_;
    const Config& config_;

    // Deduplication: processed announcement IDs
    std::unordered_set<std::string> processed_announcements_;
    std::mutex processed_mutex_;

    // Max announcements to keep for deduplication (prevent memory leak)
    static constexpr size_t MAX_PROCESSED_CACHE = 10000;

    // Admin tokens (loaded from config)
    std::unordered_set<std::string> admin_tokens_;

#ifdef ENABLE_REDIS
    std::shared_ptr<RedisPubSub> pubsub_;
#endif
};

} // namespace chat
