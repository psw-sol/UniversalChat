"""
Configuration management using pydantic-settings
"""
from typing import Literal
from pydantic_settings import BaseSettings
from functools import lru_cache


class Settings(BaseSettings):
    """Application settings loaded from environment variables"""

    # Model settings
    model_name: str = "facebook/m2m100_418M"
    finetuned_model_path: str = ""  # Path to fine-tuned LoRA adapter (empty = use base model)
    use_quantization: bool = True  # Use 4-bit quantization for fine-tuned model
    device: Literal["cuda", "cpu"] = "cuda"
    max_length: int = 256
    batch_size: int = 8

    # Redis settings
    redis_url: str = "redis://localhost:6379/0"
    cache_ttl: int = 86400  # 24 hours

    # Rate limiting
    rate_limit_per_minute: int = 30
    rate_limit_per_hour: int = 500
    rate_limit_per_day: int = 5000

    # Logging
    log_level: Literal["debug", "info", "warning", "error"] = "info"

    # Fallback APIs (optional)
    azure_translator_key: str = ""
    azure_translator_region: str = "koreacentral"
    azure_translator_endpoint: str = "https://api.cognitive.microsofttranslator.com"

    gemini_api_key: str = ""
    gemini_model: str = "gemini-2.0-flash-lite"

    # Fallback timeouts
    primary_timeout: float = 5.0
    azure_timeout: float = 5.0
    gemini_timeout: float = 10.0

    # Server settings
    host: str = "0.0.0.0"
    port: int = 8000

    class Config:
        env_file = ".env"
        env_file_encoding = "utf-8"
        case_sensitive = False


@lru_cache()
def get_settings() -> Settings:
    """Get cached settings instance"""
    return Settings()


# Convenience alias for direct import
settings = get_settings()


# Language mapping for M2M-100
# ISO 639-1 to M2M-100 language codes
LANGUAGE_MAP = {
    # Primary languages
    "ko": "ko",  # Korean
    "en": "en",  # English
    "zh": "zh",  # Chinese
    "ja": "ja",  # Japanese

    # Extended languages (for future)
    "es": "es",  # Spanish
    "pt": "pt",  # Portuguese
    "fr": "fr",  # French
    "de": "de",  # German
    "ru": "ru",  # Russian
    "vi": "vi",  # Vietnamese
    "th": "th",  # Thai
    "id": "id",  # Indonesian
}

# Primary supported languages
PRIMARY_LANGUAGES = ["ko", "en", "zh", "ja"]

# All supported languages
SUPPORTED_LANGUAGES = list(LANGUAGE_MAP.keys())
