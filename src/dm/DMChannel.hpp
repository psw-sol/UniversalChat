#pragma once

#include <string>
#include <deque>
#include <mutex>
#include <memory>
#include <vector>
#include <unordered_set>
#include <universalchat/Types.hpp>

namespace chat {

class Session;

/**
 * DMChannel
 *
 * Represents a 1:1 direct message channel between exactly two users.
 * Manages message history and read receipts.
 */
class DMChannel {
public:
    using SessionPtr = std::shared_ptr<Session>;

    /**
     * Create a DM channel between two users.
     * @param user1_id First user ID (alphabetically sorted)
     * @param user2_id Second user ID (alphabetically sorted)
     * @param max_history Maximum messages to keep in memory
     */
    DMChannel(const std::string& user1_id, const std::string& user2_id, int max_history = 500);
    ~DMChannel() = default;

    // Non-copyable
    DMChannel(const DMChannel&) = delete;
    DMChannel& operator=(const DMChannel&) = delete;

    // === Channel Info ===
    const std::string& channelId() const { return channel_id_; }
    const std::string& user1Id() const { return user1_id_; }
    const std::string& user2Id() const { return user2_id_; }
    int64_t createdAt() const { return created_at_; }

    /**
     * Get the peer user ID for a given user.
     * @param my_user_id The requesting user's ID
     * @return The peer's user ID, or empty string if not a member
     */
    std::string getPeerId(const std::string& my_user_id) const;

    /**
     * Check if a user is a member of this DM channel.
     */
    bool isMember(const std::string& user_id) const;

    // === Message History ===
    void addMessage(const ChatMessage& message);
    std::vector<ChatMessage> getRecentMessages(int count = 20) const;
    std::vector<ChatMessage> getMessagesBefore(int64_t before_timestamp, int count = 30) const;
    const ChatMessage* getLastMessage() const;

    // === Read Receipts ===
    void markAsRead(const std::string& user_id, const std::string& message_id, int64_t timestamp);
    int64_t getLastReadTimestamp(const std::string& user_id) const;
    std::string getLastReadMessageId(const std::string& user_id) const;
    int getUnreadCount(const std::string& user_id) const;

    // === Deletion (soft delete per user) ===
    void markDeletedForUser(const std::string& user_id);
    bool isDeletedForUser(const std::string& user_id) const;
    void unmarkDeletedForUser(const std::string& user_id);

    // === Static Helpers ===
    /**
     * Generate a canonical DM channel ID from two user IDs.
     * The IDs are sorted alphabetically to ensure consistency.
     */
    static std::string makeDMChannelId(const std::string& user_id_a, const std::string& user_id_b);

private:
    std::string channel_id_;
    std::string user1_id_;  // Alphabetically first
    std::string user2_id_;  // Alphabetically second
    int64_t created_at_;
    int max_history_;

    // Message history (in-memory)
    mutable std::mutex history_mutex_;
    std::deque<ChatMessage> messages_;

    // Read receipts: user_id -> {last_read_message_id, last_read_timestamp}
    mutable std::mutex read_mutex_;
    struct ReadState {
        std::string last_read_message_id;
        int64_t last_read_timestamp = 0;
    };
    std::unordered_map<std::string, ReadState> read_states_;

    // Soft deletion per user
    mutable std::mutex delete_mutex_;
    std::unordered_set<std::string> deleted_by_;
};

} // namespace chat
