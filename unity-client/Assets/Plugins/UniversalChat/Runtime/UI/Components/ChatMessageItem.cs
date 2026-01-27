using System;
using UnityEngine;
using UnityEngine.UI;
using UniversalChat.Core;
using UniversalChat.Translation;

namespace UniversalChat.UI
{
    /// <summary>
    /// 개별 채팅 메시지를 표시하는 UI 아이템
    /// </summary>
    public class ChatMessageItem : MonoBehaviour
    {
        #region Inspector

        [Header("References")]
        [SerializeField] private Text _nicknameText;
        [SerializeField] private Text _contentText;
        [SerializeField] private Text _timestampText;
        [SerializeField] private Image _bubbleBackground;
        [SerializeField] private RectTransform _bubbleRect;

        #endregion

        #region Properties

        public string MessageId { get; private set; }
        public string SenderId { get; private set; }
        public TranslatableMessage TranslatableMessage { get; private set; }

        #endregion

        #region Public Methods

        /// <summary>
        /// 런타임 빌더에서 호출하는 초기화 메서드
        /// </summary>
        public void Initialize(Text nicknameText, Text contentText, Text timestampText,
            Image bubbleBackground, RectTransform bubbleRect)
        {
            _nicknameText = nicknameText;
            _contentText = contentText;
            _timestampText = timestampText;
            _bubbleBackground = bubbleBackground;
            _bubbleRect = bubbleRect;
        }

        public void Setup(ChannelMessage message, ChatUIConfig config, string currentUserId = null)
        {
            Setup(message, config, currentUserId, enableTranslation: false, sourceLang: null);
        }

        public void Setup(ChannelMessage message, ChatUIConfig config, string currentUserId, bool enableTranslation, string sourceLang = null)
        {
            MessageId = message.MessageId;
            SenderId = message.SenderId;

            bool isMyMessage = !string.IsNullOrEmpty(currentUserId) &&
                               message.SenderId == currentUserId;

            // Set nickname
            if (_nicknameText != null)
            {
                _nicknameText.text = message.SenderNickname ?? message.SenderId;

                if (config != null)
                {
                    _nicknameText.fontSize = config.NicknameFontSize;
                    _nicknameText.color = config.NicknameColor;
                }
            }

            // Set content
            if (_contentText != null)
            {
                _contentText.text = message.Content;

                if (config != null)
                {
                    _contentText.fontSize = config.MessageFontSize;
                    _contentText.color = GetMessageColor(message, config, isMyMessage);
                }
            }

            // Set timestamp
            if (_timestampText != null)
            {
                var timestamp = DateTimeOffset.FromUnixTimeMilliseconds(message.Timestamp).LocalDateTime;
                _timestampText.text = timestamp.ToString("HH:mm");

                bool showTimestamp = config?.ShowTimestamps ?? true;
                _timestampText.gameObject.SetActive(showTimestamp);

                if (config != null)
                {
                    _timestampText.fontSize = config.TimestampFontSize;
                    _timestampText.color = config.TimestampColor;
                }
            }

            // Set bubble background
            if (_bubbleBackground != null && config != null)
            {
                _bubbleBackground.color = GetBubbleColor(message, config, isMyMessage);
            }

            // Setup translation (for non-system messages, not my message)
            if (enableTranslation && !isMyMessage && message.MessageType != 1 && message.SenderId != "SYSTEM")
            {
                SetupTranslation(message.Content, sourceLang);
            }
        }

        /// <summary>
        /// 번역 기능 설정
        /// </summary>
        private void SetupTranslation(string content, string sourceLang)
        {
            TranslatableMessage = GetComponent<TranslatableMessage>();
            if (TranslatableMessage == null)
            {
                TranslatableMessage = gameObject.AddComponent<TranslatableMessage>();
            }

            TranslatableMessage.Initialize(content, sourceLang);
        }

        /// <summary>
        /// 메시지 번역 요청
        /// </summary>
        public void Translate()
        {
            if (TranslatableMessage != null)
            {
                TranslatableMessage.Translate();
            }
        }

        /// <summary>
        /// 원본/번역 토글
        /// </summary>
        public void ToggleTranslation()
        {
            if (TranslatableMessage != null)
            {
                TranslatableMessage.ToggleTranslation();
            }
        }

        #endregion

        #region Helper Methods

        private Color GetMessageColor(ChannelMessage message, ChatUIConfig config, bool isMyMessage)
        {
            if (config == null) return Color.white;

            // System message
            if (message.MessageType == 1 || message.SenderId == "SYSTEM")
            {
                return config.SystemTextColor;
            }

            return config.MessageColor;
        }

        private Color GetBubbleColor(ChannelMessage message, ChatUIConfig config, bool isMyMessage)
        {
            if (config == null) return Color.gray;

            // System message
            if (message.MessageType == 1 || message.SenderId == "SYSTEM")
            {
                return config.SystemBubbleColor;
            }

            // My message vs others
            return isMyMessage ? config.MyBubbleColor : config.OtherBubbleColor;
        }

        #endregion
    }
}
