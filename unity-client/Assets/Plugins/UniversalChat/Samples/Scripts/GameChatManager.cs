using System.Threading.Tasks;
using UnityEngine;
using UniversalChat.Core;
using UniversalChat.UI;

namespace UniversalChat.Game
{
    /// <summary>
    /// 게임 전용 채팅 매니저 (ChatServiceBase 기반)
    ///
    /// ChatServiceBase가 제공하는 내장 기능:
    /// - ChatClient 이벤트 브릿징 (Proto → Domain 자동 변환)
    /// - 자동 재연결
    /// - 인증 정보 캐싱 (재연결 시 자동 재로그인)
    /// - 채널 타입별 자동 분류 및 이벤트 분배
    /// - 메시지 히스토리 캐싱 (채널별)
    /// - 멤버 목록 관리 (채널별)
    ///
    /// 이 클래스가 구현하는 것:
    /// - ClassifyChannel() → 채널 타입 분류 로직 (유일한 필수 구현)
    /// - 게임 특화 편의 메서드 (JoinWorldChannelAsync, SendWorldMessageAsync 등)
    /// - VirtualizedChatPanel 브릿지 (선택)
    ///
    /// 사용법:
    /// <code>
    /// // MonoBehaviour에서
    /// var chatService = new GameChatManager();
    /// chatUIManager.SetChatService(chatService);  // UI 연결
    ///
    /// await chatService.ConnectAndLoginAsync("localhost", 7777, "user123", null, "Player1");
    /// await chatService.JoinWorldChannelAsync();
    /// await chatService.SendWorldMessageAsync("Hello!");
    /// </code>
    /// </summary>
    public class GameChatManager : ChatServiceBase<GameChatManager.ChannelType>
    {
        /// <summary>
        /// 채널 타입
        /// </summary>
        public enum ChannelType
        {
            World,      // 월드 채널 (world_*)
            Guild,      // 길드 채널 (guild_*)
            Alliance,   // 연맹 채널 (alliance_*)
            Party,      // 파티 채널 (party_*)
            DM,         // DM 채널 (dm:*)
            Custom      // 사용자 정의
        }

        #region Constructor

        public GameChatManager(float heartbeatInterval = 30f) : base(heartbeatInterval) { }

        #endregion

        #region Abstract Implementation (유일한 필수 구현)

        /// <summary>
        /// 채널 ID로 채널 타입 결정.
        /// 게임의 채널 명명 규칙에 맞게 수정하세요.
        /// </summary>
        protected override ChannelType ClassifyChannel(string channelId)
        {
            if (string.IsNullOrEmpty(channelId))
                return ChannelType.Custom;

            if (channelId.StartsWith("world") || channelId.StartsWith("world_"))
                return ChannelType.World;
            if (channelId.StartsWith("guild_"))
                return ChannelType.Guild;
            if (channelId.StartsWith("alliance_"))
                return ChannelType.Alliance;
            if (channelId.StartsWith("party_"))
                return ChannelType.Party;
            if (channelId.StartsWith("dm:"))
                return ChannelType.DM;

            return ChannelType.Custom;
        }

        #endregion

        #region Game-Specific Convenience Methods

        /// <summary>
        /// 월드 채널 자동 배정 요청
        /// </summary>
        public async Task JoinWorldChannelAsync()
        {
            await RequestAutoAssignChannelAsync("world");
        }

        /// <summary>
        /// 길드 채널 입장
        /// </summary>
        public async Task JoinGuildChannelAsync(string guildId, string password = null)
        {
            await JoinChannelAsync($"guild_{guildId}", password);
        }

        /// <summary>
        /// 파티 채널 입장
        /// </summary>
        public async Task JoinPartyChannelAsync(string partyId, string password = null)
        {
            await JoinChannelAsync($"party_{partyId}", password);
        }

        /// <summary>
        /// 월드 채널에 메시지 전송
        /// </summary>
        public async Task SendWorldMessageAsync(string content)
        {
            await SendMessageToChannelTypeAsync(ChannelType.World, content);
        }

        /// <summary>
        /// 길드 채널에 메시지 전송
        /// </summary>
        public async Task SendGuildMessageAsync(string content)
        {
            await SendMessageToChannelTypeAsync(ChannelType.Guild, content);
        }

        /// <summary>
        /// 파티 채널에 메시지 전송
        /// </summary>
        public async Task SendPartyMessageAsync(string content)
        {
            await SendMessageToChannelTypeAsync(ChannelType.Party, content);
        }

        /// <summary>
        /// 길드 채널 퇴장
        /// </summary>
        public async Task LeaveGuildChannelAsync()
        {
            var channelId = GetCurrentChannelId(ChannelType.Guild);
            if (!string.IsNullOrEmpty(channelId))
                await LeaveChannelAsync(channelId);
        }

        /// <summary>
        /// 파티 채널 퇴장
        /// </summary>
        public async Task LeavePartyChannelAsync()
        {
            var channelId = GetCurrentChannelId(ChannelType.Party);
            if (!string.IsNullOrEmpty(channelId))
                await LeaveChannelAsync(channelId);
        }

        /// <summary>
        /// 채널 타입별 현재 채널 ID (편의 프로퍼티)
        /// </summary>
        public string CurrentWorldChannelId => GetCurrentChannelId(ChannelType.World);
        public string CurrentGuildChannelId => GetCurrentChannelId(ChannelType.Guild);
        public string CurrentAllianceChannelId => GetCurrentChannelId(ChannelType.Alliance);
        public string CurrentPartyChannelId => GetCurrentChannelId(ChannelType.Party);

        /// <summary>
        /// 연맹 채널 입장 (서버에서 자동 생성)
        /// </summary>
        public async Task JoinAllianceChannelAsync(string allianceId, string password = null)
        {
            await JoinChannelAsync($"alliance_{allianceId}", password);
        }

        /// <summary>
        /// 연맹 채널에 메시지 전송
        /// </summary>
        public async Task SendAllianceMessageAsync(string content)
        {
            await SendMessageToChannelTypeAsync(ChannelType.Alliance, content);
        }

        /// <summary>
        /// 연맹 채널 퇴장
        /// </summary>
        public async Task LeaveAllianceChannelAsync()
        {
            var channelId = GetCurrentChannelId(ChannelType.Alliance);
            if (!string.IsNullOrEmpty(channelId))
                await LeaveChannelAsync(channelId);
        }

        /// <summary>
        /// DM 대화 시작 (편의 메서드)
        /// </summary>
        public Task<DMConversation> StartDirectMessageAsync(string targetUserId)
        {
            return StartDMAsync(targetUserId);
        }

        #endregion

        #region Virtual Extension Point Examples

        protected override void OnChannelJoinedInternal(ChannelType type, string channelId, ChannelJoinResult result)
        {
            Log($"[{type}] Channel joined: {channelId} (members={result.Members?.Count ?? 0}, history={result.RecentMessages?.Count ?? 0})");
        }

        protected override void OnMessageReceivedInternal(ChannelType type, ChannelMessage message)
        {
            // 게임 특화 처리 예:
            // - VIP 유저 메시지 하이라이트
            // - 금칙어 필터링
            // - 게임 아이템 링크 파싱
        }

        protected override void OnDisconnectedInternal(string reason)
        {
            // 게임 특화 처리 예:
            // - UI 상태 업데이트
            // - 재연결 대기 화면 표시
        }

        protected override void OnReconnectedInternal()
        {
            Log("Reconnected - previous channels restored");
        }

        #endregion
    }

    /// <summary>
    /// MonoBehaviour 래퍼 (씬에 배치하여 사용할 경우)
    ///
    /// Inspector에서 설정 후 ChatUIManager와 자동 연결됩니다.
    /// 코드로 직접 GameChatManager를 생성하는 경우 이 래퍼는 불필요합니다.
    /// </summary>
    public class GameChatManagerBehaviour : MonoBehaviour
    {
        [Header("Connection")]
        [SerializeField] private string _serverHost = "localhost";
        [SerializeField] private int _serverPort = 7777;

        [Header("Auto Connect")]
        [SerializeField] private bool _autoConnect = false;
        [SerializeField] private string _autoLoginUserId = "";
        [SerializeField] private string _autoLoginNickname = "";

        [Header("UI")]
        [SerializeField] private ChatUIManager _chatUIManager;

        private GameChatManager _chatService;

        public GameChatManager ChatService => _chatService;
        public bool IsConnected => _chatService?.IsConnected ?? false;

        private void Awake()
        {
            _chatService = new GameChatManager();

            // ChatUIManager와 연결
            if (_chatUIManager != null)
            {
                _chatUIManager.SetChatService(_chatService);
            }
        }

        private async void Start()
        {
            if (_autoConnect && !string.IsNullOrEmpty(_autoLoginUserId))
            {
                string nickname = string.IsNullOrEmpty(_autoLoginNickname) ? _autoLoginUserId : _autoLoginNickname;
                bool success = await _chatService.ConnectAndLoginAsync(
                    _serverHost, _serverPort, _autoLoginUserId, null, nickname);

                if (success)
                {
                    await _chatService.JoinWorldChannelAsync();
                }
            }
        }

        private void OnDestroy()
        {
            _chatService?.Dispose();
        }
    }
}
