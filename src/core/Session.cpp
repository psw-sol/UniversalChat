#include "Session.hpp"
#include "SessionManager.hpp"
#include "../channel/ChannelManager.hpp"
#include "../message/MessageDispatcher.hpp"
#include "../protocol/PacketCodec.hpp"
#include "../protocol/PacketHeader.hpp"
#include "../util/Config.hpp"
#include "../util/Logger.hpp"
#include "../util/Snowflake.hpp"

namespace chat {

Session::Session(boost::asio::io_context& io_context,
                 SessionManager& manager,
                 ChannelManager& channel_manager,
                 MessageDispatcher& dispatcher,
                 const Config& config)
    : socket_(io_context)
    , manager_(manager)
    , channel_manager_(channel_manager)
    , dispatcher_(dispatcher)
    , config_(config)
    , session_id_(generateSessionId())
    , last_heartbeat_(Clock::now())
{
    body_buffer_.reserve(config.read_buffer_size);
}

Session::~Session() {
    close();
}

void Session::start() {
    connected_ = true;

    // Reset heartbeat when connection is actually established
    // (Session object may have been created earlier while waiting for accept)
    updateHeartbeat();

    try {
        auto endpoint = socket_.remote_endpoint();
        LOG_INFO("Session {} connected from {}:{}",
                 session_id_,
                 endpoint.address().to_string(),
                 endpoint.port());
    } catch (const std::exception& e) {
        LOG_WARN("Session {} failed to get remote endpoint: {}",
                session_id_, e.what());
    }

    // Add to session manager
    manager_.add(shared_from_this());

    // Start reading
    doReadHeader();
}

void Session::close() {
    if (!connected_.exchange(false)) {
        return;  // Already closed
    }

    LOG_INFO("Session {} closing", session_id_);

    // 1. Channel cleanup (before socket shutdown)
    try {
        channel_manager_.onSessionDisconnect(shared_from_this());
    } catch (const std::exception& e) {
        LOG_ERROR("Session {} channel cleanup error: {}", session_id_, e.what());
    }

    // 2. Close socket
    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);

    // 3. Remove from session manager
    manager_.remove(session_id_);
}

Session::SendResult Session::send(const Packet& packet) {
    if (!connected_) return SendResult::Disconnected;

    auto data = PacketCodec::encode(packet);
    return sendRaw(std::move(data));
}

Session::SendResult Session::sendRaw(std::vector<char> data) {
    if (!connected_) return SendResult::Disconnected;

    size_t data_size = data.size();

    {
        std::lock_guard<std::mutex> lock(write_mutex_);

        // Check queue limits
        if (write_queue_.size() >= MAX_WRITE_QUEUE_SIZE ||
            write_queue_bytes_.load() + data_size > MAX_QUEUE_BYTES) {

            LOG_WARN("Session {} write queue overflow (items={}, bytes={})",
                     session_id_, write_queue_.size(), write_queue_bytes_.load());

            // Schedule close (deferred to avoid deadlock)
            boost::asio::post(socket_.get_executor(), [self = shared_from_this()]() {
                self->close();
            });
            return SendResult::QueueFull;
        }

        // Warning threshold check
        if (write_queue_.size() >= WRITE_QUEUE_WARNING_THRESHOLD) {
            LOG_WARN("Session {} write queue high (items={}, bytes={})",
                     session_id_, write_queue_.size(), write_queue_bytes_.load());
        }

        write_queue_bytes_ += data_size;
        write_queue_.push_back(std::move(data));
    }

    // Start writing if not already
    if (!writing_.exchange(true)) {
        doWrite();
    }

    return SendResult::Success;
}

size_t Session::writeQueueSize() const {
    std::lock_guard<std::mutex> lock(write_mutex_);
    return write_queue_.size();
}

size_t Session::writeQueueBytes() const {
    return write_queue_bytes_.load();
}

bool Session::isWriteQueueFull() const {
    std::lock_guard<std::mutex> lock(write_mutex_);
    return write_queue_.size() >= MAX_WRITE_QUEUE_SIZE ||
           write_queue_bytes_.load() >= MAX_QUEUE_BYTES;
}

std::string Session::remoteAddress() const {
    try {
        return socket_.remote_endpoint().address().to_string();
    } catch (...) {
        return "";
    }
}

uint16_t Session::remotePort() const {
    try {
        return socket_.remote_endpoint().port();
    } catch (...) {
        return 0;
    }
}

void Session::setAuthenticated(const std::string& user_id,
                               const std::string& nickname,
                               const std::string& auth_token,
                               const std::string& profile_image,
                               const std::string& frame_image,
                               const std::string& extra_data) {
    user_id_ = user_id;
    nickname_ = nickname;
    auth_token_ = auth_token;
    profile_image_ = profile_image;
    frame_image_ = frame_image;
    extra_data_ = extra_data;
    authenticated_ = true;

    LOG_INFO("Session {} authenticated as {} ({}) profile={} frame={}",
             session_id_, nickname, user_id, profile_image, frame_image);

    // Update user mapping in session manager
    manager_.updateUserMapping(user_id, session_id_);
}

void Session::updateProfile(const std::string* nickname,
                            const std::string* profile_image,
                            const std::string* frame_image,
                            const std::string* extra_data) {
    if (nickname && !nickname->empty()) {
        nickname_ = *nickname;
    }
    if (profile_image) {
        profile_image_ = *profile_image;
    }
    if (frame_image) {
        frame_image_ = *frame_image;
    }
    if (extra_data) {
        extra_data_ = *extra_data;
    }

    LOG_DEBUG("Session {} profile updated: nickname={}, profile={}, frame={}",
              session_id_, nickname_, profile_image_, frame_image_);
}

void Session::addChannel(const std::string& channel_id) {
    std::unique_lock<std::shared_mutex> lock(channels_mutex_);
    joined_channels_.insert(channel_id);
    LOG_DEBUG("Session {} joined channel {}", session_id_, channel_id);
}

void Session::removeChannel(const std::string& channel_id) {
    std::unique_lock<std::shared_mutex> lock(channels_mutex_);
    joined_channels_.erase(channel_id);
    LOG_DEBUG("Session {} left channel {}", session_id_, channel_id);
}

bool Session::isInChannel(const std::string& channel_id) const {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    return joined_channels_.count(channel_id) > 0;
}

std::set<std::string> Session::getJoinedChannels() const {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    return joined_channels_;
}

size_t Session::channelCount() const {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    return joined_channels_.size();
}

void Session::updateHeartbeat() {
    std::lock_guard<std::mutex> lock(heartbeat_mutex_);
    last_heartbeat_ = Clock::now();
}

TimePoint Session::lastHeartbeat() const {
    std::lock_guard<std::mutex> lock(heartbeat_mutex_);
    return last_heartbeat_;
}

bool Session::isExpired(Seconds timeout) const {
    auto now = Clock::now();
    auto last = lastHeartbeat();
    return (now - last) > timeout;
}

SessionStats Session::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

// ==========================================
// Private Methods
// ==========================================

void Session::doReadHeader() {
    if (!connected_) return;

    auto self = shared_from_this();

    boost::asio::async_read(
        socket_,
        boost::asio::buffer(header_buffer_),
        [this, self](const boost::system::error_code& ec, std::size_t bytes) {
            handleReadHeader(ec, bytes);
        }
    );
}

void Session::handleReadHeader(const boost::system::error_code& ec,
                               std::size_t bytes) {
    if (ec) {
        if (ec != boost::asio::error::eof &&
            ec != boost::asio::error::operation_aborted &&
            ec != boost::asio::error::connection_reset) {
            LOG_ERROR("Session {} read header error: {}",
                     session_id_, ec.message());
        }
        close();
        return;
    }

    // Parse header
    PacketHeader header = PacketHeader::fromBytes(header_buffer_.data());

    // Validate header
    if (!PacketCodec::validateHeader(header, config_.max_packet_size)) {
        LOG_WARN("Session {} invalid packet header: length={}, type={}",
                session_id_, header.length, header.type);
        close();
        return;
    }

    uint32_t body_length = header.bodyLength();

    if (body_length > 0) {
        body_buffer_.resize(body_length);
        doReadBody(body_length);
    } else {
        processPacket();
        doReadHeader();
    }
}

void Session::doReadBody(uint32_t body_length) {
    if (!connected_) return;

    auto self = shared_from_this();

    boost::asio::async_read(
        socket_,
        boost::asio::buffer(body_buffer_),
        [this, self](const boost::system::error_code& ec, std::size_t bytes) {
            handleReadBody(ec, bytes);
        }
    );
}

void Session::handleReadBody(const boost::system::error_code& ec,
                             std::size_t bytes) {
    if (ec) {
        LOG_ERROR("Session {} read body error: {}", session_id_, ec.message());
        close();
        return;
    }

    processPacket();

    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.bytes_received += HEADER_SIZE + bytes;
        stats_.packets_received++;
    }

    // Continue reading
    doReadHeader();
}

void Session::processPacket() {
    PacketHeader header = PacketHeader::fromBytes(header_buffer_.data());

    Packet packet;
    packet.type = static_cast<PacketType>(header.type);
    packet.data = std::move(body_buffer_);

    // Dispatch to message handler
    dispatcher_.dispatch(shared_from_this(), packet);

    // Clear body buffer for next packet
    body_buffer_.clear();
}

void Session::doWrite() {
    if (!connected_) return;

    std::vector<char> data;
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (write_queue_.empty()) {
            writing_ = false;
            return;
        }
        data = std::move(write_queue_.front());
        write_queue_.pop_front();
        write_queue_bytes_ -= data.size();  // Decrement byte counter
    }

    auto self = shared_from_this();
    auto data_size = data.size();

    boost::asio::async_write(
        socket_,
        boost::asio::buffer(data),
        [this, self, data_size](const boost::system::error_code& ec, std::size_t bytes) {
            handleWrite(ec, bytes);
        }
    );
}

void Session::handleWrite(const boost::system::error_code& ec,
                          std::size_t bytes) {
    if (ec) {
        LOG_ERROR("Session {} write error: {}", session_id_, ec.message());
        close();
        return;
    }

    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.bytes_sent += bytes;
        stats_.packets_sent++;
    }

    // Continue writing if more data
    doWrite();
}

std::string Session::generateSessionId() {
    return Snowflake::generate();
}

} // namespace chat
