using UnityEngine;
using UnityEngine.UI;
using TMPro;
using UniversalChat.Core;
using UniversalChat.UI;

namespace UniversalChat.RichContent
{
    /// <summary>
    /// Rich Content 지원 채팅 메시지 아이템
    /// 닉네임 클릭 (유저 정보) + 메시지 내 링크 (아이템 등) 지원
    /// </summary>
    public class RichChatMessageItem : MonoBehaviour
    {
        [Header("Components")]
        [SerializeField] private RichChatText _nicknameText;
        [SerializeField] private RichChatText _messageText;
        [SerializeField] private TextMeshProUGUI _timestampText;
        [SerializeField] private Image _bubbleBackground;
        [SerializeField] private LayoutElement _layoutElement;
        [SerializeField] private RectTransform _bubbleRect;

        // 현재 메시지 데이터
        private string _currentUserId;
        private string _currentNickname;
        private bool _isMyMessage;
        private bool _isSystemMessage;

        // UI 설정 캐시
        private ChatUIConfig _chatConfig;

        #region Properties

        /// <summary>
        /// 현재 유저 ID
        /// </summary>
        public string UserId => _currentUserId;

        /// <summary>
        /// 현재 닉네임
        /// </summary>
        public string Nickname => _currentNickname;

        /// <summary>
        /// 내 메시지 여부
        /// </summary>
        public bool IsMyMessage => _isMyMessage;

        /// <summary>
        /// 시스템 메시지 여부
        /// </summary>
        public bool IsSystemMessage => _isSystemMessage;

        #endregion

        #region Initialization

        /// <summary>
        /// 초기화 (ChatUIBuilder에서 호출)
        /// </summary>
        public void Initialize(
            RichChatText nicknameText,
            RichChatText messageText,
            TextMeshProUGUI timestampText,
            Image bubbleBackground,
            LayoutElement layoutElement,
            RectTransform bubbleRect = null)
        {
            _nicknameText = nicknameText;
            _messageText = messageText;
            _timestampText = timestampText;
            _bubbleBackground = bubbleBackground;
            _layoutElement = layoutElement;
            _bubbleRect = bubbleRect;
        }

        /// <summary>
        /// ChatUIConfig 설정
        /// </summary>
        public void SetChatConfig(ChatUIConfig config)
        {
            _chatConfig = config;
        }

        #endregion

        #region Public API

        /// <summary>
        /// 메시지 데이터 설정
        /// </summary>
        /// <param name="nickname">닉네임</param>
        /// <param name="message">메시지 내용</param>
        /// <param name="timestamp">타임스탬프</param>
        /// <param name="oderId">유저 ID (선택)</param>
        /// <param name="isMyMessage">내 메시지 여부</param>
        /// <param name="isSystem">시스템 메시지 여부</param>
        public void SetData(
            string nickname,
            string message,
            string timestamp,
            string oderId = null,
            bool isMyMessage = false,
            bool isSystem = false)
        {
            _currentUserId = oderId;
            _currentNickname = nickname;
            _isMyMessage = isMyMessage;
            _isSystemMessage = isSystem;

            // 닉네임 설정
            SetNicknameText(nickname, oderId, isSystem);

            // 메시지 설정 (Rich Content 자동 파싱)
            if (_messageText != null)
            {
                _messageText.SetRawText(message);
            }

            // 타임스탬프 설정
            if (_timestampText != null)
            {
                _timestampText.text = timestamp;
            }

            // 버블 색상 업데이트
            UpdateBubbleColor();
        }

        /// <summary>
        /// 시스템 메시지 설정
        /// </summary>
        public void SetSystemMessage(string message, string timestamp = null)
        {
            SetData(
                nickname: "System",
                message: message,
                timestamp: timestamp ?? System.DateTime.Now.ToString("HH:mm"),
                oderId: null,
                isMyMessage: false,
                isSystem: true
            );
        }

        /// <summary>
        /// ChannelMessage로 데이터 설정 (기존 호환성)
        /// </summary>
        public void SetData(ChannelMessage channelMessage, string myUserId = null)
        {
            bool isMyMsg = !string.IsNullOrEmpty(myUserId)
                && channelMessage.SenderId == myUserId;

            SetData(
                nickname: channelMessage.SenderNickname,
                message: channelMessage.Content,
                timestamp: channelMessage.DateTime.ToString("HH:mm"),
                oderId: channelMessage.SenderId,
                isMyMessage: isMyMsg,
                isSystem: false
            );
        }

        /// <summary>
        /// 높이 가져오기 (레이아웃용)
        /// </summary>
        public float GetPreferredHeight()
        {
            if (_layoutElement != null)
            {
                return _layoutElement.preferredHeight;
            }

            // TMP 기반 높이 계산
            if (_messageText?.TextComponent != null)
            {
                var preferredHeight = _messageText.TextComponent.GetPreferredValues().y;
                return Mathf.Max(70f, preferredHeight + 50f); // 여백 포함
            }

            return 70f; // 기본값
        }

        #endregion

        #region Internal Methods

        private void SetNicknameText(string nickname, string oderId, bool isSystem)
        {
            if (_nicknameText == null)
                return;

            if (!isSystem && !string.IsNullOrEmpty(oderId))
            {
                // 유저 링크 태그로 변환 (클릭 가능)
                string userTag = RichTextParser.CreateUserTag(oderId, nickname);
                _nicknameText.SetRawText(userTag);
            }
            else
            {
                // 일반 텍스트
                _nicknameText.SetRawText(nickname);
            }
        }

        private void UpdateBubbleColor()
        {
            if (_bubbleBackground == null)
                return;

            // Config 가져오기
            var config = _chatConfig ?? GetChatUIConfig();

            if (_isSystemMessage)
            {
                _bubbleBackground.color = config?.SystemBubbleColor
                    ?? new Color(0.3f, 0.3f, 0.2f, 1f);
            }
            else if (_isMyMessage)
            {
                _bubbleBackground.color = config?.MyBubbleColor
                    ?? new Color(0.2f, 0.4f, 0.6f, 1f);
            }
            else
            {
                _bubbleBackground.color = config?.OtherBubbleColor
                    ?? new Color(0.25f, 0.25f, 0.25f, 1f);
            }
        }

        private ChatUIConfig GetChatUIConfig()
        {
            if (_chatConfig != null)
                return _chatConfig;

            // ChatUIManager에서 찾기
            var uiManager = FindFirstObjectByType<ChatUIManager>();
            return uiManager?.GetConfig();
        }

        #endregion
    }
}
