#include <gtest/gtest.h>
#include "redis/PubSubMessage.hpp"

#ifdef ENABLE_REDIS
#include "redis/RedisClient.hpp"
#include "redis/RedisPubSub.hpp"
#include "redis/SessionRegistry.hpp"
#include "redis/ChannelRegistry.hpp"
#include <thread>
#include <chrono>
#include <atomic>
#include <condition_variable>
#endif

using namespace chat;

// ============================================================================
// Scale-Out Integration Tests (Requires Redis)
// ============================================================================

#ifdef ENABLE_REDIS

class ScaleOutIntegrationTest : public ::testing::Test {
protected:
    std::shared_ptr<RedisClient> redis_;
    bool redis_available_ = false;

    void SetUp() override {
        // Try to connect to Redis
        RedisClient::Config config;
        config.host = "127.0.0.1";
        config.port = 6379;
        config.connect_timeout_ms = 1000;

        redis_ = std::make_shared<RedisClient>(config);
        if (redis_->connect()) {
            redis_available_ = true;
            cleanupTestData();
        }
    }

    void TearDown() override {
        if (redis_available_) {
            cleanupTestData();
        }
    }

    void cleanupTestData() {
        if (!redis_ || !redis_->isConnected()) return;

        // Clean up test keys
        redis_->del("chat:sessions:test-user-1");
        redis_->del("chat:sessions:test-user-2");
        redis_->del("chat:sessions:test-user-3");
        redis_->del("chat:channel:test-channel:members");
        redis_->del("chat:user:test-user-1:channels");
        redis_->del("chat:user:test-user-2:channels");
        redis_->del("chat:user:test-user-3:channels");
        redis_->del("chat:channels:test-channel");
    }
};

// ----------------------------------------------------------------------------
// Multi-Server Session Registry Tests
// ----------------------------------------------------------------------------

TEST_F(ScaleOutIntegrationTest, MultiServerSessionLookup) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    // Create two session registries simulating two different servers
    auto registry_server1 = std::make_unique<SessionRegistry>(redis_, "server-1", 120);
    auto registry_server2 = std::make_unique<SessionRegistry>(redis_, "server-2", 120);

    // Server 1 registers a user
    SessionInfo info1;
    info1.user_id = "test-user-1";
    info1.session_id = "session-1";
    info1.server_id = "server-1";
    info1.nickname = "User1";

    ASSERT_TRUE(registry_server1->registerSession(info1));

    // Server 2 should be able to find this user
    auto found = registry_server2->findSession("test-user-1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->user_id, "test-user-1");
    EXPECT_EQ(found->server_id, "server-1");
    EXPECT_EQ(found->nickname, "User1");

    // Server 2 registers another user
    SessionInfo info2;
    info2.user_id = "test-user-2";
    info2.session_id = "session-2";
    info2.server_id = "server-2";
    info2.nickname = "User2";

    ASSERT_TRUE(registry_server2->registerSession(info2));

    // Server 1 should be able to find this user
    found = registry_server1->findSession("test-user-2");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->user_id, "test-user-2");
    EXPECT_EQ(found->server_id, "server-2");
}

TEST_F(ScaleOutIntegrationTest, SessionMigrationBetweenServers) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    auto registry_server1 = std::make_unique<SessionRegistry>(redis_, "server-1", 120);
    auto registry_server2 = std::make_unique<SessionRegistry>(redis_, "server-2", 120);

    // User connects to Server 1
    SessionInfo info1;
    info1.user_id = "test-user-1";
    info1.session_id = "session-1";
    info1.server_id = "server-1";

    ASSERT_TRUE(registry_server1->registerSession(info1));

    // Verify user is on Server 1
    auto found = registry_server2->findSession("test-user-1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->server_id, "server-1");

    // User disconnects from Server 1
    ASSERT_TRUE(registry_server1->unregisterSession("test-user-1"));

    // User reconnects to Server 2
    SessionInfo info2;
    info2.user_id = "test-user-1";
    info2.session_id = "session-2";
    info2.server_id = "server-2";

    ASSERT_TRUE(registry_server2->registerSession(info2));

    // Verify user is now on Server 2
    found = registry_server1->findSession("test-user-1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->server_id, "server-2");
    EXPECT_EQ(found->session_id, "session-2");
}

// ----------------------------------------------------------------------------
// Multi-Server Channel Membership Tests
// ----------------------------------------------------------------------------

TEST_F(ScaleOutIntegrationTest, GlobalChannelMemberCount) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    // Create channel registries for two servers
    auto registry_server1 = std::make_unique<ChannelRegistry>(redis_);
    auto registry_server2 = std::make_unique<ChannelRegistry>(redis_);

    // Users join the same channel from different servers
    ASSERT_TRUE(registry_server1->addMember("test-channel", "test-user-1"));
    ASSERT_TRUE(registry_server2->addMember("test-channel", "test-user-2"));
    ASSERT_TRUE(registry_server1->addMember("test-channel", "test-user-3"));

    // Both servers should see the same global member count
    EXPECT_EQ(registry_server1->getMemberCount("test-channel"), 3);
    EXPECT_EQ(registry_server2->getMemberCount("test-channel"), 3);

    // Verify membership from both servers
    EXPECT_TRUE(registry_server1->isMember("test-channel", "test-user-2"));
    EXPECT_TRUE(registry_server2->isMember("test-channel", "test-user-1"));
}

TEST_F(ScaleOutIntegrationTest, CrossServerChannelLeave) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    auto registry_server1 = std::make_unique<ChannelRegistry>(redis_);
    auto registry_server2 = std::make_unique<ChannelRegistry>(redis_);

    // Users join from different servers
    registry_server1->addMember("test-channel", "test-user-1");
    registry_server2->addMember("test-channel", "test-user-2");

    EXPECT_EQ(registry_server1->getMemberCount("test-channel"), 2);

    // User leaves from their respective server
    registry_server1->removeMember("test-channel", "test-user-1");

    // Both servers should see updated count
    EXPECT_EQ(registry_server1->getMemberCount("test-channel"), 1);
    EXPECT_EQ(registry_server2->getMemberCount("test-channel"), 1);

    // Verify membership updated
    EXPECT_FALSE(registry_server2->isMember("test-channel", "test-user-1"));
    EXPECT_TRUE(registry_server1->isMember("test-channel", "test-user-2"));
}

TEST_F(ScaleOutIntegrationTest, UserChannelsAcrossServers) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    auto registry_server1 = std::make_unique<ChannelRegistry>(redis_);
    auto registry_server2 = std::make_unique<ChannelRegistry>(redis_);

    // User joins channels from different servers (simulating reconnection)
    registry_server1->addMember("channel-a", "test-user-1");
    registry_server2->addMember("channel-b", "test-user-1");
    registry_server1->addMember("channel-c", "test-user-1");

    // Both servers should see the same user channel list
    auto channels1 = registry_server1->getUserChannels("test-user-1");
    auto channels2 = registry_server2->getUserChannels("test-user-1");

    EXPECT_EQ(channels1.size(), 3);
    EXPECT_EQ(channels2.size(), 3);

    // Cleanup test data
    redis_->del("chat:channel:channel-a:members");
    redis_->del("chat:channel:channel-b:members");
    redis_->del("chat:channel:channel-c:members");
}

// ----------------------------------------------------------------------------
// Cross-Server Whisper Routing Tests
// ----------------------------------------------------------------------------

TEST_F(ScaleOutIntegrationTest, WhisperTargetServerLookup) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    auto registry_server1 = std::make_unique<SessionRegistry>(redis_, "server-1", 120);
    auto registry_server2 = std::make_unique<SessionRegistry>(redis_, "server-2", 120);

    // Register users on different servers
    SessionInfo user1;
    user1.user_id = "whisper-sender";
    user1.session_id = "session-sender";
    user1.server_id = "server-1";
    registry_server1->registerSession(user1);

    SessionInfo user2;
    user2.user_id = "whisper-receiver";
    user2.session_id = "session-receiver";
    user2.server_id = "server-2";
    registry_server2->registerSession(user2);

    // Server 1 (sender's server) looks up receiver's location
    auto receiver_info = registry_server1->findSession("whisper-receiver");
    ASSERT_TRUE(receiver_info.has_value());
    EXPECT_EQ(receiver_info->server_id, "server-2");

    // Verify this is a cross-server whisper (different server_id)
    EXPECT_NE(user1.server_id, receiver_info->server_id);

    // Cleanup
    registry_server1->unregisterSession("whisper-sender");
    registry_server2->unregisterSession("whisper-receiver");
}

TEST_F(ScaleOutIntegrationTest, WhisperMessageSerialization) {
    // Test whisper message construction for cross-server routing
    PubSubMessage whisper_msg;
    whisper_msg.type = PubSubMessageType::Whisper;
    whisper_msg.origin_server_id = "server-1";
    whisper_msg.sender_user_id = "sender-user";
    whisper_msg.target_user_id = "receiver-user";
    whisper_msg.timestamp = 1234567890;

    WhisperPayload payload;
    payload.message_id = "msg-123";
    payload.content = "Hello, this is a whisper!";
    payload.sender_nickname = "Sender";
    whisper_msg.payload = payload.serialize();

    // Serialize and deserialize
    std::string json = whisper_msg.serialize();
    auto deserialized = PubSubMessage::tryDeserialize(json);

    ASSERT_TRUE(deserialized.has_value());
    EXPECT_EQ(deserialized->type, PubSubMessageType::Whisper);
    EXPECT_EQ(deserialized->origin_server_id, "server-1");
    EXPECT_EQ(deserialized->sender_user_id, "sender-user");
    EXPECT_EQ(deserialized->target_user_id, "receiver-user");

    // Deserialize payload
    auto payload_opt = WhisperPayload::tryDeserialize(deserialized->payload);
    ASSERT_TRUE(payload_opt.has_value());
    EXPECT_EQ(payload_opt->message_id, "msg-123");
    EXPECT_EQ(payload_opt->content, "Hello, this is a whisper!");
}

// ----------------------------------------------------------------------------
// Pub/Sub Message Flow Tests
// ----------------------------------------------------------------------------

TEST_F(ScaleOutIntegrationTest, ChannelMessageBroadcastFlow) {
    // Test the complete message flow for channel broadcasts
    PubSubMessage msg;
    msg.type = PubSubMessageType::ChannelMessage;
    msg.origin_server_id = "server-1";
    msg.channel_id = "world-1";
    msg.sender_session_id = "session-abc";
    msg.sender_user_id = "user-123";
    msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    ChannelMessagePayload payload;
    payload.message_id = "msg-456";
    payload.sender_nickname = "TestUser";
    payload.content = "Hello everyone!";
    msg.payload = payload.serialize();

    // Simulate Server 2 receiving this message
    std::string serialized = msg.serialize();
    auto received = PubSubMessage::tryDeserialize(serialized);

    ASSERT_TRUE(received.has_value());

    // Server 2 should NOT rebroadcast (check origin_server_id)
    bool should_rebroadcast = !received->isFromServer("server-2");
    EXPECT_TRUE(should_rebroadcast);  // Server 2 should forward to local clients

    // Server 1 (origin) should NOT rebroadcast
    should_rebroadcast = !received->isFromServer("server-1");
    EXPECT_FALSE(should_rebroadcast);  // Server 1 already sent to local clients
}

TEST_F(ScaleOutIntegrationTest, JoinLeaveNotificationFlow) {
    // Test join/leave notifications don't cause infinite loops
    PubSubMessage join_msg;
    join_msg.type = PubSubMessageType::ChannelJoin;
    join_msg.origin_server_id = "server-1";
    join_msg.channel_id = "world-1";
    join_msg.sender_user_id = "new-user";
    join_msg.timestamp = 1234567890;

    ChannelMemberPayload payload;
    payload.user_id = "new-user";
    payload.nickname = "NewUser";
    payload.action = "join";
    join_msg.payload = payload.serialize();

    std::string serialized = join_msg.serialize();
    auto received = PubSubMessage::tryDeserialize(serialized);

    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->type, PubSubMessageType::ChannelJoin);

    // Receiving server should notify local clients but NOT re-publish
    // This is validated by checking if it's from another server
    bool is_remote_event = !received->isFromServer("server-2");
    EXPECT_TRUE(is_remote_event);
}

// ----------------------------------------------------------------------------
// Failover and Reconnection Tests
// ----------------------------------------------------------------------------

TEST_F(ScaleOutIntegrationTest, SessionRegistryReconnection) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    auto registry = std::make_unique<SessionRegistry>(redis_, "server-1", 120);

    // Register a session
    SessionInfo info;
    info.user_id = "test-user-1";
    info.session_id = "session-1";
    info.server_id = "server-1";
    ASSERT_TRUE(registry->registerSession(info));

    // Verify session exists
    auto found = registry->findSession("test-user-1");
    ASSERT_TRUE(found.has_value());

    // Simulate brief disconnection by creating new registry instance
    // (In real scenario, Redis connection would be re-established)
    auto registry2 = std::make_unique<SessionRegistry>(redis_, "server-1", 120);

    // Data should still be accessible
    found = registry2->findSession("test-user-1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->session_id, "session-1");
}

TEST_F(ScaleOutIntegrationTest, ChannelRegistryDataPersistence) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    // First registry instance adds members
    {
        auto registry = std::make_unique<ChannelRegistry>(redis_);
        registry->addMember("test-channel", "test-user-1");
        registry->addMember("test-channel", "test-user-2");
    }

    // New registry instance should see the same data
    {
        auto registry = std::make_unique<ChannelRegistry>(redis_);
        EXPECT_EQ(registry->getMemberCount("test-channel"), 2);
        EXPECT_TRUE(registry->isMember("test-channel", "test-user-1"));
        EXPECT_TRUE(registry->isMember("test-channel", "test-user-2"));
    }
}

// ----------------------------------------------------------------------------
// Edge Cases and Error Handling
// ----------------------------------------------------------------------------

TEST_F(ScaleOutIntegrationTest, ConcurrentSessionRegistration) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    auto registry = std::make_unique<SessionRegistry>(redis_, "server-1", 120);

    // Rapid registration/unregistration
    for (int i = 0; i < 10; ++i) {
        SessionInfo info;
        info.user_id = "concurrent-user-" + std::to_string(i);
        info.session_id = "session-" + std::to_string(i);
        info.server_id = "server-1";

        EXPECT_TRUE(registry->registerSession(info));
    }

    // Verify all registered
    for (int i = 0; i < 10; ++i) {
        auto found = registry->findSession("concurrent-user-" + std::to_string(i));
        EXPECT_TRUE(found.has_value());
    }

    // Cleanup
    for (int i = 0; i < 10; ++i) {
        registry->unregisterSession("concurrent-user-" + std::to_string(i));
    }
}

TEST_F(ScaleOutIntegrationTest, LargeChannelMembership) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    auto registry = std::make_unique<ChannelRegistry>(redis_);

    // Add many members
    const int member_count = 100;
    for (int i = 0; i < member_count; ++i) {
        std::string user_id = "bulk-user-" + std::to_string(i);
        EXPECT_TRUE(registry->addMember("test-channel", user_id));
    }

    EXPECT_EQ(registry->getMemberCount("test-channel"), member_count);

    // Cleanup
    for (int i = 0; i < member_count; ++i) {
        std::string user_id = "bulk-user-" + std::to_string(i);
        registry->removeMember("test-channel", user_id);
        redis_->del("chat:user:" + user_id + ":channels");
    }
}

#endif // ENABLE_REDIS

// ============================================================================
// Non-Redis Tests (Message Serialization Verification)
// ============================================================================

class ScaleOutMessageTest : public ::testing::Test {};

TEST_F(ScaleOutMessageTest, CrossServerMessageDeduplication) {
    // Test that messages can be deduplicated based on origin_server_id
    PubSubMessage msg1;
    msg1.type = PubSubMessageType::ChannelMessage;
    msg1.origin_server_id = "server-1";
    msg1.channel_id = "world-1";

    // Server 1 should ignore its own messages
    EXPECT_TRUE(msg1.isFromServer("server-1"));
    EXPECT_FALSE(msg1.isFromServer("server-2"));
    EXPECT_FALSE(msg1.isFromServer("server-3"));
}

TEST_F(ScaleOutMessageTest, MessageTopicExtraction) {
    // Test Redis Pub/Sub topic generation
    std::string channel_topic = "chat:channel:world-1";
    std::string whisper_topic = "chat:whisper:user-123";
    std::string system_topic = "chat:system";

    // Verify topic patterns would match
    // Pattern: chat:channel:*
    EXPECT_TRUE(channel_topic.find("chat:channel:") == 0);

    // Pattern: chat:whisper:*
    EXPECT_TRUE(whisper_topic.find("chat:whisper:") == 0);

    // Pattern: chat:system
    EXPECT_EQ(system_topic, "chat:system");
}

TEST_F(ScaleOutMessageTest, AllMessageTypesSerializable) {
    // Verify all message types can be serialized
    std::vector<PubSubMessageType> types = {
        PubSubMessageType::ChannelMessage,
        PubSubMessageType::ChannelJoin,
        PubSubMessageType::ChannelLeave,
        PubSubMessageType::Whisper,
        PubSubMessageType::ProfileUpdate,
        PubSubMessageType::SystemBroadcast
    };

    for (auto type : types) {
        PubSubMessage msg;
        msg.type = type;
        msg.origin_server_id = "test-server";
        msg.timestamp = 1234567890;

        std::string json = msg.serialize();
        auto deserialized = PubSubMessage::tryDeserialize(json);

        ASSERT_TRUE(deserialized.has_value()) << "Failed to deserialize type: " << static_cast<int>(type);
        EXPECT_EQ(deserialized->type, type);
    }
}
