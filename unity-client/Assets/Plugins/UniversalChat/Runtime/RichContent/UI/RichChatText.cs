using UnityEngine;
using UnityEngine.EventSystems;
using TMPro;

namespace UniversalChat.RichContent
{
    /// <summary>
    /// 링크 클릭 감지가 가능한 TextMeshPro 컴포넌트
    /// Rich Content 태그를 자동으로 파싱하여 클릭 가능한 링크로 변환
    /// </summary>
    [RequireComponent(typeof(TextMeshProUGUI))]
    public class RichChatText : MonoBehaviour, IPointerClickHandler, IPointerDownHandler, IPointerUpHandler
    {
        private TextMeshProUGUI _textComponent;
        private Camera _uiCamera;
        private Canvas _rootCanvas;

        // 롱프레스 감지용
        private float _pressStartTime;
        private bool _isPressed;
        private string _pressedLinkId;
        private Vector2 _pressPosition;
        private bool _longPressTriggered;

        // 원본 텍스트 (Rich Content 태그 포함)
        private string _rawText;

        /// <summary>
        /// 원본 텍스트 (태그 포함)
        /// </summary>
        public string RawText => _rawText;

        /// <summary>
        /// TextMeshPro 컴포넌트
        /// </summary>
        public TextMeshProUGUI TextComponent => _textComponent;

        private RichContentConfig Config => RichContentManager.HasInstance
            ? RichContentManager.Instance.Config
            : null;

        #region Unity Lifecycle

        private void Awake()
        {
            _textComponent = GetComponent<TextMeshProUGUI>();

            // Canvas 찾기
            _rootCanvas = GetComponentInParent<Canvas>();
            if (_rootCanvas != null)
            {
                // Screen Space - Overlay가 아닌 경우 카메라 필요
                if (_rootCanvas.renderMode != RenderMode.ScreenSpaceOverlay)
                {
                    _uiCamera = _rootCanvas.worldCamera;
                }
            }
        }

        private void Update()
        {
            // 롱프레스 체크 (Update에서 처리하여 정확한 타이밍 보장)
            if (_isPressed && !_longPressTriggered && !string.IsNullOrEmpty(_pressedLinkId))
            {
                float threshold = Config?.LongPressThreshold ?? 0.5f;
                if (Time.unscaledTime - _pressStartTime >= threshold)
                {
                    _longPressTriggered = true;
                    TriggerLongPress();
                }
            }
        }

        #endregion

        #region Public API

        /// <summary>
        /// 원본 텍스트 설정 (자동으로 Rich Text 변환)
        /// </summary>
        /// <param name="rawText">원본 텍스트 (Rich Content 태그 포함 가능)</param>
        public void SetRawText(string rawText)
        {
            _rawText = rawText;

            if (_textComponent == null)
                _textComponent = GetComponent<TextMeshProUGUI>();

            if (RichContentManager.HasInstance)
            {
                _textComponent.text = RichContentManager.Instance.ProcessMessage(rawText);
            }
            else
            {
                // RichContentManager가 없으면 원본 그대로 표시
                _textComponent.text = rawText;
            }
        }

        /// <summary>
        /// TMP 텍스트 직접 설정 (변환 없이)
        /// </summary>
        public void SetProcessedText(string processedText)
        {
            _rawText = null; // 원본 텍스트 없음
            if (_textComponent != null)
            {
                _textComponent.text = processedText;
            }
        }

        /// <summary>
        /// 텍스트 색상 설정
        /// </summary>
        public void SetColor(Color color)
        {
            if (_textComponent != null)
            {
                _textComponent.color = color;
            }
        }

        #endregion

        #region Pointer Event Handlers

        public void OnPointerDown(PointerEventData eventData)
        {
            _pressStartTime = Time.unscaledTime;
            _isPressed = true;
            _pressPosition = eventData.position;
            _longPressTriggered = false;

            // 링크 ID 미리 확인
            _pressedLinkId = GetLinkIdAtPosition(eventData.position);
        }

        public void OnPointerUp(PointerEventData eventData)
        {
            _isPressed = false;
        }

        public void OnPointerClick(PointerEventData eventData)
        {
            // 롱프레스가 트리거되었으면 클릭 무시
            if (_longPressTriggered)
                return;

            // 짧은 클릭만 처리
            float threshold = Config?.LongPressThreshold ?? 0.5f;
            if (Time.unscaledTime - _pressStartTime < threshold)
            {
                HandleClick(eventData.position);
            }
        }

        #endregion

        #region Internal Methods

        private void HandleClick(Vector2 screenPosition)
        {
            string linkId = GetLinkIdAtPosition(screenPosition);

            if (!string.IsNullOrEmpty(linkId))
            {
                RichContentManager.Instance?.HandleLinkClick(linkId);
            }
        }

        private void TriggerLongPress()
        {
            if (!string.IsNullOrEmpty(_pressedLinkId))
            {
                RichContentManager.Instance?.HandleLinkLongPress(_pressedLinkId, _pressPosition);
            }
        }

        private string GetLinkIdAtPosition(Vector2 screenPosition)
        {
            if (_textComponent == null)
                return null;

            // TMP에서 링크 찾기
            int linkIndex = TMP_TextUtilities.FindIntersectingLink(
                _textComponent, screenPosition, _uiCamera);

            if (linkIndex >= 0 && linkIndex < _textComponent.textInfo.linkCount)
            {
                return _textComponent.textInfo.linkInfo[linkIndex].GetLinkID();
            }

            return null;
        }

        #endregion
    }
}
