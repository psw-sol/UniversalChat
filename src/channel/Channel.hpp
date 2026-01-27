#pragma once

#include <string>
#include <unordered_map>
#include <deque>
#include <shared_mutex>
#include <memory>
#include <vector>
#include <universalchat/Types.hpp>
#include "../protocol/PacketTypes.hpp"

namespace chat {

class Session;
class IMessageStore;

#ifdef ENABLE_REDIS
class RedisPubSub;
struct PubSubMessage;
#endif

/**
 * Channel
 *
 * Represents a chat channel containing multiple members.
 * Handles member management, message broadcasting, and history.
 */
class Channel {
public:
    using SessionPtr = std::shared_ptr<Session>;

    // Join result codes
    enum class JoinResult {
        Success,
        AlreadyJoined,
        ChannelFull,
        PasswordRequired,
        PasswordIncorrect,
        Forbidden
    };

    explicit Channel(const ChannelConfig& config, std::shared_ptr<IMessageStore> message_store = nullptr);
    ~Channel() = default;

#ifdef ENABLE_REDIS
    /**
     * Set the Pub/Sub client for cross-server message distribution
     * @param pubsub Shared pointer to RedisPubSub client
     */
    void setPubSub(std::shared_ptr<RedisPubSub> pubsub);

    /**
     * Handle incoming Pub/Sub message from another server
     * @param msg The message received from Redis Pub/Sub
     */
    void onPubSubMessage(const PubSubMessage& msg);
#endif

    // Non-copyable
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // === Channel Information ===
    const std::string& id() const { return config_.channel_id; }
    const std::string& name() const { return config_.channel_name; }
    int maxMembers() const { return config_.max_members; }
    bool isSystem() const { return config_.is_system; }
    bool isPersistent() const { return config_.is_persistent; }
    bool hasPassword() const { return !config_.password.empty(); }
    const ChannelConfig& getConfig() const { return config_; }

    // === Member Management ===
    JoinResult addMember(SessionPtr session, const std::string& password = "");
    bool removeMember(const std::string& session_id);
    bool hasMember(const std::string& session_id) const;
    size_t memberCount() const;
    bool isFull() const;

    // Get member list
    std::vector<SessionPtr> getMembers() const;
    SessionPtr getMember(const std::string& session_id) const;

    // === Broadcasting ===
    /**
     * Broadcast a packet to all channel members
     * @param packet The packet to broadcast
     * @param exclude_session Session ID to exclude (usually the sender)
     * @param publish_to_redis If true, also publish to Redis Pub/Sub for other servers (default: true)
     */
    void broadcast(const Packet& packet, const std::string& exclude_session = "", bool publish_to_redis = true);

    /**
     * Broadcast raw data to all local channel members only
     * @param data Raw packet data
     * @param exclude_session Session ID to exclude
     */
    void broadcastRaw(const std::vector<char>& data, const std::string& exclude_session = "");

    // === Message History ===
    void addToHistory(const ChatMessage& message);
    std::vector<ChatMessage> getRecentMessages(int count = 20) const;
    void clearHistory();

private:
    ChannelConfig config_;

    mutable std::shared_mutex members_mutex_;
    std::unordered_map<std::string, SessionPtr> members_;  // session_id -> Session

    // Message storage (can be Redis-backed or in-memory)
    std::shared_ptr<IMessageStore> message_store_;

    // Local in-memory history (fallback when no external store)
    mutable std::mutex history_mutex_;
    std::deque<ChatMessage> local_history_;
    bool use_local_history_ = true;

#ifdef ENABLE_REDIS
    // Redis Pub/Sub for cross-server message distribution
    std::shared_ptr<RedisPubSub> pubsub_;
#endif
};

} // namespace chat
