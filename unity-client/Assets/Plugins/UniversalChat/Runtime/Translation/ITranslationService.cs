using System.Threading.Tasks;

namespace UniversalChat.Translation
{
    /// <summary>
    /// Translation result containing translated text and metadata
    /// </summary>
    public class TranslationResult
    {
        public string TranslatedText { get; set; }
        public string SourceLang { get; set; }
        public string TargetLang { get; set; }
        public string Provider { get; set; }
        public bool CacheHit { get; set; }
        public float LatencyMs { get; set; }
        public bool Success { get; set; }
        public string ErrorMessage { get; set; }

        public static TranslationResult Error(string message)
        {
            return new TranslationResult
            {
                Success = false,
                ErrorMessage = message
            };
        }
    }

    /// <summary>
    /// Interface for translation services
    /// </summary>
    public interface ITranslationService
    {
        /// <summary>
        /// Check if service is available
        /// </summary>
        bool IsAvailable { get; }

        /// <summary>
        /// Translate text asynchronously
        /// </summary>
        /// <param name="text">Text to translate</param>
        /// <param name="sourceLang">Source language code (e.g., "ko")</param>
        /// <param name="targetLang">Target language code (e.g., "en")</param>
        /// <returns>Translation result</returns>
        Task<TranslationResult> TranslateAsync(string text, string sourceLang, string targetLang);

        /// <summary>
        /// Get list of supported language codes
        /// </summary>
        Task<string[]> GetSupportedLanguagesAsync();
    }
}
