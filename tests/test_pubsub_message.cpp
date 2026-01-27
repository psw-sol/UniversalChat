#include <gtest/gtest.h>
#include "redis/PubSubMessage.hpp"

using namespace chat;

class PubSubMessageTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

// ============================================================================
// PubSubMessage Serialization Tests
// ============================================================================

TEST_F(PubSubMessageTest, DefaultConstruction) {
    PubSubMessage msg;

    EXPECT_EQ(msg.type, PubSubMessageType::ChannelMessage);
    EXPECT_TRUE(msg.origin_server_id.empty());
    EXPECT_TRUE(msg.channel_id.empty());
    EXPECT_TRUE(msg.sender_session_id.empty());
    EXPECT_TRUE(msg.payload.empty());
    EXPECT_GT(msg.timestamp, 0);  // Should have current timestamp
    EXPECT_EQ(msg.sequence, 0);
}

TEST_F(PubSubMessageTest, SerializeDeserialize) {
    PubSubMessage original;
    original.type = PubSubMessageType::ChannelMessage;
    original.origin_server_id = "server-1";
    original.channel_id = "world-1";
    original.sender_session_id = "sess-abc123";
    original.sender_user_id = "user-456";
    original.target_user_id = "";
    original.payload = R"({"content":"Hello, World!"})";
    original.timestamp = 1705827600000;
    original.sequence = 42;

    std::string json = original.serialize();

    // Deserialize
    PubSubMessage restored = PubSubMessage::deserialize(json);

    EXPECT_EQ(restored.type, original.type);
    EXPECT_EQ(restored.origin_server_id, original.origin_server_id);
    EXPECT_EQ(restored.channel_id, original.channel_id);
    EXPECT_EQ(restored.sender_session_id, original.sender_session_id);
    EXPECT_EQ(restored.sender_user_id, original.sender_user_id);
    EXPECT_EQ(restored.target_user_id, original.target_user_id);
    EXPECT_EQ(restored.payload, original.payload);
    EXPECT_EQ(restored.timestamp, original.timestamp);
    EXPECT_EQ(restored.sequence, original.sequence);
}

TEST_F(PubSubMessageTest, SerializeAllTypes) {
    std::vector<PubSubMessageType> types = {
        PubSubMessageType::ChannelMessage,
        PubSubMessageType::ChannelJoin,
        PubSubMessageType::ChannelLeave,
        PubSubMessageType::Whisper,
        PubSubMessageType::ProfileUpdate,
        PubSubMessageType::SystemBroadcast,
        PubSubMessageType::Presence
    };

    for (auto type : types) {
        PubSubMessage msg;
        msg.type = type;
        msg.origin_server_id = "test-server";

        std::string json = msg.serialize();
        auto restored = PubSubMessage::deserialize(json);

        EXPECT_EQ(restored.type, type)
            << "Failed for type: " << pubSubMessageTypeToString(type);
    }
}

TEST_F(PubSubMessageTest, TryDeserializeValid) {
    PubSubMessage original;
    original.type = PubSubMessageType::Whisper;
    original.origin_server_id = "server-2";
    original.target_user_id = "target-user";

    std::string json = original.serialize();
    auto result = PubSubMessage::tryDeserialize(json);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, PubSubMessageType::Whisper);
    EXPECT_EQ(result->origin_server_id, "server-2");
    EXPECT_EQ(result->target_user_id, "target-user");
}

TEST_F(PubSubMessageTest, TryDeserializeInvalid) {
    // Invalid JSON
    auto result1 = PubSubMessage::tryDeserialize("not valid json");
    EXPECT_FALSE(result1.has_value());

    // Empty string
    auto result2 = PubSubMessage::tryDeserialize("");
    EXPECT_FALSE(result2.has_value());

    // Partial JSON
    auto result3 = PubSubMessage::tryDeserialize("{\"type\": 1,");
    EXPECT_FALSE(result3.has_value());
}

TEST_F(PubSubMessageTest, TryDeserializeMissingFields) {
    // JSON with missing fields should use defaults
    std::string json = R"({"type": 4})";  // Only type field

    auto result = PubSubMessage::tryDeserialize(json);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, PubSubMessageType::Whisper);
    EXPECT_TRUE(result->origin_server_id.empty());
    EXPECT_TRUE(result->channel_id.empty());
    EXPECT_EQ(result->timestamp, 0);
}

TEST_F(PubSubMessageTest, IsFromServer) {
    PubSubMessage msg;
    msg.origin_server_id = "server-1";

    EXPECT_TRUE(msg.isFromServer("server-1"));
    EXPECT_FALSE(msg.isFromServer("server-2"));
    EXPECT_FALSE(msg.isFromServer(""));
}

TEST_F(PubSubMessageTest, MessageTypeToString) {
    EXPECT_STREQ(pubSubMessageTypeToString(PubSubMessageType::ChannelMessage), "ChannelMessage");
    EXPECT_STREQ(pubSubMessageTypeToString(PubSubMessageType::ChannelJoin), "ChannelJoin");
    EXPECT_STREQ(pubSubMessageTypeToString(PubSubMessageType::ChannelLeave), "ChannelLeave");
    EXPECT_STREQ(pubSubMessageTypeToString(PubSubMessageType::Whisper), "Whisper");
    EXPECT_STREQ(pubSubMessageTypeToString(PubSubMessageType::ProfileUpdate), "ProfileUpdate");
    EXPECT_STREQ(pubSubMessageTypeToString(PubSubMessageType::SystemBroadcast), "SystemBroadcast");
    EXPECT_STREQ(pubSubMessageTypeToString(PubSubMessageType::Presence), "Presence");
    EXPECT_STREQ(pubSubMessageTypeToString(static_cast<PubSubMessageType>(255)), "Unknown");
}

TEST_F(PubSubMessageTest, UnicodeContent) {
    PubSubMessage msg;
    msg.payload = R"({"content":"안녕하세요! 🎮"})";
    msg.origin_server_id = "서버-1";

    std::string json = msg.serialize();
    auto restored = PubSubMessage::deserialize(json);

    EXPECT_EQ(restored.payload, msg.payload);
    EXPECT_EQ(restored.origin_server_id, msg.origin_server_id);
}

TEST_F(PubSubMessageTest, LargePayload) {
    PubSubMessage msg;

    // Create a large payload (10KB)
    std::string large_content(10240, 'x');
    msg.payload = large_content;
    msg.origin_server_id = "server-1";

    std::string json = msg.serialize();
    auto restored = PubSubMessage::deserialize(json);

    EXPECT_EQ(restored.payload.size(), large_content.size());
    EXPECT_EQ(restored.payload, large_content);
}

// ============================================================================
// ChannelMessagePayload Tests
// ============================================================================

TEST_F(PubSubMessageTest, ChannelMessagePayloadSerialize) {
    ChannelMessagePayload payload;
    payload.message_id = "msg-12345";
    payload.content = "Hello, World!";
    payload.message_type = 0;
    payload.sender_nickname = "Player1";
    payload.sender_profile_image = "avatar1.png";
    payload.sender_frame_image = "frame1.png";
    payload.sender_extra_data = R"({"level":50})";

    std::string json = payload.serialize();
    auto restored = ChannelMessagePayload::deserialize(json);

    EXPECT_EQ(restored.message_id, payload.message_id);
    EXPECT_EQ(restored.content, payload.content);
    EXPECT_EQ(restored.message_type, payload.message_type);
    EXPECT_EQ(restored.sender_nickname, payload.sender_nickname);
    EXPECT_EQ(restored.sender_profile_image, payload.sender_profile_image);
    EXPECT_EQ(restored.sender_frame_image, payload.sender_frame_image);
    EXPECT_EQ(restored.sender_extra_data, payload.sender_extra_data);
}

// ============================================================================
// WhisperPayload Tests
// ============================================================================

TEST_F(PubSubMessageTest, WhisperPayloadSerialize) {
    WhisperPayload payload;
    payload.message_id = "whisper-001";
    payload.content = "Secret message";
    payload.sender_nickname = "Whisperer";
    payload.sender_profile_image = "whisper_avatar.png";
    payload.sender_frame_image = "whisper_frame.png";

    std::string json = payload.serialize();
    auto restored = WhisperPayload::deserialize(json);

    EXPECT_EQ(restored.message_id, payload.message_id);
    EXPECT_EQ(restored.content, payload.content);
    EXPECT_EQ(restored.sender_nickname, payload.sender_nickname);
    EXPECT_EQ(restored.sender_profile_image, payload.sender_profile_image);
    EXPECT_EQ(restored.sender_frame_image, payload.sender_frame_image);
}

// ============================================================================
// ChannelMemberPayload Tests
// ============================================================================

TEST_F(PubSubMessageTest, ChannelMemberPayloadSerialize) {
    ChannelMemberPayload payload;
    payload.user_id = "user-789";
    payload.session_id = "sess-xyz";
    payload.nickname = "NewMember";
    payload.profile_image = "member_avatar.png";
    payload.frame_image = "member_frame.png";

    std::string json = payload.serialize();
    auto restored = ChannelMemberPayload::deserialize(json);

    EXPECT_EQ(restored.user_id, payload.user_id);
    EXPECT_EQ(restored.session_id, payload.session_id);
    EXPECT_EQ(restored.nickname, payload.nickname);
    EXPECT_EQ(restored.profile_image, payload.profile_image);
    EXPECT_EQ(restored.frame_image, payload.frame_image);
}

// ============================================================================
// Integration: PubSubMessage with Payload
// ============================================================================

TEST_F(PubSubMessageTest, MessageWithNestedPayload) {
    // Create channel message payload
    ChannelMessagePayload content_payload;
    content_payload.message_id = "msg-nested-001";
    content_payload.content = "Nested message content";
    content_payload.sender_nickname = "NestedSender";

    // Create pub/sub message with payload
    PubSubMessage msg;
    msg.type = PubSubMessageType::ChannelMessage;
    msg.origin_server_id = "server-1";
    msg.channel_id = "world-1";
    msg.payload = content_payload.serialize();

    // Serialize and deserialize the entire message
    std::string json = msg.serialize();
    auto restored_msg = PubSubMessage::deserialize(json);

    // Extract and verify nested payload
    auto restored_payload = ChannelMessagePayload::deserialize(restored_msg.payload);

    EXPECT_EQ(restored_payload.message_id, content_payload.message_id);
    EXPECT_EQ(restored_payload.content, content_payload.content);
    EXPECT_EQ(restored_payload.sender_nickname, content_payload.sender_nickname);
}
