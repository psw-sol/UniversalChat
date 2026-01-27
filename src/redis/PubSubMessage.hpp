#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include <nlohmann/json.hpp>

namespace chat {

/**
 * Pub/Sub message types for cross-server communication
 */
enum class PubSubMessageType : uint8_t {
    ChannelMessage = 1,     // Chat message in channel
    ChannelJoin = 2,        // User joined channel notification
    ChannelLeave = 3,       // User left channel notification
    Whisper = 4,            // Private message (whisper)
    ProfileUpdate = 5,      // User profile update notification
    SystemBroadcast = 6,    // System-wide broadcast
    Presence = 7            // User presence update (online/offline)
};

/**
 * Convert PubSubMessageType to string for logging
 */
inline const char* pubSubMessageTypeToString(PubSubMessageType type) {
    switch (type) {
        case PubSubMessageType::ChannelMessage:   return "ChannelMessage";
        case PubSubMessageType::ChannelJoin:      return "ChannelJoin";
        case PubSubMessageType::ChannelLeave:     return "ChannelLeave";
        case PubSubMessageType::Whisper:          return "Whisper";
        case PubSubMessageType::ProfileUpdate:    return "ProfileUpdate";
        case PubSubMessageType::SystemBroadcast:  return "SystemBroadcast";
        case PubSubMessageType::Presence:         return "Presence";
        default:                                   return "Unknown";
    }
}

/**
 * PubSubMessage
 *
 * Message structure for Redis Pub/Sub communication between server instances.
 * Contains all necessary information for cross-server message routing.
 */
struct PubSubMessage {
    PubSubMessageType type;
    std::string origin_server_id;    // Server that originated this message (for dedup)
    std::string channel_id;          // Target channel (for channel messages)
    std::string sender_session_id;   // Original sender's session (to exclude from local broadcast)
    std::string sender_user_id;      // Sender's user ID
    std::string target_user_id;      // Target user ID (for whispers)
    std::string payload;             // JSON-serialized message data
    int64_t timestamp;               // Message timestamp (Unix milliseconds)
    uint64_t sequence;               // Sequence number for ordering

    PubSubMessage()
        : type(PubSubMessageType::ChannelMessage)
        , timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch()).count())
        , sequence(0)
    {}

    /**
     * Serialize to JSON string
     */
    std::string serialize() const {
        nlohmann::json j;
        j["type"] = static_cast<uint8_t>(type);
        j["origin_server_id"] = origin_server_id;
        j["channel_id"] = channel_id;
        j["sender_session_id"] = sender_session_id;
        j["sender_user_id"] = sender_user_id;
        j["target_user_id"] = target_user_id;
        j["payload"] = payload;
        j["timestamp"] = timestamp;
        j["sequence"] = sequence;
        return j.dump();
    }

    /**
     * Deserialize from JSON string
     * @throws nlohmann::json::exception on parse error
     */
    static PubSubMessage deserialize(const std::string& json_str) {
        auto j = nlohmann::json::parse(json_str);
        PubSubMessage msg;
        msg.type = static_cast<PubSubMessageType>(j.value("type", 0));
        msg.origin_server_id = j.value("origin_server_id", "");
        msg.channel_id = j.value("channel_id", "");
        msg.sender_session_id = j.value("sender_session_id", "");
        msg.sender_user_id = j.value("sender_user_id", "");
        msg.target_user_id = j.value("target_user_id", "");
        msg.payload = j.value("payload", "");
        msg.timestamp = j.value("timestamp", int64_t(0));
        msg.sequence = j.value("sequence", uint64_t(0));
        return msg;
    }

    /**
     * Try to deserialize, returns nullopt on failure
     */
    static std::optional<PubSubMessage> tryDeserialize(const std::string& json_str) {
        try {
            return deserialize(json_str);
        } catch (const nlohmann::json::exception&) {
            return std::nullopt;
        }
    }

    /**
     * Check if this message originated from the given server
     */
    bool isFromServer(const std::string& server_id) const {
        return origin_server_id == server_id;
    }

    /**
     * Get age of message in milliseconds
     */
    int64_t ageMs() const {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return now - timestamp;
    }
};

/**
 * Channel message payload (for ChannelMessage type)
 */
struct ChannelMessagePayload {
    std::string message_id;
    std::string content;
    int message_type = 0;  // 0: normal, 1: system, etc.
    std::string sender_nickname;
    std::string sender_profile_image;
    std::string sender_frame_image;
    std::string sender_extra_data;

    std::string serialize() const {
        nlohmann::json j;
        j["message_id"] = message_id;
        j["content"] = content;
        j["message_type"] = message_type;
        j["sender_nickname"] = sender_nickname;
        j["sender_profile_image"] = sender_profile_image;
        j["sender_frame_image"] = sender_frame_image;
        j["sender_extra_data"] = sender_extra_data;
        return j.dump();
    }

    static ChannelMessagePayload deserialize(const std::string& json_str) {
        auto j = nlohmann::json::parse(json_str);
        ChannelMessagePayload p;
        p.message_id = j.value("message_id", "");
        p.content = j.value("content", "");
        p.message_type = j.value("message_type", 0);
        p.sender_nickname = j.value("sender_nickname", "");
        p.sender_profile_image = j.value("sender_profile_image", "");
        p.sender_frame_image = j.value("sender_frame_image", "");
        p.sender_extra_data = j.value("sender_extra_data", "");
        return p;
    }

    /**
     * Try to deserialize, returns nullopt on failure
     */
    static std::optional<ChannelMessagePayload> tryDeserialize(const std::string& json_str) {
        try {
            return deserialize(json_str);
        } catch (const nlohmann::json::exception&) {
            return std::nullopt;
        }
    }
};

/**
 * Whisper message payload (for Whisper type)
 */
struct WhisperPayload {
    std::string message_id;
    std::string content;
    std::string sender_nickname;
    std::string sender_profile_image;
    std::string sender_frame_image;

    std::string serialize() const {
        nlohmann::json j;
        j["message_id"] = message_id;
        j["content"] = content;
        j["sender_nickname"] = sender_nickname;
        j["sender_profile_image"] = sender_profile_image;
        j["sender_frame_image"] = sender_frame_image;
        return j.dump();
    }

    static WhisperPayload deserialize(const std::string& json_str) {
        auto j = nlohmann::json::parse(json_str);
        WhisperPayload p;
        p.message_id = j.value("message_id", "");
        p.content = j.value("content", "");
        p.sender_nickname = j.value("sender_nickname", "");
        p.sender_profile_image = j.value("sender_profile_image", "");
        p.sender_frame_image = j.value("sender_frame_image", "");
        return p;
    }

    static std::optional<WhisperPayload> tryDeserialize(const std::string& json_str) {
        try {
            return deserialize(json_str);
        } catch (const nlohmann::json::exception&) {
            return std::nullopt;
        }
    }
};

/**
 * Channel join/leave payload
 */
struct ChannelMemberPayload {
    std::string user_id;
    std::string session_id;
    std::string nickname;
    std::string profile_image;
    std::string frame_image;

    std::string serialize() const {
        nlohmann::json j;
        j["user_id"] = user_id;
        j["session_id"] = session_id;
        j["nickname"] = nickname;
        j["profile_image"] = profile_image;
        j["frame_image"] = frame_image;
        return j.dump();
    }

    static ChannelMemberPayload deserialize(const std::string& json_str) {
        auto j = nlohmann::json::parse(json_str);
        ChannelMemberPayload p;
        p.user_id = j.value("user_id", "");
        p.session_id = j.value("session_id", "");
        p.nickname = j.value("nickname", "");
        p.profile_image = j.value("profile_image", "");
        p.frame_image = j.value("frame_image", "");
        return p;
    }

    static std::optional<ChannelMemberPayload> tryDeserialize(const std::string& json_str) {
        try {
            return deserialize(json_str);
        } catch (const nlohmann::json::exception&) {
            return std::nullopt;
        }
    }
};

} // namespace chat
