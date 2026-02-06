using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using UnityEngine;
using UniversalChat.Core;
using Chat.Protocol;

namespace UniversalChat.Game
{
    /// <summary>
    /// 게임 전용 채팅 매니저 (ChatManager 래퍼)
    /// 채널 타입별 관리, 메시지 히스토리 캐싱, 확장 기능 제공
    /// </summary>
    public class GameChatManager : MonoBehaviour
    {
        #region Singleton

        private static GameChatManager _instance;
        public static GameChatManager Instance
        {
            get
            {
                if (_instance == null)
                {
                    _instance = FindObjectOfType<GameChatManager>();

                    if (_instance == null)
                    {
                        var go = new GameObject("[GameChatManager]");
                        _instance = go.AddComponent<GameChatManager>();
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

        #region Enums

        /// <summary>
        /// 채널 타입
        /// </summary>
        public enum ChannelType
        {
            World,      // 월드 채널 (world_*)
            Guild,      // 길드 채널 (guild_*)
            Party,      // 파티 채널 (party_*)
            Whisper,    // 귓속말 (1:1)
            System,     // 시스템 메시지
            Custom      // 사용자 정의
        }

        #endregion

        #region Inspector Settings

        [Header("Connection Settings")]
        [SerializeField] private string _serverHost = "localhost";
        [SerializeField] private int _serverPort = 7777;

        [Header("History Settings")]
        [SerializeField] private int _maxHistoryPerChannel = 100;
        [SerializeField] private bool _cacheMessageHistory = true;

        [Header("Debug")]
        [SerializeField] private bool _enableLogging = true;

        #endregion

        #region Events

        // === 연결 이벤트 ===
        public event Action OnConnected;
        public event Action<string> OnDisconnected;
        public event Action<string> OnError;
        public event Action<bool, string> OnAuthenticated;

        // === 채널 타입별 메시지 이벤트 ===
        public event Action<ChannelMessage> OnWorldMessageReceived;
        public event Action<ChannelMessage> OnGuildMessageReceived;
        public event Action<ChannelMessage> OnPartyMessageReceived;
        public event Action<ChannelMessage> OnAnyMessageReceived;

        // === 채널 이벤트 ===
        public event Action<ChannelType, string> OnChannelJoined;
        public event Action<ChannelType, string> OnChannelLeft;
        public event Action<string, List<UserInfo>> OnChannelMembersUpdated;

        // === 히스토리 이벤트 ===
        public event Action<string, List<ChannelMessage>> OnHistoryLoaded;

        // === 시스템 이벤트 ===
        public event Action<AnnouncementMessage> OnAnnouncementReceived;
        public event Action<UserActionNotificationMessage> OnUserActionReceived;

        #endregion

        #region Properties

        public bool IsConnected => ChatManager.Instance?.IsConnected ?? false;
        public bool IsAuthenticated => ChatManager.Instance?.IsAuthenticated ?? false;
        public string UserId => ChatManager.Instance?.UserId;

        // 현재 활성 채널
        public string CurrentWorldChannelId { get; private set; }
        public string CurrentGuildChannelId { get; private set; }
        public string CurrentPartyChannelId { get; private set; }

        // 가입한 채널 목록
        public IReadOnlyList<string> JoinedChannels => _joinedChannels.ToList();

        #endregion

        #region Fields

        private readonly List<string> _joinedChannels = new List<string>();
        private readonly Dictionary<string, List<ChannelMessage>> _messageHistory = new Dictionary<string, List<ChannelMessage>>();
        private readonly Dictionary<string, List<UserInfo>> _channelMembers = new Dictionary<string, List<UserInfo>>();

        private bool _isInitialized;

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
        }

        private void Start()
        {
            Initialize();
        }

        private void OnDestroy()
        {
            Cleanup();

            if (_instance == this)
            {
                _instance = null;
            }
        }

        #endregion

        #region Initialization

        private void Initialize()
        {
            if (_isInitialized) return;

            var chatManager = ChatManager.Instance;
            if (chatManager == null)
            {
                LogError("ChatManager not found!");
                return;
            }

            // ChatManager 이벤트 구독
            chatManager.OnConnected += HandleConnected;
            chatManager.OnDisconnected += HandleDisconnected;
            chatManager.OnError += HandleError;
            chatManager.OnAuthenticated += HandleAuthenticated;
            chatManager.OnMessageReceived += HandleMessageReceived;
            chatManager.OnChannelJoined += HandleChannelJoined;
            chatManager.OnChannelLeft += HandleChannelLeft;
            chatManager.OnUserListUpdated += HandleUserListUpdated;
            chatManager.OnAnnouncementReceived += HandleAnnouncementReceived;
            chatManager.OnUserActionNotificationReceived += HandleUserActionReceived;

            _isInitialized = true;
            Log("GameChatManager initialized");
        }

        private void Cleanup()
        {
            if (!_isInitialized) return;

            var chatManager = ChatManager.Instance;
            if (chatManager != null)
            {
                chatManager.OnConnected -= HandleConnected;
                chatManager.OnDisconnected -= HandleDisconnected;
                chatManager.OnError -= HandleError;
                chatManager.OnAuthenticated -= HandleAuthenticated;
                chatManager.OnMessageReceived -= HandleMessageReceived;
                chatManager.OnChannelJoined -= HandleChannelJoined;
                chatManager.OnChannelLeft -= HandleChannelLeft;
                chatManager.OnUserListUpdated -= HandleUserListUpdated;
                chatManager.OnAnnouncementReceived -= HandleAnnouncementReceived;
                chatManager.OnUserActionNotificationReceived -= HandleUserActionReceived;
            }

            _messageHistory.Clear();
            _channelMembers.Clear();
            _joinedChannels.Clear();

            _isInitialized = false;
        }

        #endregion

        #region Public API - Connection

        /// <summary>
        /// 서버 설정
        /// </summary>
        public void Configure(string host, int port)
        {
            _serverHost = host;
            _serverPort = port;
            ChatManager.Instance.Configure(host, port);
        }

        /// <summary>
        /// 서버 연결
        /// </summary>
        public async Task<bool> ConnectAsync()
        {
            Initialize();
            ChatManager.Instance.Configure(_serverHost, _serverPort);
            return await ChatManager.Instance.ConnectAsync();
        }

        /// <summary>
        /// 연결 해제
        /// </summary>
        public void Disconnect()
        {
            ChatManager.Instance.Disconnect();
            ClearAllState();
        }

        /// <summary>
        /// 로그인
        /// </summary>
        public async Task<bool> LoginAsync(
            string userId,
            string authToken = null,
            string nickname = null,
            string profileImage = null,
            string frameImage = null,
            string extraData = null)
        {
            return await ChatManager.Instance.LoginAsync(userId, authToken, nickname, profileImage, frameImage, extraData);
        }

        /// <summary>
        /// 연결 + 로그인 한 번에
        /// </summary>
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

        #endregion

        #region Public API - Channel Operations

        /// <summary>
        /// 월드 채널 자동 배정 요청
        /// </summary>
        public async Task JoinWorldChannelAsync()
        {
            Log("Requesting world channel auto-assign...");
            await ChatManager.Instance.JoinAutoAssignedChannelAsync("world");
        }

        /// <summary>
        /// 길드 채널 입장
        /// </summary>
        public async Task JoinGuildChannelAsync(string guildId, string password = null)
        {
            string channelId = $"guild_{guildId}";
            Log($"Joining guild channel: {channelId}");
            await ChatManager.Instance.JoinChannelAsync(channelId, password);
        }

        /// <summary>
        /// 파티 채널 입장
        /// </summary>
        public async Task JoinPartyChannelAsync(string partyId, string password = null)
        {
            string channelId = $"party_{partyId}";
            Log($"Joining party channel: {channelId}");
            await ChatManager.Instance.JoinChannelAsync(channelId, password);
        }

        /// <summary>
        /// 특정 채널 입장
        /// </summary>
        public async Task JoinChannelAsync(string channelId, string password = null)
        {
            await ChatManager.Instance.JoinChannelAsync(channelId, password);
        }

        /// <summary>
        /// 채널 퇴장
        /// </summary>
        public async Task LeaveChannelAsync(string channelId)
        {
            await ChatManager.Instance.LeaveChannelAsync(channelId);
        }

        /// <summary>
        /// 길드 채널 퇴장
        /// </summary>
        public async Task LeaveGuildChannelAsync()
        {
            if (!string.IsNullOrEmpty(CurrentGuildChannelId))
            {
                await LeaveChannelAsync(CurrentGuildChannelId);
            }
        }

        /// <summary>
        /// 파티 채널 퇴장
        /// </summary>
        public async Task LeavePartyChannelAsync()
        {
            if (!string.IsNullOrEmpty(CurrentPartyChannelId))
            {
                await LeaveChannelAsync(CurrentPartyChannelId);
            }
        }

        #endregion

        #region Public API - Message Operations

        /// <summary>
        /// 월드 채널에 메시지 전송
        /// </summary>
        public async Task SendWorldMessageAsync(string content)
        {
            if (string.IsNullOrEmpty(CurrentWorldChannelId))
            {
                LogError("Not in any world channel");
                return;
            }
            await ChatManager.Instance.SendMessageToChannelAsync(CurrentWorldChannelId, content);
        }

        /// <summary>
        /// 길드 채널에 메시지 전송
        /// </summary>
        public async Task SendGuildMessageAsync(string content)
        {
            if (string.IsNullOrEmpty(CurrentGuildChannelId))
            {
                LogError("Not in any guild channel");
                return;
            }
            await ChatManager.Instance.SendMessageToChannelAsync(CurrentGuildChannelId, content);
        }

        /// <summary>
        /// 파티 채널에 메시지 전송
        /// </summary>
        public async Task SendPartyMessageAsync(string content)
        {
            if (string.IsNullOrEmpty(CurrentPartyChannelId))
            {
                LogError("Not in any party channel");
                return;
            }
            await ChatManager.Instance.SendMessageToChannelAsync(CurrentPartyChannelId, content);
        }

        /// <summary>
        /// 특정 채널에 메시지 전송
        /// </summary>
        public async Task SendMessageAsync(string channelId, string content)
        {
            await ChatManager.Instance.SendMessageToChannelAsync(channelId, content);
        }

        /// <summary>
        /// 현재 활성 채널에 메시지 전송
        /// </summary>
        public async Task SendMessageAsync(string content)
        {
            await ChatManager.Instance.SendMessageAsync(content);
        }

        #endregion

        #region Public API - History & Members

        /// <summary>
        /// 채널의 메시지 히스토리 조회
        /// </summary>
        public List<ChannelMessage> GetMessageHistory(string channelId, int count = 50)
        {
            if (!_messageHistory.TryGetValue(channelId, out var history))
            {
                return new List<ChannelMessage>();
            }

            return history.TakeLast(count).ToList();
        }

        /// <summary>
        /// 월드 채널 메시지 히스토리
        /// </summary>
        public List<ChannelMessage> GetWorldMessageHistory(int count = 50)
        {
            return string.IsNullOrEmpty(CurrentWorldChannelId)
                ? new List<ChannelMessage>()
                : GetMessageHistory(CurrentWorldChannelId, count);
        }

        /// <summary>
        /// 길드 채널 메시지 히스토리
        /// </summary>
        public List<ChannelMessage> GetGuildMessageHistory(int count = 50)
        {
            return string.IsNullOrEmpty(CurrentGuildChannelId)
                ? new List<ChannelMessage>()
                : GetMessageHistory(CurrentGuildChannelId, count);
        }

        /// <summary>
        /// 파티 채널 메시지 히스토리
        /// </summary>
        public List<ChannelMessage> GetPartyMessageHistory(int count = 50)
        {
            return string.IsNullOrEmpty(CurrentPartyChannelId)
                ? new List<ChannelMessage>()
                : GetMessageHistory(CurrentPartyChannelId, count);
        }

        /// <summary>
        /// 채널 멤버 목록 조회
        /// </summary>
        public List<UserInfo> GetChannelMembers(string channelId)
        {
            return _channelMembers.TryGetValue(channelId, out var members)
                ? new List<UserInfo>(members)
                : new List<UserInfo>();
        }

        /// <summary>
        /// 채널 히스토리 클리어
        /// </summary>
        public void ClearHistory(string channelId)
        {
            if (_messageHistory.ContainsKey(channelId))
            {
                _messageHistory[channelId].Clear();
            }
        }

        /// <summary>
        /// 모든 히스토리 클리어
        /// </summary>
        public void ClearAllHistory()
        {
            _messageHistory.Clear();
        }

        #endregion

        #region Public API - Utility

        /// <summary>
        /// 채널 ID로 채널 타입 판별
        /// </summary>
        public static ChannelType GetChannelType(string channelId)
        {
            if (string.IsNullOrEmpty(channelId))
                return ChannelType.Custom;

            if (channelId.StartsWith("world") || channelId.StartsWith("world_"))
                return ChannelType.World;
            if (channelId.StartsWith("guild_"))
                return ChannelType.Guild;
            if (channelId.StartsWith("party_"))
                return ChannelType.Party;
            if (channelId.StartsWith("system"))
                return ChannelType.System;

            return ChannelType.Custom;
        }

        /// <summary>
        /// 특정 채널에 가입되어 있는지 확인
        /// </summary>
        public bool IsInChannel(string channelId)
        {
            return _joinedChannels.Contains(channelId);
        }

        /// <summary>
        /// 특정 타입의 채널에 가입되어 있는지 확인
        /// </summary>
        public bool IsInChannelType(ChannelType type)
        {
            return type switch
            {
                ChannelType.World => !string.IsNullOrEmpty(CurrentWorldChannelId),
                ChannelType.Guild => !string.IsNullOrEmpty(CurrentGuildChannelId),
                ChannelType.Party => !string.IsNullOrEmpty(CurrentPartyChannelId),
                _ => false
            };
        }

        #endregion

        #region Event Handlers

        private void HandleConnected()
        {
            Log("Connected to chat server");
            OnConnected?.Invoke();
        }

        private void HandleDisconnected(string reason)
        {
            Log($"Disconnected: {reason}");
            ClearAllState();
            OnDisconnected?.Invoke(reason);
        }

        private void HandleError(string error)
        {
            LogError(error);
            OnError?.Invoke(error);
        }

        private void HandleAuthenticated(bool success, string message)
        {
            if (success)
            {
                Log("Authenticated successfully");
            }
            else
            {
                LogError($"Authentication failed: {message}");
            }
            OnAuthenticated?.Invoke(success, message);
        }

        private void HandleMessageReceived(ChannelMessage message)
        {
            // 히스토리에 저장
            if (_cacheMessageHistory)
            {
                AddToHistory(message.ChannelId, message);
            }

            // 모든 메시지 이벤트
            OnAnyMessageReceived?.Invoke(message);

            // 채널 타입별 이벤트
            var channelType = GetChannelType(message.ChannelId);
            switch (channelType)
            {
                case ChannelType.World:
                    OnWorldMessageReceived?.Invoke(message);
                    break;
                case ChannelType.Guild:
                    OnGuildMessageReceived?.Invoke(message);
                    break;
                case ChannelType.Party:
                    OnPartyMessageReceived?.Invoke(message);
                    break;
            }
        }

        private void HandleChannelJoined(string channelId, string channelName)
        {
            Log($"Joined channel: {channelId}");

            if (!_joinedChannels.Contains(channelId))
            {
                _joinedChannels.Add(channelId);
            }

            // 채널 타입별 현재 채널 설정
            var channelType = GetChannelType(channelId);
            switch (channelType)
            {
                case ChannelType.World:
                    CurrentWorldChannelId = channelId;
                    break;
                case ChannelType.Guild:
                    CurrentGuildChannelId = channelId;
                    break;
                case ChannelType.Party:
                    CurrentPartyChannelId = channelId;
                    break;
            }

            // 히스토리 초기화
            if (!_messageHistory.ContainsKey(channelId))
            {
                _messageHistory[channelId] = new List<ChannelMessage>();
            }

            OnChannelJoined?.Invoke(channelType, channelId);
        }

        private void HandleChannelLeft(string channelId)
        {
            Log($"Left channel: {channelId}");

            _joinedChannels.Remove(channelId);

            // 채널 타입별 현재 채널 해제
            var channelType = GetChannelType(channelId);
            switch (channelType)
            {
                case ChannelType.World:
                    if (CurrentWorldChannelId == channelId)
                        CurrentWorldChannelId = null;
                    break;
                case ChannelType.Guild:
                    if (CurrentGuildChannelId == channelId)
                        CurrentGuildChannelId = null;
                    break;
                case ChannelType.Party:
                    if (CurrentPartyChannelId == channelId)
                        CurrentPartyChannelId = null;
                    break;
            }

            // 히스토리 및 멤버 정보 유지 (옵션에 따라 삭제 가능)
            _channelMembers.Remove(channelId);

            OnChannelLeft?.Invoke(channelType, channelId);
        }

        private void HandleUserListUpdated(string channelId, List<UserInfo> members)
        {
            _channelMembers[channelId] = new List<UserInfo>(members);
            OnChannelMembersUpdated?.Invoke(channelId, members);
        }

        private void HandleAnnouncementReceived(AnnouncementMessage announcement)
        {
            Log($"[Announcement] {announcement.Content}");
            OnAnnouncementReceived?.Invoke(announcement);
        }

        private void HandleUserActionReceived(UserActionNotificationMessage notification)
        {
            Log($"[UserAction] {notification.ActorNickname}: {notification.Title}");
            OnUserActionReceived?.Invoke(notification);
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

            // 최대 개수 초과 시 오래된 메시지 삭제
            while (history.Count > _maxHistoryPerChannel)
            {
                history.RemoveAt(0);
            }
        }

        /// <summary>
        /// 채널 입장 시 받은 히스토리를 캐시에 추가
        /// (ChatManager의 OnChannelJoined에서 RecentMessages를 직접 접근할 수 없으므로
        ///  필요시 ChatManager 수정하거나 별도 처리 필요)
        /// </summary>
        public void LoadHistoryFromJoinResponse(string channelId, List<ChannelMessage> messages)
        {
            if (!_messageHistory.TryGetValue(channelId, out var history))
            {
                history = new List<ChannelMessage>();
                _messageHistory[channelId] = history;
            }

            // 기존 히스토리 클리어 후 새로 로드
            history.Clear();
            history.AddRange(messages);

            Log($"Loaded {messages.Count} messages for channel {channelId}");
            OnHistoryLoaded?.Invoke(channelId, messages);
        }

        #endregion

        #region State Management

        private void ClearAllState()
        {
            CurrentWorldChannelId = null;
            CurrentGuildChannelId = null;
            CurrentPartyChannelId = null;
            _joinedChannels.Clear();
            _channelMembers.Clear();
            // 히스토리는 유지 (옵션에 따라 변경 가능)
        }

        #endregion

        #region Logging

        private void Log(string message)
        {
            if (_enableLogging)
            {
                Debug.Log($"[GameChatManager] {message}");
            }
        }

        private void LogError(string message)
        {
            Debug.LogError($"[GameChatManager] {message}");
        }

        #endregion
    }
}
