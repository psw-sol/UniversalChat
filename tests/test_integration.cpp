#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <future>
#include <boost/asio.hpp>

#include "core/Server.hpp"
#include "core/Session.hpp"
#include "util/Config.hpp"
#include "util/Logger.hpp"
#include "util/Snowflake.hpp"
#include "protocol/PacketCodec.hpp"
#include "protocol/PacketTypes.hpp"
#include "chat.pb.h"

using namespace chat;
using namespace chat::protocol;
using boost::asio::ip::tcp;

/**
 * Simple test client for integration testing
 */
class TestClient {
public:
    TestClient(boost::asio::io_context& io_context)
        : socket_(io_context)
        , connected_(false)
    {}

    bool connect(const std::string& host, uint16_t port) {
        try {
            tcp::resolver resolver(socket_.get_executor());
            auto endpoints = resolver.resolve(host, std::to_string(port));
            boost::asio::connect(socket_, endpoints);
            connected_ = true;
            return true;
        } catch (const std::exception& e) {
            return false;
        }
    }

    void disconnect() {
        if (connected_) {
            boost::system::error_code ec;
            socket_.shutdown(tcp::socket::shutdown_both, ec);
            socket_.close(ec);
            connected_ = false;
        }
    }

    bool isConnected() const { return connected_; }

    bool sendPacket(const Packet& packet) {
        if (!connected_) return false;

        try {
            auto data = PacketCodec::encode(packet);
            boost::asio::write(socket_, boost::asio::buffer(data));
            return true;
        } catch (const std::exception& e) {
            return false;
        }
    }

    std::optional<Packet> receivePacket(int timeoutMs = 5000) {
        if (!connected_) return std::nullopt;

        try {
            // Set socket to non-blocking temporarily
            socket_.non_blocking(true);

            std::array<char, 8> header;
            boost::system::error_code ec;
            size_t totalRead = 0;

            auto deadline = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(timeoutMs);

            // Read header with timeout
            while (totalRead < 8) {
                if (std::chrono::steady_clock::now() > deadline) {
                    socket_.non_blocking(false);
                    return std::nullopt;  // Timeout
                }

                size_t n = socket_.read_some(
                    boost::asio::buffer(header.data() + totalRead, 8 - totalRead), ec);

                if (ec == boost::asio::error::would_block) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                if (ec) {
                    socket_.non_blocking(false);
                    return std::nullopt;
                }
                totalRead += n;
            }

            // Parse header
            uint32_t totalLength = 0;
            std::memcpy(&totalLength, header.data(), 4);

            uint16_t type = 0;
            std::memcpy(&type, header.data() + 4, 2);

            // Body length = total length - header size (8 bytes)
            uint32_t bodyLength = totalLength - 8;

            // Read body
            std::vector<char> body(bodyLength);
            totalRead = 0;

            while (totalRead < bodyLength) {
                if (std::chrono::steady_clock::now() > deadline) {
                    socket_.non_blocking(false);
                    return std::nullopt;
                }

                size_t n = socket_.read_some(
                    boost::asio::buffer(body.data() + totalRead, bodyLength - totalRead), ec);

                if (ec == boost::asio::error::would_block) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                if (ec) {
                    socket_.non_blocking(false);
                    return std::nullopt;
                }
                totalRead += n;
            }

            socket_.non_blocking(false);

            Packet packet;
            packet.type = static_cast<PacketType>(type);
            packet.data = std::move(body);
            return packet;

        } catch (const std::exception& e) {
            socket_.non_blocking(false);
            return std::nullopt;
        }
    }

    // Send auth request
    bool authenticate(const std::string& userId, const std::string& nickname) {
        AuthRequest request;
        request.set_user_id(userId);
        request.set_nickname(nickname);

        std::string serialized;
        request.SerializeToString(&serialized);

        Packet packet(PacketType::AuthRequest,
                     std::vector<char>(serialized.begin(), serialized.end()));

        return sendPacket(packet);
    }

    // Check if auth response is success
    bool waitForAuthResponse() {
        auto response = receivePacket();
        if (!response) return false;

        if (response->type == PacketType::AuthResponse) {
            AuthResponse authResp;
            if (authResp.ParseFromArray(response->data.data(), response->data.size())) {
                return authResp.success();
            }
        }
        return false;
    }

private:
    tcp::socket socket_;
    bool connected_;
};

/**
 * Integration test fixture
 */
class IntegrationTest : public ::testing::Test {
protected:
    static constexpr uint16_t TEST_PORT = 17777;  // Different from default to avoid conflicts

    void SetUp() override {
        // Initialize logger and snowflake (once)
        static bool initialized = false;
        if (!initialized) {
            Logger::init("", "error", true, 10, 3);  // Minimal logging
            Snowflake::init(0);
            initialized = true;
        }

        // Create config
        config_ = std::make_unique<Config>();
        config_->port = TEST_PORT;
        config_->io_threads = 2;
        config_->heartbeat_timeout = 60;

        // Create and start server
        server_ = Server::create(*config_);
        server_->start();

        // Wait for server to be ready
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void TearDown() override {
        // Stop server
        if (server_) {
            server_->stop();
            server_.reset();
        }

        // Wait for cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::unique_ptr<TestClient> createClient() {
        auto client = std::make_unique<TestClient>(io_context_);
        return client;
    }

    boost::asio::io_context io_context_;
    std::unique_ptr<Config> config_;
    Server::Ptr server_;
};

// Test basic connection
TEST_F(IntegrationTest, BasicConnection) {
    auto client = createClient();

    EXPECT_TRUE(client->connect("127.0.0.1", TEST_PORT));
    EXPECT_TRUE(client->isConnected());

    client->disconnect();
    EXPECT_FALSE(client->isConnected());
}

// Test multiple connections
TEST_F(IntegrationTest, MultipleConnections) {
    std::vector<std::unique_ptr<TestClient>> clients;

    // Connect 5 clients
    for (int i = 0; i < 5; ++i) {
        auto client = createClient();
        EXPECT_TRUE(client->connect("127.0.0.1", TEST_PORT));
        clients.push_back(std::move(client));
    }

    // All should be connected
    for (const auto& client : clients) {
        EXPECT_TRUE(client->isConnected());
    }

    // Disconnect all
    for (auto& client : clients) {
        client->disconnect();
    }
}

// Test heartbeat
TEST_F(IntegrationTest, Heartbeat) {
    auto client = createClient();
    ASSERT_TRUE(client->connect("127.0.0.1", TEST_PORT));

    // Send heartbeat
    Packet heartbeat(PacketType::Heartbeat);
    EXPECT_TRUE(client->sendPacket(heartbeat));

    // Should receive heartbeat ack
    auto response = client->receivePacket(2000);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->type, PacketType::HeartbeatAck);

    client->disconnect();
}

// Test authentication flow
TEST_F(IntegrationTest, Authentication) {
    auto client = createClient();
    ASSERT_TRUE(client->connect("127.0.0.1", TEST_PORT));

    // Authenticate
    EXPECT_TRUE(client->authenticate("user123", "TestUser"));

    // Wait for response
    EXPECT_TRUE(client->waitForAuthResponse());

    client->disconnect();
}

// Test channel join (requires authentication)
TEST_F(IntegrationTest, ChannelJoin) {
    auto client = createClient();
    ASSERT_TRUE(client->connect("127.0.0.1", TEST_PORT));

    // Authenticate first
    EXPECT_TRUE(client->authenticate("user456", "ChannelTester"));
    EXPECT_TRUE(client->waitForAuthResponse());

    // Request channel list
    Packet listRequest(PacketType::ChannelListRequest);
    EXPECT_TRUE(client->sendPacket(listRequest));

    // Wait for channel list response
    auto response = client->receivePacket(2000);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->type, PacketType::ChannelListResponse);

    client->disconnect();
}

// Test unauthenticated request rejection
TEST_F(IntegrationTest, UnauthenticatedRejection) {
    auto client = createClient();
    ASSERT_TRUE(client->connect("127.0.0.1", TEST_PORT));

    // Try to request channel list without authentication
    Packet listRequest(PacketType::ChannelListRequest);
    EXPECT_TRUE(client->sendPacket(listRequest));

    // Should receive error response
    auto response = client->receivePacket(2000);
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->type, PacketType::Error);

    client->disconnect();
}

// Test rapid connect/disconnect
TEST_F(IntegrationTest, RapidConnectDisconnect) {
    for (int i = 0; i < 10; ++i) {
        auto client = createClient();
        EXPECT_TRUE(client->connect("127.0.0.1", TEST_PORT));
        client->disconnect();
    }
}

// Test concurrent authentication
TEST_F(IntegrationTest, ConcurrentAuthentication) {
    std::vector<std::future<bool>> futures;

    for (int i = 0; i < 5; ++i) {
        futures.push_back(std::async(std::launch::async, [this, i]() {
            boost::asio::io_context io;
            TestClient client(io);

            if (!client.connect("127.0.0.1", TEST_PORT)) {
                return false;
            }

            std::string userId = "user" + std::to_string(i);
            std::string nickname = "User" + std::to_string(i);

            if (!client.authenticate(userId, nickname)) {
                return false;
            }

            bool result = client.waitForAuthResponse();
            client.disconnect();
            return result;
        }));
    }

    // All should succeed
    for (auto& future : futures) {
        EXPECT_TRUE(future.get());
    }
}

// Test server graceful shutdown with connected clients
TEST_F(IntegrationTest, GracefulShutdown) {
    std::vector<std::unique_ptr<TestClient>> clients;

    // Connect multiple clients
    for (int i = 0; i < 3; ++i) {
        auto client = createClient();
        if (client->connect("127.0.0.1", TEST_PORT)) {
            clients.push_back(std::move(client));
        }
    }

    EXPECT_EQ(clients.size(), 3);

    // Server will be stopped in TearDown
    // Just verify clients are still connected before shutdown
    for (const auto& client : clients) {
        EXPECT_TRUE(client->isConnected());
    }
}

// Test message exchange between clients
TEST_F(IntegrationTest, DISABLED_MessageExchange) {
    // This test is disabled as it requires full message flow implementation
    // Enable when ready for end-to-end message testing

    auto client1 = createClient();
    auto client2 = createClient();

    ASSERT_TRUE(client1->connect("127.0.0.1", TEST_PORT));
    ASSERT_TRUE(client2->connect("127.0.0.1", TEST_PORT));

    // Authenticate both
    client1->authenticate("sender", "Sender");
    client1->waitForAuthResponse();

    client2->authenticate("receiver", "Receiver");
    client2->waitForAuthResponse();

    // TODO: Join channel, send message, verify receipt

    client1->disconnect();
    client2->disconnect();
}
