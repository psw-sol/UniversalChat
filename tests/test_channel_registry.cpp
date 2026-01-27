#include <gtest/gtest.h>
#include "redis/ChannelRegistry.hpp"

#ifdef ENABLE_REDIS
#include "redis/RedisClient.hpp"
#endif

using namespace chat;

// ============================================================================
// ChannelRegistryInfo Tests (No Redis Required)
// ============================================================================

class ChannelRegistryInfoTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(ChannelRegistryInfoTest, DefaultConstruction) {
    ChannelRegistryInfo info;

    EXPECT_TRUE(info.channel_id.empty());
    EXPECT_TRUE(info.channel_name.empty());
    EXPECT_EQ(info.max_members, 0);
    EXPECT_FALSE(info.is_system);
    EXPECT_TRUE(info.is_persistent);
    EXPECT_EQ(info.created_at, 0);
    EXPECT_EQ(info.global_member_count, 0);
}

TEST_F(ChannelRegistryInfoTest, IsValidEmpty) {
    ChannelRegistryInfo info;

    // Empty channel_id is not valid
    EXPECT_FALSE(info.isValid());
}

TEST_F(ChannelRegistryInfoTest, IsValidWithChannelId) {
    ChannelRegistryInfo info;
    info.channel_id = "world-1";

    // channel_id alone makes it valid
    EXPECT_TRUE(info.isValid());
}

TEST_F(ChannelRegistryInfoTest, IsValidWithFullData) {
    ChannelRegistryInfo info;
    info.channel_id = "world-1";
    info.channel_name = "World Chat #1";
    info.max_members = 1000;
    info.is_system = true;
    info.is_persistent = true;
    info.created_at = 1705827600;
    info.global_member_count = 150;

    EXPECT_TRUE(info.isValid());
}

// ============================================================================
// ChannelRegistry Tests (Requires Redis)
// ============================================================================

#ifdef ENABLE_REDIS

class ChannelRegistryTest : public ::testing::Test {
protected:
    std::shared_ptr<RedisClient> redis_;
    std::unique_ptr<ChannelRegistry> registry_;
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
            registry_ = std::make_unique<ChannelRegistry>(redis_);

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

        // Delete test channel keys
        redis_->del("chat:channel:test-channel-1:members");
        redis_->del("chat:channel:test-channel-2:members");
        redis_->del("chat:channel:test-channel-3:members");
        redis_->del("chat:channels:test-channel-1");
        redis_->del("chat:channels:test-channel-2");
        redis_->del("chat:channels:test-channel-3");
        redis_->del("chat:user:test-user-1:channels");
        redis_->del("chat:user:test-user-2:channels");
        redis_->del("chat:user:test-user-3:channels");
    }
};

TEST_F(ChannelRegistryTest, IsAvailable) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    EXPECT_TRUE(registry_->isAvailable());
}

TEST_F(ChannelRegistryTest, AddMember) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    bool result = registry_->addMember("test-channel-1", "test-user-1");
    EXPECT_TRUE(result);

    // Verify membership
    EXPECT_TRUE(registry_->isMember("test-channel-1", "test-user-1"));
    EXPECT_EQ(registry_->getMemberCount("test-channel-1"), 1);
}

TEST_F(ChannelRegistryTest, AddMemberMultiple) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    registry_->addMember("test-channel-1", "test-user-1");
    registry_->addMember("test-channel-1", "test-user-2");
    registry_->addMember("test-channel-1", "test-user-3");

    EXPECT_EQ(registry_->getMemberCount("test-channel-1"), 3);

    // Verify all are members
    EXPECT_TRUE(registry_->isMember("test-channel-1", "test-user-1"));
    EXPECT_TRUE(registry_->isMember("test-channel-1", "test-user-2"));
    EXPECT_TRUE(registry_->isMember("test-channel-1", "test-user-3"));
}

TEST_F(ChannelRegistryTest, AddMemberDuplicate) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    registry_->addMember("test-channel-1", "test-user-1");
    registry_->addMember("test-channel-1", "test-user-1");  // Duplicate

    // Should still be just 1 member (sets don't allow duplicates)
    EXPECT_EQ(registry_->getMemberCount("test-channel-1"), 1);
}

TEST_F(ChannelRegistryTest, RemoveMember) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    registry_->addMember("test-channel-1", "test-user-1");
    registry_->addMember("test-channel-1", "test-user-2");

    EXPECT_EQ(registry_->getMemberCount("test-channel-1"), 2);

    bool result = registry_->removeMember("test-channel-1", "test-user-1");
    EXPECT_TRUE(result);

    EXPECT_FALSE(registry_->isMember("test-channel-1", "test-user-1"));
    EXPECT_TRUE(registry_->isMember("test-channel-1", "test-user-2"));
    EXPECT_EQ(registry_->getMemberCount("test-channel-1"), 1);
}

TEST_F(ChannelRegistryTest, RemoveMemberNotExist) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    // Remove from empty channel - should succeed without error
    bool result = registry_->removeMember("test-channel-1", "non-existent-user");
    EXPECT_TRUE(result);
}

TEST_F(ChannelRegistryTest, IsMemberFalse) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    EXPECT_FALSE(registry_->isMember("test-channel-1", "non-existent-user"));
}

TEST_F(ChannelRegistryTest, GetUserChannels) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    // User joins multiple channels
    registry_->addMember("test-channel-1", "test-user-1");
    registry_->addMember("test-channel-2", "test-user-1");
    registry_->addMember("test-channel-3", "test-user-1");

    auto channels = registry_->getUserChannels("test-user-1");
    EXPECT_EQ(channels.size(), 3);

    // Verify all channels are in the list (order not guaranteed)
    std::set<std::string> channel_set(channels.begin(), channels.end());
    EXPECT_EQ(channel_set.count("test-channel-1"), 1);
    EXPECT_EQ(channel_set.count("test-channel-2"), 1);
    EXPECT_EQ(channel_set.count("test-channel-3"), 1);
}

TEST_F(ChannelRegistryTest, GetUserChannelsEmpty) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    auto channels = registry_->getUserChannels("non-existent-user");
    EXPECT_TRUE(channels.empty());
}

TEST_F(ChannelRegistryTest, RemoveUserFromAllChannels) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    // User joins multiple channels
    registry_->addMember("test-channel-1", "test-user-1");
    registry_->addMember("test-channel-2", "test-user-1");
    registry_->addMember("test-channel-3", "test-user-1");

    // Also add another user to some channels
    registry_->addMember("test-channel-1", "test-user-2");

    // Remove test-user-1 from all channels
    int removed = registry_->removeUserFromAllChannels("test-user-1");
    EXPECT_EQ(removed, 3);

    // Verify test-user-1 is no longer a member
    EXPECT_FALSE(registry_->isMember("test-channel-1", "test-user-1"));
    EXPECT_FALSE(registry_->isMember("test-channel-2", "test-user-1"));
    EXPECT_FALSE(registry_->isMember("test-channel-3", "test-user-1"));

    // Verify test-user-2 is still a member
    EXPECT_TRUE(registry_->isMember("test-channel-1", "test-user-2"));

    // Verify user's channel list is empty
    auto channels = registry_->getUserChannels("test-user-1");
    EXPECT_TRUE(channels.empty());
}

TEST_F(ChannelRegistryTest, SetChannelMetadata) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    ChannelConfig config;
    config.channel_id = "test-channel-1";
    config.channel_name = "Test Channel";
    config.max_members = 500;
    config.is_system = true;
    config.is_persistent = true;

    bool result = registry_->setChannelMetadata("test-channel-1", config);
    EXPECT_TRUE(result);

    // Retrieve and verify
    auto info = registry_->getChannelMetadata("test-channel-1");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->channel_id, "test-channel-1");
    EXPECT_EQ(info->channel_name, "Test Channel");
    EXPECT_EQ(info->max_members, 500);
    EXPECT_TRUE(info->is_system);
    EXPECT_TRUE(info->is_persistent);
    EXPECT_GT(info->created_at, 0);
}

TEST_F(ChannelRegistryTest, GetChannelMetadataNotFound) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    auto info = registry_->getChannelMetadata("non-existent-channel");
    EXPECT_FALSE(info.has_value());
}

TEST_F(ChannelRegistryTest, GetChannelMetadataWithMemberCount) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    // Set metadata
    ChannelConfig config;
    config.channel_id = "test-channel-1";
    config.channel_name = "Test Channel";
    config.max_members = 500;
    registry_->setChannelMetadata("test-channel-1", config);

    // Add some members
    registry_->addMember("test-channel-1", "test-user-1");
    registry_->addMember("test-channel-1", "test-user-2");

    // Get metadata should include member count
    auto info = registry_->getChannelMetadata("test-channel-1");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->global_member_count, 2);
}

TEST_F(ChannelRegistryTest, DeleteChannelMetadata) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    // Set metadata
    ChannelConfig config;
    config.channel_id = "test-channel-1";
    config.channel_name = "Test Channel";
    registry_->setChannelMetadata("test-channel-1", config);

    // Add some members
    registry_->addMember("test-channel-1", "test-user-1");

    // Delete metadata
    bool result = registry_->deleteChannelMetadata("test-channel-1");
    EXPECT_TRUE(result);

    // Verify metadata is gone
    auto info = registry_->getChannelMetadata("test-channel-1");
    EXPECT_FALSE(info.has_value());

    // Verify members are also gone
    EXPECT_EQ(registry_->getMemberCount("test-channel-1"), 0);
}

TEST_F(ChannelRegistryTest, GetMemberCountBatch) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    // Add members to different channels
    registry_->addMember("test-channel-1", "test-user-1");
    registry_->addMember("test-channel-1", "test-user-2");
    registry_->addMember("test-channel-2", "test-user-1");
    registry_->addMember("test-channel-3", "test-user-1");
    registry_->addMember("test-channel-3", "test-user-2");
    registry_->addMember("test-channel-3", "test-user-3");

    std::vector<std::string> channel_ids = {
        "test-channel-1", "test-channel-2", "test-channel-3"
    };

    auto counts = registry_->getMemberCountBatch(channel_ids);

    EXPECT_EQ(counts.size(), 3);
    EXPECT_EQ(counts["test-channel-1"], 2);
    EXPECT_EQ(counts["test-channel-2"], 1);
    EXPECT_EQ(counts["test-channel-3"], 3);
}

TEST_F(ChannelRegistryTest, EmptyInputs) {
    if (!redis_available_) {
        GTEST_SKIP() << "Redis not available";
    }

    // Empty channel_id
    EXPECT_FALSE(registry_->addMember("", "test-user-1"));
    EXPECT_FALSE(registry_->removeMember("", "test-user-1"));
    EXPECT_FALSE(registry_->isMember("", "test-user-1"));
    EXPECT_EQ(registry_->getMemberCount(""), 0);

    // Empty user_id
    EXPECT_FALSE(registry_->addMember("test-channel-1", ""));
    EXPECT_FALSE(registry_->removeMember("test-channel-1", ""));
    EXPECT_FALSE(registry_->isMember("test-channel-1", ""));

    // Empty user_id for getUserChannels
    auto channels = registry_->getUserChannels("");
    EXPECT_TRUE(channels.empty());
}

#endif // ENABLE_REDIS
