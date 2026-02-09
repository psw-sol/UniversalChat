using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using UnityEngine;
using Chat.Protocol;

namespace UniversalChat.Core
{
    /// <summary>
    /// 게임 프로젝트를 위한 채팅 서비스 기본 구현
    ///
    /// 내장 기능 (재구현 불필요):
    /// - ChatClient 이벤트 브릿징 (Proto → Domain 자동 변환)
    /// - 자동 재연결 (설정 가능)
    /// - 인증 정보 캐싱 (재연결 시 자동 재로그인)
    /// - 채널 상태 추적 (입장/퇴장, 현재 채널)
    /// - 메시지 히스토리 캐싱 (채널별)
    /// - 멤버 목록 관리 (채널별)
    /// - 채널 타입별 자동 분류 및 이벤트 분배
    ///
    /// 게임이 구현해야 하는 것:
    /// - ClassifyChannel(channelId) → 채널 타입 분류 로직
    ///
    /// 사용 예:
    /// <code>
    /// public enum MyChannelType { World, Guild, Party, Custom }
    ///
    /// public class MyChatService : ChatServiceBase&lt;MyChannelType&gt;
    /// {
    ///     protected override MyChannelType ClassifyChannel(string channelId)
    ///     {
    ///         if (channelId.StartsWith("world")) return MyChannelType.World;
    ///         if (channelId.StartsWith("guild_")) return MyChannelType.Guild;
    ///         if (channelId.StartsWith("party_")) return MyChannelType.Party;
    ///         return MyChannelType.Custom;
    ///     }
    /// }
    /// </code>
    /// </summary>
    /// <typeparam name="TChannelType">게임의 채널 타입 Enum</typeparam>
    public abstract class ChatServiceBase<TChannelType> : IChatService, IDisposable
        where TChannelType : struct, Enum
    {
        #region ChatClient

        protected readonly ChatClient Client;

        #endregion

        #region IChatService State

        public bool IsConnected => Client.IsConnected;
        public bool IsAuthenticated => Client.IsAuthenticated;
        public string UserId { get; protected set; }
        public string CurrentChannelId { get; protected set; }
        public IReadOnlyList<string> JoinedChannels => _joinedChannels.AsReadOnly();

        #endregion

        #region Reconnection Settings

        public bool AutoReconnect { get; set; } = true;
        public int MaxReconnectAttempts { get; set; } = 3;
        public float ReconnectDelay { get; set; } = 5f;

        private int _reconnectAttempts;
        private bool _isReconnecting;

        #endregion

        #region History & Members

        public int MaxHistoryPerChannel { get; set; } = 100;

        private readonly Dictionary<string, List<ChannelMessage>> _messageHistory = new();
        private readonly Dictionary<string, List<UserInfo>> _channelMembers = new();

        #endregion

        #region Channel Type Tracking

        /// <summary>
        /// 채널 타입별 현재 활성 채널 ID
        /// </summary>
        public IReadOnlyDictionary<TChannelType, string> CurrentChannelsByType => _currentChannelsByType;
        private readonly Dictionary<TChannelType, string> _currentChannelsByType = new();

        #endregion

        #region Internal State

        private readonly List<ChannelInfo> _channelList = new();
        private readonly List<string> _joinedChannels = new();

        // 재연결용 서버 정보 캐싱
        private string _lastHost;
        private int _lastPort;

        // 재연결용 인증 정보 캐싱
        private string _lastUserId;
        private string _lastAuthToken;
        private string _lastNickname;
        private string _lastProfileImage;
        private string _lastFrameImage;
        private string _lastExtraData;

        // 로깅
        protected bool EnableLogging { get; set; } = true;
        private string LogTag => $"[{GetType().Name}]";

        #endregion

        #region IChatService Events

        public event Action OnConnected;
        public event Action<string> OnDisconnected;
        public event Action<string> OnError;
        public event Action<bool, string> OnAuthenticated;
        public event Action<string, string> OnChannelJoined;
        public event Action<ChannelJoinResult> OnChannelJoinedWithHistory;
        public event Action<string> OnChannelLeft;
        public event Action<List<ChannelInfo>> OnChannelListUpdated;
        public event Action<string, List<UserInfo>> OnUserListUpdated;
        public event Action<ChannelMessage> OnMessageReceived;
        public event Action<WhisperMessage> OnWhisperReceived;
        public event Action<AnnouncementMessage> OnAnnouncementReceived;
        public event Action<UserActionNotificationMessage> OnUserActionNotificationReceived;
        public event Action OnChatReady;

        #endregion

        #region Typed Channel Events

        /// <summary>
        /// 채널 타입별 메시지 수신 이벤트
        /// </summary>
        public event Action<TChannelType, ChannelMessage> OnTypedMessageReceived;

        /// <summary>
        /// 채널 타입별 입장 이벤트 (타입, 채널ID, 입장 결과)
        /// </summary>
        public event Action<TChannelType, string, ChannelJoinResult> OnTypedChannelJoined;

        /// <summary>
        /// 채널 타입별 퇴장 이벤트
        /// </summary>
        public event Action<TChannelType, string> OnTypedChannelLeft;

        /// <summary>
        /// 채널 타입별 멤버 업데이트 이벤트
        /// </summary>
        public event Action<TChannelType, string, List<UserInfo>> OnTypedMembersUpdated;

        #endregion

        #region Abstract Methods

        /// <summary>
        /// 채널 ID로 채널 타입을 결정합니다.
        /// 게임의 채널 명명 규칙에 맞게 구현하세요.
        /// </summary>
        protected abstract TChannelType ClassifyChannel(string channelId);

        #endregion

        #region Virtual Extension Points

        /// <summary>채널 입장 성공 후 호출. 게임 특화 처리 가능.</summary>
        protected virtual void OnChannelJoinedInternal(TChannelType type, string channelId, ChannelJoinResult result) { }

        /// <summary>메시지 수신 후 호출. 게임 특화 처리 가능.</summary>
        protected virtual void OnMessageReceivedInternal(TChannelType type, ChannelMessage message) { }

        /// <summary>연결 끊김 후 호출. 게임 특화 처리 가능.</summary>
        protected virtual void OnDisconnectedInternal(string reason) { }

        /// <summary>재연결 성공 후 호출. 게임 특화 처리 가능.</summary>
        protected virtual void OnReconnectedInternal() { }

        /// <summary>귓속말 수신 후 호출. 게임 특화 처리 가능.</summary>
        protected virtual void OnWhisperReceivedInternal(WhisperMessage whisper) { }

        /// <summary>인증 완료 후 호출. 게임 특화 처리 가능.</summary>
        protected virtual void OnAuthenticatedInternal(bool success, string errorMessage) { }

        #endregion

        #region Constructor & Dispose

        protected ChatServiceBase(float heartbeatInterval = 30f)
        {
            Client = new ChatClient(heartbeatInterval);
            SubscribeToClientEvents();
        }

        public void Dispose()
        {
            UnsubscribeFromClientEvents();
            Client.Dispose();
        }

        #endregion

        #region IChatService Methods

        public async Task<bool> ConnectAsync(string host, int port, int timeoutMs = 5000)
        {
            _lastHost = host;
            _lastPort = port;

            if (IsConnected)
            {
                Log("Already connected");
                return true;
            }

            Log($"Connecting to {host}:{port}...");
            return await Client.ConnectAsync(host, port, timeoutMs);
        }

        public void Disconnect()
        {
            AutoReconnect = false;
            Client.Disconnect();
            CurrentChannelId = null;
            _joinedChannels.Clear();
        }

        public async Task<bool> LoginAsync(
            string userId,
            string authToken = null,
            string nickname = null,
            string profileImage = null,
            string frameImage = null,
            string extraData = null)
        {
            if (!IsConnected)
            {
                LogError("Not connected to server");
                return false;
            }

            // 재연결용 인증 정보 저장
            _lastUserId = userId;
            _lastAuthToken = authToken;
            _lastNickname = nickname;
            _lastProfileImage = profileImage;
            _lastFrameImage = frameImage;
            _lastExtraData = extraData;

            Log($"Logging in as {userId}...");
            return await Client.AuthenticateAsync(userId, authToken, nickname, profileImage, frameImage, extraData);
        }

        /// <summary>
        /// 연결 + 로그인을 한 번에 수행
        /// </summary>
        public async Task<bool> ConnectAndLoginAsync(
            string host, int port,
            string userId,
            string authToken = null,
            string nickname = null,
            string profileImage = null,
            string frameImage = null,
            string extraData = null)
        {
            bool connected = await ConnectAsync(host, port);
            if (!connected) return false;
            return await LoginAsync(userId, authToken, nickname, profileImage, frameImage, extraData);
        }

        public async Task JoinChannelAsync(string channelId, string password = null)
        {
            await Client.JoinChannelAsync(channelId, password);
        }

        public async Task LeaveChannelAsync(string channelId)
        {
            await Client.LeaveChannelAsync(channelId);
        }

        public async Task RequestAutoAssignChannelAsync(string channelType = "world")
        {
            if (!IsAuthenticated)
            {
                LogError("Not authenticated - cannot request auto-assign");
                return;
            }
            Log($"Requesting auto-assign to {channelType} channel...");
            await Client.RequestAutoAssignChannelAsync(channelType);
        }

        public async Task SendMessageAsync(string channelId, string content, int messageType = 0)
        {
            await Client.SendMessageAsync(channelId, content, (MessageType)messageType);
        }

        /// <summary>
        /// 현재 활성 채널에 메시지 전송 (편의 메서드)
        /// </summary>
        public async Task SendMessageToCurrentChannelAsync(string content)
        {
            if (string.IsNullOrEmpty(CurrentChannelId))
            {
                LogError("Not in any channel");
                return;
            }
            await SendMessageAsync(CurrentChannelId, content);
        }

        /// <summary>
        /// 특정 채널 타입의 활성 채널에 메시지 전송
        /// </summary>
        public async Task SendMessageToChannelTypeAsync(TChannelType channelType, string content)
        {
            if (_currentChannelsByType.TryGetValue(channelType, out var channelId))
            {
                await SendMessageAsync(channelId, content);
            }
            else
            {
                LogError($"Not in any {channelType} channel");
            }
        }

        public async Task SendWhisperAsync(string targetUserId, string content)
        {
            if (string.IsNullOrEmpty(targetUserId))
            {
                LogError("Target user ID is required for whisper");
                return;
            }
            await Client.SendWhisperAsync(targetUserId, content);
        }

        public async Task UpdateProfileAsync(
            string nickname = null,
            string profileImage = null,
            string frameImage = null,
            string extraData = null)
        {
            await Client.UpdateProfileAsync(nickname, profileImage, frameImage, extraData);
        }

        #endregion

        #region History & Members API

        /// <summary>
        /// 특정 채널의 메시지 히스토리 조회
        /// </summary>
        public List<ChannelMessage> GetMessageHistory(string channelId, int count = 50)
        {
            if (!_messageHistory.TryGetValue(channelId, out var history)) return new List<ChannelMessage>();
            return history.Skip(Math.Max(0, history.Count - count)).ToList();
        }

        /// <summary>
        /// 특정 채널 타입의 메시지 히스토리 조회
        /// </summary>
        public List<ChannelMessage> GetMessageHistory(TChannelType channelType, int count = 50)
        {
            if (_currentChannelsByType.TryGetValue(channelType, out var channelId))
                return GetMessageHistory(channelId, count);
            return new List<ChannelMessage>();
        }

        /// <summary>
        /// 특정 채널의 멤버 목록 조회
        /// </summary>
        public List<UserInfo> GetChannelMembers(string channelId)
        {
            return _channelMembers.TryGetValue(channelId, out var members)
                ? new List<UserInfo>(members)
                : new List<UserInfo>();
        }

        /// <summary>
        /// 특정 채널 타입에 현재 입장해 있는지 확인
        /// </summary>
        public bool IsInChannelType(TChannelType channelType)
        {
            return _currentChannelsByType.ContainsKey(channelType);
        }

        /// <summary>
        /// 특정 채널 타입의 현재 채널 ID 조회
        /// </summary>
        public string GetCurrentChannelId(TChannelType channelType)
        {
            return _currentChannelsByType.TryGetValue(channelType, out var id) ? id : null;
        }

        #endregion

        #region Event Subscription (ChatClient → ChatServiceBase)

        private void SubscribeToClientEvents()
        {
            Client.OnConnected += HandleClientConnected;
            Client.OnDisconnected += HandleClientDisconnected;
            Client.OnError += HandleClientError;
            Client.OnAuthenticated += HandleClientAuthenticated;
            Client.OnMessageReceived += HandleClientMessageReceived;
            Client.OnChannelJoined += HandleClientChannelJoined;
            Client.OnChannelLeft += HandleClientChannelLeft;
            Client.OnChannelListReceived += HandleClientChannelListReceived;
            Client.OnMemberUpdated += HandleClientMemberUpdated;
            Client.OnChannelAutoAssigned += HandleClientChannelAutoAssigned;
            Client.OnWhisperReceived += HandleClientWhisperReceived;
            Client.OnAnnouncementReceived += HandleClientAnnouncementReceived;
            Client.OnUserActionNotificationReceived += HandleClientUserActionNotificationReceived;
            Client.OnProfileUpdated += HandleClientProfileUpdated;
            Client.OnProfileChanged += HandleClientProfileChanged;
        }

        private void UnsubscribeFromClientEvents()
        {
            Client.OnConnected -= HandleClientConnected;
            Client.OnDisconnected -= HandleClientDisconnected;
            Client.OnError -= HandleClientError;
            Client.OnAuthenticated -= HandleClientAuthenticated;
            Client.OnMessageReceived -= HandleClientMessageReceived;
            Client.OnChannelJoined -= HandleClientChannelJoined;
            Client.OnChannelLeft -= HandleClientChannelLeft;
            Client.OnChannelListReceived -= HandleClientChannelListReceived;
            Client.OnMemberUpdated -= HandleClientMemberUpdated;
            Client.OnChannelAutoAssigned -= HandleClientChannelAutoAssigned;
            Client.OnWhisperReceived -= HandleClientWhisperReceived;
            Client.OnAnnouncementReceived -= HandleClientAnnouncementReceived;
            Client.OnUserActionNotificationReceived -= HandleClientUserActionNotificationReceived;
            Client.OnProfileUpdated -= HandleClientProfileUpdated;
            Client.OnProfileChanged -= HandleClientProfileChanged;
        }

        #endregion

        #region Event Handlers (Proto → Domain + Business Logic)

        private void HandleClientConnected()
        {
            Log("Connected to server");
            _reconnectAttempts = 0;
            _isReconnecting = false;
            OnConnected?.Invoke();
        }

        private async void HandleClientDisconnected(string reason)
        {
            Log($"Disconnected: {reason}");
            CurrentChannelId = null;

            OnDisconnected?.Invoke(reason);
            OnDisconnectedInternal(reason);

            if (AutoReconnect && !_isReconnecting)
            {
                await ReconnectAsync();
            }
        }

        private void HandleClientError(string error)
        {
            LogError(error);
            OnError?.Invoke(error);
        }

        private void HandleClientAuthenticated(AuthResponse response)
        {
            if (response.Success)
            {
                UserId = _lastUserId;
                Log($"Authenticated with session: {response.SessionId}");
            }
            else
            {
                LogError($"Authentication failed: {response.ErrorMessage}");
            }

            OnAuthenticated?.Invoke(response.Success, response.ErrorMessage ?? string.Empty);
            OnAuthenticatedInternal(response.Success, response.ErrorMessage ?? string.Empty);
        }

        private void HandleClientMessageReceived(MessageReceive proto)
        {
            var message = new ChannelMessage(proto);
            Log($"[{message.ChannelId}] {message.SenderNickname}: {message.Content}");

            // 히스토리 추가
            AddToHistory(message.ChannelId, message);

            // IChatService 이벤트
            OnMessageReceived?.Invoke(message);

            // 채널 타입별 이벤트
            var channelType = ClassifyChannel(message.ChannelId);
            OnTypedMessageReceived?.Invoke(channelType, message);
            OnMessageReceivedInternal(channelType, message);
        }

        private void HandleClientChannelJoined(ChannelJoinAck proto)
        {
            var result = new ChannelJoinResult(proto);
            ProcessChannelJoin(result);
        }

        private void HandleClientChannelAutoAssigned(ChannelAutoAssignAck proto)
        {
            var result = new ChannelJoinResult(proto);
            ProcessChannelJoin(result);
        }

        private void ProcessChannelJoin(ChannelJoinResult result)
        {
            if (result.Success)
            {
                Log($"Joined channel: {result.ChannelId} (auto={result.IsAutoAssign})");
                CurrentChannelId = result.ChannelId;

                if (!_joinedChannels.Contains(result.ChannelId))
                    _joinedChannels.Add(result.ChannelId);

                // 멤버 목록 캐싱
                _channelMembers[result.ChannelId] = result.Members;
                OnUserListUpdated?.Invoke(result.ChannelId, result.Members);

                // 히스토리 초기화 + RecentMessages 캐싱
                if (!_messageHistory.ContainsKey(result.ChannelId))
                    _messageHistory[result.ChannelId] = new List<ChannelMessage>();

                if (result.RecentMessages?.Count > 0)
                {
                    _messageHistory[result.ChannelId].Clear();
                    _messageHistory[result.ChannelId].AddRange(result.RecentMessages);
                }

                // 채널 타입별 추적
                var channelType = ClassifyChannel(result.ChannelId);
                _currentChannelsByType[channelType] = result.ChannelId;

                // IChatService 이벤트
                OnChannelJoined?.Invoke(result.ChannelId, result.ChannelId);
                OnChannelJoinedWithHistory?.Invoke(result);

                // 채널 타입별 이벤트
                OnTypedChannelJoined?.Invoke(channelType, result.ChannelId, result);
                OnChannelJoinedInternal(channelType, result.ChannelId, result);
            }
            else
            {
                LogError($"Failed to join channel: {result.ErrorMessage}");
                OnError?.Invoke(result.ErrorMessage ?? "Unknown error");
            }
        }

        private void HandleClientChannelLeft(ChannelLeaveAck proto)
        {
            var response = new ChannelLeaveResponse(proto);

            if (response.Success)
            {
                Log($"Left channel: {response.ChannelId}");
                _joinedChannels.Remove(response.ChannelId);
                _channelMembers.Remove(response.ChannelId);

                // 채널 타입별 추적 해제
                var channelType = ClassifyChannel(response.ChannelId);
                _currentChannelsByType.Remove(channelType);

                if (CurrentChannelId == response.ChannelId)
                    CurrentChannelId = _joinedChannels.Count > 0 ? _joinedChannels[0] : null;

                OnChannelLeft?.Invoke(response.ChannelId);
                OnTypedChannelLeft?.Invoke(channelType, response.ChannelId);
            }
        }

        private void HandleClientChannelListReceived(Chat.Protocol.ChannelListResponse proto)
        {
            var response = new ChannelListResponse(proto);
            _channelList.Clear();
            _channelList.AddRange(response.Channels);
            Log($"Received {_channelList.Count} channels");
            OnChannelListUpdated?.Invoke(_channelList);
        }

        private void HandleClientMemberUpdated(ChannelMemberUpdate update)
        {
            Log($"Member update in {update.ChannelId}: {update.User?.Nickname} ({update.UpdateType})");

            if (_channelMembers.TryGetValue(update.ChannelId, out var members))
            {
                var userInfo = new UserInfo(update.User);

                if (update.UpdateType == ChannelMemberUpdate.Types.UpdateType.Join)
                {
                    if (!members.Any(m => m.UserId == userInfo.UserId))
                        members.Add(userInfo);
                }
                else if (update.UpdateType == ChannelMemberUpdate.Types.UpdateType.Leave)
                {
                    members.RemoveAll(m => m.UserId == userInfo.UserId);
                }

                var channelType = ClassifyChannel(update.ChannelId);
                OnUserListUpdated?.Invoke(update.ChannelId, members);
                OnTypedMembersUpdated?.Invoke(channelType, update.ChannelId, members);
            }
        }

        private void HandleClientWhisperReceived(WhisperReceive proto)
        {
            var whisper = new WhisperMessage(proto);
            Log($"[Whisper] From {whisper.SenderNickname}: {whisper.Content}");
            OnWhisperReceived?.Invoke(whisper);
            OnWhisperReceivedInternal(whisper);
        }

        private void HandleClientAnnouncementReceived(AnnouncementReceive proto)
        {
            var announcement = new AnnouncementMessage(proto);
            Log($"[Announcement] [{announcement.Type}] {announcement.SenderName}: {announcement.Content}");
            OnAnnouncementReceived?.Invoke(announcement);
        }

        private void HandleClientUserActionNotificationReceived(UserActionNotificationReceive proto)
        {
            var notification = new UserActionNotificationMessage(proto);
            Log($"[UserAction] [{notification.ActionType}] {notification.ActorNickname}: {notification.Title}");
            OnUserActionNotificationReceived?.Invoke(notification);
        }

        private void HandleClientProfileUpdated(ProfileUpdateResponse proto)
        {
            var data = new ProfileUpdateResponseData(proto);
            Log($"Profile updated: {data.Success}");
        }

        private void HandleClientProfileChanged(ProfileChanged proto)
        {
            var data = new ProfileChangedData(proto);
            Log($"Profile changed for user: {data.UserId}");
        }

        #endregion

        #region Reconnection

        private async Task ReconnectAsync()
        {
            if (_isReconnecting) return;
            if (_reconnectAttempts >= MaxReconnectAttempts)
            {
                LogError("Max reconnect attempts reached");
                return;
            }

            _isReconnecting = true;
            _reconnectAttempts++;

            Log($"Reconnecting... (attempt {_reconnectAttempts}/{MaxReconnectAttempts})");

            await Task.Delay(TimeSpan.FromSeconds(ReconnectDelay));

            if (!IsConnected && !string.IsNullOrEmpty(_lastUserId))
            {
                bool connected = await Client.ConnectAsync(
                    _lastHost ?? "localhost", _lastPort > 0 ? _lastPort : 7777);

                if (connected)
                {
                    await LoginAsync(_lastUserId, _lastAuthToken, _lastNickname,
                        _lastProfileImage, _lastFrameImage, _lastExtraData);

                    // 이전 채널 재입장
                    await RejoinPreviousChannelsAsync();
                    OnReconnectedInternal();
                }
            }

            _isReconnecting = false;
        }

        private async Task RejoinPreviousChannelsAsync()
        {
            var previousChannels = new List<string>(_joinedChannels);
            _joinedChannels.Clear();
            _currentChannelsByType.Clear();

            foreach (var channelId in previousChannels)
            {
                try
                {
                    await Client.JoinChannelAsync(channelId);
                    await Task.Delay(100); // 서버 부하 방지
                }
                catch (Exception ex)
                {
                    LogError($"Failed to rejoin channel {channelId}: {ex.Message}");
                }
            }
        }

        #endregion

        #region History Management

        private void AddToHistory(string channelId, ChannelMessage message)
        {
            if (!_messageHistory.TryGetValue(channelId, out var history))
            {
                history = new List<ChannelMessage>();
                _messageHistory[channelId] = history;
            }

            history.Add(message);

            while (history.Count > MaxHistoryPerChannel)
                history.RemoveAt(0);
        }

        /// <summary>
        /// 모든 상태 초기화 (로그아웃 시 등)
        /// </summary>
        public void ClearAllState()
        {
            _joinedChannels.Clear();
            _currentChannelsByType.Clear();
            _messageHistory.Clear();
            _channelMembers.Clear();
            _channelList.Clear();
            CurrentChannelId = null;
            UserId = null;
        }

        #endregion

        #region Logging

        protected void Log(string message)
        {
            if (EnableLogging)
                Debug.Log($"{LogTag} {message}");
        }

        protected void LogError(string message)
        {
            Debug.LogError($"{LogTag} {message}");
        }

        #endregion
    }
}
