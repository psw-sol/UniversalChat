using System;
using System.Threading.Tasks;
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.Events;

namespace UniversalChat.Translation
{
    /// <summary>
    /// UI component for message translation
    /// Attach to a chat message item to enable translation
    /// </summary>
    public class TranslationUI : MonoBehaviour
    {
        [Header("UI References")]
        [SerializeField] private Button _translateButton;
        [SerializeField] private Text _translateButtonText;
        [SerializeField] private Text _translatedText;
        [SerializeField] private Text _originalText;
        [SerializeField] private GameObject _loadingIndicator;
        [SerializeField] private Button _toggleButton;

        [Header("Settings")]
        [SerializeField] private string _translateButtonLabel = "Translate";
        [SerializeField] private string _showOriginalLabel = "Original";
        [SerializeField] private string _showTranslatedLabel = "Translated";

        private string _originalContent;
        private string _translatedContent;
        private string _sourceLang;
        private bool _showingTranslated;
        private bool _isLoading;

        /// <summary>
        /// Event fired when translation is requested
        /// </summary>
        public UnityEvent<string, string> OnTranslateRequested;

        /// <summary>
        /// Event fired when translation completes
        /// </summary>
        public UnityEvent<TranslationResult> OnTranslationComplete;

        private void Awake()
        {
            if (_translateButton != null)
            {
                _translateButton.onClick.AddListener(OnTranslateButtonClick);
            }

            if (_toggleButton != null)
            {
                _toggleButton.onClick.AddListener(OnToggleButtonClick);
                _toggleButton.gameObject.SetActive(false);
            }

            if (_loadingIndicator != null)
            {
                _loadingIndicator.SetActive(false);
            }

            if (_translatedText != null)
            {
                _translatedText.gameObject.SetActive(false);
            }
        }

        /// <summary>
        /// Setup the translation UI with message content
        /// </summary>
        /// <param name="content">Original message content</param>
        /// <param name="sourceLang">Source language code (optional)</param>
        public void Setup(string content, string sourceLang = null)
        {
            _originalContent = content;
            _sourceLang = sourceLang ?? "auto";
            _translatedContent = null;
            _showingTranslated = false;

            if (_originalText != null)
            {
                _originalText.text = content;
            }

            if (_translateButton != null)
            {
                _translateButton.gameObject.SetActive(true);
            }

            if (_toggleButton != null)
            {
                _toggleButton.gameObject.SetActive(false);
            }

            if (_translatedText != null)
            {
                _translatedText.gameObject.SetActive(false);
            }
        }

        /// <summary>
        /// Request translation of the message
        /// </summary>
        public async void RequestTranslation()
        {
            if (_isLoading || string.IsNullOrEmpty(_originalContent))
            {
                return;
            }

            // If already translated, just toggle view
            if (_translatedContent != null)
            {
                ToggleView();
                return;
            }

            _isLoading = true;
            SetLoadingState(true);

            try
            {
                var manager = TranslationManager.Instance;
                var targetLang = manager.Config?.GetMyLanguage() ?? "en";

                // Fire event for custom handling
                OnTranslateRequested?.Invoke(_originalContent, _sourceLang);

                // Perform translation
                var result = await manager.TranslateAsync(_originalContent, _sourceLang, targetLang);

                if (result.Success)
                {
                    _translatedContent = result.TranslatedText;
                    ShowTranslation();
                }
                else
                {
                    Debug.LogWarning($"[TranslationUI] Translation failed: {result.ErrorMessage}");
                    ShowError(result.ErrorMessage);
                }

                OnTranslationComplete?.Invoke(result);
            }
            catch (Exception ex)
            {
                Debug.LogError($"[TranslationUI] Exception: {ex.Message}");
                ShowError(ex.Message);
            }
            finally
            {
                _isLoading = false;
                SetLoadingState(false);
            }
        }

        /// <summary>
        /// Request translation with specific target language
        /// </summary>
        public async Task<TranslationResult> TranslateToAsync(string targetLang)
        {
            if (_isLoading || string.IsNullOrEmpty(_originalContent))
            {
                return TranslationResult.Error("Invalid state or empty content");
            }

            _isLoading = true;
            SetLoadingState(true);

            try
            {
                var manager = TranslationManager.Instance;
                var result = await manager.TranslateAsync(_originalContent, _sourceLang, targetLang);

                if (result.Success)
                {
                    _translatedContent = result.TranslatedText;
                    ShowTranslation();
                }

                OnTranslationComplete?.Invoke(result);
                return result;
            }
            finally
            {
                _isLoading = false;
                SetLoadingState(false);
            }
        }

        private void OnTranslateButtonClick()
        {
            RequestTranslation();
        }

        private void OnToggleButtonClick()
        {
            ToggleView();
        }

        private void ToggleView()
        {
            _showingTranslated = !_showingTranslated;

            if (_originalText != null && _translatedText != null)
            {
                _originalText.gameObject.SetActive(!_showingTranslated);
                _translatedText.gameObject.SetActive(_showingTranslated);
            }

            UpdateToggleButton();
        }

        private void ShowTranslation()
        {
            _showingTranslated = true;

            if (_translatedText != null)
            {
                _translatedText.text = _translatedContent;
                _translatedText.gameObject.SetActive(true);
            }

            if (_originalText != null)
            {
                _originalText.gameObject.SetActive(false);
            }

            // Hide translate button, show toggle
            if (_translateButton != null)
            {
                _translateButton.gameObject.SetActive(false);
            }

            if (_toggleButton != null)
            {
                _toggleButton.gameObject.SetActive(true);
                UpdateToggleButton();
            }
        }

        private void ShowError(string message)
        {
            // Could show error in UI
            if (_translateButtonText != null)
            {
                _translateButtonText.text = "Retry";
            }
        }

        private void SetLoadingState(bool isLoading)
        {
            if (_loadingIndicator != null)
            {
                _loadingIndicator.SetActive(isLoading);
            }

            if (_translateButton != null)
            {
                _translateButton.interactable = !isLoading;
            }

            if (_translateButtonText != null && isLoading)
            {
                _translateButtonText.text = "...";
            }
            else if (_translateButtonText != null)
            {
                _translateButtonText.text = _translateButtonLabel;
            }
        }

        private void UpdateToggleButton()
        {
            if (_toggleButton != null)
            {
                var text = _toggleButton.GetComponentInChildren<Text>();
                if (text != null)
                {
                    text.text = _showingTranslated ? _showOriginalLabel : _showTranslatedLabel;
                }
            }
        }

        /// <summary>
        /// Get current displayed text (original or translated)
        /// </summary>
        public string GetDisplayedText()
        {
            return _showingTranslated && _translatedContent != null
                ? _translatedContent
                : _originalContent;
        }

        /// <summary>
        /// Check if message has been translated
        /// </summary>
        public bool IsTranslated => _translatedContent != null;

        /// <summary>
        /// Check if showing translated version
        /// </summary>
        public bool IsShowingTranslated => _showingTranslated;
    }
}
