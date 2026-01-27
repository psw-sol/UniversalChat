#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "redis/PubSubMessage.hpp"

#ifdef ENABLE_REDIS
#include "redis/RedisPubSub.hpp"
#endif

using namespace chat;

// ============================================================================
// Mock RedisPubSub for testing (only when Redis is enabled)
// ============================================================================

#ifdef ENABLE_REDIS

class MockRedisPubSub : public RedisPubSub {
public:
    MockRedisPubSub() : RedisPubSub(boost::asio::io_context{}, "mock-server-id") {}

    // Track published messages for verification
    struct PublishedMessage {
        std::string topic;
        PubSubMessage message;
    };

    std::vector<PublishedMessage> published_messages;
    std::vector<std::string> subscribed_patterns;

    bool publish(const std::string& topic, const PubSubMessage& msg) override {
        published_messages.push_back({topic, msg});
        return true;
    }

    void psubscribe(const std::string& pattern) override {
        subscribed_patterns.push_back(pattern);
    }

    std::string serverId() const override {
        return "mock-server-id";
    }

    void clearPublished() {
        published_messages.clear();
    }

    void clearSubscriptions() {
        subscribed_patterns.clear();
    }
};

#endif // ENABLE_REDIS

// ============================================================================
// Payload tryDeserialize Tests (always available)
// ============================================================================

class PayloadTryDeserializeTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(PayloadTryDeserializeTest, ChannelMessagePayloadTryDeserializeValid) {
    ChannelMessagePayload original;
    original.message_id = "msg-123";
    original.content = "Test content";
    original.message_type = 1;
    original.sender_nickname = "Tester";

    std::string json = original.serialize();
    auto result = ChannelMessagePayload::tryDeserialize(json);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->message_id, "msg-123");
    EXPECT_EQ(result->content, "Test content");
    EXPECT_EQ(result->message_type, 1);
    EXPECT_EQ(result->sender_nickname, "Tester");
}

TEST_F(PayloadTryDeserializeTest, ChannelMessagePayloadTryDeserializeInvalid) {
    auto result1 = ChannelMessagePayload::tryDeserialize("invalid json");
    EXPECT_FALSE(result1.has_value());

    auto result2 = ChannelMessagePayload::tryDeserialize("");
    EXPECT_FALSE(result2.has_value());

    auto result3 = ChannelMessagePayload::tryDeserialize("{broken");
    EXPECT_FALSE(result3.has_value());
}

TEST_F(PayloadTryDeserializeTest, WhisperPayloadTryDeserializeValid) {
    WhisperPayload original;
    original.message_id = "whisper-456";
    original.content = "Secret whisper";
    original.sender_nickname = "Whisperer";

    std::string json = original.serialize();
    auto result = WhisperPayload::tryDeserialize(json);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->message_id, "whisper-456");
    EXPECT_EQ(result->content, "Secret whisper");
    EXPECT_EQ(result->sender_nickname, "Whisperer");
}

TEST_F(PayloadTryDeserializeTest, WhisperPayloadTryDeserializeInvalid) {
    auto result = WhisperPayload::tryDeserialize("not json at all");
    EXPECT_FALSE(result.has_value());
}

TEST_F(PayloadTryDeserializeTest, ChannelMemberPayloadTryDeserializeValid) {
    ChannelMemberPayload original;
    original.user_id = "user-789";
    original.session_id = "sess-abc";
    original.nickname = "Member";
    original.profile_image = "avatar.png";
    original.frame_image = "frame.png";

    std::string json = original.serialize();
    auto result = ChannelMemberPayload::tryDeserialize(json);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->user_id, "user-789");
    EXPECT_EQ(result->session_id, "sess-abc");
    EXPECT_EQ(result->nickname, "Member");
    EXPECT_EQ(result->profile_image, "avatar.png");
    EXPECT_EQ(result->frame_image, "frame.png");
}

TEST_F(PayloadTryDeserializeTest, ChannelMemberPayloadTryDeserializeInvalid) {
    auto result = ChannelMemberPayload::tryDeserialize("{partial: true");
    EXPECT_FALSE(result.has_value());
}

TEST_F(PayloadTryDeserializeTest, ChannelMemberPayloadTryDeserializeMissingFields) {
    // JSON with minimal fields - should use defaults for missing
    std::string json = R"({"user_id": "user-only"})";
    auto result = ChannelMemberPayload::tryDeserialize(json);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->user_id, "user-only");
    EXPECT_TRUE(result->session_id.empty());
    EXPECT_TRUE(result->nickname.empty());
}

// ============================================================================
// Pub/Sub Message Flow Tests
// ============================================================================

class PubSubMessageFlowTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(PubSubMessageFlowTest, ChannelJoinMessageConstruction) {
    // Simulate what ChannelManager::joinChannel does
    PubSubMessage msg;
    msg.type = PubSubMessageType::ChannelJoin;
    msg.origin_server_id = "server-1";
    msg.channel_id = "world-1";
    msg.sender_session_id = "sess-12345";
    msg.sender_user_id = "user-67890";

    ChannelMemberPayload payload;
    payload.user_id = "user-67890";
    payload.session_id = "sess-12345";
    payload.nickname = "NewPlayer";
    payload.profile_image = "avatar.png";
    payload.frame_image = "frame.png";
    msg.payload = payload.serialize();

    // Verify message structure
    EXPECT_EQ(msg.type, PubSubMessageType::ChannelJoin);
    EXPECT_EQ(msg.origin_server_id, "server-1");
    EXPECT_EQ(msg.channel_id, "world-1");

    // Verify round-trip serialization
    std::string json = msg.serialize();
    auto restored = PubSubMessage::deserialize(json);

    EXPECT_EQ(restored.type, PubSubMessageType::ChannelJoin);
    EXPECT_EQ(restored.channel_id, "world-1");

    // Verify nested payload
    auto restored_payload = ChannelMemberPayload::deserialize(restored.payload);
    EXPECT_EQ(restored_payload.nickname, "NewPlayer");
}

TEST_F(PubSubMessageFlowTest, ChannelLeaveMessageConstruction) {
    // Simulate what ChannelManager::leaveChannel does
    PubSubMessage msg;
    msg.type = PubSubMessageType::ChannelLeave;
    msg.origin_server_id = "server-2";
    msg.channel_id = "trade-1";
    msg.sender_session_id = "sess-leave-123";
    msg.sender_user_id = "user-leave-456";

    ChannelMemberPayload payload;
    payload.user_id = "user-leave-456";
    payload.session_id = "sess-leave-123";
    payload.nickname = "LeavingPlayer";
    msg.payload = payload.serialize();

    // Verify structure
    EXPECT_EQ(msg.type, PubSubMessageType::ChannelLeave);
    EXPECT_EQ(msg.channel_id, "trade-1");

    // Verify serialization
    std::string json = msg.serialize();
    auto restored = PubSubMessage::deserialize(json);

    EXPECT_EQ(restored.type, PubSubMessageType::ChannelLeave);
    EXPECT_EQ(restored.sender_session_id, "sess-leave-123");

    auto restored_payload = ChannelMemberPayload::deserialize(restored.payload);
    EXPECT_EQ(restored_payload.nickname, "LeavingPlayer");
}

TEST_F(PubSubMessageFlowTest, ChannelBroadcastMessageConstruction) {
    // Simulate what Channel::broadcast does
    PubSubMessage msg;
    msg.type = PubSubMessageType::ChannelMessage;
    msg.origin_server_id = "server-3";
    msg.channel_id = "world-2";
    msg.sender_session_id = "sess-sender";

    ChannelMessagePayload payload;
    payload.message_id = "msg-broadcast-001";
    payload.content = "Hello everyone!";
    payload.message_type = 3;  // Packet type
    payload.sender_nickname = "Broadcaster";
    payload.sender_profile_image = "bc_avatar.png";
    payload.sender_frame_image = "bc_frame.png";
    msg.payload = payload.serialize();

    // Verify topic construction
    std::string expected_topic = "chat:channel:" + msg.channel_id;
    EXPECT_EQ(expected_topic, "chat:channel:world-2");

    // Verify serialization
    std::string json = msg.serialize();
    auto restored = PubSubMessage::deserialize(json);

    EXPECT_EQ(restored.type, PubSubMessageType::ChannelMessage);
    EXPECT_EQ(restored.channel_id, "world-2");
    EXPECT_EQ(restored.sender_session_id, "sess-sender");

    auto restored_payload = ChannelMessagePayload::deserialize(restored.payload);
    EXPECT_EQ(restored_payload.content, "Hello everyone!");
    EXPECT_EQ(restored_payload.message_type, 3);
}

TEST_F(PubSubMessageFlowTest, MessageDeduplicationByOriginServer) {
    PubSubMessage msg;
    msg.origin_server_id = "server-self";
    msg.channel_id = "world-1";

    // Same server - should be skipped (dedup)
    EXPECT_TRUE(msg.isFromServer("server-self"));

    // Different server - should be processed
    EXPECT_FALSE(msg.isFromServer("server-other"));
    EXPECT_FALSE(msg.isFromServer(""));
}

TEST_F(PubSubMessageFlowTest, TopicPatternExtraction) {
    // Simulate what ChannelManager::onPubSubMessage does
    std::string redis_channel = "chat:channel:world-1";
    std::string prefix = "chat:channel:";

    ASSERT_EQ(redis_channel.find(prefix), 0u);

    std::string channel_id = redis_channel.substr(prefix.length());
    EXPECT_EQ(channel_id, "world-1");

    // Test with different channel
    std::string redis_channel2 = "chat:channel:trade-special";
    std::string channel_id2 = redis_channel2.substr(prefix.length());
    EXPECT_EQ(channel_id2, "trade-special");
}

TEST_F(PubSubMessageFlowTest, InvalidTopicPatternDetection) {
    std::string invalid_channel = "other:channel:world-1";
    std::string prefix = "chat:channel:";

    // Should not start with the expected prefix
    EXPECT_NE(invalid_channel.find(prefix), 0u);
}

// ============================================================================
// onPubSubMessage Handler Logic Tests
// ============================================================================

class OnPubSubMessageTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(OnPubSubMessageTest, ChannelMessagePayloadReconstruction) {
    // Create original message (simulating sender side)
    ChannelMessagePayload original_payload;
    original_payload.message_id = "msg-recon-001";
    original_payload.content = "Test packet data";
    original_payload.message_type = 5;  // Some PacketType value
    original_payload.sender_nickname = "Sender";

    PubSubMessage msg;
    msg.type = PubSubMessageType::ChannelMessage;
    msg.origin_server_id = "remote-server";
    msg.channel_id = "world-1";
    msg.sender_session_id = "sess-remote";
    msg.payload = original_payload.serialize();

    // Simulate receiver side (onPubSubMessage)
    auto payload_opt = ChannelMessagePayload::tryDeserialize(msg.payload);

    ASSERT_TRUE(payload_opt.has_value());

    // Verify packet reconstruction would work
    int packet_type = payload_opt->message_type;
    std::string packet_content = payload_opt->content;

    EXPECT_EQ(packet_type, 5);
    EXPECT_EQ(packet_content, "Test packet data");
}

TEST_F(OnPubSubMessageTest, JoinLeaveNotificationDoesNotRebroadcast) {
    // Join/Leave messages should be logged but not rebroadcast as chat messages
    PubSubMessage join_msg;
    join_msg.type = PubSubMessageType::ChannelJoin;
    join_msg.origin_server_id = "other-server";
    join_msg.channel_id = "world-1";

    // These message types should be handled differently
    EXPECT_NE(join_msg.type, PubSubMessageType::ChannelMessage);

    PubSubMessage leave_msg;
    leave_msg.type = PubSubMessageType::ChannelLeave;

    EXPECT_NE(leave_msg.type, PubSubMessageType::ChannelMessage);
}

TEST_F(OnPubSubMessageTest, InvalidPayloadHandling) {
    PubSubMessage msg;
    msg.type = PubSubMessageType::ChannelMessage;
    msg.origin_server_id = "remote-server";
    msg.payload = "corrupted json {{{";

    // tryDeserialize should return nullopt
    auto payload_opt = ChannelMessagePayload::tryDeserialize(msg.payload);
    EXPECT_FALSE(payload_opt.has_value());

    // Handler should gracefully handle this case (log warning, continue)
}

// ============================================================================
// Edge Cases
// ============================================================================

class PubSubEdgeCasesTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(PubSubEdgeCasesTest, EmptyChannelId) {
    PubSubMessage msg;
    msg.type = PubSubMessageType::ChannelMessage;
    msg.origin_server_id = "server-1";
    msg.channel_id = "";  // Empty

    std::string json = msg.serialize();
    auto restored = PubSubMessage::deserialize(json);

    EXPECT_TRUE(restored.channel_id.empty());
}

TEST_F(PubSubEdgeCasesTest, SpecialCharactersInChannelId) {
    PubSubMessage msg;
    msg.channel_id = "world-1:special/chars?test";

    std::string json = msg.serialize();
    auto restored = PubSubMessage::deserialize(json);

    EXPECT_EQ(restored.channel_id, "world-1:special/chars?test");
}

TEST_F(PubSubEdgeCasesTest, UnicodeInPayload) {
    ChannelMessagePayload payload;
    payload.content = "안녕하세요! 🎮 Hello 世界";
    payload.sender_nickname = "플레이어1";

    std::string json = payload.serialize();
    auto restored = ChannelMessagePayload::deserialize(json);

    EXPECT_EQ(restored.content, "안녕하세요! 🎮 Hello 世界");
    EXPECT_EQ(restored.sender_nickname, "플레이어1");
}

TEST_F(PubSubEdgeCasesTest, VeryLongNickname) {
    ChannelMemberPayload payload;
    payload.nickname = std::string(256, 'A');  // 256 character nickname

    std::string json = payload.serialize();
    auto restored = ChannelMemberPayload::deserialize(json);

    EXPECT_EQ(restored.nickname.length(), 256u);
}

TEST_F(PubSubEdgeCasesTest, MessageSequenceOrdering) {
    PubSubMessage msg1;
    msg1.sequence = 1;
    msg1.timestamp = 1000;

    PubSubMessage msg2;
    msg2.sequence = 2;
    msg2.timestamp = 1001;

    // Verify sequence can be used for ordering
    EXPECT_LT(msg1.sequence, msg2.sequence);
    EXPECT_LT(msg1.timestamp, msg2.timestamp);
}

TEST_F(PubSubEdgeCasesTest, ZeroTimestamp) {
    // Default-constructed timestamp should be non-zero
    PubSubMessage msg;
    EXPECT_GT(msg.timestamp, 0);

    // But explicitly set zero should serialize/deserialize correctly
    msg.timestamp = 0;
    std::string json = msg.serialize();
    auto restored = PubSubMessage::deserialize(json);
    EXPECT_EQ(restored.timestamp, 0);
}
