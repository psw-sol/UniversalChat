#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <vector>
#include <functional>
#include <chrono>
#include <universalchat/Types.hpp>
#include "../protocol/PacketTypes.hpp"

namespace chat {

class Session;
class Config;

#ifdef ENABLE_REDIS
class SessionRegistry;
#endif

/**
 * SessionManager
 *
 * Manages all active sessions, including:
 * - Adding/removing sessions
 * - Looking up sessions by ID or user ID
 * - Broadcasting to all sessions
 * - Heartbeat expiration checks
 */
class SessionManager {
public:
    explicit SessionManager(const Config& config);
    ~SessionManager();

    // Non-copyable
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    // === Session Management ===
    void add(SessionPtr session);
    void remove(const std::string& session_id);
    SessionPtr get(const std::string& session_id) const;
    SessionPtr getByUserId(const std::string& user_id) const;

    // === User Mapping ===
    void updateUserMapping(const std::string& user_id, const std::string& session_id);
    void removeUserMapping(const std::string& user_id);

#ifdef ENABLE_REDIS
    // === Session Registry (Redis) ===
    /**
     * Set the session registry for cross-server session lookup
     * @param registry Shared pointer to SessionRegistry
     */
    void setSessionRegistry(std::shared_ptr<SessionRegistry> registry);

    /**
     * Get the session registry
     * @return Shared pointer to SessionRegistry (may be null)
     */
    std::shared_ptr<SessionRegistry> getSessionRegistry() const { return session_registry_; }

    /**
     * Register session in global registry (called after authentication)
     * @param session The session to register
     * @return true if registration succeeded
     */
    bool registerInGlobalRegistry(SessionPtr session);

    /**
     * Unregister session from global registry (called on disconnect)
     * @param session The session to unregister
     */
    void unregisterFromGlobalRegistry(SessionPtr session);

    /**
     * Refresh session in global registry (called on heartbeat)
     * @param user_id User ID to refresh
     * @return true if refresh succeeded
     */
    bool refreshInGlobalRegistry(const std::string& user_id);
#endif

    // === Queries ===
    size_t count() const;
    std::vector<SessionPtr> getAll() const;
    std::vector<SessionPtr> getExpiredSessions(Seconds timeout) const;
    std::vector<SessionPtr> getAuthenticatedSessions() const;

    // === Iteration ===
    void forEach(const SessionCallback& callback) const;
    void forEachAuthenticated(const SessionCallback& callback) const;

    // === Bulk Operations ===
    void closeAll();
    void broadcast(const Packet& packet);

    // === Statistics ===
    struct Stats {
        size_t total_sessions = 0;
        size_t authenticated_sessions = 0;
    };
    Stats getStats() const;

private:
    const Config& config_;

    mutable std::shared_mutex sessions_mutex_;
    std::unordered_map<std::string, SessionPtr> sessions_;           // session_id -> Session
    std::unordered_map<std::string, std::string> user_session_map_;  // user_id -> session_id

#ifdef ENABLE_REDIS
    // Global session registry for cross-server lookup
    std::shared_ptr<SessionRegistry> session_registry_;
#endif
};

} // namespace chat
