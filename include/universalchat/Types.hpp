#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <memory>
#include <functional>

namespace chat {

// Forward declarations
class Server;
class Session;
class SessionManager;
class Channel;
class ChannelManager;
class MessageDispatcher;
class Config;

// Type aliases
using SessionPtr = std::shared_ptr<Session>;
using SessionWeakPtr = std::weak_ptr<Session>;
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using SystemClock = std::chrono::system_clock;
using Milliseconds = std::chrono::milliseconds;
using Seconds = std::chrono::seconds;

// Common structures
struct ChatMessage {
    std::string message_id;
    std::string channel_id;
    std::string sender_id;
    std::string sender_nickname;
    std::string sender_profile_image;
    std::string sender_frame_image;
    std::string sender_extra_data;
    std::string content;
    int64_t timestamp;
    int message_type = 0;
};

// Channel configuration
struct ChannelConfig {
    std::string channel_id;
    std::string channel_name;
    int max_members = 100;
    int message_history_size = 50;
    bool is_persistent = true;
    bool is_system = false;
    std::string password;  // Empty means no password
};

// Server statistics
struct ServerStats {
    uint64_t total_connections = 0;
    uint64_t active_connections = 0;
    uint64_t total_messages = 0;
    uint64_t messages_per_second = 0;
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    int64_t uptime_seconds = 0;
};

// Session statistics
struct SessionStats {
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    uint64_t packets_sent = 0;
    uint64_t packets_received = 0;
};

// Callback types
using SessionCallback = std::function<void(SessionPtr)>;
using MessageCallback = std::function<void(const ChatMessage&)>;

// Constants
namespace constants {
    constexpr size_t HEADER_SIZE = 8;
    constexpr size_t MAX_PACKET_SIZE = 65535;
    constexpr size_t DEFAULT_BUFFER_SIZE = 8192;
    constexpr int DEFAULT_HEARTBEAT_INTERVAL = 30;
    constexpr int DEFAULT_HEARTBEAT_TIMEOUT = 90;
    constexpr int DEFAULT_MAX_CONNECTIONS = 10000;
    constexpr int DEFAULT_IO_THREADS = 4;
    constexpr int DEFAULT_PORT = 7777;
}

} // namespace chat
