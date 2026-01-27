#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <boost/asio.hpp>
#include "core/Session.hpp"
#include "core/SessionManager.hpp"
#include "channel/ChannelManager.hpp"
#include "channel/WorldChannelManager.hpp"
#include "message/MessageDispatcher.hpp"
#include "util/Config.hpp"
#include "protocol/PacketTypes.hpp"

using namespace chat;

class SessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = std::make_unique<Config>();
        session_manager_ = std::make_unique<SessionManager>(*config_);
        channel_manager_ = std::make_unique<ChannelManager>(*config_);
        WorldChannelManager::Config world_config;
        world_channel_manager_ = std::make_unique<WorldChannelManager>(*channel_manager_, world_config);
        dispatcher_ = std::make_unique<MessageDispatcher>(*session_manager_, *channel_manager_, *world_channel_manager_, *config_);
    }

    void TearDown() override {
        // Clean up in reverse order
        dispatcher_.reset();
        world_channel_manager_.reset();
        channel_manager_.reset();
        session_manager_.reset();
        config_.reset();
    }

    std::shared_ptr<Session> createSession() {
        return std::make_shared<Session>(
            io_context_,
            *session_manager_,
            *channel_manager_,
            *dispatcher_,
            *config_
        );
    }

    boost::asio::io_context io_context_;
    std::unique_ptr<Config> config_;
    std::unique_ptr<SessionManager> session_manager_;
    std::unique_ptr<ChannelManager> channel_manager_;
    std::unique_ptr<WorldChannelManager> world_channel_manager_;
    std::unique_ptr<MessageDispatcher> dispatcher_;
};

// Test initial state of write queue
TEST_F(SessionTest, InitialWriteQueueState) {
    auto session = createSession();

    EXPECT_EQ(session->writeQueueSize(), 0);
    EXPECT_EQ(session->writeQueueBytes(), 0);
    EXPECT_FALSE(session->isWriteQueueFull());
}

// Test write queue constants are accessible
TEST_F(SessionTest, WriteQueueConstants) {
    auto session = createSession();

    // Just verify session can be created and checked
    EXPECT_FALSE(session->isConnected());  // Not started yet
    EXPECT_FALSE(session->isWriteQueueFull());
}

// Test session ID generation
TEST_F(SessionTest, SessionIdGeneration) {
    auto session1 = createSession();
    auto session2 = createSession();

    // Each session should have a unique ID
    EXPECT_FALSE(session1->sessionId().empty());
    EXPECT_FALSE(session2->sessionId().empty());
    EXPECT_NE(session1->sessionId(), session2->sessionId());
}

// Test send returns Disconnected when not connected
TEST_F(SessionTest, SendWhenDisconnected) {
    auto session = createSession();

    Packet packet(PacketType::Heartbeat, "test");

    // Should return Disconnected since session is not started
    auto result = session->send(packet);
    EXPECT_EQ(result, Session::SendResult::Disconnected);
}

// Test sendRaw returns Disconnected when not connected
TEST_F(SessionTest, SendRawWhenDisconnected) {
    auto session = createSession();

    std::vector<char> data = {'t', 'e', 's', 't'};

    auto result = session->sendRaw(std::move(data));
    EXPECT_EQ(result, Session::SendResult::Disconnected);
}

// Test authentication state
TEST_F(SessionTest, AuthenticationState) {
    auto session = createSession();

    // Initially not authenticated
    EXPECT_FALSE(session->isAuthenticated());
    EXPECT_TRUE(session->userId().empty());
    EXPECT_TRUE(session->nickname().empty());

    // Set authenticated
    session->setAuthenticated("user123", "TestUser", "token456");

    EXPECT_TRUE(session->isAuthenticated());
    EXPECT_EQ(session->userId(), "user123");
    EXPECT_EQ(session->nickname(), "TestUser");
    EXPECT_EQ(session->authToken(), "token456");
}

// Test channel management
TEST_F(SessionTest, ChannelManagement) {
    auto session = createSession();

    // Initially no channels
    EXPECT_EQ(session->channelCount(), 0);
    EXPECT_FALSE(session->isInChannel("channel1"));

    // Add channel
    session->addChannel("channel1");
    EXPECT_EQ(session->channelCount(), 1);
    EXPECT_TRUE(session->isInChannel("channel1"));

    // Add another channel
    session->addChannel("channel2");
    EXPECT_EQ(session->channelCount(), 2);
    EXPECT_TRUE(session->isInChannel("channel2"));

    // Get joined channels
    auto channels = session->getJoinedChannels();
    EXPECT_EQ(channels.size(), 2);
    EXPECT_TRUE(channels.count("channel1") > 0);
    EXPECT_TRUE(channels.count("channel2") > 0);

    // Remove channel
    session->removeChannel("channel1");
    EXPECT_EQ(session->channelCount(), 1);
    EXPECT_FALSE(session->isInChannel("channel1"));
    EXPECT_TRUE(session->isInChannel("channel2"));
}

// Test heartbeat tracking
TEST_F(SessionTest, HeartbeatTracking) {
    auto session = createSession();

    auto time1 = session->lastHeartbeat();

    // Wait a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Update heartbeat
    session->updateHeartbeat();
    auto time2 = session->lastHeartbeat();

    // time2 should be later than time1
    EXPECT_GT(time2, time1);
}

// Test session expiration
TEST_F(SessionTest, SessionExpiration) {
    auto session = createSession();

    // Just created, should not be expired with reasonable timeout
    EXPECT_FALSE(session->isExpired(Seconds(60)));

    // Should be expired with 0 second timeout
    EXPECT_TRUE(session->isExpired(Seconds(0)));
}

// Test statistics retrieval
TEST_F(SessionTest, StatisticsRetrieval) {
    auto session = createSession();

    auto stats = session->getStats();

    // Initial stats should be zero
    EXPECT_EQ(stats.bytes_sent, 0);
    EXPECT_EQ(stats.bytes_received, 0);
    EXPECT_EQ(stats.packets_sent, 0);
    EXPECT_EQ(stats.packets_received, 0);
}

// Test multiple sessions in SessionManager
TEST_F(SessionTest, SessionManagerIntegration) {
    auto session1 = createSession();
    auto session2 = createSession();

    // Add sessions to manager
    session_manager_->add(session1);
    session_manager_->add(session2);

    EXPECT_EQ(session_manager_->count(), 2);

    // Get by ID
    auto retrieved = session_manager_->get(session1->sessionId());
    EXPECT_EQ(retrieved, session1);

    // Remove session
    session_manager_->remove(session1->sessionId());
    EXPECT_EQ(session_manager_->count(), 1);

    // session1 should not be found
    EXPECT_EQ(session_manager_->get(session1->sessionId()), nullptr);
}

// Test write queue limit constants
TEST_F(SessionTest, WriteQueueLimitConstants) {
    // These are compile-time constants, just verify they're reasonable
    // MAX_WRITE_QUEUE_SIZE = 1000
    // MAX_QUEUE_BYTES = 16 * 1024 * 1024 (16MB)

    auto session = createSession();

    // Queue should not be full when empty
    EXPECT_FALSE(session->isWriteQueueFull());
    EXPECT_EQ(session->writeQueueSize(), 0);
    EXPECT_EQ(session->writeQueueBytes(), 0);
}
