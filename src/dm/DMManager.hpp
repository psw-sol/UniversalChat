#pragma once

#include "DMChannel.hpp"
#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <vector>
#include <universalchat/Types.hpp>

namespace chat {

class Session;
class SessionManager;
class Config;

/**
 * DM conversation summary for listing
 */
struct DMConversationSummary {
    std::string dm_channel_id;
    std::string peer_user_id;
    std::string peer_nickname;
    std::string peer_profile_image;
    std::string peer_frame_image;
    std::string peer_extra_data;
    std::string last_message_content;
    int64_t last_message_timestamp = 0;
    int unread_count = 0;
};

/**
 * DMManager
 *
 * Manages all DM channels:
 * - Creating/finding DM channels between two users
 * - Listing a user's DM conversations
 * - Routing DM messages
 * - Managing read receipts
 */
class DMManager {
public:
    using SessionPtr = std::shared_ptr<Session>;

    DMManager(SessionManager& session_manager, const Config& config);
    ~DMManager() = default;

    // Non-copyable
    DMManager(const DMManager&) = delete;
    DMManager& operator=(const DMManager&) = delete;

    /**
     * Get or create a DM channel between two users.
     * If the channel already exists, returns it. Otherwise creates a new one.
     * @return Pointer to the DM channel, or nullptr on error
     */
    DMChannel* getOrCreateDMChannel(const std::string& user1_id, const std::string& user2_id);

    /**
     * Get a DM channel by its ID.
     * @return Pointer to the DM channel, or nullptr if not found
     */
    DMChannel* getDMChannel(const std::string& dm_channel_id);

    /**
     * Get a user's DM conversation list with summaries.
     * @param user_id The user requesting their DM list
     * @param limit Maximum number of conversations to return
     * @param before_timestamp Only return conversations with last message before this timestamp (0 = no filter)
     * @return List of DM conversation summaries sorted by last message time (newest first)
     */
    std::vector<DMConversationSummary> getUserDMList(
        const std::string& user_id,
        int limit = 50,
        int64_t before_timestamp = 0);

    /**
     * Send a DM message. Delivers to the recipient if online.
     * @param dm_channel_id The DM channel ID
     * @param sender Session of the sender
     * @param content Message content
     * @param message_type Message type (0=text, etc.)
     * @param client_message_id Client-provided dedup ID
     * @return The generated message, or empty message on failure
     */
    ChatMessage sendDMMessage(const std::string& dm_channel_id,
                              SessionPtr sender,
                              const std::string& content,
                              int message_type = 0,
                              const std::string& client_message_id = "");

    /**
     * Mark messages as read in a DM channel.
     */
    void markAsRead(const std::string& dm_channel_id,
                    const std::string& user_id,
                    const std::string& last_read_message_id,
                    int64_t last_read_timestamp);

    /**
     * Delete (hide) a DM conversation for a user.
     * @return true if successful
     */
    bool deleteDMForUser(const std::string& dm_channel_id, const std::string& user_id);

    /**
     * Clean up resources when a session disconnects.
     */
    void onSessionDisconnect(SessionPtr session);

    /**
     * Get a list of user IDs that this user has DM channels with.
     */
    std::vector<std::string> getUserDMChannelIds(const std::string& user_id) const;

private:
    SessionManager& session_manager_;
    const Config& config_;

    // All DM channels indexed by channel_id
    mutable std::shared_mutex channels_mutex_;
    std::unordered_map<std::string, std::unique_ptr<DMChannel>> channels_;

    // User -> list of DM channel IDs (for fast lookup)
    mutable std::shared_mutex user_index_mutex_;
    std::unordered_map<std::string, std::vector<std::string>> user_dm_channels_;

    /**
     * Add a channel ID to a user's index.
     */
    void addToUserIndex(const std::string& user_id, const std::string& dm_channel_id);
};

} // namespace chat
