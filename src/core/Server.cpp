#include "Server.hpp"
#include "IOContextPool.hpp"
#include "SessionManager.hpp"
#include "Session.hpp"
#include "../channel/ChannelManager.hpp"
#include "../channel/WorldChannelManager.hpp"
#include "../message/MessageDispatcher.hpp"
#include "../util/Config.hpp"
#include "../util/Logger.hpp"

#ifdef ENABLE_REDIS
#include "../redis/RedisClient.hpp"
#include "../redis/RedisPubSub.hpp"
#include "../redis/SessionRegistry.hpp"
#include "../redis/ChannelRegistry.hpp"
#include "../redis/PubSubMessage.hpp"
#include "../protocol/PacketCodec.hpp"
#include "../protocol/PacketTypes.hpp"
#include "chat.pb.h"
#endif

namespace chat {

Server::Ptr Server::create(const Config& config) {
    return Ptr(new Server(config));
}

Server::Server(const Config& config)
    : config_(config)
    , io_pool_(std::make_unique<IOContextPool>(config.io_threads))
    , acceptor_(io_pool_->getMainIOContext())
{
    // Initialize components
    session_manager_ = std::make_unique<SessionManager>(config);
    channel_manager_ = std::make_unique<ChannelManager>(config);

    // Initialize Redis client if enabled
#ifdef ENABLE_REDIS
    if (config.redis_enabled) {
        RedisClient::Config redis_config;
        redis_config.host = config.redis_host;
        redis_config.port = config.redis_port;
        redis_config.password = config.redis_password;
        redis_config.db = config.redis_db;
        redis_config.connect_timeout_ms = config.redis_connect_timeout_ms;
        redis_config.command_timeout_ms = config.redis_command_timeout_ms;

        redis_client_ = std::make_shared<RedisClient>(redis_config);
        if (redis_client_->connect()) {
            LOG_INFO("Redis connected to {}:{}", config.redis_host, config.redis_port);
        } else {
            LOG_WARN("Redis connection failed, continuing without Redis");
            redis_client_.reset();
        }
    }
    // Initialize message store (pass Redis client if available)
    channel_manager_->initializeMessageStore(redis_client_);
#else
    // Initialize message store without Redis
    channel_manager_->initializeMessageStore(nullptr);
#endif

    // Create predefined channels (non-world channels like trade, help)
    for (const auto& ch_config : config.predefined_channels) {
        // world 타입은 WorldChannelManager에서 관리하므로 스킵
        if (ch_config.channel_id.find("world") == 0 && ch_config.channel_id.find("-") == std::string::npos) {
            // "world" 채널은 스킵 (WorldChannelManager가 "world-1", "world-2" 등으로 관리)
            continue;
        }
        channel_manager_->createChannel(ch_config);
    }

    // Initialize WorldChannelManager for world channels
    WorldChannelManager::Config world_config;
    world_config.channel_type = "world";
    world_config.channel_name_prefix = "World Chat";
    world_config.min_channels = config.world_channel_min_count > 0 ?
                                config.world_channel_min_count : 3;
    world_config.max_members_per_channel = config.world_channel_max_members > 0 ?
                                           config.world_channel_max_members : 1000;
    world_config.scale_up_threshold = config.world_channel_scale_threshold > 0 ?
                                      config.world_channel_scale_threshold : 0.7f;
    world_config.cleanup_empty_threshold = 4;
    world_config.maintenance_interval_seconds = 30;
    world_config.is_system = true;

    world_channel_manager_ = std::make_unique<WorldChannelManager>(*channel_manager_, world_config);
    world_channel_manager_->initialize();

    // Initialize MessageDispatcher (after WorldChannelManager)
    message_dispatcher_ = std::make_unique<MessageDispatcher>(
        *session_manager_, *channel_manager_, *world_channel_manager_, config);

#ifdef ENABLE_REDIS
    // Initialize Redis Pub/Sub and Session Registry
    if (redis_client_ && redis_client_->isConnected()) {
        initializeRedisPubSub();

        // Pass Redis components to MessageDispatcher for cross-server routing
        if (session_registry_ && pubsub_) {
            message_dispatcher_->setRedisComponents(session_registry_, pubsub_);
        }

        // Pass SessionRegistry to SessionManager
        if (session_registry_) {
            session_manager_->setSessionRegistry(session_registry_);
        }
    }
#endif

    LOG_INFO("Server initialized");
}

Server::~Server() {
    stop();
}

void Server::start() {
    if (running_.exchange(true)) {
        return;  // Already running
    }

    LOG_INFO("Starting server on {}:{}...", config_.host, config_.port);

    try {
        // Configure acceptor
        boost::asio::ip::tcp::endpoint endpoint(
            boost::asio::ip::make_address(config_.host),
            config_.port
        );

        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();

        // Start accepting connections
        doAccept();

        // Start IO thread pool
        io_pool_->run();

        // Start heartbeat checker thread
        heartbeat_thread_ = std::thread([this]() {
            runHeartbeatChecker();
        });

        // Start stats updater thread
        stats_thread_ = std::thread([this]() {
            runStatsUpdater();
        });

        // Start WorldChannelManager maintenance
        world_channel_manager_->startMaintenance();

        start_time_ = Clock::now();

        LOG_INFO("Server started successfully on {}:{}", config_.host, config_.port);

    } catch (const boost::system::system_error& e) {
        running_ = false;
        LOG_ERROR("Failed to start server: {}", e.what());
        throw;
    }
}

void Server::stop() {
    if (!running_.exchange(false)) {
        return;  // Already stopped
    }

    LOG_INFO("Stopping server...");

#ifdef ENABLE_REDIS
    // Clean up Redis data before closing sessions
    cleanupRedisOnShutdown();
#endif

    // Stop WorldChannelManager maintenance
    if (world_channel_manager_) {
        world_channel_manager_->stopMaintenance();
    }

    // Close acceptor
    boost::system::error_code ec;
    acceptor_.close(ec);

    // Close all sessions
    session_manager_->closeAll();

    // Stop IO pool
    io_pool_->stop();

    // Join threads
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    if (stats_thread_.joinable()) {
        stats_thread_.join();
    }

    LOG_INFO("Server stopped");
}

void Server::doAccept() {
    auto session = std::make_shared<Session>(
        io_pool_->getIOContext(),
        *session_manager_,
        *channel_manager_,
        *message_dispatcher_,
        config_
    );

    acceptor_.async_accept(
        session->socket(),
        [this, session](const boost::system::error_code& ec) {
            handleAccept(session, ec);
        }
    );
}

void Server::handleAccept(std::shared_ptr<Session> session,
                          const boost::system::error_code& ec) {
    if (!running_) {
        return;
    }

    if (!ec) {
        // Check max connections
        if (session_manager_->count() >= static_cast<size_t>(config_.max_connections)) {
            LOG_WARN("Max connections ({}) reached, rejecting new connection",
                    config_.max_connections);
            session->socket().close();
        } else {
            // Start the session
            session->start();

            // Update statistics
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.total_connections++;
            }
        }
    } else if (ec != boost::asio::error::operation_aborted) {
        LOG_ERROR("Accept error: {}", ec.message());
    }

    // Accept next connection
    doAccept();
}

void Server::runHeartbeatChecker() {
    const auto interval = Seconds(config_.heartbeat_check_interval);
    const auto timeout = Seconds(config_.heartbeat_timeout);

    while (running_) {
        std::this_thread::sleep_for(interval);

        if (!running_) break;

        auto expired = session_manager_->getExpiredSessions(timeout);

        for (auto& session : expired) {
            LOG_INFO("Session {} heartbeat timeout ({}s)",
                    session->sessionId(), config_.heartbeat_timeout);
            session->close();
        }

        if (!expired.empty()) {
            LOG_DEBUG("Closed {} expired sessions", expired.size());
        }
    }
}

void Server::runStatsUpdater() {
    while (running_) {
        std::this_thread::sleep_for(Seconds(1));

        if (!running_) break;

        // Calculate messages per second
        uint64_t current = messages_count_.load();
        uint64_t last = last_messages_count_.exchange(current);
        uint64_t mps = current - last;

        // Update stats
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.messages_per_second = mps;
            stats_.active_connections = session_manager_->count();

            auto now = Clock::now();
            stats_.uptime_seconds = std::chrono::duration_cast<Seconds>(
                now - start_time_).count();
        }
    }
}

ServerStats Server::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

#ifdef ENABLE_REDIS
void Server::initializeRedisPubSub() {
    // Create SessionRegistry
    session_registry_ = std::make_shared<SessionRegistry>(
        redis_client_,
        config_.server_id,
        config_.session_registry_ttl > 0 ? config_.session_registry_ttl : 120
    );

    LOG_INFO("SessionRegistry initialized (server_id={})", config_.server_id);

    // Create ChannelRegistry for global channel membership
    channel_registry_ = std::make_shared<ChannelRegistry>(redis_client_);
    channel_manager_->initializeChannelRegistry(channel_registry_);

    LOG_INFO("ChannelRegistry initialized");

    // Create RedisPubSub
    RedisPubSub::Config pubsub_config;
    pubsub_config.host = config_.redis_host;
    pubsub_config.port = config_.redis_port;
    pubsub_config.password = config_.redis_password;
    pubsub_config.db = config_.redis_db;
    pubsub_config.connect_timeout_ms = config_.redis_connect_timeout_ms;
    pubsub_config.server_id = config_.server_id;

    pubsub_ = std::make_shared<RedisPubSub>(pubsub_config);
    pubsub_->setPublishClient(redis_client_);

    if (pubsub_->connect()) {
        LOG_INFO("RedisPubSub connected");
        setupPubSubHandlers();
    } else {
        LOG_WARN("RedisPubSub connection failed: {}", pubsub_->lastError());
        pubsub_.reset();
    }
}

void Server::setupPubSubHandlers() {
    if (!pubsub_) return;

    // Set message handler for incoming Pub/Sub messages
    pubsub_->setMessageHandler([this](const std::string& channel, const PubSubMessage& message) {
        // Skip messages from our own server
        if (message.isFromServer(pubsub_->serverId())) {
            return;
        }

        switch (message.type) {
            case PubSubMessageType::Whisper:
                handleIncomingWhisper(channel, message);
                break;

            // Other message types can be handled here (ChannelMessage, etc.)
            default:
                LOG_DEBUG("Received Pub/Sub message type {} on channel {}",
                          static_cast<int>(message.type), channel);
                break;
        }
    });

    // Subscribe to whisper pattern for this server's users
    // When a user authenticates, we'll subscribe to their specific whisper channel
    // For now, we use a pattern subscription to catch all whispers routed to this server
    pubsub_->psubscribe("chat:whisper:*");

    // Start listening
    pubsub_->startListening();

    LOG_INFO("Pub/Sub handlers configured and listening started");
}

void Server::handleIncomingWhisper(const std::string& channel, const PubSubMessage& message) {
    // Extract target user_id from channel (chat:whisper:{user_id})
    std::string target_user_id = message.target_user_id;
    if (target_user_id.empty()) {
        // Try to extract from channel name
        const std::string prefix = "chat:whisper:";
        if (channel.find(prefix) == 0) {
            target_user_id = channel.substr(prefix.length());
        }
    }

    if (target_user_id.empty()) {
        LOG_WARN("Received whisper Pub/Sub message without target user ID");
        return;
    }

    // Find the target session on this server
    auto target_session = session_manager_->getByUserId(target_user_id);
    if (!target_session || !target_session->isConnected()) {
        LOG_DEBUG("Whisper target {} not found on this server (may have disconnected)",
                  target_user_id);
        return;
    }

    // Parse whisper payload
    auto payload_opt = WhisperPayload::tryDeserialize(message.payload);
    if (!payload_opt) {
        LOG_WARN("Failed to deserialize whisper payload");
        return;
    }

    const auto& payload = *payload_opt;

    // Build WhisperReceive packet
    protocol::WhisperReceive whisper;
    whisper.set_message_id(payload.message_id);
    whisper.set_sender_id(message.sender_user_id);
    whisper.set_sender_nickname(payload.sender_nickname);
    whisper.set_sender_profile_image(payload.sender_profile_image);
    whisper.set_sender_frame_image(payload.sender_frame_image);
    whisper.set_content(payload.content);
    whisper.set_timestamp(message.timestamp);

    Packet whisper_packet;
    whisper_packet.type = PacketType::WhisperReceive;
    std::string whisper_data;
    whisper.SerializeToString(&whisper_data);
    whisper_packet.data = std::vector<char>(whisper_data.begin(), whisper_data.end());

    // Send to target
    target_session->send(whisper_packet);

    LOG_DEBUG("Delivered cross-server whisper from {} to {}",
              message.sender_user_id, target_user_id);
}

void Server::cleanupRedisOnShutdown() {
    LOG_INFO("Cleaning up Redis data for server shutdown...");

    // Get all active sessions and clean up their data
    auto sessions = session_manager_->getAll();
    int session_cleanup_count = 0;
    int channel_cleanup_count = 0;

    for (const auto& session : sessions) {
        if (!session || session->userId().empty()) {
            continue;
        }

        // Unregister session from global registry
        if (session_registry_ && session_registry_->isAvailable()) {
            session_registry_->unregisterSession(session->userId());
            session_cleanup_count++;
        }

        // Remove from all channel memberships
        if (channel_registry_ && channel_registry_->isAvailable()) {
            int removed = channel_registry_->removeUserFromAllChannels(session->userId());
            channel_cleanup_count += removed;
        }
    }

    // Stop Pub/Sub listening
    if (pubsub_) {
        pubsub_->stopListening();
    }

    LOG_INFO("Redis cleanup completed: {} sessions, {} channel memberships removed",
             session_cleanup_count, channel_cleanup_count);
}
#endif

} // namespace chat
