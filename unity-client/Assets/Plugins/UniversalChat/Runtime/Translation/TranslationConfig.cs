using UnityEngine;

namespace UniversalChat.Translation
{
    /// <summary>
    /// Translation service configuration
    /// Create via Assets > Create > UniversalChat > Translation Config
    /// </summary>
    [CreateAssetMenu(fileName = "TranslationConfig", menuName = "UniversalChat/Translation Config", order = 2)]
    public class TranslationConfig : ScriptableObject
    {
        [Header("Server Settings")]
        [Tooltip("Translation server URL")]
        public string ServerUrl = "http://localhost:8000";

        [Tooltip("Request timeout in seconds")]
        [Range(1f, 30f)]
        public float Timeout = 10f;

        [Header("Language Settings")]
        [Tooltip("Default source language (auto-detect if empty)")]
        public string DefaultSourceLang = "";

        [Tooltip("Default target language for translations")]
        public string DefaultTargetLang = "en";

        [Tooltip("User's preferred language")]
        public string UserLanguage = "ko";

        [Header("UI Settings")]
        [Tooltip("Show translation button on messages")]
        public bool ShowTranslateButton = true;

        [Tooltip("Show original text after translation")]
        public bool ShowOriginalText = true;

        [Tooltip("Auto-translate incoming messages")]
        public bool AutoTranslate = false;

        [Header("Cache Settings")]
        [Tooltip("Enable local memory cache")]
        public bool EnableLocalCache = true;

        [Tooltip("Local cache duration in seconds")]
        public int LocalCacheDuration = 3600; // 1 hour

        [Header("Rate Limiting")]
        [Tooltip("Maximum translations per minute")]
        public int MaxTranslationsPerMinute = 30;

        /// <summary>
        /// Get the language code for "my" language
        /// Used to detect if translation is needed
        /// </summary>
        public string GetMyLanguage()
        {
            return !string.IsNullOrEmpty(UserLanguage) ? UserLanguage : "en";
        }

        /// <summary>
        /// Check if text should be translated
        /// </summary>
        public bool ShouldTranslate(string sourceLang)
        {
            return sourceLang != GetMyLanguage();
        }

        /// <summary>
        /// Create a translation service from this config
        /// </summary>
        public ITranslationService CreateService()
        {
            return new TranslationService(ServerUrl, Timeout);
        }
    }
}
