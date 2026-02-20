using System.Collections.Generic;
using System.Threading.Tasks;
using UnityEngine;
using UnityEngine.UI;
using UniversalChat.Core;
using UniversalChat.UI;

namespace UniversalChat.Game
{
    /// <summary>
    /// 게임 탭 채팅 UI 예시 (3탭: 월드 / 연맹 / DM)
    ///
    /// 이 클래스는 **참고용 샘플**입니다.
    /// UniversalChat 패키지는 탭 UI를 하드코딩하지 않으므로,
    /// 게임마다 자유롭게 탭 개수와 구조를 결정합니다.
    ///
    /// 예시:
    /// - RentaHero: 월드 / 연맹 / 1:1 DM (3탭)
    /// - 다른 게임: 전체 / DM (2탭)
    /// - 또 다른: 월드 / 길드 / 파티 / DM / 시스템 (5탭)
    ///
    /// 사용법:
    /// 1. GameChatManagerBehaviour를 씬에 배치
    /// 2. 이 컴포넌트를 같은 GameObject 또는 UI Canvas에 배치
    /// 3. Inspector에서 탭 버튼, 패널, DM UI 요소 연결
    /// 4. GameChatManagerBehaviour의 ChatUIManager와 이 예시가 같은 IChatService를 참조하도록 설정
    /// </summary>
    public class GameChatUIExample : MonoBehaviour
    {
        #region Inspector Fields

        [Header("Chat Service")]
        [SerializeField] private GameChatManagerBehaviour _chatManagerBehaviour;
        [SerializeField] private ChatUIManager _chatUIManager;

        [Header("Tabs")]
        [Tooltip("탭 버튼 배열 (0=월드, 1=연맹, 2=DM)")]
        [SerializeField] private Button[] _tabButtons;

        [Tooltip("각 탭에 대응하는 패널 (0=월드, 1=연맹, 2=DM)")]
        [SerializeField] private GameObject[] _tabPanels;

        [Header("Tab Colors")]
        [SerializeField] private Color _activeTabColor = new Color(0.2f, 0.6f, 1f, 1f);
        [SerializeField] private Color _inactiveTabColor = new Color(0.7f, 0.7f, 0.7f, 1f);

        [Header("DM UI")]
        [Tooltip("DM 대화 목록을 표시할 컨테이너 (ScrollView Content)")]
        [SerializeField] private Transform _dmListContainer;

        [Tooltip("DM 대화 항목 프리팹 (텍스트 + 버튼)")]
        [SerializeField] private GameObject _dmConversationPrefab;

        [Tooltip("DM 메시지 표시 영역 (대화 선택 시 활성화)")]
        [SerializeField] private GameObject _dmChatPanel;

        [Tooltip("DM 대화 목록 영역 (대화 미선택 시 활성화)")]
        [SerializeField] private GameObject _dmListPanel;

        [Header("DM Badge")]
        [Tooltip("DM 탭 버튼의 읽지 않은 메시지 뱃지")]
        [SerializeField] private GameObject _dmUnreadBadge;

        [Tooltip("뱃지에 표시할 숫자 텍스트")]
        [SerializeField] private Text _dmUnreadCountText;

        #endregion

        #region State

        private int _currentTab = 0;
        private string _currentDMChannelId;
        private int _totalUnreadDMCount = 0;
        private GameChatManager _chatService;
        private readonly Dictionary<string, GameObject> _dmListItems = new Dictionary<string, GameObject>();

        #endregion

        #region Unity Lifecycle

        private void Awake()
        {
            // 탭 버튼 클릭 이벤트 등록
            if (_tabButtons != null)
            {
                for (int i = 0; i < _tabButtons.Length; i++)
                {
                    int tabIndex = i;
                    _tabButtons[i]?.onClick.AddListener(() => SwitchTab(tabIndex));
                }
            }
        }

        private void Start()
        {
            // ChatService 참조 획득
            if (_chatManagerBehaviour != null)
            {
                _chatService = _chatManagerBehaviour.ChatService;
            }

            // ChatUIManager DM 이벤트 구독
            if (_chatUIManager != null)
            {
                _chatUIManager.OnDMStartedEvent?.AddListener(OnDMStarted);
                _chatUIManager.OnDMMessageReceivedEvent?.AddListener(OnDMMessageReceived);
                _chatUIManager.OnDMReadReceiptEvent?.AddListener(OnDMReadReceiptReceived);
                _chatUIManager.OnDMListUpdatedEvent?.AddListener(OnDMListUpdated);
            }

            // 초기 탭 상태 설정
            SwitchTab(0);
            UpdateDMUnreadBadge();
        }

        private void OnDestroy()
        {
            if (_chatUIManager != null)
            {
                _chatUIManager.OnDMStartedEvent?.RemoveListener(OnDMStarted);
                _chatUIManager.OnDMMessageReceivedEvent?.RemoveListener(OnDMMessageReceived);
                _chatUIManager.OnDMReadReceiptEvent?.RemoveListener(OnDMReadReceiptReceived);
                _chatUIManager.OnDMListUpdatedEvent?.RemoveListener(OnDMListUpdated);
            }
        }

        #endregion

        #region Tab Management

        /// <summary>
        /// 탭 전환
        /// </summary>
        /// <param name="tabIndex">0=월드, 1=연맹, 2=DM</param>
        public void SwitchTab(int tabIndex)
        {
            if (tabIndex < 0 || (_tabPanels != null && tabIndex >= _tabPanels.Length))
                return;

            _currentTab = tabIndex;

            // 패널 활성화/비활성화
            if (_tabPanels != null)
            {
                for (int i = 0; i < _tabPanels.Length; i++)
                {
                    if (_tabPanels[i] != null)
                        _tabPanels[i].SetActive(i == tabIndex);
                }
            }

            // 탭 버튼 색상 업데이트
            if (_tabButtons != null)
            {
                for (int i = 0; i < _tabButtons.Length; i++)
                {
                    if (_tabButtons[i] != null)
                    {
                        var colors = _tabButtons[i].colors;
                        colors.normalColor = (i == tabIndex) ? _activeTabColor : _inactiveTabColor;
                        _tabButtons[i].colors = colors;
                    }
                }
            }

            // DM 탭 진입 시 대화 목록 갱신
            if (tabIndex == 2)
            {
                ShowDMList();
                _ = RefreshDMListAsync();
            }
        }

        #endregion

        #region DM List Management

        /// <summary>
        /// DM 대화 목록을 서버에서 갱신
        /// </summary>
        public async Task RefreshDMListAsync()
        {
            if (_chatUIManager == null) return;

            var conversations = await _chatUIManager.GetDMListAsync(50);
            if (conversations != null)
            {
                RenderDMList(conversations);
            }
        }

        /// <summary>
        /// DM 대화 목록 렌더링
        /// </summary>
        private void RenderDMList(List<DMConversation> conversations)
        {
            if (_dmListContainer == null) return;

            // 기존 항목 정리
            foreach (var item in _dmListItems.Values)
            {
                if (item != null) Destroy(item);
            }
            _dmListItems.Clear();

            _totalUnreadDMCount = 0;

            foreach (var conv in conversations)
            {
                _totalUnreadDMCount += conv.UnreadCount;
                CreateDMListItem(conv);
            }

            UpdateDMUnreadBadge();
        }

        /// <summary>
        /// DM 대화 항목 UI 생성
        /// </summary>
        private void CreateDMListItem(DMConversation conversation)
        {
            if (_dmListContainer == null || _dmConversationPrefab == null) return;

            var item = Instantiate(_dmConversationPrefab, _dmListContainer);
            item.SetActive(true);

            // 닉네임 텍스트 설정
            var nameText = item.transform.Find("NameText")?.GetComponent<Text>();
            if (nameText != null)
            {
                nameText.text = conversation.PeerNickname;
            }

            // 마지막 메시지 텍스트 설정
            var msgText = item.transform.Find("LastMessageText")?.GetComponent<Text>();
            if (msgText != null)
            {
                msgText.text = conversation.LastMessageContent ?? "";
            }

            // 읽지 않은 메시지 뱃지
            var badge = item.transform.Find("UnreadBadge")?.gameObject;
            if (badge != null)
            {
                badge.SetActive(conversation.UnreadCount > 0);
                var countText = badge.GetComponentInChildren<Text>();
                if (countText != null)
                {
                    countText.text = conversation.UnreadCount > 99 ? "99+" : conversation.UnreadCount.ToString();
                }
            }

            // 클릭 시 DM 대화 열기
            var button = item.GetComponent<Button>();
            if (button != null)
            {
                string dmChannelId = conversation.DMChannelId;
                button.onClick.AddListener(() => OnDMConversationClicked(dmChannelId));
            }

            _dmListItems[conversation.DMChannelId] = item;
        }

        /// <summary>
        /// DM 대화 클릭 시 메시지 패널 열기
        /// </summary>
        public async void OnDMConversationClicked(string dmChannelId)
        {
            _currentDMChannelId = dmChannelId;

            // 목록 → 대화 패널 전환
            if (_dmListPanel != null) _dmListPanel.SetActive(false);
            if (_dmChatPanel != null) _dmChatPanel.SetActive(true);

            // 히스토리 로드
            if (_chatUIManager != null)
            {
                var messages = await _chatUIManager.LoadDMHistoryAsync(dmChannelId, 0, 30);
                // 게임에서 메시지를 표시하는 로직 구현
                // 예: VirtualizedChatPanel이나 커스텀 UI에 메시지 추가
                Debug.Log($"[GameChatUIExample] Loaded {messages?.Count ?? 0} DM messages for {dmChannelId}");

                // 읽음 처리
                if (messages != null && messages.Count > 0)
                {
                    var lastMsg = messages[messages.Count - 1];
                    await _chatUIManager.MarkDMReadAsync(dmChannelId, lastMsg.MessageId);
                }
            }
        }

        /// <summary>
        /// DM 대화에서 목록으로 돌아가기
        /// </summary>
        public void BackToDMList()
        {
            _currentDMChannelId = null;

            if (_dmChatPanel != null) _dmChatPanel.SetActive(false);
            if (_dmListPanel != null) _dmListPanel.SetActive(true);

            _ = RefreshDMListAsync();
        }

        /// <summary>
        /// DM 목록 패널 표시
        /// </summary>
        private void ShowDMList()
        {
            if (_dmListPanel != null) _dmListPanel.SetActive(true);
            if (_dmChatPanel != null) _dmChatPanel.SetActive(false);
            _currentDMChannelId = null;
        }

        /// <summary>
        /// 새 DM 대화 시작 (유저 ID로)
        /// </summary>
        public async Task StartNewDMAsync(string targetUserId)
        {
            if (_chatService == null) return;

            var conversation = await _chatService.StartDirectMessageAsync(targetUserId);
            if (conversation != null)
            {
                // DM 탭으로 전환하고 대화 열기
                SwitchTab(2);
                OnDMConversationClicked(conversation.DMChannelId);
            }
        }

        /// <summary>
        /// 현재 DM 대화에 메시지 전송
        /// </summary>
        public async Task SendCurrentDMMessageAsync(string content)
        {
            if (_chatUIManager == null || string.IsNullOrEmpty(_currentDMChannelId)) return;

            await _chatUIManager.SendDMMessageAsync(_currentDMChannelId, content);
        }

        #endregion

        #region DM Unread Badge

        private void UpdateDMUnreadBadge()
        {
            if (_dmUnreadBadge != null)
            {
                _dmUnreadBadge.SetActive(_totalUnreadDMCount > 0);
            }

            if (_dmUnreadCountText != null)
            {
                _dmUnreadCountText.text = _totalUnreadDMCount > 99 ? "99+" : _totalUnreadDMCount.ToString();
            }
        }

        #endregion

        #region ChatUIManager Event Handlers

        private void OnDMStarted(DMConversation conversation)
        {
            // 새 DM 대화가 시작됨 → 목록 갱신
            if (_currentTab == 2)
            {
                _ = RefreshDMListAsync();
            }
        }

        private void OnDMMessageReceived(ChannelMessage message)
        {
            // DM 메시지 수신 시
            // 현재 열린 대화가 아니면 unread 증가
            if (message.ChannelId != _currentDMChannelId)
            {
                _totalUnreadDMCount++;
                UpdateDMUnreadBadge();
            }

            // 현재 DM 탭이면 목록 갱신
            if (_currentTab == 2 && _currentDMChannelId == null)
            {
                _ = RefreshDMListAsync();
            }
        }

        private void OnDMReadReceiptReceived(DMReadReceiptData receipt)
        {
            // 상대방이 읽음 확인 → UI 업데이트 (예: 체크 마크)
            Debug.Log($"[GameChatUIExample] Read receipt: {receipt.ReaderUserId} read {receipt.DMChannelId}");
        }

        private void OnDMListUpdated(List<DMConversation> conversations)
        {
            if (_currentTab == 2)
            {
                RenderDMList(conversations);
            }
        }

        #endregion
    }
}
