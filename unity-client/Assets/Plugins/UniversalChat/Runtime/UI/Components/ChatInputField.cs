using System;
using UnityEngine;
using UnityEngine.UI;

#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

namespace UniversalChat.UI
{
    /// <summary>
    /// 채팅 입력 필드 컴포넌트
    /// </summary>
    public class ChatInputField : MonoBehaviour
    {
        #region Inspector

        [Header("References")]
        [SerializeField] private InputField _inputField;

        [Header("Settings")]
        [SerializeField] private int _maxLength = 500;
        [SerializeField] private bool _submitOnEnter = true;
        [SerializeField] private bool _clearOnSubmit = true;

        #endregion

        #region Events

        public event Action<string> OnSubmit;
        public event Action<string> OnTextChanged;

        #endregion

        #region Properties

        public string Text
        {
            get => _inputField?.text ?? string.Empty;
            set
            {
                if (_inputField != null)
                {
                    _inputField.text = value;
                }
            }
        }

        public bool IsFocused => _inputField?.isFocused ?? false;

        #endregion

        #region Unity Lifecycle

        private void Awake()
        {
            if (_inputField == null)
            {
                _inputField = GetComponent<InputField>();
            }

            if (_inputField != null)
            {
                _inputField.characterLimit = _maxLength;
                _inputField.onValueChanged.AddListener(HandleTextChanged);
                _inputField.onEndEdit.AddListener(HandleEndEdit);
            }
        }

        private void OnDestroy()
        {
            if (_inputField != null)
            {
                _inputField.onValueChanged.RemoveListener(HandleTextChanged);
                _inputField.onEndEdit.RemoveListener(HandleEndEdit);
            }
        }

        private void Update()
        {
            if (!_submitOnEnter || !IsFocused)
                return;

            // Enter key submission (New Input System / Old Input compatible)
            if (WasEnterPressedThisFrame() && !IsShiftHeld())
            {
                Submit();
            }
        }

        #endregion

        #region Input Helpers

        private static bool WasEnterPressedThisFrame()
        {
#if ENABLE_INPUT_SYSTEM
            var kb = Keyboard.current;
            return kb != null && (kb.enterKey.wasPressedThisFrame || kb.numpadEnterKey.wasPressedThisFrame);
#else
            return Input.GetKeyDown(KeyCode.Return) || Input.GetKeyDown(KeyCode.KeypadEnter);
#endif
        }

        private static bool IsShiftHeld()
        {
#if ENABLE_INPUT_SYSTEM
            var kb = Keyboard.current;
            return kb != null && (kb.leftShiftKey.isPressed || kb.rightShiftKey.isPressed);
#else
            return Input.GetKey(KeyCode.LeftShift) || Input.GetKey(KeyCode.RightShift);
#endif
        }

        #endregion

        #region Public Methods

        /// <summary>
        /// 런타임 빌더에서 호출하는 초기화 메서드
        /// </summary>
        public void Initialize(InputField inputField)
        {
            _inputField = inputField;

            if (_inputField != null)
            {
                _inputField.characterLimit = _maxLength;
                _inputField.onValueChanged.RemoveListener(HandleTextChanged);
                _inputField.onEndEdit.RemoveListener(HandleEndEdit);
                _inputField.onValueChanged.AddListener(HandleTextChanged);
                _inputField.onEndEdit.AddListener(HandleEndEdit);
            }
        }

        public void Focus()
        {
            _inputField?.ActivateInputField();
            _inputField?.Select();
        }

        public void Clear()
        {
            if (_inputField != null)
            {
                _inputField.text = string.Empty;
            }
        }

        public void Submit()
        {
            string text = Text.Trim();

            if (!string.IsNullOrEmpty(text))
            {
                OnSubmit?.Invoke(text);

                if (_clearOnSubmit)
                {
                    Clear();
                }
            }
        }

        public void SetPlaceholder(string placeholder)
        {
            if (_inputField?.placeholder is Text placeholderText)
            {
                placeholderText.text = placeholder;
            }
        }

        #endregion

        #region Event Handlers

        private void HandleTextChanged(string text)
        {
            OnTextChanged?.Invoke(text);
        }

        private void HandleEndEdit(string text)
        {
            // Submit is handled in Update() for better control
        }

        #endregion
    }
}
