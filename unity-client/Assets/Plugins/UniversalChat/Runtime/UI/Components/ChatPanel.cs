using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;
using UniversalChat.Core;

namespace UniversalChat.UI
{
    /// <summary>
    /// 채팅 메시지를 표시하는 스크롤 가능한 패널
    /// </summary>
    public class ChatPanel : MonoBehaviour
    {
        #region Inspector

        [Header("References")]
        [SerializeField] private ScrollRect _scrollRect;
        [SerializeField] private RectTransform _content;
        [SerializeField] private ChatMessageItem _messagePrefab;

        [Header("Settings")]
        [SerializeField] private int _maxVisibleMessages = 100;
        [SerializeField] private bool _autoScroll = true;

        #endregion

        #region Fields

        private readonly List<ChatMessageItem> _messageItems = new List<ChatMessageItem>();
        private readonly Queue<ChatMessageItem> _messagePool = new Queue<ChatMessageItem>();
        private bool _isNearBottom = true;
        private ChatUIConfig _config;
        private string _currentUserId;

        #endregion

        #region Unity Lifecycle

        private void Awake()
        {
            if (_scrollRect != null)
            {
                _scrollRect.onValueChanged.AddListener(OnScrollValueChanged);
            }
        }

        private void OnDestroy()
        {
            if (_scrollRect != null)
            {
                _scrollRect.onValueChanged.RemoveListener(OnScrollValueChanged);
            }
        }

        #endregion

        #region Public Methods

        /// <summary>
        /// 런타임 빌더에서 호출하는 초기화 메서드
        /// </summary>
        public void Initialize(ScrollRect scrollRect, RectTransform content, ChatUIConfig config)
        {
            _scrollRect = scrollRect;
            _content = content;
            _config = config;

            if (config != null)
            {
                _maxVisibleMessages = config.MaxVisibleMessages;
                _autoScroll = config.AutoScrollToBottom;
            }

            // 스크롤 이벤트 등록
            if (_scrollRect != null)
            {
                _scrollRect.onValueChanged.RemoveListener(OnScrollValueChanged);
                _scrollRect.onValueChanged.AddListener(OnScrollValueChanged);
            }
        }

        /// <summary>
        /// 현재 사용자 ID 설정 (내 메시지 구분용)
        /// </summary>
        public void SetCurrentUserId(string userId)
        {
            _currentUserId = userId;
        }

        public void AddMessage(ChannelMessage message, ChatUIConfig config = null)
        {
            var effectiveConfig = config ?? _config;
            var item = GetOrCreateMessageItem(effectiveConfig);
            item.Setup(message, effectiveConfig, _currentUserId);
            item.transform.SetAsLastSibling();

            _messageItems.Add(item);

            // Limit visible messages
            while (_messageItems.Count > _maxVisibleMessages)
            {
                var oldItem = _messageItems[0];
                _messageItems.RemoveAt(0);
                ReturnToPool(oldItem);
            }

            // Auto scroll to bottom
            if (_autoScroll && _isNearBottom)
            {
                ScrollToBottom();
            }
        }

        public void ClearMessages()
        {
            foreach (var item in _messageItems)
            {
                ReturnToPool(item);
            }
            _messageItems.Clear();
        }

        public void ScrollToBottom()
        {
            Canvas.ForceUpdateCanvases();

            if (_scrollRect != null)
            {
                _scrollRect.verticalNormalizedPosition = 0f;
            }
        }

        #endregion

        #region Object Pooling

        private ChatMessageItem GetOrCreateMessageItem(ChatUIConfig config)
        {
            ChatMessageItem item;

            if (_messagePool.Count > 0)
            {
                item = _messagePool.Dequeue();
                item.gameObject.SetActive(true);
            }
            else if (_messagePrefab != null)
            {
                item = Instantiate(_messagePrefab, _content);
            }
            else
            {
                // 프리팹이 없으면 런타임 빌더로 메시지 아이템 생성
                item = ChatUIBuilder.BuildMessageItem(_content, config);
            }

            return item;
        }

        private void ReturnToPool(ChatMessageItem item)
        {
            item.gameObject.SetActive(false);
            _messagePool.Enqueue(item);
        }

        #endregion

        #region Scroll Handling

        private void OnScrollValueChanged(Vector2 position)
        {
            // Check if near bottom (within 10% of bottom)
            _isNearBottom = position.y < 0.1f;
        }

        #endregion
    }
}
