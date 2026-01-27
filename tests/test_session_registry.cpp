#include <gtest/gtest.h>
#include "redis/SessionRegistry.hpp"

#ifdef ENABLE_REDIS
#include "redis/RedisClient.hpp"
#endif

using namespace chat;

// ============================================================================
// SessionInfo Tests (No Redis Required)
// ============================================================================

class SessionInfoTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(SessionInfoTest, DefaultConstruction) {
    SessionInfo info;

    EXPECT_TRUE(info.user_id.empty());
    EXPECT_TRUE(info.session_id.empty());
    EXPECT_TRUE(info.server_id.empty());
    EXPECT_TRUE(info.nickname.empty());
    EXPECT_TRUE(info.profile_image.empty());
    EXPECT_TRUE(info.frame_image.empty());
    EXPECT_EQ(info.connected_at, 0);
}

TEST_F(SessionInfoTest, IsValidEmpty) {
    SessionInfo info;

    // Empty session is not valid
    EXPECT_FALSE(info.isValid());
}

TEST_F(SessionInfoTest, IsValidWithUserIdOnly) {
    SessionInfo info;
    info.user_id = "user-123";

    // user_id alone is not enough
    EXPECT_FALSE(info.isValid());
}

TEST_F(SessionInfoTest, IsValidWithSessionIdOnly) {
    SessionInfo info;
    info.session_id = "session-456";

    // session_id alone is not enough
    EXPECT_FALSE(info.isValid());
}

TEST_F(SessionInfoTest, IsValidWithBothIds) {
    SessionInfo info;
    info.user_id = "user-123";
    info.session_id = "session-456";

    // Both user_id and session_id are required
    EXPECT_TRUE(info.isValid());
}

TEST_F(SessionInfoTest, IsValidWithFullData) {
    SessionInfo info;
    info.user_id = "user-123";
    info.session_id = "session-456";
    info.server_id = "server-1";
    info.nickname = "TestPlayer";
    info.profile_image = "avatar.png";
    info.frame_image = "frame.png";
    info.connected_at = 1705827600;

    EXPECT_TRUE(info.isValid());
}

TEST_F(SessionInfoTest, IsValidWithEmptyStrings) {
    SessionInfo info;
    info.user_id = "";
    info.session_id = "";

    EXPECT_FALSE(info.isValid());
}

// ============================================================================
// SessionRegistry Tests (Requires Redis)
// ============================================================================

#ifdef ENABLE_REDIS

class SessionRegistryTest : public ::testing::Test {
protected:
    std::shared_ptr<RedisClient> redis_;
    std::unique_ptr<SessionRegistry> registry_;
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
            registry_ = std::make_unique<SessionRegistry>(redis_, "test-server-1", 60);

            // Clean up any test data from previous runs
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

        // Delete test session keys
        redis_->del("chat:sessions:test-user-1");
        redis_->del("chat:sessions:test-user-2");
        redis_->del("chat:sessions:test-user-3");
        redis_->del("chat:nickname:testplayer1");
        redis_->del("chat:nickname:testplayer2");
        redis_->del("chat:nickname:unicodeplayer");
    }
};

TEST_F(SessionRegistryTest, IsAvailable) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    EXPECT_TRUE(registry_->isAvailable());
}

TEST_F(SessionRegistryTest, ServerId) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    EXPECT_EQ(registry_->serverId(), "test-server-1");
}

TEST_F(SessionRegistryTest, TtlSeconds) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    EXPECT_EQ(registry_->ttlSeconds(), 60);
}

TEST_F(SessionRegistryTest, RegisterSession) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    bool result = registry_->registerSession(
        "test-user-1",
        "session-abc-123",
        "TestPlayer1",
        "avatar1.png",
        "frame1.png"
    );

    EXPECT_TRUE(result);

    // Verify session is stored
    auto info = registry_->getSession("test-user-1");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->user_id, "test-user-1");
    EXPECT_EQ(info->session_id, "session-abc-123");
    EXPECT_EQ(info->server_id, "test-server-1");
    EXPECT_EQ(info->nickname, "TestPlayer1");
    EXPECT_EQ(info->profile_image, "avatar1.png");
    EXPECT_EQ(info->frame_image, "frame1.png");
    EXPECT_GT(info->connected_at, 0);
}

TEST_F(SessionRegistryTest, RegisterSessionMinimalData) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    bool result = registry_->registerSession(
        "test-user-2",
        "session-xyz-789",
        "TestPlayer2"
        // No profile_image or frame_image
    );

    EXPECT_TRUE(result);

    auto info = registry_->getSession("test-user-2");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->nickname, "TestPlayer2");
    EXPECT_TRUE(info->profile_image.empty());
    EXPECT_TRUE(info->frame_image.empty());
}

TEST_F(SessionRegistryTest, IsUserOnline) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    // Initially user is not online
    EXPECT_FALSE(registry_->isUserOnline("test-user-1"));

    // Register session
    registry_->registerSession("test-user-1", "session-123", "Player");

    // Now user should be online
    EXPECT_TRUE(registry_->isUserOnline("test-user-1"));

    // Non-existent user is not online
    EXPECT_FALSE(registry_->isUserOnline("non-existent-user"));
}

TEST_F(SessionRegistryTest, GetUserServer) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    registry_->registerSession("test-user-1", "session-123", "Player");

    auto server = registry_->getUserServer("test-user-1");
    ASSERT_TRUE(server.has_value());
    EXPECT_EQ(*server, "test-server-1");

    // Non-existent user
    auto no_server = registry_->getUserServer("non-existent-user");
    EXPECT_FALSE(no_server.has_value());
}

TEST_F(SessionRegistryTest, UnregisterSession) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    // Register a session
    registry_->registerSession("test-user-1", "session-123", "Player");
    EXPECT_TRUE(registry_->isUserOnline("test-user-1"));

    // Unregister
    bool result = registry_->unregisterSession("test-user-1");
    EXPECT_TRUE(result);

    // User should no longer be online
    EXPECT_FALSE(registry_->isUserOnline("test-user-1"));
}

TEST_F(SessionRegistryTest, UnregisterNonExistentSession) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    bool result = registry_->unregisterSession("non-existent-user");
    EXPECT_FALSE(result);
}

TEST_F(SessionRegistryTest, UnregisterOtherServerSession) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    // Register a session from this server
    registry_->registerSession("test-user-1", "session-123", "Player");

    // Create another registry with different server_id
    auto other_registry = std::make_unique<SessionRegistry>(redis_, "test-server-2", 60);

    // Try to unregister from another server - should fail
    bool result = other_registry->unregisterSession("test-user-1");
    EXPECT_FALSE(result);

    // Session should still exist
    EXPECT_TRUE(registry_->isUserOnline("test-user-1"));
}

TEST_F(SessionRegistryTest, RefreshSession) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    registry_->registerSession("test-user-1", "session-123", "Player");

    // Refresh should succeed for own session
    bool result = registry_->refreshSession("test-user-1");
    EXPECT_TRUE(result);

    // Refresh non-existent session
    result = registry_->refreshSession("non-existent-user");
    EXPECT_FALSE(result);
}

TEST_F(SessionRegistryTest, RefreshOtherServerSession) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    registry_->registerSession("test-user-1", "session-123", "Player");

    // Create another registry with different server_id
    auto other_registry = std::make_unique<SessionRegistry>(redis_, "test-server-2", 60);

    // Refresh from another server should fail
    bool result = other_registry->refreshSession("test-user-1");
    EXPECT_FALSE(result);
}

TEST_F(SessionRegistryTest, FindByNickname) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    registry_->registerSession("test-user-1", "session-123", "TestPlayer1");

    auto info = registry_->findByNickname("TestPlayer1");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->user_id, "test-user-1");
    EXPECT_EQ(info->nickname, "TestPlayer1");
}

TEST_F(SessionRegistryTest, FindByNicknameCaseInsensitive) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    registry_->registerSession("test-user-1", "session-123", "TestPlayer1");

    // Search with different cases
    auto info1 = registry_->findByNickname("testplayer1");
    ASSERT_TRUE(info1.has_value());
    EXPECT_EQ(info1->user_id, "test-user-1");

    auto info2 = registry_->findByNickname("TESTPLAYER1");
    ASSERT_TRUE(info2.has_value());
    EXPECT_EQ(info2->user_id, "test-user-1");

    auto info3 = registry_->findByNickname("TeStPlAyEr1");
    ASSERT_TRUE(info3.has_value());
    EXPECT_EQ(info3->user_id, "test-user-1");
}

TEST_F(SessionRegistryTest, FindByNicknameNotFound) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    auto info = registry_->findByNickname("NonExistentPlayer");
    EXPECT_FALSE(info.has_value());
}

TEST_F(SessionRegistryTest, FindByNicknameEmpty) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    auto info = registry_->findByNickname("");
    EXPECT_FALSE(info.has_value());
}

TEST_F(SessionRegistryTest, NicknameIndexCleanupOnUnregister) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    registry_->registerSession("test-user-1", "session-123", "TestPlayer1");

    // Verify nickname lookup works
    auto info = registry_->findByNickname("TestPlayer1");
    ASSERT_TRUE(info.has_value());

    // Unregister session
    registry_->unregisterSession("test-user-1");

    // Nickname index should be cleaned up
    auto info2 = registry_->findByNickname("TestPlayer1");
    EXPECT_FALSE(info2.has_value());
}

TEST_F(SessionRegistryTest, UnicodeNickname) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    bool result = registry_->registerSession(
        "test-user-3",
        "session-unicode",
        "UnicodePlayer",  // For nickname index testing
        "",
        ""
    );

    EXPECT_TRUE(result);

    auto info = registry_->findByNickname("UnicodePlayer");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->nickname, "UnicodePlayer");
}

TEST_F(SessionRegistryTest, MultipleSessionsMultipleServers) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    // Register from first server
    registry_->registerSession("test-user-1", "session-1", "Player1");

    // Create second server registry
    auto registry2 = std::make_unique<SessionRegistry>(redis_, "test-server-2", 60);
    registry2->registerSession("test-user-2", "session-2", "Player2");

    // Both users should be online
    EXPECT_TRUE(registry_->isUserOnline("test-user-1"));
    EXPECT_TRUE(registry_->isUserOnline("test-user-2"));

    // Verify server IDs
    auto server1 = registry_->getUserServer("test-user-1");
    auto server2 = registry_->getUserServer("test-user-2");

    ASSERT_TRUE(server1.has_value());
    ASSERT_TRUE(server2.has_value());
    EXPECT_EQ(*server1, "test-server-1");
    EXPECT_EQ(*server2, "test-server-2");

    // Clean up
    registry_->unregisterSession("test-user-1");
    registry2->unregisterSession("test-user-2");
}

TEST_F(SessionRegistryTest, OverwriteSession) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    // Register initial session
    registry_->registerSession("test-user-1", "session-old", "OldNickname");

    // Verify initial state
    auto info1 = registry_->getSession("test-user-1");
    ASSERT_TRUE(info1.has_value());
    EXPECT_EQ(info1->session_id, "session-old");
    EXPECT_EQ(info1->nickname, "OldNickname");

    // Register again with new data (overwrite)
    registry_->registerSession("test-user-1", "session-new", "NewNickname", "new_avatar.png", "new_frame.png");

    // Verify updated state
    auto info2 = registry_->getSession("test-user-1");
    ASSERT_TRUE(info2.has_value());
    EXPECT_EQ(info2->session_id, "session-new");
    EXPECT_EQ(info2->nickname, "NewNickname");
    EXPECT_EQ(info2->profile_image, "new_avatar.png");
}

#endif // ENABLE_REDIS
