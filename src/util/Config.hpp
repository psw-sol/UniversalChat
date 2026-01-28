#pragma once

#include <string>
#include <vector>
#include <universalchat/Types.hpp>
#include <nlohmann/json.hpp>

namespace chat {

class Config {
public:
    Config();
    ~Config() = default;

    // Load configuration from file
    bool loadFromFile(const std::string& filepath);

    // Load configuration from JSON string
    bool loadFromJson(const std::string& json_str);

    // === Server Settings ===
    std::string host = "0.0.0.0";
    uint16_t port = 7777;
    int io_threads = 4;
    int max_connections = 10000;
    size_t max_packet_size = 65535;

    // === Connection Settings ===
    int heartbeat_interval = 30;      // seconds
    int heartbeat_timeout = 90;       // seconds
    int heartbeat_check_interval = 10; // seconds
    size_t read_buffer_size = 8192;
    size_t write_buffer_size = 16384;

    // === Channel Settings ===
    int default_max_members = 100;
    int message_history_size = 50;
    std::vector<ChannelConfig> predefined_channels;

    // === World Channel Sharding Settings ===
    int world_channel_min_count = 3;           // 최소 월드 채널 수
    int world_channel_max_members = 1000;      // 월드 채널당 최대 인원
    float world_channel_scale_threshold = 0.7f; // 확장 임계치 (70%)

    // === Auth Settings ===
    std::string auth_provider = "simple";
    bool token_validation = false;

    // === Announcement Settings ===
    std::vector<std::string> admin_tokens;  // Admin tokens for announcement authorization

    // Get admin tokens
    const std::vector<std::string>& getAdminTokens() const { return admin_tokens; }

    // === Redis Settings ===
    bool redis_enabled = false;
    std::string redis_host = "127.0.0.1";
    uint16_t redis_port = 6379;
    std::string redis_password;
    int redis_db = 0;
    int redis_connect_timeout_ms = 5000;
    int redis_command_timeout_ms = 1000;

    // === Scale-Out Settings ===
    std::string server_id;              // Unique ID for this server instance (auto-generated if empty)

    // === Pub/Sub Settings ===
    bool pubsub_enabled = true;                     // Enable Redis Pub/Sub
    int pubsub_reconnect_interval_ms = 5000;        // Reconnect interval on disconnect
    std::vector<std::string> pubsub_channel_patterns = {   // Pub/Sub channel patterns to subscribe
        "chat:channel:*",
        "chat:whisper:*",
        "chat:system"
    };

    // === Session Registry Settings ===
    bool session_registry_enabled = true;           // Enable session registry
    int session_registry_ttl = 120;                 // Session TTL in Redis (seconds)
    bool session_registry_refresh_on_heartbeat = true;  // Refresh TTL on heartbeat

    // === Channel Registry Settings ===
    bool channel_registry_enabled = true;           // Enable channel registry
    bool channel_registry_sync_member_count = true; // Sync global member count

    // === Message Storage Settings ===
    bool message_persistence_enabled = false;
    std::string message_store_key_prefix = "chat:messages:";
    int message_store_max_per_channel = 1000;
    int message_store_ttl_seconds = 86400 * 7;  // 7 days

    // === Logging Settings ===
    std::string log_level = "info";
    std::string log_file;
    bool log_console = true;
    size_t log_max_size_mb = 100;
    size_t log_max_files = 10;

    // Get config as JSON
    nlohmann::json toJson() const;

private:
    void setDefaults();
    void parseJson(const nlohmann::json& j);
};

} // namespace chat
