#include "SessionManager.hpp"
#include "Session.hpp"
#include "../util/Config.hpp"
#include "../util/Logger.hpp"
#include "../protocol/PacketCodec.hpp"

#ifdef ENABLE_REDIS
#include "../redis/SessionRegistry.hpp"
#endif

namespace chat {

SessionManager::SessionManager(const Config& config)
    : config_(config)
{
    sessions_.reserve(config.max_connections);
    LOG_INFO("SessionManager initialized (max_connections={})", config.max_connections);
}

SessionManager::~SessionManager() {
    closeAll();
}

void SessionManager::add(SessionPtr session) {
    std::unique_lock<std::shared_mutex> lock(sessions_mutex_);
    sessions_[session->sessionId()] = session;
    LOG_DEBUG("Session {} added (total={})", session->sessionId(), sessions_.size());
}

void SessionManager::remove(const std::string& session_id) {
    std::unique_lock<std::shared_mutex> lock(sessions_mutex_);

    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        // Remove user mapping if authenticated
        const auto& user_id = it->second->userId();
        if (!user_id.empty()) {
            user_session_map_.erase(user_id);
        }

        sessions_.erase(it);
        LOG_DEBUG("Session {} removed (total={})", session_id, sessions_.size());
    }
}

SessionPtr SessionManager::get(const std::string& session_id) const {
    std::shared_lock<std::shared_mutex> lock(sessions_mutex_);

    auto it = sessions_.find(session_id);
    return (it != sessions_.end()) ? it->second : nullptr;
}

SessionPtr SessionManager::getByUserId(const std::string& user_id) const {
    std::shared_lock<std::shared_mutex> lock(sessions_mutex_);

    auto it = user_session_map_.find(user_id);
    if (it != user_session_map_.end()) {
        auto session_it = sessions_.find(it->second);
        if (session_it != sessions_.end()) {
            return session_it->second;
        }
    }
    return nullptr;
}

void SessionManager::updateUserMapping(const std::string& user_id,
                                       const std::string& session_id) {
    std::unique_lock<std::shared_mutex> lock(sessions_mutex_);
    user_session_map_[user_id] = session_id;
}

void SessionManager::removeUserMapping(const std::string& user_id) {
    std::unique_lock<std::shared_mutex> lock(sessions_mutex_);
    user_session_map_.erase(user_id);
}

size_t SessionManager::count() const {
    std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
    return sessions_.size();
}

std::vector<SessionPtr> SessionManager::getAll() const {
    std::shared_lock<std::shared_mutex> lock(sessions_mutex_);

    std::vector<SessionPtr> result;
    result.reserve(sessions_.size());

    for (const auto& [id, session] : sessions_) {
        result.push_back(session);
    }

    return result;
}

std::vector<SessionPtr> SessionManager::getExpiredSessions(Seconds timeout) const {
    std::shared_lock<std::shared_mutex> lock(sessions_mutex_);

    std::vector<SessionPtr> expired;

    for (const auto& [id, session] : sessions_) {
        if (session->isExpired(timeout)) {
            expired.push_back(session);
        }
    }

    return expired;
}

std::vector<SessionPtr> SessionManager::getAuthenticatedSessions() const {
    std::shared_lock<std::shared_mutex> lock(sessions_mutex_);

    std::vector<SessionPtr> authenticated;

    for (const auto& [id, session] : sessions_) {
        if (session->isAuthenticated()) {
            authenticated.push_back(session);
        }
    }

    return authenticated;
}

void SessionManager::forEach(const SessionCallback& callback) const {
    std::shared_lock<std::shared_mutex> lock(sessions_mutex_);

    for (const auto& [id, session] : sessions_) {
        callback(session);
    }
}

void SessionManager::forEachAuthenticated(const SessionCallback& callback) const {
    std::shared_lock<std::shared_mutex> lock(sessions_mutex_);

    for (const auto& [id, session] : sessions_) {
        if (session->isAuthenticated()) {
            callback(session);
        }
    }
}

void SessionManager::closeAll() {
    std::vector<SessionPtr> all_sessions;

    {
        std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
        for (const auto& [id, session] : sessions_) {
            all_sessions.push_back(session);
        }
    }

    for (auto& session : all_sessions) {
        session->close();
    }

    LOG_INFO("Closed {} sessions", all_sessions.size());
}

void SessionManager::broadcast(const Packet& packet) {
    auto encoded = PacketCodec::encode(packet);

    std::shared_lock<std::shared_mutex> lock(sessions_mutex_);

    for (const auto& [id, session] : sessions_) {
        if (session->isConnected()) {
            session->sendRaw(encoded);
        }
    }
}

SessionManager::Stats SessionManager::getStats() const {
    std::shared_lock<std::shared_mutex> lock(sessions_mutex_);

    Stats stats;
    stats.total_sessions = sessions_.size();

    for (const auto& [id, session] : sessions_) {
        if (session->isAuthenticated()) {
            stats.authenticated_sessions++;
        }
    }

    return stats;
}

#ifdef ENABLE_REDIS
void SessionManager::setSessionRegistry(std::shared_ptr<SessionRegistry> registry) {
    session_registry_ = std::move(registry);
    LOG_INFO("SessionManager: SessionRegistry set (available={})",
             session_registry_ ? session_registry_->isAvailable() : false);
}

bool SessionManager::registerInGlobalRegistry(SessionPtr session) {
    if (!session_registry_ || !session_registry_->isAvailable()) {
        return false;
    }

    if (!session->isAuthenticated()) {
        LOG_WARN("SessionManager: Cannot register unauthenticated session in global registry");
        return false;
    }

    bool success = session_registry_->registerSession(
        session->userId(),
        session->sessionId(),
        session->nickname(),
        session->profileImage(),
        session->frameImage()
    );

    if (success) {
        LOG_DEBUG("SessionManager: Registered session {} (user={}) in global registry",
                  session->sessionId(), session->userId());
    } else {
        LOG_WARN("SessionManager: Failed to register session {} in global registry",
                 session->sessionId());
    }

    return success;
}

void SessionManager::unregisterFromGlobalRegistry(SessionPtr session) {
    if (!session_registry_ || !session_registry_->isAvailable()) {
        return;
    }

    if (session->userId().empty()) {
        return;
    }

    bool success = session_registry_->unregisterSession(session->userId());

    if (success) {
        LOG_DEBUG("SessionManager: Unregistered session {} (user={}) from global registry",
                  session->sessionId(), session->userId());
    }
}

bool SessionManager::refreshInGlobalRegistry(const std::string& user_id) {
    if (!session_registry_ || !session_registry_->isAvailable()) {
        return false;
    }

    return session_registry_->refreshSession(user_id);
}
#endif

} // namespace chat
