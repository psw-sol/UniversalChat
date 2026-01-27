using System;
using UnityEngine;
using UnityEngine.UI;

namespace UniversalChat.RichContent
{
    /// <summary>
    /// 채팅 입력창 옆에 배치되는 링크 입력 도우미
    /// 버튼 클릭 시 링크 타입 선택 이벤트 발생
    /// </summary>
    public class RichLinkInputHelper : MonoBehaviour
    {
        [SerializeField] private Button _linkButton;
        [SerializeField] private Image _buttonIcon;
        [SerializeField] private InputField _targetInputField;

        // TMP InputField 지원
        [SerializeField] private TMPro.TMP_InputField _targetTMPInputField;

        /// <summary>
        /// 링크 버튼 클릭 이벤트
        /// 게임에서 구독하여 링크 타입 선택 UI 표시 후 InsertLink 호출
        /// </summary>
        public event Action<RichLinkInputHelper> OnLinkButtonClicked;

        /// <summary>
        /// 연결된 InputField
        /// </summary>
        public InputField TargetInputField => _targetInputField;

        /// <summary>
        /// 연결된 TMP InputField
        /// </summary>
        public TMPro.TMP_InputField TargetTMPInputField => _targetTMPInputField;

        private void Awake()
        {
            if (_linkButton != null)
            {
                _linkButton.onClick.AddListener(OnButtonClick);
            }
        }

        private void OnDestroy()
        {
            if (_linkButton != null)
            {
                _linkButton.onClick.RemoveListener(OnButtonClick);
            }
        }

        #region Public API

        /// <summary>
        /// 초기화
        /// </summary>
        public void Initialize(Button linkButton, InputField inputField)
        {
            _linkButton = linkButton;
            _targetInputField = inputField;

            if (_linkButton != null)
            {
                _linkButton.onClick.RemoveListener(OnButtonClick);
                _linkButton.onClick.AddListener(OnButtonClick);
            }
        }

        /// <summary>
        /// TMP InputField로 초기화
        /// </summary>
        public void Initialize(Button linkButton, TMPro.TMP_InputField inputField)
        {
            _linkButton = linkButton;
            _targetTMPInputField = inputField;

            if (_linkButton != null)
            {
                _linkButton.onClick.RemoveListener(OnButtonClick);
                _linkButton.onClick.AddListener(OnButtonClick);
            }
        }

        /// <summary>
        /// 링크 태그 삽입 (일반 InputField용)
        /// </summary>
        /// <param name="linkType">링크 타입</param>
        /// <param name="parameters">파라미터들</param>
        public void InsertLink(string linkType, params string[] parameters)
        {
            string tag = RichTextParser.CreateTag(linkType, parameters);

            if (_targetInputField != null)
            {
                InsertTextToInputField(_targetInputField, tag);
            }
            else if (_targetTMPInputField != null)
            {
                InsertTextToTMPInputField(_targetTMPInputField, tag);
            }
        }

        /// <summary>
        /// 아이템 링크 삽입
        /// </summary>
        public void InsertItemLink(string itemId, int enhancement = 0)
        {
            string tag = RichTextParser.CreateItemTag(itemId, enhancement);

            if (_targetInputField != null)
            {
                InsertTextToInputField(_targetInputField, tag);
            }
            else if (_targetTMPInputField != null)
            {
                InsertTextToTMPInputField(_targetTMPInputField, tag);
            }
        }

        /// <summary>
        /// 유저 링크 삽입
        /// </summary>
        public void InsertUserLink(string oderId, string displayName)
        {
            string tag = RichTextParser.CreateUserTag(oderId, displayName);

            if (_targetInputField != null)
            {
                InsertTextToInputField(_targetInputField, tag);
            }
            else if (_targetTMPInputField != null)
            {
                InsertTextToTMPInputField(_targetTMPInputField, tag);
            }
        }

        /// <summary>
        /// 버튼 활성화/비활성화
        /// </summary>
        public void SetEnabled(bool enabled)
        {
            if (_linkButton != null)
            {
                _linkButton.interactable = enabled;
            }
        }

        /// <summary>
        /// 버튼 표시/숨김
        /// </summary>
        public void SetVisible(bool visible)
        {
            gameObject.SetActive(visible);
        }

        /// <summary>
        /// 아이콘 설정
        /// </summary>
        public void SetIcon(Sprite icon)
        {
            if (_buttonIcon != null)
            {
                _buttonIcon.sprite = icon;
            }
        }

        #endregion

        #region Internal Methods

        private void OnButtonClick()
        {
            OnLinkButtonClicked?.Invoke(this);
        }

        private void InsertTextToInputField(InputField inputField, string text)
        {
            if (inputField == null || string.IsNullOrEmpty(text))
                return;

            string currentText = inputField.text ?? string.Empty;
            int caretPos = inputField.caretPosition;

            // 캐럿 위치가 유효하지 않으면 끝에 추가
            if (caretPos < 0 || caretPos > currentText.Length)
            {
                caretPos = currentText.Length;
            }

            // 텍스트 삽입
            inputField.text = currentText.Insert(caretPos, text);

            // 캐럿 위치 업데이트
            inputField.caretPosition = caretPos + text.Length;

            // 포커스 유지
            inputField.ActivateInputField();
            inputField.Select();
        }

        private void InsertTextToTMPInputField(TMPro.TMP_InputField inputField, string text)
        {
            if (inputField == null || string.IsNullOrEmpty(text))
                return;

            string currentText = inputField.text ?? string.Empty;
            int caretPos = inputField.caretPosition;

            if (caretPos < 0 || caretPos > currentText.Length)
            {
                caretPos = currentText.Length;
            }

            inputField.text = currentText.Insert(caretPos, text);
            inputField.caretPosition = caretPos + text.Length;

            inputField.ActivateInputField();
            inputField.Select();
        }

        #endregion
    }
}
