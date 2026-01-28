#include "MessageDispatcher.hpp"
#include "../core/Session.hpp"
#include "../core/SessionManager.hpp"
#include "../channel/ChannelManager.hpp"
#include "../channel/WorldChannelManager.hpp"
#include "../channel/Channel.hpp"
#include "../announcement/AnnouncementService.hpp"
#include "../notification/UserActionService.hpp"
#include "../protocol/PacketCodec.hpp"
#include "../util/Config.hpp"
#include "../util/Logger.hpp"
#include "../util/Snowflake.hpp"
#include <universalchat/Errors.hpp>

#ifdef ENABLE_REDIS
#include "../redis/SessionRegistry.hpp"
#include "../redis/RedisPubSub.hpp"
#include "../redis/PubSubMessage.hpp"
#include "../redis/ChannelRegistry.hpp"
#endif

// Include protobuf generated headers
#include "chat.pb.h"

namespace chat {

MessageDispatcher::MessageDispatcher(SessionManager& session_manager,
                                     ChannelManager& channel_manager,
                                     WorldChannelManager& world_channel_manager,
                                     const Config& config)
    : session_manager_(session_manager)
    , channel_manager_(channel_manager)
    , world_channel_manager_(world_channel_manager)
    , config_(config)
{
    // Register default handlers
    registerHandler(PacketType::Heartbeat,
        [this](auto s, const auto& p) { handleHeartbeat(s, p); });
    registerHandler(PacketType::AuthRequest,
        [this](auto s, const auto& p) { handleAuth(s, p); });
    registerHandler(PacketType::ChannelListRequest,
        [this](auto s, const auto& p) { handleChannelList(s, p); });
    registerHandler(PacketType::ChannelJoin,
        [this](auto s, const auto& p) { handleChannelJoin(s, p); });
    registerHandler(PacketType::ChannelLeave,
        [this](auto s, const auto& p) { handleChannelLeaveRequest(s, p); });
    registerHandler(PacketType::MessageSend,
        [this](auto s, const auto& p) { handleMessageSend(s, p); });
    registerHandler(PacketType::WhisperSend,
        [this](auto s, const auto& p) { handleWhisperSend(s, p); });
    registerHandler(PacketType::ProfileUpdateRequest,
        [this](auto s, const auto& p) { handleProfileUpdate(s, p); });
    registerHandler(PacketType::ChannelAutoAssign,
        [this](auto s, const auto& p) { handleChannelAutoAssign(s, p); });
    registerHandler(PacketType::AnnouncementSend,
        [this](auto s, const auto& p) { handleAnnouncementSend(s, p); });
    registerHandler(PacketType::UserActionNotificationSend,
        [this](auto s, const auto& p) { handleUserActionNotificationSend(s, p); });

    LOG_INFO("MessageDispatcher initialized with {} handlers", handlers_.size());
}

void MessageDispatcher::setAnnouncementService(std::shared_ptr<AnnouncementService> announcement_service) {
    announcement_service_ = std::move(announcement_service);
    LOG_INFO("MessageDispatcher: AnnouncementService set (available={})",
             announcement_service_ ? "true" : "false");
}

void MessageDispatcher::setUserActionService(std::shared_ptr<UserActionService> user_action_service) {
    user_action_service_ = std::move(user_action_service);
    LOG_INFO("MessageDispatcher: UserActionService set (available={})",
             user_action_service_ ? "true" : "false");
}

#ifdef ENABLE_REDIS
void MessageDispatcher::setRedisComponents(std::shared_ptr<SessionRegistry> session_registry,
                                           std::shared_ptr<RedisPubSub> pubsub) {
    session_registry_ = std::move(session_registry);
    pubsub_ = std::move(pubsub);
    LOG_INFO("MessageDispatcher: Redis components set (registry={}, pubsub={})",
             session_registry_ ? "available" : "null",
             pubsub_ ? "available" : "null");
}
#endif

void MessageDispatcher::dispatch(SessionPtr session, const Packet& packet) {
    auto it = handlers_.find(packet.type);
    if (it != handlers_.end()) {
        try {
            // Check authentication for protected packet types
            if (requiresAuth(packet.type) && !session->isAuthenticated()) {
                LOG_WARN("Session {} sent {} without authentication",
                        session->sessionId(), getPacketTypeName(packet.type));
                sendError(session, toInt(ErrorCode::AuthRequired), "Authentication required");
                return;
            }

            // Rate limiting check
            int cost = getPacketCost(packet.type);
            if (cost > 0 && !rate_limiter_.tryConsume(session->sessionId(), cost)) {
                sendError(session, toInt(ErrorCode::RateLimited), "Too many requests");
                return;
            }

            it->second(session, packet);

        } catch (const std::exception& e) {
            LOG_ERROR("Handler error for packet type {}: {}",
                     getPacketTypeName(packet.type), e.what());
            sendError(session, toInt(ErrorCode::InternalError), "Internal server error");
        }
    } else {
        LOG_WARN("Unknown packet type: 0x{:04X}", static_cast<int>(packet.type));
    }
}

void MessageDispatcher::registerHandler(PacketType type, Handler handler) {
    handlers_[type] = std::move(handler);
}

void MessageDispatcher::unregisterHandler(PacketType type) {
    handlers_.erase(type);
}

// ==========================================
// Handler Implementations
// ==========================================

void MessageDispatcher::handleHeartbeat(SessionPtr session, const Packet& packet) {
    session->updateHeartbeat();

    // Send heartbeat acknowledgment
    protocol::HeartbeatAck ack;
    ack.set_server_time(
        std::chrono::duration_cast<Milliseconds>(
            SystemClock::now().time_since_epoch()).count());

    Packet response;
    response.type = PacketType::HeartbeatAck;
    std::string data;
    ack.SerializeToString(&data);
    response.data = std::vector<char>(data.begin(), data.end());

    session->send(response);
}

void MessageDispatcher::handleAuth(SessionPtr session, const Packet& packet) {
    protocol::AuthRequest request;
    if (!request.ParseFromArray(packet.data.data(), static_cast<int>(packet.data.size()))) {
        LOG_WARN("Failed to parse AuthRequest from session {}", session->sessionId());
        sendError(session, toInt(ErrorCode::InvalidPacket), "Invalid auth request");
        return;
    }

    protocol::AuthResponse response;

    // Validate user ID
    if (request.user_id().empty()) {
        response.set_success(false);
        response.set_error_message("User ID is required");
        response.set_error_code(toInt(ErrorCode::InvalidUserId));
    }
    // Check for duplicate sessions (optional - based on config)
    else if (session_manager_.getByUserId(request.user_id()) != nullptr) {
        response.set_success(false);
        response.set_error_message("User already connected");
        response.set_error_code(toInt(ErrorCode::DuplicateSession));
    }
    else {
        // Authentication successful
        std::string nickname = request.nickname().empty() ?
                              request.user_id() : request.nickname();

        session->setAuthenticated(
            request.user_id(),
            nickname,
            request.auth_token(),
            request.profile_image(),
            request.frame_image(),
            request.extra_data()
        );

        response.set_success(true);
        response.set_session_id(session->sessionId());
        response.set_server_time(
            std::chrono::duration_cast<Milliseconds>(
                SystemClock::now().time_since_epoch()).count());
        response.set_heartbeat_interval(config_.heartbeat_interval);
    }

    Packet resp_packet;
    resp_packet.type = PacketType::AuthResponse;
    std::string data;
    response.SerializeToString(&data);
    resp_packet.data = std::vector<char>(data.begin(), data.end());

    session->send(resp_packet);
}

void MessageDispatcher::handleChannelList(SessionPtr session, const Packet& packet) {
    if (!checkAuthenticated(session, packet)) return;

    protocol::ChannelListResponse response;

    auto channel_list = channel_manager_.getChannelList();

#ifdef ENABLE_REDIS
    // Get global member counts from ChannelRegistry if available
    std::unordered_map<std::string, int64_t> global_member_counts;
    auto channel_registry = channel_manager_.getChannelRegistry();
    if (channel_registry && channel_registry->isAvailable()) {
        std::vector<std::string> channel_ids;
        channel_ids.reserve(channel_list.size());
        for (const auto& ch : channel_list) {
            channel_ids.push_back(ch.channel_id);
        }
        global_member_counts = channel_registry->getMemberCountBatch(channel_ids);
    }
#endif

    for (const auto& ch : channel_list) {
        auto* channel_info = response.add_channels();
        channel_info->set_channel_id(ch.channel_id);
        channel_info->set_channel_name(ch.channel_name);

#ifdef ENABLE_REDIS
        // Use global member count if available, otherwise use local count
        auto it = global_member_counts.find(ch.channel_id);
        if (it != global_member_counts.end() && it->second > 0) {
            channel_info->set_member_count(static_cast<int>(it->second));
        } else {
            channel_info->set_member_count(static_cast<int>(ch.member_count));
        }
#else
        channel_info->set_member_count(static_cast<int>(ch.member_count));
#endif

        channel_info->set_max_members(ch.max_members);
        channel_info->set_is_system(ch.is_system);
    }

    Packet resp_packet;
    resp_packet.type = PacketType::ChannelListResponse;
    std::string data;
    response.SerializeToString(&data);
    resp_packet.data = std::vector<char>(data.begin(), data.end());

    session->send(resp_packet);
}

void MessageDispatcher::handleChannelJoin(SessionPtr session, const Packet& packet) {
    if (!checkAuthenticated(session, packet)) return;

    protocol::ChannelJoin request;
    if (!request.ParseFromArray(packet.data.data(), static_cast<int>(packet.data.size()))) {
        return;
    }

    protocol::ChannelJoinAck response;
    response.set_channel_id(request.channel_id());

    auto result = channel_manager_.joinChannel(
        request.channel_id(), session, request.password());

    if (result == Channel::JoinResult::Success) {
        response.set_success(true);

        // Add member list
        auto* channel = channel_manager_.getChannel(request.channel_id());
        if (channel) {
            for (const auto& member : channel->getMembers()) {
                auto* user_info = response.add_members();
                user_info->set_user_id(member->userId());
                user_info->set_nickname(member->nickname());
                user_info->set_profile_image(member->profileImage());
                user_info->set_frame_image(member->frameImage());
                user_info->set_extra_data(member->extraData());
            }

            // Add recent messages
            for (const auto& msg : channel->getRecentMessages(20)) {
                auto* msg_data = response.add_recent_messages();
                msg_data->set_message_id(msg.message_id);
                msg_data->set_channel_id(msg.channel_id);
                msg_data->set_sender_id(msg.sender_id);
                msg_data->set_sender_nickname(msg.sender_nickname);
                msg_data->set_sender_profile_image(msg.sender_profile_image);
                msg_data->set_sender_frame_image(msg.sender_frame_image);
                msg_data->set_sender_extra_data(msg.sender_extra_data);
                msg_data->set_content(msg.content);
                msg_data->set_timestamp(msg.timestamp);
            }
        }

        // Notify other members
        protocol::ChannelMemberUpdate update;
        update.set_channel_id(request.channel_id());
        update.set_update_type(protocol::ChannelMemberUpdate::JOIN);
        auto* user = update.mutable_user();
        user->set_user_id(session->userId());
        user->set_nickname(session->nickname());
        user->set_profile_image(session->profileImage());
        user->set_frame_image(session->frameImage());
        user->set_extra_data(session->extraData());

        Packet update_packet;
        update_packet.type = PacketType::ChannelMemberUpdate;
        std::string update_data;
        update.SerializeToString(&update_data);
        update_packet.data = std::vector<char>(update_data.begin(), update_data.end());

        channel_manager_.broadcastToChannel(request.channel_id(),
                                           update_packet,
                                           session->sessionId());

    } else {
        response.set_success(false);
        switch (result) {
            case Channel::JoinResult::ChannelFull:
                response.set_error_message("Channel is full");
                response.set_error_code(toInt(ErrorCode::ChannelFull));
                break;
            case Channel::JoinResult::AlreadyJoined:
                response.set_error_message("Already in channel");
                response.set_error_code(toInt(ErrorCode::AlreadyInChannel));
                break;
            case Channel::JoinResult::PasswordRequired:
                response.set_error_message("Password required");
                response.set_error_code(toInt(ErrorCode::ChannelPasswordRequired));
                break;
            case Channel::JoinResult::PasswordIncorrect:
                response.set_error_message("Incorrect password");
                response.set_error_code(toInt(ErrorCode::ChannelPasswordIncorrect));
                break;
            default:
                response.set_error_message("Channel not found");
                response.set_error_code(toInt(ErrorCode::ChannelNotFound));
                break;
        }
    }

    Packet resp_packet;
    resp_packet.type = PacketType::ChannelJoinAck;
    std::string data;
    response.SerializeToString(&data);
    resp_packet.data = std::vector<char>(data.begin(), data.end());

    session->send(resp_packet);
}

void MessageDispatcher::handleChannelLeaveRequest(SessionPtr session, const Packet& packet) {
    if (!checkAuthenticated(session, packet)) return;

    protocol::ChannelLeave request;
    if (!request.ParseFromArray(packet.data.data(), static_cast<int>(packet.data.size()))) {
        return;
    }

    handleChannelLeave(session, request.channel_id());
}

void MessageDispatcher::handleChannelLeave(SessionPtr session, const std::string& channel_id) {
    bool success = channel_manager_.leaveChannel(channel_id, session);

    // Send leave acknowledgment
    protocol::ChannelLeaveAck ack;
    ack.set_success(success);
    ack.set_channel_id(channel_id);
    if (!success) {
        ack.set_error_message("Not in channel");
    }

    Packet ack_packet;
    ack_packet.type = PacketType::ChannelLeaveAck;
    std::string data;
    ack.SerializeToString(&data);
    ack_packet.data = std::vector<char>(data.begin(), data.end());

    session->send(ack_packet);

    if (success) {
        // Notify other members
        protocol::ChannelMemberUpdate update;
        update.set_channel_id(channel_id);
        update.set_update_type(protocol::ChannelMemberUpdate::LEAVE);
        auto* user = update.mutable_user();
        user->set_user_id(session->userId());
        user->set_nickname(session->nickname());

        Packet update_packet;
        update_packet.type = PacketType::ChannelMemberUpdate;
        std::string update_data;
        update.SerializeToString(&update_data);
        update_packet.data = std::vector<char>(update_data.begin(), update_data.end());

        channel_manager_.broadcastToChannel(channel_id, update_packet);
    }
}

void MessageDispatcher::handleMessageSend(SessionPtr session, const Packet& packet) {
    if (!checkAuthenticated(session, packet)) return;

    protocol::MessageSend request;
    if (!request.ParseFromArray(packet.data.data(), static_cast<int>(packet.data.size()))) {
        return;
    }

    // Check if user is in channel
    if (!session->isInChannel(request.channel_id())) {
        LOG_WARN("Session {} tried to send message to channel {} without membership",
                session->sessionId(), request.channel_id());

        protocol::MessageAck ack;
        ack.set_success(false);
        ack.set_error_message("Not in channel");
        ack.set_error_code(toInt(ErrorCode::NotInChannel));

        Packet ack_packet;
        ack_packet.type = PacketType::MessageAck;
        std::string data;
        ack.SerializeToString(&data);
        ack_packet.data = std::vector<char>(data.begin(), data.end());

        session->send(ack_packet);
        return;
    }

    // Generate message ID and timestamp
    auto message_id = Snowflake::generate();
    auto timestamp = std::chrono::duration_cast<Milliseconds>(
        SystemClock::now().time_since_epoch()).count();

    // Create broadcast message
    protocol::MessageReceive broadcast_msg;
    auto* msg_data = broadcast_msg.mutable_message();
    msg_data->set_message_id(message_id);
    msg_data->set_channel_id(request.channel_id());
    msg_data->set_sender_id(session->userId());
    msg_data->set_sender_nickname(session->nickname());
    msg_data->set_sender_profile_image(session->profileImage());
    msg_data->set_sender_frame_image(session->frameImage());
    msg_data->set_sender_extra_data(session->extraData());
    msg_data->set_content(request.content());
    msg_data->set_timestamp(timestamp);
    msg_data->set_message_type(request.message_type());

    Packet broadcast_packet;
    broadcast_packet.type = PacketType::MessageReceive;
    std::string bcast_data;
    broadcast_msg.SerializeToString(&bcast_data);
    broadcast_packet.data = std::vector<char>(bcast_data.begin(), bcast_data.end());

    // Broadcast to channel (including sender)
    channel_manager_.broadcastToChannel(request.channel_id(), broadcast_packet);

    // Save to history
    auto* channel = channel_manager_.getChannel(request.channel_id());
    if (channel) {
        ChatMessage history_msg;
        history_msg.message_id = message_id;
        history_msg.channel_id = request.channel_id();
        history_msg.sender_id = session->userId();
        history_msg.sender_nickname = session->nickname();
        history_msg.sender_profile_image = session->profileImage();
        history_msg.sender_frame_image = session->frameImage();
        history_msg.sender_extra_data = session->extraData();
        history_msg.content = request.content();
        history_msg.timestamp = timestamp;
        history_msg.message_type = static_cast<int>(request.message_type());

        channel->addToHistory(history_msg);
    }

    // Send acknowledgment
    protocol::MessageAck ack;
    ack.set_success(true);
    ack.set_message_id(message_id);
    ack.set_client_message_id(request.client_message_id());

    Packet ack_packet;
    ack_packet.type = PacketType::MessageAck;
    std::string ack_data;
    ack.SerializeToString(&ack_data);
    ack_packet.data = std::vector<char>(ack_data.begin(), ack_data.end());

    session->send(ack_packet);
}

void MessageDispatcher::handleWhisperSend(SessionPtr session, const Packet& packet) {
    if (!checkAuthenticated(session, packet)) return;

    protocol::WhisperSend request;
    if (!request.ParseFromArray(packet.data.data(), static_cast<int>(packet.data.size()))) {
        return;
    }

    auto message_id = Snowflake::generate();
    auto timestamp = std::chrono::duration_cast<Milliseconds>(
        SystemClock::now().time_since_epoch()).count();

    // Check if trying to whisper self
    if (request.target_user_id() == session->userId()) {
        protocol::WhisperAck ack;
        ack.set_success(false);
        ack.set_error_message("Cannot whisper to yourself");
        ack.set_error_code(toInt(ErrorCode::CannotWhisperSelf));

        Packet ack_packet;
        ack_packet.type = PacketType::WhisperAck;
        std::string data;
        ack.SerializeToString(&data);
        ack_packet.data = std::vector<char>(data.begin(), data.end());

        session->send(ack_packet);
        return;
    }

    protocol::WhisperAck ack;
    ack.set_message_id(message_id);
    bool whisper_sent = false;

    // Try local delivery first
    auto target_session = session_manager_.getByUserId(request.target_user_id());
    if (target_session && target_session->isConnected()) {
        // Send whisper to local target
        protocol::WhisperReceive whisper;
        whisper.set_message_id(message_id);
        whisper.set_sender_id(session->userId());
        whisper.set_sender_nickname(session->nickname());
        whisper.set_sender_profile_image(session->profileImage());
        whisper.set_sender_frame_image(session->frameImage());
        whisper.set_sender_extra_data(session->extraData());
        whisper.set_content(request.content());
        whisper.set_timestamp(timestamp);

        Packet whisper_packet;
        whisper_packet.type = PacketType::WhisperReceive;
        std::string whisper_data;
        whisper.SerializeToString(&whisper_data);
        whisper_packet.data = std::vector<char>(whisper_data.begin(), whisper_data.end());

        target_session->send(whisper_packet);
        whisper_sent = true;

        LOG_DEBUG("Whisper from {} to {} delivered locally",
                  session->userId(), request.target_user_id());
    }
#ifdef ENABLE_REDIS
    else if (session_registry_ && pubsub_ && session_registry_->isAvailable()) {
        // Try cross-server delivery via Redis
        auto target_info = session_registry_->getSession(request.target_user_id());

        if (target_info && target_info->isValid()) {
            // User is online on another server, route via Pub/Sub
            WhisperPayload payload;
            payload.message_id = message_id;
            payload.content = request.content();
            payload.sender_nickname = session->nickname();
            payload.sender_profile_image = session->profileImage();
            payload.sender_frame_image = session->frameImage();

            PubSubMessage msg;
            msg.type = PubSubMessageType::Whisper;
            msg.origin_server_id = pubsub_->serverId();
            msg.sender_user_id = session->userId();
            msg.sender_session_id = session->sessionId();
            msg.target_user_id = request.target_user_id();
            msg.payload = payload.serialize();

            std::string whisper_channel = "chat:whisper:" + request.target_user_id();
            if (pubsub_->publish(whisper_channel, msg)) {
                whisper_sent = true;
                LOG_DEBUG("Whisper from {} to {} routed to server {} via Pub/Sub",
                          session->userId(), request.target_user_id(), target_info->server_id);
            } else {
                LOG_WARN("Failed to publish whisper to {} via Pub/Sub",
                         request.target_user_id());
            }
        } else {
            LOG_DEBUG("Target user {} not found in global registry",
                      request.target_user_id());
        }
    }
#endif

    if (whisper_sent) {
        ack.set_success(true);
    } else {
        ack.set_success(false);
        ack.set_error_message("User is offline");
        ack.set_error_code(toInt(ErrorCode::UserOffline));
    }

    Packet ack_packet;
    ack_packet.type = PacketType::WhisperAck;
    std::string ack_data;
    ack.SerializeToString(&ack_data);
    ack_packet.data = std::vector<char>(ack_data.begin(), ack_data.end());

    session->send(ack_packet);
}

void MessageDispatcher::handleProfileUpdate(SessionPtr session, const Packet& packet) {
    if (!checkAuthenticated(session, packet)) return;

    protocol::ProfileUpdateRequest request;
    if (!request.ParseFromArray(packet.data.data(), static_cast<int>(packet.data.size()))) {
        sendError(session, toInt(ErrorCode::InvalidPacket), "Invalid profile update request");
        return;
    }

    // Update session profile
    const std::string* nickname_ptr = request.has_nickname() ? &request.nickname() : nullptr;
    const std::string* profile_ptr = request.has_profile_image() ? &request.profile_image() : nullptr;
    const std::string* frame_ptr = request.has_frame_image() ? &request.frame_image() : nullptr;
    const std::string* extra_ptr = request.has_extra_data() ? &request.extra_data() : nullptr;

    session->updateProfile(nickname_ptr, profile_ptr, frame_ptr, extra_ptr);

    LOG_INFO("Session {} updated profile: nickname={}, profile={}, frame={}",
             session->sessionId(), session->nickname(),
             session->profileImage(), session->frameImage());

    // Send response to requester
    protocol::ProfileUpdateResponse response;
    response.set_success(true);
    auto* updated_profile = response.mutable_updated_profile();
    updated_profile->set_user_id(session->userId());
    updated_profile->set_nickname(session->nickname());
    updated_profile->set_profile_image(session->profileImage());
    updated_profile->set_frame_image(session->frameImage());
    updated_profile->set_extra_data(session->extraData());

    Packet resp_packet;
    resp_packet.type = PacketType::ProfileUpdateResponse;
    std::string resp_data;
    response.SerializeToString(&resp_data);
    resp_packet.data = std::vector<char>(resp_data.begin(), resp_data.end());

    session->send(resp_packet);

    // Broadcast profile change to all channels the user is in
    protocol::ProfileChanged change_notify;
    change_notify.set_user_id(session->userId());
    auto* new_profile = change_notify.mutable_new_profile();
    new_profile->set_user_id(session->userId());
    new_profile->set_nickname(session->nickname());
    new_profile->set_profile_image(session->profileImage());
    new_profile->set_frame_image(session->frameImage());
    new_profile->set_extra_data(session->extraData());

    Packet notify_packet;
    notify_packet.type = PacketType::ProfileChanged;
    std::string notify_data;
    change_notify.SerializeToString(&notify_data);
    notify_packet.data = std::vector<char>(notify_data.begin(), notify_data.end());

    // Broadcast to all channels the user is in
    for (const auto& channel_id : session->getJoinedChannels()) {
        channel_manager_.broadcastToChannel(channel_id, notify_packet, session->sessionId());
    }
}

void MessageDispatcher::handleChannelAutoAssign(SessionPtr session, const Packet& packet) {
    if (!checkAuthenticated(session, packet)) return;

    protocol::ChannelAutoAssign request;
    if (!request.ParseFromArray(packet.data.data(), static_cast<int>(packet.data.size()))) {
        sendError(session, toInt(ErrorCode::InvalidPacket), "Invalid auto-assign request");
        return;
    }

    protocol::ChannelAutoAssignAck response;

    // channel_type이 "world"인 경우 WorldChannelManager 사용
    std::string channel_type = request.channel_type();
    if (channel_type.empty()) {
        channel_type = "world";  // 기본값
    }

    std::string assigned_channel_id;

    if (channel_type == "world") {
        assigned_channel_id = world_channel_manager_.selectBestChannel();
    } else {
        // 다른 타입은 그대로 채널 ID로 사용 (trade, help 등)
        assigned_channel_id = channel_type;
    }

    if (assigned_channel_id.empty()) {
        response.set_success(false);
        response.set_error_message("No available channel");
        response.set_error_code(toInt(ErrorCode::ChannelNotFound));
    } else {
        // 채널 가입 시도
        auto result = channel_manager_.joinChannel(assigned_channel_id, session);

        if (result == Channel::JoinResult::Success) {
            response.set_success(true);
            response.set_assigned_channel_id(assigned_channel_id);

            // 멤버 목록 및 최근 메시지 추가
            auto* channel = channel_manager_.getChannel(assigned_channel_id);
            if (channel) {
                for (const auto& member : channel->getMembers()) {
                    auto* user_info = response.add_members();
                    user_info->set_user_id(member->userId());
                    user_info->set_nickname(member->nickname());
                    user_info->set_profile_image(member->profileImage());
                    user_info->set_frame_image(member->frameImage());
                    user_info->set_extra_data(member->extraData());
                }

                for (const auto& msg : channel->getRecentMessages(20)) {
                    auto* msg_data = response.add_recent_messages();
                    msg_data->set_message_id(msg.message_id);
                    msg_data->set_channel_id(msg.channel_id);
                    msg_data->set_sender_id(msg.sender_id);
                    msg_data->set_sender_nickname(msg.sender_nickname);
                    msg_data->set_sender_profile_image(msg.sender_profile_image);
                    msg_data->set_sender_frame_image(msg.sender_frame_image);
                    msg_data->set_sender_extra_data(msg.sender_extra_data);
                    msg_data->set_content(msg.content);
                    msg_data->set_timestamp(msg.timestamp);
                }
            }

            // 다른 멤버에게 입장 알림
            protocol::ChannelMemberUpdate update;
            update.set_channel_id(assigned_channel_id);
            update.set_update_type(protocol::ChannelMemberUpdate::JOIN);
            auto* user = update.mutable_user();
            user->set_user_id(session->userId());
            user->set_nickname(session->nickname());
            user->set_profile_image(session->profileImage());
            user->set_frame_image(session->frameImage());
            user->set_extra_data(session->extraData());

            Packet update_packet;
            update_packet.type = PacketType::ChannelMemberUpdate;
            std::string update_data;
            update.SerializeToString(&update_data);
            update_packet.data = std::vector<char>(update_data.begin(), update_data.end());

            channel_manager_.broadcastToChannel(assigned_channel_id,
                                               update_packet,
                                               session->sessionId());

            LOG_INFO("Session {} auto-assigned to channel '{}'",
                    session->sessionId(), assigned_channel_id);

        } else {
            response.set_success(false);
            response.set_assigned_channel_id(assigned_channel_id);
            switch (result) {
                case Channel::JoinResult::ChannelFull:
                    response.set_error_message("Channel is full");
                    response.set_error_code(toInt(ErrorCode::ChannelFull));
                    break;
                case Channel::JoinResult::AlreadyJoined:
                    // 이미 가입되어 있으면 성공으로 처리
                    response.set_success(true);
                    break;
                default:
                    response.set_error_message("Failed to join channel");
                    response.set_error_code(toInt(ErrorCode::ChannelNotFound));
                    break;
            }
        }
    }

    Packet resp_packet;
    resp_packet.type = PacketType::ChannelAutoAssignAck;
    std::string data;
    response.SerializeToString(&data);
    resp_packet.data = std::vector<char>(data.begin(), data.end());

    session->send(resp_packet);
}

void MessageDispatcher::handleAnnouncementSend(SessionPtr session, const Packet& packet) {
    // Note: AnnouncementSend doesn't require regular user authentication
    // It uses admin_token for authorization instead

    protocol::AnnouncementSend request;
    if (!request.ParseFromArray(packet.data.data(), static_cast<int>(packet.data.size()))) {
        LOG_WARN("Failed to parse AnnouncementSend from session {}", session->sessionId());
        sendError(session, toInt(ErrorCode::InvalidPacket), "Invalid announcement request");
        return;
    }

    // Check if announcement service is available
    if (!announcement_service_) {
        LOG_ERROR("AnnouncementService not available");
        sendError(session, toInt(ErrorCode::InternalError), "Announcement service unavailable");
        return;
    }

    // Process the announcement
    int delivered_count = announcement_service_->processAnnouncement(
        request.announcement_id(),
        request.content(),
        static_cast<int>(request.type()),
        request.sender_name(),
        request.duration_seconds(),
        request.target_channel(),
        request.admin_token(),
        request.extra_data()
    );

    // Send acknowledgment
    protocol::AnnouncementAck ack;
    ack.set_announcement_id(request.announcement_id());

    if (delivered_count >= 0) {
        ack.set_success(true);
        ack.set_delivered_count(delivered_count);
        LOG_INFO("Announcement {} processed successfully, delivered to {} local users",
                 request.announcement_id(), delivered_count);
    } else {
        ack.set_success(false);
        ack.set_error_message("Invalid admin token");
        ack.set_error_code(toInt(ErrorCode::AuthRequired));
        LOG_WARN("Announcement {} rejected: invalid admin token", request.announcement_id());
    }

    Packet ack_packet;
    ack_packet.type = PacketType::AnnouncementAck;
    std::string ack_data;
    ack.SerializeToString(&ack_data);
    ack_packet.data = std::vector<char>(ack_data.begin(), ack_data.end());

    session->send(ack_packet);
}

void MessageDispatcher::handleUserActionNotificationSend(SessionPtr session, const Packet& packet) {
    // Note: UserActionNotificationSend doesn't require regular user authentication
    // It uses admin_token for authorization instead

    protocol::UserActionNotificationSend request;
    if (!request.ParseFromArray(packet.data.data(), static_cast<int>(packet.data.size()))) {
        LOG_WARN("Failed to parse UserActionNotificationSend from session {}", session->sessionId());
        sendError(session, toInt(ErrorCode::InvalidPacket), "Invalid user action notification request");
        return;
    }

    // Check if user action service is available
    if (!user_action_service_) {
        LOG_ERROR("UserActionService not available");
        sendError(session, toInt(ErrorCode::InternalError), "User action service unavailable");
        return;
    }

    // Process the notification
    int delivered_count = user_action_service_->processNotification(
        request.notification_id(),
        static_cast<int>(request.action_type()),
        request.actor_user_id(),
        request.actor_nickname(),
        request.actor_profile_image(),
        request.actor_frame_image(),
        request.title(),
        request.content(),
        request.icon_id(),
        request.extra_data(),
        request.target_channel(),
        request.exclude_actor(),
        request.admin_token()
    );

    // Send acknowledgment
    protocol::UserActionNotificationAck ack;
    ack.set_notification_id(request.notification_id());

    if (delivered_count >= 0) {
        ack.set_success(true);
        ack.set_delivered_count(delivered_count);
        LOG_INFO("UserAction notification {} processed successfully, delivered to {} local users",
                 request.notification_id(), delivered_count);
    } else {
        ack.set_success(false);
        ack.set_error_message("Invalid admin token");
        ack.set_error_code(toInt(ErrorCode::AuthRequired));
        LOG_WARN("UserAction notification {} rejected: invalid admin token", request.notification_id());
    }

    Packet ack_packet;
    ack_packet.type = PacketType::UserActionNotificationAck;
    std::string ack_data;
    ack.SerializeToString(&ack_data);
    ack_packet.data = std::vector<char>(ack_data.begin(), ack_data.end());

    session->send(ack_packet);
}

// ==========================================
// Helper Functions
// ==========================================

void MessageDispatcher::sendError(SessionPtr session, int error_code, const std::string& message) {
    protocol::Error error;
    error.set_error_code(error_code);
    error.set_error_message(message);

    Packet error_packet;
    error_packet.type = PacketType::Error;
    std::string data;
    error.SerializeToString(&data);
    error_packet.data = std::vector<char>(data.begin(), data.end());

    session->send(error_packet);
}

bool MessageDispatcher::checkAuthenticated(SessionPtr session, const Packet& packet) {
    if (!session->isAuthenticated()) {
        LOG_WARN("Session {} sent {} without authentication",
                session->sessionId(), getPacketTypeName(packet.type));
        sendError(session, toInt(ErrorCode::AuthRequired), "Authentication required");
        return false;
    }
    return true;
}

int MessageDispatcher::getPacketCost(PacketType type) const {
    switch (type) {
        // Free packets (no rate limiting)
        case PacketType::Heartbeat:
        case PacketType::HeartbeatAck:
            return 0;

        // Higher cost for channel operations
        case PacketType::ChannelJoin:
        case PacketType::ChannelCreate:
        case PacketType::ChannelLeave:
            return 2;

        // Standard cost for other packets
        default:
            return 1;
    }
}

} // namespace chat
