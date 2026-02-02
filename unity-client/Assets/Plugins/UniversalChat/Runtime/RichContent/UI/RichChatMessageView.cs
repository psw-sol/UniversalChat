using UnityEngine;
using UnityEngine.UI;
using TMPro;
using UniversalChat.Core;
using UniversalChat.UI;

namespace UniversalChat.RichContent
{
    /// <summary>
    /// 가상화 스크롤용 Rich Content 메시지 뷰
    /// VirtualizedChatPanel에서 사용 (재사용 가능한 뷰)
    /// </summary>
    public class RichChatMessageView : MonoBehaviour
    {
        [Header("Components")]
        [SerializeField] private RichChatText _nicknameText;
        [SerializeField] private RichChatText _messageText;
        [SerializeField] private TextMeshProUGUI _timestampText;
        [SerializeField] private Image _bubbleBackground;
        [SerializeField] private LayoutElement _layoutElement;
        [SerializeField] private HorizontalLayoutGroup _rootLayout;

        // 현재 바인딩된 데이터 인덱스 (가상화용)
        private int _dataIndex = -1;

        // 현재 메시지 정보
        private string _currentUserId;
        private bool _isMyMessage;
        private bool _isSystemMessage;

        // UI 설정 캐시
        private ChatUIConfig _chatConfig;

        #region Properties

        /// <summary>
        /// 현재 바인딩된 데이터 인덱스
        /// </summary>
        public int DataIndex => _dataIndex;

        /// <summary>
        /// 유저 ID
        /// </summary>
        public string UserId => _currentUserId;

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
            HorizontalLayoutGroup rootLayout = null)
        {
            _nicknameText = nicknameText;
            _messageText = messageText;
            _timestampText = timestampText;
            _bubbleBackground = bubbleBackground;
            _layoutElement = layoutElement;
            _rootLayout = rootLayout;
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
        /// 데이터 설정 (가상화 스크롤에서 호출)
        /// </summary>
        /// <param name="dataIndex">데이터 인덱스</param>
        /// <param name="oderId">유저 ID</param>
        /// <param name="nickname">닉네임</param>
        /// <param name="message">메시지</param>
        /// <param name="timestamp">타임스탬프</param>
        /// <param name="isMyMessage">내 메시지 여부</param>
        /// <param name="isSystem">시스템 메시지 여부</param>
        public void SetData(
            int dataIndex,
            string oderId,
            string nickname,
            string message,
            string timestamp,
            bool isMyMessage,
            bool isSystem)
        {
            _dataIndex = dataIndex;
            _currentUserId = oderId;
            _isMyMessage = isMyMessage;
            _isSystemMessage = isSystem;

            // 닉네임 설정
            SetNicknameText(nickname, oderId, isSystem);

            // 메시지 설정
            if (_messageText != null)
            {
                _messageText.SetRawText(message);
            }

            // 타임스탬프
            if (_timestampText != null)
            {
                _timestampText.text = timestamp;
            }

            // 버블 색상
            UpdateBubbleColor();

            // 정렬 (내 메시지: 우측, 다른 사람: 좌측)
            UpdateAlignment();
        }

        /// <summary>
        /// 메시지 정렬 업데이트 (내 메시지: 우측, 다른 사람: 좌측)
        /// </summary>
        private void UpdateAlignment()
        {
            if (_rootLayout != null)
            {
                _rootLayout.childAlignment = _isMyMessage
                    ? TextAnchor.UpperRight
                    : TextAnchor.UpperLeft;
            }
        }

        /// <summary>
        /// ChannelMessage로 데이터 설정
        /// </summary>
        public void SetData(int dataIndex, ChannelMessage channelMessage, string myUserId = null)
        {
            bool isMyMsg = !string.IsNullOrEmpty(myUserId)
                && channelMessage.SenderId == myUserId;

            SetData(
                dataIndex: dataIndex,
                oderId: channelMessage.SenderId,
                nickname: channelMessage.SenderNickname,
                message: channelMessage.Content,
                timestamp: channelMessage.DateTime.ToString("HH:mm"),
                isMyMessage: isMyMsg,
                isSystem: false
            );
        }

        /// <summary>
        /// 높이 계산 (가상화 레이아웃용)
        /// </summary>
        public float GetPreferredHeight()
        {
            if (_messageText?.TextComponent != null)
            {
                var preferredHeight = _messageText.TextComponent.GetPreferredValues().y;
                return Mathf.Max(70f, preferredHeight + 50f);
            }

            return _chatConfig?.DefaultItemHeight ?? 70f;
        }

        /// <summary>
        /// 뷰 재사용 전 초기화
        /// </summary>
        public void ResetView()
        {
            _dataIndex = -1;
            _currentUserId = null;
            _isMyMessage = false;
            _isSystemMessage = false;

            if (_nicknameText != null)
                _nicknameText.SetRawText(string.Empty);

            if (_messageText != null)
                _messageText.SetRawText(string.Empty);

            if (_timestampText != null)
                _timestampText.text = string.Empty;
        }

        #endregion

        #region Internal Methods

        private void SetNicknameText(string nickname, string oderId, bool isSystem)
        {
            if (_nicknameText == null)
                return;

            if (!isSystem && !string.IsNullOrEmpty(oderId))
            {
                string userTag = RichTextParser.CreateUserTag(oderId, nickname);
                _nicknameText.SetRawText(userTag);
            }
            else
            {
                _nicknameText.SetRawText(nickname ?? "System");
            }
        }

        private void UpdateBubbleColor()
        {
            if (_bubbleBackground == null)
                return;

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
            var uiManager = FindFirstObjectByType<ChatUIManager>();
            return uiManager?.GetConfig();
        }

        #endregion
    }
}
