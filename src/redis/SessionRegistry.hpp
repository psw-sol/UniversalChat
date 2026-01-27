#pragma once

#include <string>
#include <optional>
#include <memory>
#include <cstdint>

namespace chat {

class RedisClient;

/**
 * SessionInfo
 *
 * Global session information stored in Redis
 */
struct SessionInfo {
    std::string user_id;
    std::string session_id;
    std::string server_id;
    std::string nickname;
    std::string profile_image;
    std::string frame_image;
    int64_t connected_at = 0;

    bool isValid() const { return !user_id.empty() && !session_id.empty(); }
};

/**
 * SessionRegistry
 *
 * Global session registry using Redis.
 * Enables cross-server session lookup for whisper routing and presence.
 *
 * Redis data structure:
 *   HSET chat:sessions:{user_id}
 *     server_id     "server-1"
 *     session_id    "sess-abc123"
 *     nickname      "PlayerOne"
 *     profile_image "avatar.png"
 *     frame_image   "frame.png"
 *     connected_at  "1705827600"
 *
 *   EXPIRE chat:sessions:{user_id} 120
 *
 * Nickname index (for findByNickname):
 *   SET chat:nickname:{lowercase_nickname} {user_id}
 *   EXPIRE chat:nickname:{lowercase_nickname} 120
 */
class SessionRegistry {
public:
    /**
     * Constructor
     * @param redis Shared Redis client
     * @param server_id Unique identifier for this server instance
     * @param ttl_seconds Session TTL in seconds (default: 120)
     */
    explicit SessionRegistry(std::shared_ptr<RedisClient> redis,
                             const std::string& server_id,
                             int ttl_seconds = 120);

    ~SessionRegistry() = default;

    // Non-copyable
    SessionRegistry(const SessionRegistry&) = delete;
    SessionRegistry& operator=(const SessionRegistry&) = delete;

    // === Session Registration ===

    /**
     * Register a new session in the global registry
     * @param user_id User's unique identifier
     * @param session_id Session's unique identifier
     * @param nickname User's display name
     * @param profile_image User's profile image URL
     * @param frame_image User's frame image URL
     * @return true if registration succeeded
     */
    bool registerSession(const std::string& user_id,
                         const std::string& session_id,
                         const std::string& nickname,
                         const std::string& profile_image = "",
                         const std::string& frame_image = "");

    /**
     * Unregister a session from the global registry
     * Only removes if the session belongs to this server
     * @param user_id User's unique identifier
     * @return true if unregistration succeeded
     */
    bool unregisterSession(const std::string& user_id);

    // === Session Query ===

    /**
     * Get session information by user ID
     * @param user_id User's unique identifier
     * @return SessionInfo if found, nullopt otherwise
     */
    std::optional<SessionInfo> getSession(const std::string& user_id) const;

    /**
     * Check if a user is online (has an active session)
     * @param user_id User's unique identifier
     * @return true if user has an active session
     */
    bool isUserOnline(const std::string& user_id) const;

    /**
     * Get the server ID where a user is connected
     * @param user_id User's unique identifier
     * @return Server ID if found, nullopt otherwise
     */
    std::optional<std::string> getUserServer(const std::string& user_id) const;

    // === Heartbeat ===

    /**
     * Refresh session TTL (called on heartbeat)
     * @param user_id User's unique identifier
     * @return true if refresh succeeded
     */
    bool refreshSession(const std::string& user_id);

    // === Nickname Lookup ===

    /**
     * Find a user by their nickname (case-insensitive)
     * @param nickname Nickname to search for
     * @return SessionInfo if found, nullopt otherwise
     */
    std::optional<SessionInfo> findByNickname(const std::string& nickname) const;

    // === Utility ===

    /**
     * Check if the registry is available (Redis connected)
     * @return true if Redis is connected
     */
    bool isAvailable() const;

    /**
     * Get the current server ID
     */
    const std::string& serverId() const { return server_id_; }

    /**
     * Get the TTL in seconds
     */
    int ttlSeconds() const { return ttl_seconds_; }

private:
    /**
     * Generate Redis key for session
     * @param user_id User's unique identifier
     * @return Redis key (e.g., "chat:sessions:user123")
     */
    std::string makeSessionKey(const std::string& user_id) const;

    /**
     * Generate Redis key for nickname index
     * @param nickname User's nickname (will be lowercased)
     * @return Redis key (e.g., "chat:nickname:playerone")
     */
    std::string makeNicknameKey(const std::string& nickname) const;

    /**
     * Convert nickname to lowercase for case-insensitive lookup
     */
    static std::string toLower(const std::string& str);

    std::shared_ptr<RedisClient> redis_;
    std::string server_id_;
    int ttl_seconds_;

    static constexpr const char* SESSION_KEY_PREFIX = "chat:sessions:";
    static constexpr const char* NICKNAME_KEY_PREFIX = "chat:nickname:";

    // Hash field names
    static constexpr const char* FIELD_SERVER_ID = "server_id";
    static constexpr const char* FIELD_SESSION_ID = "session_id";
    static constexpr const char* FIELD_NICKNAME = "nickname";
    static constexpr const char* FIELD_PROFILE_IMAGE = "profile_image";
    static constexpr const char* FIELD_FRAME_IMAGE = "frame_image";
    static constexpr const char* FIELD_CONNECTED_AT = "connected_at";
};

} // namespace chat
