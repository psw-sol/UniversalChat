#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <set>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <atomic>
#include <functional>
#include <universalchat/Types.hpp>
#include "../protocol/PacketTypes.hpp"

namespace chat {

// Forward declarations
class SessionManager;
class ChannelManager;
class MessageDispatcher;
class Config;

/**
 * Session
 *
 * Represents a single client connection.
 * Handles async reading/writing and maintains session state.
 */
class Session : public std::enable_shared_from_this<Session> {
public:
    using Ptr = std::shared_ptr<Session>;

    // Send operation result
    enum class SendResult { Success, QueueFull, Disconnected };

    Session(boost::asio::io_context& io_context,
            SessionManager& manager,
            ChannelManager& channel_manager,
            MessageDispatcher& dispatcher,
            const Config& config);

    ~Session();

    // Non-copyable
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // === Connection Management ===
    void start();
    void close();
    bool isConnected() const { return connected_.load(); }
    boost::asio::ip::tcp::socket& socket() { return socket_; }

    // === Send Operations ===
    SendResult send(const Packet& packet);
    SendResult sendRaw(std::vector<char> data);

    // === Write Queue Info ===
    size_t writeQueueSize() const;
    size_t writeQueueBytes() const;
    bool isWriteQueueFull() const;

    // === Session Information ===
    const std::string& sessionId() const { return session_id_; }
    const std::string& userId() const { return user_id_; }
    const std::string& nickname() const { return nickname_; }
    const std::string& profileImage() const { return profile_image_; }
    const std::string& frameImage() const { return frame_image_; }
    const std::string& extraData() const { return extra_data_; }
    std::string remoteAddress() const;
    uint16_t remotePort() const;

    // === Authentication ===
    void setAuthenticated(const std::string& user_id,
                          const std::string& nickname,
                          const std::string& auth_token = "",
                          const std::string& profile_image = "",
                          const std::string& frame_image = "",
                          const std::string& extra_data = "");
    bool isAuthenticated() const { return authenticated_.load(); }
    const std::string& authToken() const { return auth_token_; }

    // === Profile Management ===
    void updateProfile(const std::string* nickname,
                       const std::string* profile_image,
                       const std::string* frame_image,
                       const std::string* extra_data);

    // === Channel Management ===
    void addChannel(const std::string& channel_id);
    void removeChannel(const std::string& channel_id);
    bool isInChannel(const std::string& channel_id) const;
    std::set<std::string> getJoinedChannels() const;
    size_t channelCount() const;

    // === Heartbeat ===
    void updateHeartbeat();
    TimePoint lastHeartbeat() const;
    bool isExpired(Seconds timeout) const;

    // === Statistics ===
    SessionStats getStats() const;

private:
    // === Write Queue Limits ===
    static constexpr size_t MAX_WRITE_QUEUE_SIZE = 1000;
    static constexpr size_t WRITE_QUEUE_WARNING_THRESHOLD = 800;
    static constexpr size_t MAX_QUEUE_BYTES = 16 * 1024 * 1024;  // 16MB

    void doReadHeader();
    void doReadBody(uint32_t body_length);
    void doWrite();

    void handleReadHeader(const boost::system::error_code& ec, std::size_t bytes);
    void handleReadBody(const boost::system::error_code& ec, std::size_t bytes);
    void handleWrite(const boost::system::error_code& ec, std::size_t bytes);

    void processPacket();
    std::string generateSessionId();

    // Network
    boost::asio::ip::tcp::socket socket_;
    SessionManager& manager_;
    ChannelManager& channel_manager_;
    MessageDispatcher& dispatcher_;
    const Config& config_;

    // Session info
    std::string session_id_;
    std::string user_id_;
    std::string nickname_;
    std::string auth_token_;

    // Profile info
    std::string profile_image_;
    std::string frame_image_;
    std::string extra_data_;

    // State
    std::atomic<bool> connected_{false};
    std::atomic<bool> authenticated_{false};

    // Channels
    mutable std::shared_mutex channels_mutex_;
    std::set<std::string> joined_channels_;

    // Read buffer
    static constexpr size_t HEADER_SIZE = 8;
    std::array<char, HEADER_SIZE> header_buffer_;
    std::vector<char> body_buffer_;

    // Write queue
    mutable std::mutex write_mutex_;
    std::deque<std::vector<char>> write_queue_;
    std::atomic<bool> writing_{false};
    std::atomic<size_t> write_queue_bytes_{0};

    // Heartbeat
    mutable std::mutex heartbeat_mutex_;
    TimePoint last_heartbeat_;

    // Statistics
    mutable std::mutex stats_mutex_;
    SessionStats stats_;
};

} // namespace chat
