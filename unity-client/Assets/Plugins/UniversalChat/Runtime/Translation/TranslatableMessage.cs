using System;
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.EventSystems;

namespace UniversalChat.Translation
{
    /// <summary>
    /// Makes a chat message translatable with long-press or button
    /// Attach to ChatMessageItem prefab
    /// </summary>
    public class TranslatableMessage : MonoBehaviour, IPointerClickHandler, IPointerDownHandler, IPointerUpHandler
    {
        [Header("References")]
        [SerializeField] private Text _messageText;
        [SerializeField] private Button _translateButton;
        [SerializeField] private Text _translatedLabel;
        [SerializeField] private GameObject _loadingSpinner;

        [Header("Settings")]
        [SerializeField] private float _longPressTime = 0.5f;
        [SerializeField] private bool _enableLongPress = true;
        [SerializeField] private Color _translatedLabelColor = new Color(0.5f, 0.8f, 0.5f);

        private string _originalText;
        private string _translatedText;
        private string _sourceLang;
        private bool _isTranslated;
        private bool _showingTranslated;
        private bool _isLoading;
        private bool _isPointerDown;
        private float _pointerDownTime;

        /// <summary>
        /// Initialize with message data
        /// </summary>
        public void Initialize(string text, string sourceLang = null)
        {
            _originalText = text;
            _sourceLang = sourceLang ?? DetectLanguage(text);
            _translatedText = null;
            _isTranslated = false;
            _showingTranslated = false;

            if (_messageText != null)
            {
                _messageText.text = text;
            }

            UpdateUI();
        }

        private void Update()
        {
            // Long press detection
            if (_enableLongPress && _isPointerDown && !_isLoading)
            {
                if (Time.time - _pointerDownTime >= _longPressTime)
                {
                    _isPointerDown = false;
                    OnLongPress();
                }
            }
        }

        public void OnPointerDown(PointerEventData eventData)
        {
            _isPointerDown = true;
            _pointerDownTime = Time.time;
        }

        public void OnPointerUp(PointerEventData eventData)
        {
            _isPointerDown = false;
        }

        public void OnPointerClick(PointerEventData eventData)
        {
            // Short click toggles if already translated
            if (_isTranslated)
            {
                ToggleTranslation();
            }
        }

        private void OnLongPress()
        {
            if (!_isTranslated)
            {
                Translate();
            }
            else
            {
                ToggleTranslation();
            }
        }

        /// <summary>
        /// Translate the message
        /// </summary>
        public async void Translate()
        {
            if (_isLoading || string.IsNullOrEmpty(_originalText))
            {
                return;
            }

            if (_isTranslated)
            {
                ToggleTranslation();
                return;
            }

            _isLoading = true;
            UpdateUI();

            try
            {
                var manager = TranslationManager.Instance;
                var result = await manager.TranslateToMyLanguageAsync(_originalText, _sourceLang);

                if (result.Success)
                {
                    _translatedText = result.TranslatedText;
                    _isTranslated = true;
                    _showingTranslated = true;

                    if (_messageText != null)
                    {
                        _messageText.text = _translatedText;
                    }
                }
                else
                {
                    Debug.LogWarning($"Translation failed: {result.ErrorMessage}");
                }
            }
            catch (Exception ex)
            {
                Debug.LogError($"Translation error: {ex.Message}");
            }
            finally
            {
                _isLoading = false;
                UpdateUI();
            }
        }

        /// <summary>
        /// Toggle between original and translated text
        /// </summary>
        public void ToggleTranslation()
        {
            if (!_isTranslated)
            {
                return;
            }

            _showingTranslated = !_showingTranslated;

            if (_messageText != null)
            {
                _messageText.text = _showingTranslated ? _translatedText : _originalText;
            }

            UpdateUI();
        }

        private void UpdateUI()
        {
            // Loading spinner
            if (_loadingSpinner != null)
            {
                _loadingSpinner.SetActive(_isLoading);
            }

            // Translate button
            if (_translateButton != null)
            {
                _translateButton.interactable = !_isLoading;
                var buttonText = _translateButton.GetComponentInChildren<Text>();
                if (buttonText != null)
                {
                    if (_isLoading)
                    {
                        buttonText.text = "...";
                    }
                    else if (_isTranslated)
                    {
                        buttonText.text = _showingTranslated ? "Original" : "Translated";
                    }
                    else
                    {
                        buttonText.text = "Translate";
                    }
                }
            }

            // Translated indicator label
            if (_translatedLabel != null)
            {
                _translatedLabel.gameObject.SetActive(_isTranslated && _showingTranslated);
                _translatedLabel.text = "Translated";
                _translatedLabel.color = _translatedLabelColor;
            }
        }

        /// <summary>
        /// Simple language detection based on character ranges
        /// </summary>
        private string DetectLanguage(string text)
        {
            if (string.IsNullOrEmpty(text))
            {
                return "en";
            }

            int korean = 0, japanese = 0, chinese = 0, english = 0;

            foreach (char c in text)
            {
                // Korean
                if (c >= 0xAC00 && c <= 0xD7AF) korean++;
                // Japanese Hiragana/Katakana
                else if ((c >= 0x3040 && c <= 0x309F) || (c >= 0x30A0 && c <= 0x30FF)) japanese++;
                // Chinese (CJK)
                else if (c >= 0x4E00 && c <= 0x9FFF) chinese++;
                // English letters
                else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) english++;
            }

            // Japanese also uses Chinese characters, so check hiragana/katakana first
            if (japanese > 0) return "ja";
            if (korean > 0) return "ko";
            if (chinese > 0) return "zh";
            return "en";
        }

        #region Public Properties

        public bool IsTranslated => _isTranslated;
        public bool IsShowingTranslated => _showingTranslated;
        public string OriginalText => _originalText;
        public string TranslatedText => _translatedText;
        public string CurrentText => _showingTranslated ? _translatedText : _originalText;

        #endregion
    }
}
