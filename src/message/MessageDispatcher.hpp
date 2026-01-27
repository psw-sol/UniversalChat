#pragma once

#include "../protocol/PacketTypes.hpp"
#include "../util/RateLimiter.hpp"
#include <functional>
#include <unordered_map>
#include <memory>
#include <universalchat/Types.hpp>

namespace chat {

class Session;
class SessionManager;
class ChannelManager;
class WorldChannelManager;
class Config;

#ifdef ENABLE_REDIS
class SessionRegistry;
class RedisPubSub;
#endif

/**
 * MessageDispatcher
 *
 * Routes incoming packets to appropriate handlers.
 * Manages all packet type handling logic.
 */
class MessageDispatcher {
public:
    using SessionPtr = std::shared_ptr<Session>;
    using Handler = std::function<void(SessionPtr, const Packet&)>;

    MessageDispatcher(SessionManager& session_manager,
                      ChannelManager& channel_manager,
                      WorldChannelManager& world_channel_manager,
                      const Config& config);

    ~MessageDispatcher() = default;

#ifdef ENABLE_REDIS
    /**
     * Set Redis components for cross-server communication
     * @param session_registry Global session registry for user lookup
     * @param pubsub Redis Pub/Sub client for cross-server messaging
     */
    void setRedisComponents(std::shared_ptr<SessionRegistry> session_registry,
                            std::shared_ptr<RedisPubSub> pubsub);
#endif

    // Non-copyable
    MessageDispatcher(const MessageDispatcher&) = delete;
    MessageDispatcher& operator=(const MessageDispatcher&) = delete;

    // === Message Dispatch ===
    void dispatch(SessionPtr session, const Packet& packet);

    // === Handler Registration ===
    void registerHandler(PacketType type, Handler handler);
    void unregisterHandler(PacketType type);

    // === Public Handlers (for internal use) ===
    void handleChannelLeave(SessionPtr session, const std::string& channel_id);

private:
    // === Handler Functions ===
    void handleHeartbeat(SessionPtr session, const Packet& packet);
    void handleAuth(SessionPtr session, const Packet& packet);
    void handleChannelList(SessionPtr session, const Packet& packet);
    void handleChannelJoin(SessionPtr session, const Packet& packet);
    void handleChannelLeaveRequest(SessionPtr session, const Packet& packet);
    void handleMessageSend(SessionPtr session, const Packet& packet);
    void handleWhisperSend(SessionPtr session, const Packet& packet);
    void handleProfileUpdate(SessionPtr session, const Packet& packet);
    void handleChannelAutoAssign(SessionPtr session, const Packet& packet);

    // === Helper Functions ===
    void sendError(SessionPtr session, int error_code, const std::string& message);
    bool checkAuthenticated(SessionPtr session, const Packet& packet);
    int getPacketCost(PacketType type) const;

    SessionManager& session_manager_;
    ChannelManager& channel_manager_;
    WorldChannelManager& world_channel_manager_;
    const Config& config_;

    std::unordered_map<PacketType, Handler> handlers_;

    // Rate limiting
    RateLimiter rate_limiter_;

#ifdef ENABLE_REDIS
    // Cross-server components
    std::shared_ptr<SessionRegistry> session_registry_;
    std::shared_ptr<RedisPubSub> pubsub_;
#endif
};

} // namespace chat
