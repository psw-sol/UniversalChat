#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <universalchat/Types.hpp>

namespace chat {

// Forward declarations
class Config;
class IOContextPool;
class SessionManager;
class ChannelManager;
class WorldChannelManager;
class MessageDispatcher;
class Session;
class AnnouncementService;
class UserActionService;

#ifdef ENABLE_REDIS
class RedisClient;
class RedisPubSub;
class SessionRegistry;
class ChannelRegistry;
#endif

/**
 * Server
 *
 * Main server class that:
 * - Accepts incoming TCP connections
 * - Manages the IO thread pool
 * - Coordinates all server components
 * - Runs heartbeat checks
 */
class Server : public std::enable_shared_from_this<Server> {
public:
    using Ptr = std::shared_ptr<Server>;

    /**
     * Factory method to create a server instance
     */
    static Ptr create(const Config& config);

    ~Server();

    // Non-copyable
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // === Server Control ===
    void start();
    void stop();
    bool isRunning() const { return running_.load(); }

    // === Component Accessors ===
    SessionManager& sessionManager() { return *session_manager_; }
    ChannelManager& channelManager() { return *channel_manager_; }
    WorldChannelManager& worldChannelManager() { return *world_channel_manager_; }
    MessageDispatcher& messageDispatcher() { return *message_dispatcher_; }
    const Config& config() const { return config_; }

    // === Statistics ===
    ServerStats getStats() const;

private:
    explicit Server(const Config& config);

    void doAccept();
    void handleAccept(std::shared_ptr<Session> session,
                      const boost::system::error_code& ec);
    void runHeartbeatChecker();
    void runStatsUpdater();

    const Config& config_;
    std::unique_ptr<IOContextPool> io_pool_;
    boost::asio::ip::tcp::acceptor acceptor_;

    std::unique_ptr<SessionManager> session_manager_;
    std::unique_ptr<ChannelManager> channel_manager_;
    std::unique_ptr<WorldChannelManager> world_channel_manager_;
    std::unique_ptr<MessageDispatcher> message_dispatcher_;
    std::shared_ptr<AnnouncementService> announcement_service_;
    std::shared_ptr<UserActionService> user_action_service_;
#ifdef ENABLE_REDIS
    std::shared_ptr<RedisClient> redis_client_;
    std::shared_ptr<RedisPubSub> pubsub_;
    std::shared_ptr<SessionRegistry> session_registry_;
    std::shared_ptr<ChannelRegistry> channel_registry_;

    void initializeRedisPubSub();
    void setupPubSubHandlers();
    void handleIncomingWhisper(const std::string& channel, const class PubSubMessage& message);
    void handleIncomingAnnouncement(const std::string& channel, const class PubSubMessage& message);
    void handleIncomingUserActionNotification(const std::string& channel, const class PubSubMessage& message);
    void cleanupRedisOnShutdown();
#endif

    std::atomic<bool> running_{false};
    std::thread heartbeat_thread_;
    std::thread stats_thread_;

    // Statistics
    mutable std::mutex stats_mutex_;
    ServerStats stats_;
    TimePoint start_time_;
    std::atomic<uint64_t> messages_count_{0};
    std::atomic<uint64_t> last_messages_count_{0};
};

} // namespace chat
