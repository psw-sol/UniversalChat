using System;
using UnityEngine;
using UnityEngine.UI;

namespace UniversalChat.UI
{
    /// <summary>
    /// 재활용 가능한 메시지 뷰 컴포넌트
    /// 가상화 스크롤에서 실제 UI를 담당하며, 풀에서 관리됨
    /// </summary>
    public class ChatMessageView : MonoBehaviour
    {
        #region Inspector

        [Header("UI References")]
        [SerializeField] private Text _nicknameText;
        [SerializeField] private Text _contentText;
        [SerializeField] private Text _timestampText;
        [SerializeField] private Image _bubbleBackground;
        [SerializeField] private LayoutElement _layoutElement;

        #endregion

        #region Fields

        private RectTransform _rectTransform;
        private ChatUIConfig _cachedConfig;

        #endregion

        #region Properties

        /// <summary>
        /// 현재 바인딩된 데이터 인덱스 (-1 = 미사용)
        /// </summary>
        public int DataIndex { get; private set; } = -1;

        /// <summary>
        /// 현재 바인딩된 메시지 ID
        /// </summary>
        public string MessageId { get; private set; }

        /// <summary>
        /// RectTransform 캐시
        /// </summary>
        public RectTransform RectTransform
        {
            get
            {
                if (_rectTransform == null)
                    _rectTransform = GetComponent<RectTransform>();
                return _rectTransform;
            }
        }

        #endregion

        #region Unity Lifecycle

        private void Awake()
        {
            _rectTransform = GetComponent<RectTransform>();
        }

        #endregion

        #region Public Methods

        /// <summary>
        /// 런타임 빌더에서 호출하는 초기화 메서드
        /// </summary>
        public void Initialize(Text nicknameText, Text contentText, Text timestampText,
            Image bubbleBackground, LayoutElement layoutElement)
        {
            _nicknameText = nicknameText;
            _contentText = contentText;
            _timestampText = timestampText;
            _bubbleBackground = bubbleBackground;
            _layoutElement = layoutElement;
            _rectTransform = GetComponent<RectTransform>();
        }

        /// <summary>
        /// 데이터를 뷰에 바인딩
        /// </summary>
        /// <param name="data">메시지 데이터</param>
        /// <param name="dataIndex">데이터 리스트에서의 인덱스</param>
        /// <param name="config">UI 설정</param>
        /// <param name="currentUserId">현재 사용자 ID (내 메시지 구분용)</param>
        public void Bind(ChatMessageData data, int dataIndex, ChatUIConfig config, string currentUserId)
        {
            DataIndex = dataIndex;
            MessageId = data.MessageId;
            _cachedConfig = config;

            bool isMyMessage = !string.IsNullOrEmpty(currentUserId) &&
                               data.SenderId == currentUserId;

            // 닉네임 설정
            if (_nicknameText != null)
            {
                _nicknameText.text = data.Nickname ?? data.SenderId;

                if (config != null)
                {
                    _nicknameText.fontSize = config.NicknameFontSize;
                    _nicknameText.color = config.NicknameColor;
                }
            }

            // 메시지 내용 설정
            if (_contentText != null)
            {
                _contentText.text = data.Content;

                if (config != null)
                {
                    _contentText.fontSize = config.MessageFontSize;
                    _contentText.color = GetMessageColor(data, config, isMyMessage);
                }
            }

            // 타임스탬프 설정
            if (_timestampText != null)
            {
                var timestamp = data.DateTime;
                _timestampText.text = timestamp.ToString("HH:mm");

                bool showTimestamp = config?.ShowTimestamps ?? true;
                _timestampText.gameObject.SetActive(showTimestamp);

                if (config != null)
                {
                    _timestampText.fontSize = config.TimestampFontSize;
                    _timestampText.color = config.TimestampColor;
                }
            }

            // 말풍선 배경색 설정
            if (_bubbleBackground != null && config != null)
            {
                _bubbleBackground.color = GetBubbleColor(data, config, isMyMessage);
            }

            // 높이 설정
            float height = data.CachedHeight > 0 ? data.CachedHeight : CalculateHeight(data, config);
            SetHeight(height);

            gameObject.SetActive(true);
        }

        /// <summary>
        /// 메시지 높이 계산
        /// </summary>
        public float CalculateHeight(ChatMessageData data, ChatUIConfig config)
        {
            // 이미 캐시된 높이가 있으면 반환
            if (data.CachedHeight > 0)
                return data.CachedHeight;

            // 텍스트 높이 계산을 위해 임시로 텍스트 설정
            if (_contentText != null)
            {
                _contentText.text = data.Content;

                // preferredHeight 계산을 위해 레이아웃 강제 업데이트
                Canvas.ForceUpdateCanvases();
                float textHeight = _contentText.preferredHeight;

                // 고정 요소 높이
                float nicknameHeight = (config?.NicknameFontSize ?? 12) + 4f;
                float timestampHeight = (config?.TimestampFontSize ?? 10) + 4f;
                float padding = 24f;  // 상하 패딩
                float spacing = 8f;   // 요소 간 간격

                float totalHeight = nicknameHeight + textHeight + timestampHeight + padding + spacing;

                // 높이 제한 (최소 50, 최대 400)
                float clampedHeight = Mathf.Clamp(totalHeight, 50f, 400f);

                // 캐시에 저장
                data.CachedHeight = clampedHeight;

                return clampedHeight;
            }

            // 기본 높이
            return config?.DefaultItemHeight ?? 70f;
        }

        /// <summary>
        /// Y 위치 설정 (절대 좌표)
        /// LayoutGroup을 사용하지 않고 직접 위치 제어
        /// </summary>
        public void SetPositionY(float y)
        {
            if (RectTransform != null)
            {
                var pos = RectTransform.anchoredPosition;
                pos.y = -y; // Unity UI는 위에서 아래로 음수 방향
                RectTransform.anchoredPosition = pos;
            }
        }

        /// <summary>
        /// 높이 설정
        /// </summary>
        public void SetHeight(float height)
        {
            if (_layoutElement != null)
            {
                _layoutElement.preferredHeight = height;
                _layoutElement.minHeight = height;
            }

            if (RectTransform != null)
            {
                var size = RectTransform.sizeDelta;
                size.y = height;
                RectTransform.sizeDelta = size;
            }
        }

        /// <summary>
        /// 바인딩 해제 (풀 반환 시 호출)
        /// </summary>
        public void Unbind()
        {
            DataIndex = -1;
            MessageId = null;
            gameObject.SetActive(false);
        }

        #endregion

        #region Helper Methods

        private Color GetMessageColor(ChatMessageData data, ChatUIConfig config, bool isMyMessage)
        {
            if (config == null) return Color.white;

            // 시스템 메시지
            if (data.MessageType == 1 || data.SenderId == "SYSTEM")
            {
                return config.SystemTextColor;
            }

            return config.MessageColor;
        }

        private Color GetBubbleColor(ChatMessageData data, ChatUIConfig config, bool isMyMessage)
        {
            if (config == null) return Color.gray;

            // 시스템 메시지
            if (data.MessageType == 1 || data.SenderId == "SYSTEM")
            {
                return config.SystemBubbleColor;
            }

            // 내 메시지 vs 다른 사람 메시지
            return isMyMessage ? config.MyBubbleColor : config.OtherBubbleColor;
        }

        #endregion
    }
}
