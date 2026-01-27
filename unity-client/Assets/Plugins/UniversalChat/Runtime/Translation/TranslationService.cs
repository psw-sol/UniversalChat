using System;
using System.Text;
using System.Threading.Tasks;
using UnityEngine;
using UnityEngine.Networking;

namespace UniversalChat.Translation
{
    /// <summary>
    /// HTTP-based translation service client
    /// Connects to the M2M-100 Translation Server
    /// </summary>
    public class TranslationService : ITranslationService
    {
        private readonly string _serverUrl;
        private readonly float _timeout;
        private bool _isAvailable;
        private string[] _supportedLanguages;

        public bool IsAvailable => _isAvailable;

        /// <summary>
        /// Create a new translation service
        /// </summary>
        /// <param name="serverUrl">Translation server URL (e.g., "http://localhost:8000")</param>
        /// <param name="timeout">Request timeout in seconds</param>
        public TranslationService(string serverUrl, float timeout = 10f)
        {
            _serverUrl = serverUrl.TrimEnd('/');
            _timeout = timeout;
            _isAvailable = false;
        }

        /// <summary>
        /// Check server health and update availability status
        /// </summary>
        public async Task<bool> CheckHealthAsync()
        {
            try
            {
                using var request = UnityWebRequest.Get($"{_serverUrl}/health");
                request.timeout = (int)_timeout;

                var operation = request.SendWebRequest();
                while (!operation.isDone)
                {
                    await Task.Yield();
                }

                if (request.result == UnityWebRequest.Result.Success)
                {
                    var response = JsonUtility.FromJson<HealthResponse>(request.downloadHandler.text);
                    _isAvailable = response.model_loaded;
                    return _isAvailable;
                }

                _isAvailable = false;
                return false;
            }
            catch (Exception ex)
            {
                Debug.LogWarning($"[Translation] Health check failed: {ex.Message}");
                _isAvailable = false;
                return false;
            }
        }

        /// <summary>
        /// Translate text from source language to target language
        /// </summary>
        public async Task<TranslationResult> TranslateAsync(string text, string sourceLang, string targetLang)
        {
            if (string.IsNullOrEmpty(text))
            {
                return TranslationResult.Error("Text cannot be empty");
            }

            if (sourceLang == targetLang)
            {
                return new TranslationResult
                {
                    Success = true,
                    TranslatedText = text,
                    SourceLang = sourceLang,
                    TargetLang = targetLang,
                    Provider = "passthrough",
                    CacheHit = false,
                    LatencyMs = 0
                };
            }

            try
            {
                var requestData = new TranslationRequest
                {
                    text = text,
                    source_lang = sourceLang,
                    target_lang = targetLang
                };

                var jsonData = JsonUtility.ToJson(requestData);
                var bodyRaw = Encoding.UTF8.GetBytes(jsonData);

                using var request = new UnityWebRequest($"{_serverUrl}/translate", "POST");
                request.uploadHandler = new UploadHandlerRaw(bodyRaw);
                request.downloadHandler = new DownloadHandlerBuffer();
                request.SetRequestHeader("Content-Type", "application/json");
                request.timeout = (int)_timeout;

                var operation = request.SendWebRequest();
                while (!operation.isDone)
                {
                    await Task.Yield();
                }

                if (request.result == UnityWebRequest.Result.Success)
                {
                    var response = JsonUtility.FromJson<TranslationResponse>(request.downloadHandler.text);
                    return new TranslationResult
                    {
                        Success = true,
                        TranslatedText = response.translated_text,
                        SourceLang = response.source_lang,
                        TargetLang = response.target_lang,
                        Provider = response.provider,
                        CacheHit = response.cache_hit,
                        LatencyMs = response.latency_ms
                    };
                }
                else
                {
                    var errorMsg = $"Translation request failed: {request.error}";
                    Debug.LogWarning($"[Translation] {errorMsg}");
                    return TranslationResult.Error(errorMsg);
                }
            }
            catch (Exception ex)
            {
                Debug.LogError($"[Translation] Exception: {ex.Message}");
                return TranslationResult.Error(ex.Message);
            }
        }

        /// <summary>
        /// Get list of supported language codes
        /// </summary>
        public async Task<string[]> GetSupportedLanguagesAsync()
        {
            if (_supportedLanguages != null)
            {
                return _supportedLanguages;
            }

            try
            {
                using var request = UnityWebRequest.Get($"{_serverUrl}/languages");
                request.timeout = (int)_timeout;

                var operation = request.SendWebRequest();
                while (!operation.isDone)
                {
                    await Task.Yield();
                }

                if (request.result == UnityWebRequest.Result.Success)
                {
                    var response = JsonUtility.FromJson<LanguagesResponse>(request.downloadHandler.text);
                    _supportedLanguages = response.primary;
                    return _supportedLanguages;
                }

                return new[] { "ko", "en", "zh", "ja" }; // Default fallback
            }
            catch
            {
                return new[] { "ko", "en", "zh", "ja" }; // Default fallback
            }
        }

        #region JSON Models

        [Serializable]
        private class TranslationRequest
        {
            public string text;
            public string source_lang;
            public string target_lang;
        }

        [Serializable]
        private class TranslationResponse
        {
            public string translated_text;
            public string source_lang;
            public string target_lang;
            public string provider;
            public bool cache_hit;
            public float latency_ms;
        }

        [Serializable]
        private class HealthResponse
        {
            public string status;
            public bool model_loaded;
            public bool gpu_available;
            public bool redis_connected;
            public string version;
        }

        [Serializable]
        private class LanguagesResponse
        {
            public string[] supported;
            public string[] primary;
            public int pairs;
        }

        #endregion
    }
}
