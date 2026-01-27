using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using UnityEngine;

namespace UniversalChat.Translation
{
    /// <summary>
    /// Singleton manager for translation services
    /// Provides caching, rate limiting, and easy access
    /// </summary>
    public class TranslationManager : MonoBehaviour
    {
        private static TranslationManager _instance;
        public static TranslationManager Instance
        {
            get
            {
                if (_instance == null)
                {
                    var go = new GameObject("TranslationManager");
                    _instance = go.AddComponent<TranslationManager>();
                    DontDestroyOnLoad(go);
                }
                return _instance;
            }
        }

        [SerializeField]
        private TranslationConfig _config;

        private ITranslationService _service;
        private Dictionary<string, CachedTranslation> _cache = new Dictionary<string, CachedTranslation>();
        private int _translationsThisMinute;
        private DateTime _minuteStart;

        public TranslationConfig Config => _config;
        public bool IsAvailable => _service?.IsAvailable ?? false;

        /// <summary>
        /// Event fired when translation completes
        /// </summary>
        public event Action<string, TranslationResult> OnTranslationComplete;

        private void Awake()
        {
            if (_instance != null && _instance != this)
            {
                Destroy(gameObject);
                return;
            }

            _instance = this;
            DontDestroyOnLoad(gameObject);
            _minuteStart = DateTime.UtcNow;
        }

        /// <summary>
        /// Initialize with config
        /// </summary>
        public void Initialize(TranslationConfig config)
        {
            _config = config;
            _service = config.CreateService();

            // Check health on startup
            _ = CheckHealthAsync();
        }

        /// <summary>
        /// Initialize with server URL directly
        /// </summary>
        public void Initialize(string serverUrl, float timeout = 10f)
        {
            _service = new TranslationService(serverUrl, timeout);
            _ = CheckHealthAsync();
        }

        /// <summary>
        /// Check if translation server is healthy
        /// </summary>
        public async Task<bool> CheckHealthAsync()
        {
            if (_service is TranslationService ts)
            {
                return await ts.CheckHealthAsync();
            }
            return false;
        }

        /// <summary>
        /// Translate text
        /// </summary>
        public async Task<TranslationResult> TranslateAsync(string text, string sourceLang, string targetLang)
        {
            if (_service == null)
            {
                return TranslationResult.Error("Translation service not initialized");
            }

            // Check rate limit
            if (!CheckRateLimit())
            {
                return TranslationResult.Error("Rate limit exceeded. Please wait a moment.");
            }

            // Check cache
            var cacheKey = GetCacheKey(text, sourceLang, targetLang);
            if (_config != null && _config.EnableLocalCache && _cache.TryGetValue(cacheKey, out var cached))
            {
                if (!cached.IsExpired(_config.LocalCacheDuration))
                {
                    return new TranslationResult
                    {
                        Success = true,
                        TranslatedText = cached.TranslatedText,
                        SourceLang = sourceLang,
                        TargetLang = targetLang,
                        Provider = "local_cache",
                        CacheHit = true,
                        LatencyMs = 0
                    };
                }
                else
                {
                    _cache.Remove(cacheKey);
                }
            }

            // Perform translation
            var result = await _service.TranslateAsync(text, sourceLang, targetLang);

            // Cache successful results
            if (result.Success && _config != null && _config.EnableLocalCache)
            {
                _cache[cacheKey] = new CachedTranslation
                {
                    TranslatedText = result.TranslatedText,
                    CachedAt = DateTime.UtcNow
                };
            }

            // Increment rate limit counter
            _translationsThisMinute++;

            // Fire event
            OnTranslationComplete?.Invoke(text, result);

            return result;
        }

        /// <summary>
        /// Translate to user's preferred language
        /// </summary>
        public async Task<TranslationResult> TranslateToMyLanguageAsync(string text, string sourceLang)
        {
            var targetLang = _config != null ? _config.GetMyLanguage() : "en";
            return await TranslateAsync(text, sourceLang, targetLang);
        }

        /// <summary>
        /// Get supported languages
        /// </summary>
        public async Task<string[]> GetSupportedLanguagesAsync()
        {
            if (_service == null)
            {
                return new[] { "ko", "en", "zh", "ja" };
            }
            return await _service.GetSupportedLanguagesAsync();
        }

        private bool CheckRateLimit()
        {
            var now = DateTime.UtcNow;
            if ((now - _minuteStart).TotalMinutes >= 1)
            {
                _minuteStart = now;
                _translationsThisMinute = 0;
            }

            var limit = _config != null ? _config.MaxTranslationsPerMinute : 30;
            return _translationsThisMinute < limit;
        }

        private string GetCacheKey(string text, string sourceLang, string targetLang)
        {
            return $"{sourceLang}:{targetLang}:{text.GetHashCode()}";
        }

        /// <summary>
        /// Clear local cache
        /// </summary>
        public void ClearCache()
        {
            _cache.Clear();
        }

        private class CachedTranslation
        {
            public string TranslatedText;
            public DateTime CachedAt;

            public bool IsExpired(int durationSeconds)
            {
                return (DateTime.UtcNow - CachedAt).TotalSeconds > durationSeconds;
            }
        }
    }
}
