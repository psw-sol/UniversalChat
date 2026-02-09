using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace UniversalChat.Core
{
    /// <summary>
    /// 채팅 서비스 추상화 인터페이스
    /// ChatUIManager, VirtualizedChatPanel 등 모든 UI 컴포넌트가 이 인터페이스에 의존합니다.
    ///
    /// 사용 레벨:
    /// - Level 1 (Zero Code): ChatManager가 IChatService를 구현 → Inspector만으로 사용
    /// - Level 2 (Minimal Code): ChatServiceBase를 상속 → 채널 타입 분류만 구현
    /// - Level 3 (Full Control): IChatService를 직접 구현 → 완전한 커스터마이징
    /// </summary>
    public interface IChatService
    {
        #region State

        bool IsConnected { get; }
        bool IsAuthenticated { get; }
        string UserId { get; }
        string CurrentChannelId { get; }
        IReadOnlyList<string> JoinedChannels { get; }

        #endregion

        #region Connection

        Task<bool> ConnectAsync(string host, int port, int timeoutMs = 5000);
        void Disconnect();

        #endregion

        #region Authentication

        Task<bool> LoginAsync(
            string userId,
            string authToken = null,
            string nickname = null,
            string profileImage = null,
            string frameImage = null,
            string extraData = null);

        #endregion

        #region Channel

        Task JoinChannelAsync(string channelId, string password = null);
        Task LeaveChannelAsync(string channelId);
        Task RequestAutoAssignChannelAsync(string channelType = "world");

        #endregion

        #region Messaging

        Task SendMessageAsync(string channelId, string content, int messageType = 0);
        Task SendWhisperAsync(string targetUserId, string content);

        #endregion

        #region Profile

        Task UpdateProfileAsync(
            string nickname = null,
            string profileImage = null,
            string frameImage = null,
            string extraData = null);

        #endregion

        #region Events - Connection

        event Action OnConnected;
        event Action<string> OnDisconnected;
        event Action<string> OnError;

        #endregion

        #region Events - Auth

        event Action<bool, string> OnAuthenticated;

        #endregion

        #region Events - Channel

        event Action<string, string> OnChannelJoined;
        event Action<ChannelJoinResult> OnChannelJoinedWithHistory;
        event Action<string> OnChannelLeft;
        event Action<List<ChannelInfo>> OnChannelListUpdated;
        event Action<string, List<UserInfo>> OnUserListUpdated;

        #endregion

        #region Events - Message

        event Action<ChannelMessage> OnMessageReceived;
        event Action<WhisperMessage> OnWhisperReceived;

        #endregion

        #region Events - Notification

        event Action<AnnouncementMessage> OnAnnouncementReceived;
        event Action<UserActionNotificationMessage> OnUserActionNotificationReceived;

        #endregion

        #region Events - State

        event Action OnChatReady;

        #endregion
    }
}
