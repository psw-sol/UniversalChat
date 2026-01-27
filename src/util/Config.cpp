#include "Config.hpp"
#include "Logger.hpp"
#include <fstream>
#include <sstream>
#include <random>
#include <iomanip>

namespace chat {

Config::Config() {
    setDefaults();
}

void Config::setDefaults() {
    // Default predefined channels
    ChannelConfig world_channel;
    world_channel.channel_id = "world";
    world_channel.channel_name = "World Chat";
    world_channel.max_members = 1000;
    world_channel.is_system = true;
    world_channel.is_persistent = true;

    ChannelConfig trade_channel;
    trade_channel.channel_id = "trade";
    trade_channel.channel_name = "Trade Chat";
    trade_channel.max_members = 500;
    trade_channel.is_system = true;
    trade_channel.is_persistent = true;

    predefined_channels.push_back(world_channel);
    predefined_channels.push_back(trade_channel);

    // Generate default server_id if empty (UUID-like)
    if (server_id.empty()) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);

        std::stringstream ss;
        ss << "server-";
        for (int i = 0; i < 8; ++i) {
            ss << std::hex << dis(gen);
        }
        server_id = ss.str();
    }
}

bool Config::loadFromFile(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open config file: {}", filepath);
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();

        return loadFromJson(buffer.str());
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load config file {}: {}", filepath, e.what());
        return false;
    }
}

bool Config::loadFromJson(const std::string& json_str) {
    try {
        auto j = nlohmann::json::parse(json_str);
        parseJson(j);
        return true;
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("Failed to parse config JSON: {}", e.what());
        return false;
    }
}

void Config::parseJson(const nlohmann::json& j) {
    // Server settings
    if (j.contains("server")) {
        const auto& server = j["server"];
        if (server.contains("host")) host = server["host"];
        if (server.contains("port")) port = server["port"];
        if (server.contains("io_threads")) io_threads = server["io_threads"];
        if (server.contains("max_connections")) max_connections = server["max_connections"];
        if (server.contains("max_packet_size")) max_packet_size = server["max_packet_size"];
    }

    // Connection settings
    if (j.contains("connection")) {
        const auto& conn = j["connection"];
        if (conn.contains("heartbeat_interval")) heartbeat_interval = conn["heartbeat_interval"];
        if (conn.contains("heartbeat_timeout")) heartbeat_timeout = conn["heartbeat_timeout"];
        if (conn.contains("heartbeat_check_interval")) heartbeat_check_interval = conn["heartbeat_check_interval"];
        if (conn.contains("read_buffer_size")) read_buffer_size = conn["read_buffer_size"];
        if (conn.contains("write_buffer_size")) write_buffer_size = conn["write_buffer_size"];
    }

    // Channel settings
    if (j.contains("channel")) {
        const auto& channel = j["channel"];
        if (channel.contains("default_max_members")) default_max_members = channel["default_max_members"];
        if (channel.contains("message_history_size")) message_history_size = channel["message_history_size"];

        if (channel.contains("predefined_channels")) {
            predefined_channels.clear();
            for (const auto& ch : channel["predefined_channels"]) {
                ChannelConfig config;
                config.channel_id = ch.value("id", "");
                config.channel_name = ch.value("name", config.channel_id);
                config.max_members = ch.value("max_members", default_max_members);
                config.is_system = ch.value("is_system", false);
                config.is_persistent = ch.value("is_persistent", true);
                config.message_history_size = ch.value("message_history_size", message_history_size);
                config.password = ch.value("password", "");

                if (!config.channel_id.empty()) {
                    predefined_channels.push_back(config);
                }
            }
        }

        // World channel sharding settings
        if (channel.contains("world_channel_min_count")) world_channel_min_count = channel["world_channel_min_count"];
        if (channel.contains("world_channel_max_members")) world_channel_max_members = channel["world_channel_max_members"];
        if (channel.contains("world_channel_scale_threshold")) world_channel_scale_threshold = channel["world_channel_scale_threshold"];
    }

    // Auth settings
    if (j.contains("auth")) {
        const auto& auth = j["auth"];
        if (auth.contains("provider")) auth_provider = auth["provider"];
        if (auth.contains("token_validation")) token_validation = auth["token_validation"];
    }

    // Redis settings
    if (j.contains("redis")) {
        const auto& redis = j["redis"];
        if (redis.contains("enabled")) redis_enabled = redis["enabled"];
        if (redis.contains("host")) redis_host = redis["host"];
        if (redis.contains("port")) redis_port = redis["port"];
        if (redis.contains("password")) redis_password = redis["password"];
        if (redis.contains("db")) redis_db = redis["db"];
        if (redis.contains("connect_timeout_ms")) redis_connect_timeout_ms = redis["connect_timeout_ms"];
        if (redis.contains("command_timeout_ms")) redis_command_timeout_ms = redis["command_timeout_ms"];
    }

    // Scale-out settings
    if (j.contains("scale_out")) {
        const auto& scale = j["scale_out"];
        if (scale.contains("server_id")) server_id = scale["server_id"];
    }

    // Pub/Sub settings
    if (j.contains("pubsub")) {
        const auto& pubsub = j["pubsub"];
        if (pubsub.contains("enabled")) pubsub_enabled = pubsub["enabled"];
        if (pubsub.contains("reconnect_interval_ms")) pubsub_reconnect_interval_ms = pubsub["reconnect_interval_ms"];
        if (pubsub.contains("channel_patterns")) {
            pubsub_channel_patterns.clear();
            for (const auto& pattern : pubsub["channel_patterns"]) {
                pubsub_channel_patterns.push_back(pattern.get<std::string>());
            }
        }
    }

    // Session registry settings
    if (j.contains("session_registry")) {
        const auto& session_reg = j["session_registry"];
        if (session_reg.contains("enabled")) session_registry_enabled = session_reg["enabled"];
        if (session_reg.contains("ttl_seconds")) session_registry_ttl = session_reg["ttl_seconds"];
        if (session_reg.contains("refresh_on_heartbeat")) session_registry_refresh_on_heartbeat = session_reg["refresh_on_heartbeat"];
    }

    // Channel registry settings
    if (j.contains("channel_registry")) {
        const auto& channel_reg = j["channel_registry"];
        if (channel_reg.contains("enabled")) channel_registry_enabled = channel_reg["enabled"];
        if (channel_reg.contains("sync_member_count")) channel_registry_sync_member_count = channel_reg["sync_member_count"];
    }

    // Message storage settings
    if (j.contains("message_storage")) {
        const auto& storage = j["message_storage"];
        if (storage.contains("enabled")) message_persistence_enabled = storage["enabled"];
        if (storage.contains("key_prefix")) message_store_key_prefix = storage["key_prefix"];
        if (storage.contains("max_per_channel")) message_store_max_per_channel = storage["max_per_channel"];
        if (storage.contains("ttl_seconds")) message_store_ttl_seconds = storage["ttl_seconds"];
    }

    // Logging settings
    if (j.contains("logging")) {
        const auto& logging = j["logging"];
        if (logging.contains("level")) log_level = logging["level"];
        if (logging.contains("file")) log_file = logging["file"];
        if (logging.contains("console")) log_console = logging["console"];
        if (logging.contains("max_size_mb")) log_max_size_mb = logging["max_size_mb"];
        if (logging.contains("max_files")) log_max_files = logging["max_files"];
    }
}

nlohmann::json Config::toJson() const {
    nlohmann::json j;

    // Server
    j["server"]["host"] = host;
    j["server"]["port"] = port;
    j["server"]["io_threads"] = io_threads;
    j["server"]["max_connections"] = max_connections;
    j["server"]["max_packet_size"] = max_packet_size;

    // Connection
    j["connection"]["heartbeat_interval"] = heartbeat_interval;
    j["connection"]["heartbeat_timeout"] = heartbeat_timeout;
    j["connection"]["heartbeat_check_interval"] = heartbeat_check_interval;
    j["connection"]["read_buffer_size"] = read_buffer_size;
    j["connection"]["write_buffer_size"] = write_buffer_size;

    // Channel
    j["channel"]["default_max_members"] = default_max_members;
    j["channel"]["message_history_size"] = message_history_size;

    nlohmann::json channels = nlohmann::json::array();
    for (const auto& ch : predefined_channels) {
        nlohmann::json channel_obj;
        channel_obj["id"] = ch.channel_id;
        channel_obj["name"] = ch.channel_name;
        channel_obj["max_members"] = ch.max_members;
        channel_obj["is_system"] = ch.is_system;
        channel_obj["is_persistent"] = ch.is_persistent;
        channels.push_back(channel_obj);
    }
    j["channel"]["predefined_channels"] = channels;
    j["channel"]["world_channel_min_count"] = world_channel_min_count;
    j["channel"]["world_channel_max_members"] = world_channel_max_members;
    j["channel"]["world_channel_scale_threshold"] = world_channel_scale_threshold;

    // Auth
    j["auth"]["provider"] = auth_provider;
    j["auth"]["token_validation"] = token_validation;

    // Redis
    j["redis"]["enabled"] = redis_enabled;
    j["redis"]["host"] = redis_host;
    j["redis"]["port"] = redis_port;
    j["redis"]["password"] = redis_password;
    j["redis"]["db"] = redis_db;
    j["redis"]["connect_timeout_ms"] = redis_connect_timeout_ms;
    j["redis"]["command_timeout_ms"] = redis_command_timeout_ms;

    // Scale-out
    j["scale_out"]["server_id"] = server_id;

    // Pub/Sub
    j["pubsub"]["enabled"] = pubsub_enabled;
    j["pubsub"]["reconnect_interval_ms"] = pubsub_reconnect_interval_ms;
    j["pubsub"]["channel_patterns"] = pubsub_channel_patterns;

    // Session Registry
    j["session_registry"]["enabled"] = session_registry_enabled;
    j["session_registry"]["ttl_seconds"] = session_registry_ttl;
    j["session_registry"]["refresh_on_heartbeat"] = session_registry_refresh_on_heartbeat;

    // Channel Registry
    j["channel_registry"]["enabled"] = channel_registry_enabled;
    j["channel_registry"]["sync_member_count"] = channel_registry_sync_member_count;

    // Message Storage
    j["message_storage"]["enabled"] = message_persistence_enabled;
    j["message_storage"]["key_prefix"] = message_store_key_prefix;
    j["message_storage"]["max_per_channel"] = message_store_max_per_channel;
    j["message_storage"]["ttl_seconds"] = message_store_ttl_seconds;

    // Logging
    j["logging"]["level"] = log_level;
    j["logging"]["file"] = log_file;
    j["logging"]["console"] = log_console;
    j["logging"]["max_size_mb"] = log_max_size_mb;
    j["logging"]["max_files"] = log_max_files;

    return j;
}

} // namespace chat
