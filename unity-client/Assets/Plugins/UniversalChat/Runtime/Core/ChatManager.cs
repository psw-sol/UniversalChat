using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using UnityEngine;
using UniversalChat.Protocol;
using Chat.Protocol;

namespace UniversalChat.Core
{
    /// <summary>
    /// Unity MonoBehaviour 기반 채팅 매니저
    /// ChatClient를 래핑하여 Unity 생명주기와 통합
    /// </summary>
    public class ChatManager : MonoBehaviour
    {
        #region Singleton

        private static ChatManager _instance;
        public static ChatManager Instance
        {
            get
            {
                if (_instance == null)
                {
                    // 씬에 이미 배치된 ChatManager를 먼저 찾음
                    _instance = FindObjectOfType<ChatManager>();

                    // 없으면 새로 생성
                    if (_instance == null)
                    {
                        var go = new GameObject("[ChatManager]");
                        _instance = go.AddComponent<ChatManager>();
                        if (Application.isPlaying)
                        {
                            DontDestroyOnLoad(go);
                        }
                    }
                }
                return _instance;
            }
        }

        #endregion

        #region Inspector Settings

        [Header("Connection Settings")]
        [SerializeField] private string _serverHost = "localhost";
        [SerializeField] private int _serverPort = 7777;
        [SerializeField] private int _connectionTimeoutMs = 5000;
        [SerializeField] private float _heartbeatInterval = 30f;
        [SerializeField] private bool _autoReconnect = true;
        [SerializeField] private float _reconnectDelay = 5f;
        [SerializeField] private int _maxReconnectAttempts = 3;

        [Header("Auto Login (테스트용)")]
        [SerializeField] private bool _autoLogin = false;
        [SerializeField] private string _autoLoginUserId = "TestUser";
        [SerializeField] private string _autoLoginNickname = "";

        [Header("Auto Join")]
        [SerializeField] private bool _autoJoinWorldChannel = true;
        [SerializeField] private string _autoJoinChannelType = "world";

        [Header("Debug")]
        [SerializeField] private bool _enableLogging = true;

        #endregion

        #region Events

        public event Action OnConnected;
        public event Action<string> OnDisconnected;
        public event Action<string> OnError;
        public event Action<bool, string> OnAuthenticated;
        public event Action<ChannelMessage> OnMessageReceived;
        public event Action<string, string> OnChannelJoined;
        public event Action<string> OnChannelLeft;
        public event Action<List<ChannelInfo>> OnChannelListUpdated;
        public event Action<string, List<UserInfo>> OnUserListUpdated;
        public event Action<bool, string, string> OnChannelAutoAssigned; // success, channelId, errorMessage
        public event Action<AnnouncementMessage> OnAnnouncementReceived;
        public event Action<UserActionNotificationMessage> OnUserActionNotificationReceived;

        #endregion

        #region Properties

        public bool IsConnected => _client?.IsConnected ?? false;
        public bool IsAuthenticated => _client?.IsAuthenticated ?? false;
        public string UserId => _client?.UserId;
        public string CurrentChannelId { get; private set; }
        public IReadOnlyList<ChannelInfo> ChannelList => _channelList;
        public IReadOnlyDictionary<string, List<UserInfo>> UserLists => _userLists;

        #endregion

        #region Fields

        private ChatClient _client;
        private readonly List<ChannelInfo> _channelList = new List<ChannelInfo>();
        private readonly Dictionary<string, List<UserInfo>> _userLists = new Dictionary<string, List<UserInfo>>();
        private readonly List<string> _joinedChannels = new List<string>();
        private int _reconnectAttempts;
        private bool _isReconnecting;

        // 재연결 시 사용할 인증 정보
        private string _lastAuthToken;
        private string _lastNickname;
        private string _lastProfileImage;
        private string _lastFrameImage;
        private string _lastExtraData;

        #endregion

        #region Unity Lifecycle

        private void Awake()
        {
            if (_instance != null && _instance != this)
            {
                Destroy(gameObject);
                return;
            }

            _instance = this;
            if (Application.isPlaying)
            {
                DontDestroyOnLoad(gameObject);
            }

            InitializeClient();
        }

        private void OnDestroy()
        {
            Cleanup();

            if (_instance == this)
            {
                _instance = null;
            }
        }

        private void OnApplicationPause(bool paused)
        {
            if (paused)
            {
                Log("Application paused");
            }
            else
            {
                Log("Application resumed");
                // 이전에 인증된 적이 있을 때만 재연결 시도 (초기 연결은 LobbyLoadingState에서 처리)
                if (_autoReconnect && !IsConnected && !_isReconnecting && !string.IsNullOrEmpty(UserId))
                {
                    _ = ReconnectAsync();
                }
            }
        }

        #endregion

        #region Initialization

        private void InitializeClient()
        {
            _client = new ChatClient(_heartbeatInterval);

            _client.OnConnected += HandleConnected;
            _client.OnDisconnected += HandleDisconnected;
            _client.OnError += HandleError;
            _client.OnAuthenticated += HandleAuthenticated;
            _client.OnMessageReceived += HandleMessageReceived;
            _client.OnChannelJoined += HandleChannelJoined;
            _client.OnChannelLeft += HandleChannelLeft;
            _client.OnChannelListReceived += HandleChannelListReceived;
            _client.OnMemberUpdated += HandleMemberUpdated;
            _client.OnChannelAutoAssigned += HandleChannelAutoAssigned;
            _client.OnAnnouncementReceived += HandleAnnouncementReceived;
            _client.OnUserActionNotificationReceived += HandleUserActionNotificationReceived;
        }

        private void Cleanup()
        {
            if (_client != null)
            {
                _client.OnConnected -= HandleConnected;
                _client.OnDisconnected -= HandleDisconnected;
                _client.OnError -= HandleError;
                _client.OnAuthenticated -= HandleAuthenticated;
                _client.OnMessageReceived -= HandleMessageReceived;
                _client.OnChannelJoined -= HandleChannelJoined;
                _client.OnChannelLeft -= HandleChannelLeft;
                _client.OnChannelListReceived -= HandleChannelListReceived;
                _client.OnMemberUpdated -= HandleMemberUpdated;
                _client.OnChannelAutoAssigned -= HandleChannelAutoAssigned;
                _client.OnAnnouncementReceived -= HandleAnnouncementReceived;
                _client.OnUserActionNotificationReceived -= HandleUserActionNotificationReceived;

                _client.Dispose();
                _client = null;
            }
        }

        #endregion

        #region Public Methods

        public void Configure(string host, int port)
        {
            _serverHost = host;
            _serverPort = port;
        }

        public async Task<bool> ConnectAsync()
        {
            if (IsConnected)
            {
                Log("Already connected");
                return true;
            }

            Log($"Connecting to {_serverHost}:{_serverPort}...");
            return await _client.ConnectAsync(_serverHost, _serverPort, _connectionTimeoutMs);
        }

        public void Disconnect()
        {
            _autoReconnect = false;
            _client?.Disconnect();
            CurrentChannelId = null;
            _joinedChannels.Clear();
        }

        /// <summary>
        /// 채팅 서버에 로그인
        /// </summary>
        /// <param name="userId">사용자 ID</param>
        /// <param name="authToken">게임 서버에서 발급받은 인증 토큰</param>
        /// <param name="nickname">닉네임 (null이면 userId 사용)</param>
        /// <param name="profileImage">프로필 이미지 URL 또는 ID</param>
        /// <param name="frameImage">프레임 이미지 URL 또는 ID</param>
        /// <param name="extraData">기타 정보 (JSON 등)</param>
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

            // 재연결 시 사용할 인증 정보 저장
            _lastAuthToken = authToken;
            _lastNickname = nickname;
            _lastProfileImage = profileImage;
            _lastFrameImage = frameImage;
            _lastExtraData = extraData;

            Log($"Logging in as {userId}...");
            return await _client.AuthenticateAsync(userId, authToken, nickname, profileImage, frameImage, extraData);
        }

        /// <summary>
        /// 연결 및 로그인을 한 번에 수행
        /// </summary>
        /// <param name="userId">사용자 ID</param>
        /// <param name="authToken">게임 서버에서 발급받은 인증 토큰</param>
        /// <param name="nickname">닉네임 (null이면 userId 사용)</param>
        /// <param name="profileImage">프로필 이미지 URL 또는 ID</param>
        /// <param name="frameImage">프레임 이미지 URL 또는 ID</param>
        /// <param name="extraData">기타 정보 (JSON 등)</param>
        public async Task<bool> ConnectAndLoginAsync(
            string userId,
            string authToken = null,
            string nickname = null,
            string profileImage = null,
            string frameImage = null,
            string extraData = null)
        {
            bool connected = await ConnectAsync();
            if (!connected) return false;

            return await LoginAsync(userId, authToken, nickname, profileImage, frameImage, extraData);
        }

        public async Task JoinChannelAsync(string channelId, string password = null)
        {
            await _client.JoinChannelAsync(channelId, password);
        }

        public async Task LeaveChannelAsync(string channelId)
        {
            await _client.LeaveChannelAsync(channelId);
        }

        public async Task CreateChannelAsync(string channelName, string password = null, int maxUsers = 100)
        {
            await _client.CreateChannelAsync(channelName, password, maxUsers);
        }

        public async Task RefreshChannelListAsync()
        {
            await _client.RequestChannelListAsync();
        }

        public async Task RefreshUserListAsync(string channelId)
        {
            await _client.RequestUserListAsync(channelId);
        }

        public async Task SendMessageAsync(string content)
        {
            if (string.IsNullOrEmpty(CurrentChannelId))
            {
                LogError("Not in any channel");
                return;
            }

            await _client.SendMessageAsync(CurrentChannelId, content);
        }

        public async Task SendMessageToChannelAsync(string channelId, string content)
        {
            await _client.SendMessageAsync(channelId, content);
        }

        /// <summary>
        /// 자동 채널 배정 요청 (서버가 최적의 채널에 자동으로 가입시킴)
        /// </summary>
        /// <param name="channelType">채널 타입 ("world", "trade", "help" 등)</param>
        public async Task JoinAutoAssignedChannelAsync(string channelType = "world")
        {
            if (!IsAuthenticated)
            {
                LogError("Not authenticated - cannot request auto-assign");
                return;
            }

            Log($"Requesting auto-assign to {channelType} channel...");
            await _client.RequestAutoAssignChannelAsync(channelType);
        }

        #endregion

        #region Event Handlers

        private async void HandleConnected()
        {
            Log("Connected to server");
            _reconnectAttempts = 0;
            _isReconnecting = false;
            OnConnected?.Invoke();

            // 자동 로그인 (테스트용)
            if (_autoLogin && !string.IsNullOrEmpty(_autoLoginUserId))
            {
                string nickname = string.IsNullOrEmpty(_autoLoginNickname) ? _autoLoginUserId : _autoLoginNickname;
                Log($"Auto-login enabled, logging in as {_autoLoginUserId}...");
                await LoginAsync(_autoLoginUserId, null, nickname);
            }
        }

        private async void HandleDisconnected(string reason)
        {
            Log($"Disconnected: {reason}");
            CurrentChannelId = null;

            OnDisconnected?.Invoke(reason);

            if (_autoReconnect && !_isReconnecting)
            {
                await ReconnectAsync();
            }
        }

        private void HandleError(string error)
        {
            LogError(error);
            OnError?.Invoke(error);
        }

        private async void HandleAuthenticated(AuthResponse response)
        {
            if (response.Success)
            {
                Log($"Authenticated with session: {response.SessionId}");

                // 자동 월드 채널 가입
                if (_autoJoinWorldChannel)
                {
                    Log($"Auto-joining {_autoJoinChannelType} channel...");
                    await JoinAutoAssignedChannelAsync(_autoJoinChannelType);
                }
            }
            else
            {
                LogError($"Authentication failed: {response.ErrorMessage}");
            }

            OnAuthenticated?.Invoke(response.Success, response.ErrorMessage ?? string.Empty);
        }

        private void HandleMessageReceived(MessageReceive protoMessage)
        {
            var message = new ChannelMessage(protoMessage);
            Log($"[{message.ChannelId}] {message.SenderNickname}: {message.Content}");
            OnMessageReceived?.Invoke(message);
        }

        private void HandleChannelJoined(ChannelJoinAck protoResponse)
        {
            var response = new ChannelJoinResponse(protoResponse);

            if (response.Success)
            {
                Log($"Joined channel: {response.ChannelId}");
                CurrentChannelId = response.ChannelId;

                if (!_joinedChannels.Contains(response.ChannelId))
                {
                    _joinedChannels.Add(response.ChannelId);
                }

                // 유저 목록도 업데이트
                _userLists[response.ChannelId] = response.Members;
                OnUserListUpdated?.Invoke(response.ChannelId, response.Members);

                OnChannelJoined?.Invoke(response.ChannelId, response.ChannelId); // ChannelName이 proto에 없음
            }
            else
            {
                LogError($"Failed to join channel: {response.ErrorMessage}");
                OnError?.Invoke(response.ErrorMessage ?? "Unknown error");
            }
        }

        private void HandleChannelLeft(ChannelLeaveAck protoResponse)
        {
            var response = new ChannelLeaveResponse(protoResponse);

            if (response.Success)
            {
                Log($"Left channel: {response.ChannelId}");
                _joinedChannels.Remove(response.ChannelId);
                _userLists.Remove(response.ChannelId);

                if (CurrentChannelId == response.ChannelId)
                {
                    CurrentChannelId = _joinedChannels.Count > 0 ? _joinedChannels[0] : null;
                }

                OnChannelLeft?.Invoke(response.ChannelId);
            }
        }

        private void HandleChannelListReceived(Chat.Protocol.ChannelListResponse protoResponse)
        {
            var response = new ChannelListResponse(protoResponse);

            _channelList.Clear();
            _channelList.AddRange(response.Channels);

            Log($"Received {_channelList.Count} channels");
            OnChannelListUpdated?.Invoke(_channelList);
        }

        private void HandleMemberUpdated(ChannelMemberUpdate update)
        {
            // 멤버 업데이트 처리 (입장/퇴장 알림)
            Log($"Member update in {update.ChannelId}: {update.User?.Nickname} ({update.UpdateType})");
        }

        private void HandleChannelAutoAssigned(ChannelAutoAssignAck response)
        {
            if (response.Success)
            {
                Log($"Auto-assigned to channel: {response.AssignedChannelId}");
                CurrentChannelId = response.AssignedChannelId;

                if (!_joinedChannels.Contains(response.AssignedChannelId))
                {
                    _joinedChannels.Add(response.AssignedChannelId);
                }

                // 유저 목록도 업데이트 (proto -> wrapper 변환)
                var members = new List<UserInfo>();
                foreach (var protoMember in response.Members)
                {
                    members.Add(new UserInfo(protoMember));
                }
                _userLists[response.AssignedChannelId] = members;
                OnUserListUpdated?.Invoke(response.AssignedChannelId, members);

                OnChannelAutoAssigned?.Invoke(true, response.AssignedChannelId, null);
                OnChannelJoined?.Invoke(response.AssignedChannelId, response.AssignedChannelId);
            }
            else
            {
                LogError($"Auto-assign failed: {response.ErrorMessage}");
                OnChannelAutoAssigned?.Invoke(false, null, response.ErrorMessage);
                OnError?.Invoke(response.ErrorMessage ?? "Auto-assign failed");
            }
        }

        private void HandleAnnouncementReceived(AnnouncementReceive protoAnnouncement)
        {
            var announcement = new AnnouncementMessage(protoAnnouncement);
            Log($"[Announcement] [{announcement.Type}] {announcement.SenderName}: {announcement.Content}");
            OnAnnouncementReceived?.Invoke(announcement);
        }

        private void HandleUserActionNotificationReceived(UserActionNotificationReceive protoNotification)
        {
            var notification = new UserActionNotificationMessage(protoNotification);
            Log($"[UserAction] [{notification.ActionType}] {notification.ActorNickname}: {notification.Title} - {notification.Content}");
            OnUserActionNotificationReceived?.Invoke(notification);
        }

        #endregion

        #region Reconnection

        private async Task ReconnectAsync()
        {
            if (_isReconnecting) return;
            if (_reconnectAttempts >= _maxReconnectAttempts)
            {
                LogError("Max reconnect attempts reached");
                return;
            }

            _isReconnecting = true;
            _reconnectAttempts++;

            Log($"Reconnecting... (attempt {_reconnectAttempts}/{_maxReconnectAttempts})");

            await Task.Delay(TimeSpan.FromSeconds(_reconnectDelay));

            if (!IsConnected)
            {
                bool connected = await ConnectAsync();

                if (connected && !string.IsNullOrEmpty(UserId))
                {
                    // 저장된 인증 정보로 재로그인
                    await LoginAsync(UserId, _lastAuthToken, _lastNickname, _lastProfileImage, _lastFrameImage, _lastExtraData);
                }
            }

            _isReconnecting = false;
        }

        #endregion

        #region Logging

        private void Log(string message)
        {
            if (_enableLogging)
            {
                Debug.Log($"[ChatManager] {message}");
            }
        }

        private void LogError(string message)
        {
            Debug.LogError($"[ChatManager] {message}");
        }

        #endregion
    }
}
