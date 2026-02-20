using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using UnityEngine;
using UnityEngine.Events;
using UnityEngine.UI;
using UniversalChat.Core;
using UniversalChat.RichContent;
using UniversalChat.Translation;

namespace UniversalChat.UI
{
    /// <summary>
    /// 채팅 UI를 관리하는 메인 컴포넌트
    /// Inspector에서 설정 가능한 Plug & Play 방식
    /// </summary>
    public class ChatUIManager : MonoBehaviour
    {
        #region Inspector - Connection

        [Header("Connection Settings")]
        [Tooltip("Start 시 자동 연결 (ChatManager의 서버 설정 사용)")]
        [SerializeField] private bool _connectOnStart = false;

        #endregion

        #region Inspector - UI References

        [Header("UI References")]
        [SerializeField] private VirtualizedChatPanel _virtualizedChatPanel;
        [SerializeField] private ChatInputField _inputField;
        [SerializeField] private ChannelListPanel _channelListPanel;
        [SerializeField] private Button _sendButton;
        [SerializeField] private Button _connectButton;
        [SerializeField] private Text _connectionStatusText;

        #endregion

        #region Inspector - Theme

        [Header("Theme")]
        [SerializeField] private ChatUIConfig _uiConfig;

        #endregion

        #region Inspector - Translation

        [Header("Translation")]
        [Tooltip("번역 기능 활성화 여부")]
        [SerializeField] private bool _enableTranslation = false;

        [Tooltip("번역 설정 (TranslationConfig ScriptableObject)")]
        [SerializeField] private TranslationConfig _translationConfig;

        [Tooltip("메시지 수신 시 자동 번역 (AutoTranslate 설정 필요)")]
        [SerializeField] private bool _autoTranslateOnReceive = false;

        #endregion

        #region Inspector - Auto Features

        [Header("Auto Features")]
        [Tooltip("채널 입장 시 최근 메시지를 자동으로 채팅창에 표시")]
        [SerializeField] private bool _autoLoadHistoryOnJoin = true;

        [Tooltip("새 메시지 수신 시 자동으로 하단 스크롤")]
        [SerializeField] private bool _autoScrollToBottom = true;

        #endregion

        #region Inspector - Events

        [Header("Lifecycle Events")]
        [Tooltip("전체 파이프라인 완료 (Connect → Login → JoinChannel 성공)")]
        public UnityEvent OnChatReadyEvent;

        public UnityEvent OnConnectedEvent;
        public UnityEvent<string> OnDisconnectedEvent;
        public UnityEvent<string> OnErrorEvent;

        [Header("Auth Events")]
        public UnityEvent<bool> OnAuthenticatedEvent;

        [Header("Channel Events")]
        public UnityEvent<string> OnChannelJoinedEvent;

        [Tooltip("채널 입장 완료 (RecentMessages, Members 포함)")]
        public UnityEvent<ChannelJoinResult> OnChannelJoinedCompleteEvent;

        public UnityEvent<string> OnChannelLeftEvent;

        [Header("Message Events")]
        public UnityEvent<ChannelMessage> OnMessageReceivedEvent;

        [Tooltip("귓속말 수신")]
        public UnityEvent<WhisperMessage> OnWhisperReceivedEvent;

        [Header("Notification Events")]
        [Tooltip("공지사항 수신")]
        public UnityEvent<AnnouncementMessage> OnAnnouncementReceivedEvent;

        [Tooltip("유저 행동 알림 수신 (아이템 획득, 업적 등)")]
        public UnityEvent<UserActionNotificationMessage> OnUserActionNotificationEvent;

        [Header("Rich Content Events")]
        [Tooltip("Rich Content 링크 클릭 (아이템, 유저 등)")]
        public UnityEvent<RichLinkData> OnLinkClickedEvent;

        [Header("DM Events")]
        [Tooltip("DM 대화 시작됨")]
        public UnityEvent<DMConversation> OnDMStartedEvent;

        [Tooltip("DM 메시지 수신")]
        public UnityEvent<ChannelMessage> OnDMMessageReceivedEvent;

        [Tooltip("DM 읽음 확인 수신")]
        public UnityEvent<DMReadReceiptData> OnDMReadReceiptEvent;

        [Tooltip("DM 대화 목록 업데이트")]
        public UnityEvent<List<DMConversation>> OnDMListUpdatedEvent;

        #endregion

        #region Properties

        public ChatUIConfig UIConfig => _uiConfig;

        /// <summary>
        /// UI 설정 가져오기 (RichContent 컴포넌트에서 사용)
        /// </summary>
        public ChatUIConfig GetConfig() => _uiConfig;

        public bool IsConnected => _chatService?.IsConnected ?? false;
        public bool IsAuthenticated => _chatService?.IsAuthenticated ?? false;
        public string CurrentChannelId => _chatService?.CurrentChannelId;

        /// <summary>
        /// 번역 기능이 활성화되어 있는지 여부
        /// </summary>
        public bool IsTranslationEnabled => _enableTranslation && _translationConfig != null;

        /// <summary>
        /// 번역 설정
        /// </summary>
        public TranslationConfig TranslationConfig => _translationConfig;

        #endregion

        #region Fields

        private IChatService _chatService;
        private readonly List<ChannelMessage> _messageHistory = new List<ChannelMessage>();
        private AudioSource _audioSource;

        #endregion

        #region Unity Lifecycle

        private void Awake()
        {
            SetupAudioSource();
            SetupUIComponents();
            SetupTranslation();
        }

        private void Start()
        {
            // IChatService 미설정 시 ChatManager 자동 감지 (하위 호환)
            if (_chatService == null)
            {
                _chatService = ChatManager.Instance;
            }

            SubscribeToEvents();

            if (_connectOnStart)
            {
                _ = ConnectAsync();
            }
        }

        private void OnDestroy()
        {
            UnsubscribeFromEvents();
        }

        #endregion

        #region Setup

        private void SetupAudioSource()
        {
            _audioSource = GetComponent<AudioSource>();
            if (_audioSource == null && _uiConfig != null && _uiConfig.EnableSoundEffects)
            {
                _audioSource = gameObject.AddComponent<AudioSource>();
                _audioSource.playOnAwake = false;
            }
        }

        private void SetupUIComponents()
        {
            // Send Button
            if (_sendButton != null)
            {
                _sendButton.onClick.AddListener(OnSendButtonClicked);
            }

            // Connect Button
            if (_connectButton != null)
            {
                _connectButton.onClick.AddListener(OnConnectButtonClicked);
            }

            // Input Field
            if (_inputField != null)
            {
                _inputField.OnSubmit += OnInputSubmit;
            }

            UpdateConnectionStatus();
        }

        private void SetupTranslation()
        {
            if (!_enableTranslation || _translationConfig == null) return;

            // Initialize TranslationManager with config
            TranslationManager.Instance.Initialize(_translationConfig);
        }

        private void SubscribeToEvents()
        {
            if (_chatService == null) return;

            _chatService.OnConnected += HandleConnected;
            _chatService.OnDisconnected += HandleDisconnected;
            _chatService.OnError += HandleError;
            _chatService.OnAuthenticated += HandleAuthenticated;
            _chatService.OnMessageReceived += HandleMessageReceived;
            _chatService.OnChannelJoined += HandleChannelJoined;
            _chatService.OnChannelJoinedWithHistory += HandleChannelJoinedWithHistory;
            _chatService.OnChannelLeft += HandleChannelLeft;
            _chatService.OnChannelListUpdated += HandleChannelListUpdated;
            _chatService.OnWhisperReceived += HandleWhisperReceived;
            _chatService.OnAnnouncementReceived += HandleAnnouncementReceived;
            _chatService.OnUserActionNotificationReceived += HandleUserActionNotificationReceived;
            _chatService.OnChatReady += HandleChatReady;

            // DM events
            _chatService.OnDMStarted += HandleDMStarted;
            _chatService.OnDMMessageReceived += HandleDMMessageReceived;
            _chatService.OnDMReadReceiptReceived += HandleDMReadReceiptReceived;
            _chatService.OnDMListUpdated += HandleDMListUpdated;

            // RichContent 링크 클릭 이벤트
            if (RichContentManager.HasInstance)
            {
                RichContentManager.Instance.OnLinkClicked += HandleLinkClicked;
            }
        }

        private void UnsubscribeFromEvents()
        {
            if (_chatService == null) return;

            _chatService.OnConnected -= HandleConnected;
            _chatService.OnDisconnected -= HandleDisconnected;
            _chatService.OnError -= HandleError;
            _chatService.OnAuthenticated -= HandleAuthenticated;
            _chatService.OnMessageReceived -= HandleMessageReceived;
            _chatService.OnChannelJoined -= HandleChannelJoined;
            _chatService.OnChannelJoinedWithHistory -= HandleChannelJoinedWithHistory;
            _chatService.OnChannelLeft -= HandleChannelLeft;
            _chatService.OnChannelListUpdated -= HandleChannelListUpdated;
            _chatService.OnWhisperReceived -= HandleWhisperReceived;
            _chatService.OnAnnouncementReceived -= HandleAnnouncementReceived;
            _chatService.OnUserActionNotificationReceived -= HandleUserActionNotificationReceived;
            _chatService.OnChatReady -= HandleChatReady;

            // DM events
            _chatService.OnDMStarted -= HandleDMStarted;
            _chatService.OnDMMessageReceived -= HandleDMMessageReceived;
            _chatService.OnDMReadReceiptReceived -= HandleDMReadReceiptReceived;
            _chatService.OnDMListUpdated -= HandleDMListUpdated;

            if (RichContentManager.HasInstance)
            {
                RichContentManager.Instance.OnLinkClicked -= HandleLinkClicked;
            }
        }

        #endregion

        #region Public Methods

        /// <summary>
        /// 런타임 빌더에서 호출하는 초기화 메서드
        /// </summary>
        public void Initialize(VirtualizedChatPanel virtualizedChatPanel, ChatInputField inputField,
            Button sendButton, Text connectionStatusText, ChatUIConfig config)
        {
            _virtualizedChatPanel = virtualizedChatPanel;
            _inputField = inputField;
            _sendButton = sendButton;
            _connectionStatusText = connectionStatusText;
            _uiConfig = config;

            SetupUIComponents();
        }

        /// <summary>
        /// IChatService를 주입합니다.
        /// ChatServiceBase 또는 IChatService 구현체를 사용할 때 호출하세요.
        /// 미호출 시 ChatManager.Instance를 자동으로 사용합니다.
        /// </summary>
        public void SetChatService(IChatService service)
        {
            // 기존 이벤트 해제
            UnsubscribeFromEvents();

            _chatService = service;

            // 새 서비스 이벤트 구독
            SubscribeToEvents();
        }

        /// <summary>
        /// 서버에 연결 (IChatService 사용)
        /// </summary>
        public async Task ConnectAsync(string host, int port)
        {
            if (_chatService == null) return;
            await _chatService.ConnectAsync(host, port);
        }

        /// <summary>
        /// ChatManager의 Inspector 설정을 사용하여 연결
        /// (ChatManager를 IChatService로 사용 중일 때만 동작)
        /// </summary>
        public async Task ConnectAsync()
        {
            if (_chatService is ChatManager chatManager)
            {
                await chatManager.ConnectAsync();
            }
            else if (_chatService != null)
            {
                Debug.LogWarning("[ChatUIManager] ConnectAsync() without parameters requires ChatManager. Use ConnectAsync(host, port) instead.");
            }
        }

        public void Disconnect()
        {
            _chatService?.Disconnect();
        }

        public async Task LoginAsync(string userId, string authToken = null)
        {
            if (_chatService == null) return;
            await _chatService.LoginAsync(userId, authToken);
        }

        /// <summary>
        /// 프로필 정보를 포함한 로그인
        /// </summary>
        public async Task LoginAsync(string userId, string authToken, string nickname,
            string profileImage = null, string frameImage = null, string extraData = null)
        {
            if (_chatService == null) return;
            await _chatService.LoginAsync(userId, authToken, nickname, profileImage, frameImage, extraData);
        }

        /// <summary>
        /// 귓속말 전송
        /// </summary>
        public async Task SendWhisperAsync(string targetUserId, string content)
        {
            if (_chatService == null) return;
            await _chatService.SendWhisperAsync(targetUserId, content);
        }

        public async Task JoinChannelAsync(string channelId, string password = null)
        {
            if (_chatService == null) return;
            await _chatService.JoinChannelAsync(channelId, password);
        }

        public async Task LeaveChannelAsync(string channelId)
        {
            if (_chatService == null) return;
            await _chatService.LeaveChannelAsync(channelId);
        }

        public async Task SendMessageAsync(string content)
        {
            if (string.IsNullOrWhiteSpace(content) || _chatService == null) return;

            string channelId = _chatService.CurrentChannelId;
            if (string.IsNullOrEmpty(channelId)) return;

            await _chatService.SendMessageAsync(channelId, content);
            PlaySound(_uiConfig?.MessageSentSound);
        }

        public void AddSystemMessage(string content)
        {
            var message = new ChannelMessage
            {
                MessageId = Guid.NewGuid().ToString(),
                ChannelId = CurrentChannelId ?? "system",
                SenderId = "SYSTEM",
                SenderNickname = "System",
                Content = content,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                MessageType = 1 // System
            };

            AddMessageToHistory(message);
            AddMessageToPanel(message);
        }

        public void ClearMessages()
        {
            _messageHistory.Clear();
            _virtualizedChatPanel?.Clear();
        }

        /// <summary>
        /// DM 대화 시작
        /// </summary>
        public async Task<DMConversation> StartDMAsync(string targetUserId)
        {
            if (_chatService == null) return null;
            return await _chatService.StartDMAsync(targetUserId);
        }

        /// <summary>
        /// DM 대화 목록 조회
        /// </summary>
        public async Task<List<DMConversation>> GetDMListAsync(int limit = 50)
        {
            if (_chatService == null) return new List<DMConversation>();
            return await _chatService.GetDMListAsync(limit);
        }

        /// <summary>
        /// DM 메시지 전송
        /// </summary>
        public async Task SendDMMessageAsync(string dmChannelId, string content, int messageType = 0)
        {
            if (_chatService == null) return;
            await _chatService.SendDMMessageAsync(dmChannelId, content, messageType);
        }

        /// <summary>
        /// DM 읽음 확인
        /// </summary>
        public async Task MarkDMReadAsync(string dmChannelId, string lastMessageId)
        {
            if (_chatService == null) return;
            await _chatService.MarkDMReadAsync(dmChannelId, lastMessageId);
        }

        /// <summary>
        /// DM 히스토리 조회
        /// </summary>
        public async Task<List<ChannelMessage>> LoadDMHistoryAsync(string dmChannelId, long beforeTimestamp = 0, int limit = 30)
        {
            if (_chatService == null) return new List<ChannelMessage>();
            return await _chatService.LoadDMHistoryAsync(dmChannelId, beforeTimestamp, limit);
        }

        /// <summary>
        /// DM 삭제
        /// </summary>
        public async Task DeleteDMAsync(string dmChannelId)
        {
            if (_chatService == null) return;
            await _chatService.DeleteDMAsync(dmChannelId);
        }

        #endregion

        #region UI Event Handlers

        private void OnSendButtonClicked()
        {
            if (_inputField == null) return;

            string content = _inputField.Text;
            if (!string.IsNullOrWhiteSpace(content))
            {
                _ = SendMessageAsync(content);
                _inputField.Clear();
                _inputField.Focus();
            }
        }

        private void OnConnectButtonClicked()
        {
            if (IsConnected)
            {
                Disconnect();
            }
            else
            {
                _ = ConnectAsync();
            }
        }

        private void OnInputSubmit(string content)
        {
            if (!string.IsNullOrWhiteSpace(content))
            {
                _ = SendMessageAsync(content);
                _inputField?.Clear();
            }
        }

        #endregion

        #region Event Handlers

        private void HandleConnected()
        {
            UpdateConnectionStatus();
            OnConnectedEvent?.Invoke();
        }

        private void HandleDisconnected(string reason)
        {
            UpdateConnectionStatus();
            AddSystemMessage($"Disconnected: {reason}");
            OnDisconnectedEvent?.Invoke(reason);
        }

        private void HandleError(string error)
        {
            AddSystemMessage($"Error: {error}");
            PlaySound(_uiConfig?.ErrorSound);
            OnErrorEvent?.Invoke(error);
        }

        private void HandleAuthenticated(bool success, string message)
        {
            if (success)
            {
                AddSystemMessage("Successfully logged in");

                // 패널에 현재 사용자 ID 설정 (내 메시지 구분용)
                string userId = _chatService?.UserId;
                _virtualizedChatPanel?.SetCurrentUserId(userId);
            }
            else
            {
                AddSystemMessage($"Login failed: {message}");
            }

            OnAuthenticatedEvent?.Invoke(success);
        }

        private void HandleMessageReceived(ChannelMessage message)
        {
            AddMessageToHistory(message);
            AddMessageToPanel(message);
            PlaySound(_uiConfig?.MessageReceivedSound);
            OnMessageReceivedEvent?.Invoke(message);

            // 자동 번역 (설정이 켜져 있고, 내 메시지가 아닌 경우)
            if (_autoTranslateOnReceive && IsTranslationEnabled && _translationConfig.AutoTranslate)
            {
                string myUserId = _chatService?.UserId;
                if (message.SenderId != myUserId)
                {
                    _ = AutoTranslateMessageAsync(message);
                }
            }
        }

        private void HandleChannelJoined(string channelId, string channelName)
        {
            AddSystemMessage($"Joined channel: {channelName}");
            OnChannelJoinedEvent?.Invoke(channelId);
        }

        private void HandleChannelJoinedWithHistory(ChannelJoinResult result)
        {
            // 자동 히스토리 로드
            if (_autoLoadHistoryOnJoin && result.RecentMessages?.Count > 0)
            {
                _virtualizedChatPanel?.Clear();
                foreach (var msg in result.RecentMessages)
                {
                    AddMessageToHistory(msg);
                    _virtualizedChatPanel?.AddMessage(msg);
                }

                if (_autoScrollToBottom)
                {
                    _virtualizedChatPanel?.ScrollToBottom();
                }
            }

            OnChannelJoinedCompleteEvent?.Invoke(result);
        }

        private void HandleChannelLeft(string channelId)
        {
            AddSystemMessage($"Left channel: {channelId}");
            OnChannelLeftEvent?.Invoke(channelId);
        }

        private void HandleChannelListUpdated(List<ChannelInfo> channels)
        {
            _channelListPanel?.UpdateChannelList(channels);
        }

        private void HandleWhisperReceived(WhisperMessage whisper)
        {
            AddSystemMessage($"[Whisper] {whisper.SenderNickname}: {whisper.Content}");
            PlaySound(_uiConfig?.MessageReceivedSound);
            OnWhisperReceivedEvent?.Invoke(whisper);
        }

        private void HandleAnnouncementReceived(AnnouncementMessage announcement)
        {
            AddSystemMessage($"[Announcement] {announcement.Content}");
            OnAnnouncementReceivedEvent?.Invoke(announcement);
        }

        private void HandleUserActionNotificationReceived(UserActionNotificationMessage notification)
        {
            AddSystemMessage($"[Notification] {notification.Content}");
            OnUserActionNotificationEvent?.Invoke(notification);
        }

        private void HandleChatReady()
        {
            OnChatReadyEvent?.Invoke();
        }

        private void HandleLinkClicked(RichLinkData linkData)
        {
            OnLinkClickedEvent?.Invoke(linkData);
        }

        private void HandleDMStarted(DMConversation conversation)
        {
            OnDMStartedEvent?.Invoke(conversation);
        }

        private void HandleDMMessageReceived(ChannelMessage message)
        {
            OnDMMessageReceivedEvent?.Invoke(message);
        }

        private void HandleDMReadReceiptReceived(DMReadReceiptData receipt)
        {
            OnDMReadReceiptEvent?.Invoke(receipt);
        }

        private void HandleDMListUpdated(List<DMConversation> conversations)
        {
            OnDMListUpdatedEvent?.Invoke(conversations);
        }

        #endregion

        #region Helper Methods

        private void AddMessageToPanel(ChannelMessage message)
        {
            _virtualizedChatPanel?.AddMessage(message);
        }

        private void AddMessageToHistory(ChannelMessage message)
        {
            _messageHistory.Add(message);

            // Limit history size
            int maxSize = _uiConfig?.MessageHistorySize ?? 500;
            while (_messageHistory.Count > maxSize)
            {
                _messageHistory.RemoveAt(0);
            }
        }

        private void UpdateConnectionStatus()
        {
            if (_connectionStatusText != null)
            {
                _connectionStatusText.text = IsConnected ? "Connected" : "Disconnected";
                _connectionStatusText.color = IsConnected
                    ? (_uiConfig?.ConnectedColor ?? Color.green)
                    : (_uiConfig?.DisconnectedColor ?? Color.red);
            }

            if (_connectButton != null)
            {
                var buttonText = _connectButton.GetComponentInChildren<Text>();
                if (buttonText != null)
                {
                    buttonText.text = IsConnected ? "Disconnect" : "Connect";
                }
            }
        }

        private void PlaySound(AudioClip clip)
        {
            if (_audioSource != null && clip != null && _uiConfig != null && _uiConfig.EnableSoundEffects)
            {
                _audioSource.PlayOneShot(clip);
            }
        }

        private async Task AutoTranslateMessageAsync(ChannelMessage message)
        {
            if (string.IsNullOrEmpty(message.Content)) return;

            try
            {
                var result = await TranslationManager.Instance.TranslateToMyLanguageAsync(message.Content, null);
                if (result.Success && result.TranslatedText != message.Content)
                {
                    Debug.Log($"[ChatUIManager] Auto-translated message from {message.SenderNickname}: {result.TranslatedText}");
                    // Note: 실제 UI 업데이트는 TranslatableMessage 컴포넌트에서 처리
                }
            }
            catch (Exception ex)
            {
                Debug.LogWarning($"[ChatUIManager] Auto-translate failed: {ex.Message}");
            }
        }

        #endregion

        #region Translation Public Methods

        /// <summary>
        /// 메시지를 수동으로 번역 요청
        /// </summary>
        public async Task<TranslationResult> TranslateMessageAsync(string content, string sourceLang = null)
        {
            if (!IsTranslationEnabled)
            {
                return TranslationResult.Error("Translation is not enabled");
            }

            return await TranslationManager.Instance.TranslateToMyLanguageAsync(content, sourceLang);
        }

        /// <summary>
        /// 지원 언어 목록 조회
        /// </summary>
        public async Task<string[]> GetSupportedLanguagesAsync()
        {
            if (!IsTranslationEnabled)
            {
                return new[] { "ko", "en", "zh", "ja" };
            }

            return await TranslationManager.Instance.GetSupportedLanguagesAsync();
        }

        #endregion
    }
}
