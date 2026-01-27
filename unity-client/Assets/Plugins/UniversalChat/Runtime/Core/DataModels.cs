using System;
using System.Collections.Generic;
using Chat.Protocol;

namespace UniversalChat.Core
{
    /// <summary>
    /// 채널 정보 (proto: ChannelInfo)
    /// </summary>
    [Serializable]
    public class ChannelInfo
    {
        public string ChannelId { get; set; }
        public string ChannelName { get; set; }
        public int CurrentUsers { get; set; }
        public int MaxUsers { get; set; }
        public bool IsSystem { get; set; }
        public bool HasPassword { get; set; } // UI 호환용 (기본값 false)

        public ChannelInfo() { }

        public ChannelInfo(Chat.Protocol.ChannelInfo proto)
        {
            ChannelId = proto.ChannelId;
            ChannelName = proto.ChannelName;
            CurrentUsers = proto.MemberCount;
            MaxUsers = proto.MaxMembers;
            IsSystem = proto.IsSystem;
            HasPassword = false; // proto에 없음, UI 호환용 기본값
        }
    }

    /// <summary>
    /// 유저 정보 (proto: UserInfo)
    /// </summary>
    [Serializable]
    public class UserInfo
    {
        public string UserId { get; set; }
        public string Nickname { get; set; }
        public string ProfileImage { get; set; }
        public string FrameImage { get; set; }
        public string ExtraData { get; set; }
        public long JoinedAt { get; set; }

        public UserInfo() { }

        public UserInfo(Chat.Protocol.UserInfo proto)
        {
            UserId = proto.UserId;
            Nickname = proto.Nickname;
            ProfileImage = proto.ProfileImage;
            FrameImage = proto.FrameImage;
            ExtraData = proto.ExtraData;
            JoinedAt = proto.JoinedAt;
        }
    }

    /// <summary>
    /// 채널 메시지 (proto: ChatMessageData via MessageReceive)
    /// </summary>
    [Serializable]
    public class ChannelMessage
    {
        public string MessageId { get; set; }
        public string ChannelId { get; set; }
        public string SenderId { get; set; }
        public string SenderNickname { get; set; }
        public string SenderProfileImage { get; set; }
        public string SenderFrameImage { get; set; }
        public string SenderExtraData { get; set; }
        public string Content { get; set; }
        public long Timestamp { get; set; }
        public int MessageType { get; set; } // 0: Normal, 1: System, 2: Whisper
        public DateTime DateTime => DateTimeOffset.FromUnixTimeMilliseconds(Timestamp).LocalDateTime;

        public ChannelMessage() { }

        public ChannelMessage(MessageReceive proto)
        {
            if (proto.Message != null)
            {
                MessageId = proto.Message.MessageId;
                ChannelId = proto.Message.ChannelId;
                SenderId = proto.Message.SenderId;
                SenderNickname = proto.Message.SenderNickname;
                SenderProfileImage = proto.Message.SenderProfileImage;
                SenderFrameImage = proto.Message.SenderFrameImage;
                SenderExtraData = proto.Message.SenderExtraData;
                Content = proto.Message.Content;
                Timestamp = proto.Message.Timestamp;
                MessageType = 0; // 기본값: Normal
            }
        }

        public ChannelMessage(ChatMessageData data)
        {
            MessageId = data.MessageId;
            ChannelId = data.ChannelId;
            SenderId = data.SenderId;
            SenderNickname = data.SenderNickname;
            SenderProfileImage = data.SenderProfileImage;
            SenderFrameImage = data.SenderFrameImage;
            SenderExtraData = data.SenderExtraData;
            Content = data.Content;
            Timestamp = data.Timestamp;
            MessageType = 0; // 기본값: Normal
        }
    }

    /// <summary>
    /// 인증 응답 (proto: AuthResponse)
    /// Fields: Success, SessionId, ErrorMessage, ErrorCode, ServerTime, HeartbeatInterval
    /// </summary>
    [Serializable]
    public class AuthResponseData
    {
        public bool Success { get; set; }
        public string SessionId { get; set; }
        public string ErrorMessage { get; set; }
        public int ErrorCode { get; set; }
        public long ServerTime { get; set; }
        public int HeartbeatInterval { get; set; }

        public AuthResponseData() { }

        public AuthResponseData(AuthResponse proto)
        {
            Success = proto.Success;
            SessionId = proto.SessionId;
            ErrorMessage = proto.ErrorMessage;
            ErrorCode = proto.ErrorCode;
            ServerTime = proto.ServerTime;
            HeartbeatInterval = proto.HeartbeatInterval;
        }
    }

    /// <summary>
    /// 채널 입장 응답 (proto: ChannelJoinAck)
    /// Fields: Success, ChannelId, ErrorMessage, ErrorCode, Members, RecentMessages
    /// </summary>
    [Serializable]
    public class ChannelJoinResponse
    {
        public bool Success { get; set; }
        public string ChannelId { get; set; }
        public string ErrorMessage { get; set; }
        public int ErrorCode { get; set; }
        public List<UserInfo> Members { get; set; } = new List<UserInfo>();
        public List<ChannelMessage> RecentMessages { get; set; } = new List<ChannelMessage>();

        public ChannelJoinResponse() { }

        public ChannelJoinResponse(ChannelJoinAck proto)
        {
            Success = proto.Success;
            ChannelId = proto.ChannelId;
            ErrorMessage = proto.ErrorMessage;
            ErrorCode = proto.ErrorCode;

            if (proto.Members != null)
            {
                foreach (var member in proto.Members)
                {
                    Members.Add(new UserInfo(member));
                }
            }

            if (proto.RecentMessages != null)
            {
                foreach (var msg in proto.RecentMessages)
                {
                    RecentMessages.Add(new ChannelMessage(msg));
                }
            }
        }
    }

    /// <summary>
    /// 채널 퇴장 응답 (proto: ChannelLeaveAck)
    /// Fields: Success, ChannelId, ErrorMessage
    /// </summary>
    [Serializable]
    public class ChannelLeaveResponse
    {
        public bool Success { get; set; }
        public string ChannelId { get; set; }
        public string ErrorMessage { get; set; }

        public ChannelLeaveResponse() { }

        public ChannelLeaveResponse(ChannelLeaveAck proto)
        {
            Success = proto.Success;
            ChannelId = proto.ChannelId;
            ErrorMessage = proto.ErrorMessage;
        }
    }

    /// <summary>
    /// 채널 목록 응답 (proto: ChannelListResponse)
    /// </summary>
    [Serializable]
    public class ChannelListResponse
    {
        public List<ChannelInfo> Channels { get; set; } = new List<ChannelInfo>();

        public ChannelListResponse() { }

        public ChannelListResponse(Chat.Protocol.ChannelListResponse proto)
        {
            if (proto.Channels != null)
            {
                foreach (var channel in proto.Channels)
                {
                    Channels.Add(new ChannelInfo(channel));
                }
            }
        }
    }

    /// <summary>
    /// 유저 목록 응답 (채널 입장 시 Members에서 추출)
    /// </summary>
    [Serializable]
    public class UserListResponse
    {
        public string ChannelId { get; set; }
        public List<UserInfo> Users { get; set; } = new List<UserInfo>();

        public UserListResponse() { }

        public UserListResponse(string channelId, ChannelJoinAck proto)
        {
            ChannelId = channelId;

            if (proto.Members != null)
            {
                foreach (var member in proto.Members)
                {
                    Users.Add(new UserInfo(member));
                }
            }
        }
    }

    /// <summary>
    /// 귓속말 메시지 (proto: WhisperReceive)
    /// </summary>
    [Serializable]
    public class WhisperMessage
    {
        public string MessageId { get; set; }
        public string SenderId { get; set; }
        public string SenderNickname { get; set; }
        public string SenderProfileImage { get; set; }
        public string SenderFrameImage { get; set; }
        public string SenderExtraData { get; set; }
        public string Content { get; set; }
        public long Timestamp { get; set; }
        public DateTime DateTime => DateTimeOffset.FromUnixTimeMilliseconds(Timestamp).LocalDateTime;

        public WhisperMessage() { }

        public WhisperMessage(WhisperReceive proto)
        {
            MessageId = proto.MessageId;
            SenderId = proto.SenderId;
            SenderNickname = proto.SenderNickname;
            SenderProfileImage = proto.SenderProfileImage;
            SenderFrameImage = proto.SenderFrameImage;
            SenderExtraData = proto.SenderExtraData;
            Content = proto.Content;
            Timestamp = proto.Timestamp;
        }
    }

    /// <summary>
    /// 프로필 업데이트 응답 (proto: ProfileUpdateResponse)
    /// </summary>
    [Serializable]
    public class ProfileUpdateResponseData
    {
        public bool Success { get; set; }
        public string ErrorMessage { get; set; }
        public int ErrorCode { get; set; }
        public UserInfo UpdatedProfile { get; set; }

        public ProfileUpdateResponseData() { }

        public ProfileUpdateResponseData(ProfileUpdateResponse proto)
        {
            Success = proto.Success;
            ErrorMessage = proto.ErrorMessage;
            ErrorCode = proto.ErrorCode;
            if (proto.UpdatedProfile != null)
            {
                UpdatedProfile = new UserInfo(proto.UpdatedProfile);
            }
        }
    }

    /// <summary>
    /// 프로필 변경 알림 (proto: ProfileChanged)
    /// </summary>
    [Serializable]
    public class ProfileChangedData
    {
        public string UserId { get; set; }
        public UserInfo NewProfile { get; set; }

        public ProfileChangedData() { }

        public ProfileChangedData(ProfileChanged proto)
        {
            UserId = proto.UserId;
            if (proto.NewProfile != null)
            {
                NewProfile = new UserInfo(proto.NewProfile);
            }
        }
    }
}
