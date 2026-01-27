#include "SessionRegistry.hpp"
#include "RedisClient.hpp"
#include "../util/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>

namespace chat {

SessionRegistry::SessionRegistry(std::shared_ptr<RedisClient> redis,
                                 const std::string& server_id,
                                 int ttl_seconds)
    : redis_(std::move(redis))
    , server_id_(server_id)
    , ttl_seconds_(ttl_seconds)
{
    LOG_INFO("SessionRegistry initialized (server_id={}, ttl={}s)", server_id_, ttl_seconds_);
}

// === Session Registration ===

bool SessionRegistry::registerSession(const std::string& user_id,
                                       const std::string& session_id,
                                       const std::string& nickname,
                                       const std::string& profile_image,
                                       const std::string& frame_image) {
#ifdef ENABLE_REDIS
    if (!redis_ || !redis_->isConnected()) {
        LOG_WARN("SessionRegistry: Redis not available for registerSession");
        return false;
    }

    std::string session_key = makeSessionKey(user_id);

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    // Store session info in hash
    bool success = true;
    success = success && redis_->hset(session_key, FIELD_SERVER_ID, server_id_);
    success = success && redis_->hset(session_key, FIELD_SESSION_ID, session_id);
    success = success && redis_->hset(session_key, FIELD_NICKNAME, nickname);
    success = success && redis_->hset(session_key, FIELD_PROFILE_IMAGE, profile_image);
    success = success && redis_->hset(session_key, FIELD_FRAME_IMAGE, frame_image);
    success = success && redis_->hset(session_key, FIELD_CONNECTED_AT, std::to_string(timestamp));

    if (!success) {
        LOG_ERROR("SessionRegistry: Failed to register session for user {}", user_id);
        return false;
    }

    // Set TTL
    redis_->expire(session_key, ttl_seconds_);

    // Create nickname index (case-insensitive)
    if (!nickname.empty()) {
        std::string nickname_key = makeNicknameKey(nickname);
        redis_->set(nickname_key, user_id, ttl_seconds_);
    }

    LOG_DEBUG("SessionRegistry: Registered session user_id={}, session_id={}, server_id={}",
              user_id, session_id, server_id_);

    return true;
#else
    (void)user_id; (void)session_id; (void)nickname;
    (void)profile_image; (void)frame_image;
    return false;
#endif
}

bool SessionRegistry::unregisterSession(const std::string& user_id) {
#ifdef ENABLE_REDIS
    if (!redis_ || !redis_->isConnected()) {
        LOG_WARN("SessionRegistry: Redis not available for unregisterSession");
        return false;
    }

    std::string session_key = makeSessionKey(user_id);

    // First, verify this session belongs to our server
    auto server_id_opt = redis_->hget(session_key, FIELD_SERVER_ID);
    if (!server_id_opt || *server_id_opt != server_id_) {
        LOG_DEBUG("SessionRegistry: Session {} not owned by this server (owner={})",
                  user_id, server_id_opt.value_or("none"));
        return false;
    }

    // Get nickname for index cleanup
    auto nickname_opt = redis_->hget(session_key, FIELD_NICKNAME);
    if (nickname_opt && !nickname_opt->empty()) {
        std::string nickname_key = makeNicknameKey(*nickname_opt);
        redis_->del(nickname_key);
    }

    // Delete session
    bool success = redis_->del(session_key);

    if (success) {
        LOG_DEBUG("SessionRegistry: Unregistered session user_id={}", user_id);
    }

    return success;
#else
    (void)user_id;
    return false;
#endif
}

// === Session Query ===

std::optional<SessionInfo> SessionRegistry::getSession(const std::string& user_id) const {
#ifdef ENABLE_REDIS
    if (!redis_ || !redis_->isConnected()) {
        return std::nullopt;
    }

    std::string session_key = makeSessionKey(user_id);

    // Get all fields at once
    auto fields = redis_->hgetall(session_key);
    if (fields.empty()) {
        return std::nullopt;
    }

    SessionInfo info;
    info.user_id = user_id;

    for (const auto& [field, value] : fields) {
        if (field == FIELD_SERVER_ID) {
            info.server_id = value;
        } else if (field == FIELD_SESSION_ID) {
            info.session_id = value;
        } else if (field == FIELD_NICKNAME) {
            info.nickname = value;
        } else if (field == FIELD_PROFILE_IMAGE) {
            info.profile_image = value;
        } else if (field == FIELD_FRAME_IMAGE) {
            info.frame_image = value;
        } else if (field == FIELD_CONNECTED_AT) {
            try {
                info.connected_at = std::stoll(value);
            } catch (...) {
                info.connected_at = 0;
            }
        }
    }

    if (!info.isValid()) {
        return std::nullopt;
    }

    return info;
#else
    (void)user_id;
    return std::nullopt;
#endif
}

bool SessionRegistry::isUserOnline(const std::string& user_id) const {
#ifdef ENABLE_REDIS
    if (!redis_ || !redis_->isConnected()) {
        return false;
    }

    std::string session_key = makeSessionKey(user_id);
    return redis_->exists(session_key);
#else
    (void)user_id;
    return false;
#endif
}

std::optional<std::string> SessionRegistry::getUserServer(const std::string& user_id) const {
#ifdef ENABLE_REDIS
    if (!redis_ || !redis_->isConnected()) {
        return std::nullopt;
    }

    std::string session_key = makeSessionKey(user_id);
    return redis_->hget(session_key, FIELD_SERVER_ID);
#else
    (void)user_id;
    return std::nullopt;
#endif
}

// === Heartbeat ===

bool SessionRegistry::refreshSession(const std::string& user_id) {
#ifdef ENABLE_REDIS
    if (!redis_ || !redis_->isConnected()) {
        return false;
    }

    std::string session_key = makeSessionKey(user_id);

    // Verify session exists and belongs to this server
    auto server_id_opt = redis_->hget(session_key, FIELD_SERVER_ID);
    if (!server_id_opt || *server_id_opt != server_id_) {
        return false;
    }

    // Refresh TTL
    bool success = redis_->expire(session_key, ttl_seconds_);

    // Also refresh nickname index TTL
    auto nickname_opt = redis_->hget(session_key, FIELD_NICKNAME);
    if (nickname_opt && !nickname_opt->empty()) {
        std::string nickname_key = makeNicknameKey(*nickname_opt);
        redis_->expire(nickname_key, ttl_seconds_);
    }

    return success;
#else
    (void)user_id;
    return false;
#endif
}

// === Nickname Lookup ===

std::optional<SessionInfo> SessionRegistry::findByNickname(const std::string& nickname) const {
#ifdef ENABLE_REDIS
    if (!redis_ || !redis_->isConnected()) {
        return std::nullopt;
    }

    if (nickname.empty()) {
        return std::nullopt;
    }

    std::string nickname_key = makeNicknameKey(nickname);

    // Look up user_id from nickname index
    auto user_id_opt = redis_->get(nickname_key);
    if (!user_id_opt || user_id_opt->empty()) {
        LOG_DEBUG("SessionRegistry: Nickname '{}' not found in index", nickname);
        return std::nullopt;
    }

    // Get full session info
    return getSession(*user_id_opt);
#else
    (void)nickname;
    return std::nullopt;
#endif
}

// === Utility ===

bool SessionRegistry::isAvailable() const {
#ifdef ENABLE_REDIS
    return redis_ && redis_->isConnected();
#else
    return false;
#endif
}

std::string SessionRegistry::makeSessionKey(const std::string& user_id) const {
    return std::string(SESSION_KEY_PREFIX) + user_id;
}

std::string SessionRegistry::makeNicknameKey(const std::string& nickname) const {
    return std::string(NICKNAME_KEY_PREFIX) + toLower(nickname);
}

std::string SessionRegistry::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

} // namespace chat
