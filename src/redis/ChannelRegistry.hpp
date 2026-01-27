#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <cstdint>
#include <universalchat/Types.hpp>

namespace chat {

class RedisClient;

/**
 * ChannelRegistryInfo
 *
 * Channel metadata stored in Redis
 */
struct ChannelRegistryInfo {
    std::string channel_id;
    std::string channel_name;
    int max_members = 0;
    bool is_system = false;
    bool is_persistent = true;
    int64_t created_at = 0;
    int64_t global_member_count = 0;  // Cached count from Redis

    bool isValid() const { return !channel_id.empty(); }
};

/**
 * ChannelRegistry
 *
 * Global channel registry using Redis.
 * Enables accurate cross-server channel member counts and membership tracking.
 *
 * Redis data structure:
 *   # Channel members (Set)
 *   SADD chat:channel:{channel_id}:members "{user_id}"
 *
 *   # User's channels (Set) - for quick lookup of user's joined channels
 *   SADD chat:user:{user_id}:channels "{channel_id}"
 *
 *   # Channel metadata (Hash)
 *   HSET chat:channels:{channel_id}
 *     name            "World Chat #1"
 *     max_members     "1000"
 *     is_system       "1"
 *     is_persistent   "1"
 *     created_at      "1705827600"
 */
class ChannelRegistry {
public:
    /**
     * Constructor
     * @param redis Shared Redis client
     */
    explicit ChannelRegistry(std::shared_ptr<RedisClient> redis);

    ~ChannelRegistry() = default;

    // Non-copyable
    ChannelRegistry(const ChannelRegistry&) = delete;
    ChannelRegistry& operator=(const ChannelRegistry&) = delete;

    // === Member Management ===

    /**
     * Add a user to a channel
     * @param channel_id Channel identifier
     * @param user_id User identifier
     * @return true if added successfully (or already a member)
     */
    bool addMember(const std::string& channel_id, const std::string& user_id);

    /**
     * Remove a user from a channel
     * @param channel_id Channel identifier
     * @param user_id User identifier
     * @return true if removed successfully (or wasn't a member)
     */
    bool removeMember(const std::string& channel_id, const std::string& user_id);

    /**
     * Check if a user is a member of a channel
     * @param channel_id Channel identifier
     * @param user_id User identifier
     * @return true if user is a member
     */
    bool isMember(const std::string& channel_id, const std::string& user_id) const;

    // === Member Query ===

    /**
     * Get the global member count for a channel
     * @param channel_id Channel identifier
     * @return Member count (0 if channel doesn't exist or error)
     */
    int64_t getMemberCount(const std::string& channel_id) const;

    /**
     * Get a list of members in a channel (paginated)
     * @param channel_id Channel identifier
     * @param offset Starting index
     * @param count Maximum number of members to return
     * @return List of user IDs
     */
    std::vector<std::string> getMembers(const std::string& channel_id,
                                         int offset = 0,
                                         int count = 100) const;

    // === User Channel Query ===

    /**
     * Get list of channels a user has joined
     * @param user_id User identifier
     * @return List of channel IDs
     */
    std::vector<std::string> getUserChannels(const std::string& user_id) const;

    /**
     * Remove user from all channels (called on disconnect)
     * @param user_id User identifier
     * @return Number of channels the user was removed from
     */
    int removeUserFromAllChannels(const std::string& user_id);

    // === Channel Metadata ===

    /**
     * Set channel metadata in Redis
     * @param channel_id Channel identifier
     * @param config Channel configuration
     * @return true if set successfully
     */
    bool setChannelMetadata(const std::string& channel_id, const ChannelConfig& config);

    /**
     * Get channel metadata from Redis
     * @param channel_id Channel identifier
     * @return Channel info if found, nullopt otherwise
     */
    std::optional<ChannelRegistryInfo> getChannelMetadata(const std::string& channel_id) const;

    /**
     * Delete channel metadata from Redis
     * @param channel_id Channel identifier
     * @return true if deleted successfully
     */
    bool deleteChannelMetadata(const std::string& channel_id);

    /**
     * Get multiple channels' member counts in a batch
     * @param channel_ids List of channel identifiers
     * @return Map of channel_id to member count
     */
    std::unordered_map<std::string, int64_t> getMemberCountBatch(
        const std::vector<std::string>& channel_ids) const;

    // === Utility ===

    /**
     * Check if the registry is available (Redis connected)
     * @return true if Redis is connected
     */
    bool isAvailable() const;

private:
    /**
     * Generate Redis key for channel members set
     * @param channel_id Channel identifier
     * @return Redis key (e.g., "chat:channel:world-1:members")
     */
    std::string makeMembersKey(const std::string& channel_id) const;

    /**
     * Generate Redis key for user's channels set
     * @param user_id User identifier
     * @return Redis key (e.g., "chat:user:user123:channels")
     */
    std::string makeUserChannelsKey(const std::string& user_id) const;

    /**
     * Generate Redis key for channel metadata hash
     * @param channel_id Channel identifier
     * @return Redis key (e.g., "chat:channels:world-1")
     */
    std::string makeMetadataKey(const std::string& channel_id) const;

    std::shared_ptr<RedisClient> redis_;

    static constexpr const char* CHANNEL_MEMBERS_PREFIX = "chat:channel:";
    static constexpr const char* CHANNEL_MEMBERS_SUFFIX = ":members";
    static constexpr const char* USER_CHANNELS_PREFIX = "chat:user:";
    static constexpr const char* USER_CHANNELS_SUFFIX = ":channels";
    static constexpr const char* CHANNEL_METADATA_PREFIX = "chat:channels:";

    // Hash field names for channel metadata
    static constexpr const char* FIELD_NAME = "name";
    static constexpr const char* FIELD_MAX_MEMBERS = "max_members";
    static constexpr const char* FIELD_IS_SYSTEM = "is_system";
    static constexpr const char* FIELD_IS_PERSISTENT = "is_persistent";
    static constexpr const char* FIELD_CREATED_AT = "created_at";
};

} // namespace chat
