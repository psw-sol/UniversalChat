"""
Pydantic models for Translation API request/response
"""
from typing import Optional, List
from pydantic import BaseModel, Field


class TranslationRequest(BaseModel):
    """Translation request model"""
    text: str = Field(..., min_length=1, max_length=5000, description="Text to translate")
    source_lang: str = Field(..., min_length=2, max_length=5, description="Source language code (e.g., 'ko', 'en')")
    target_lang: str = Field(..., min_length=2, max_length=5, description="Target language code (e.g., 'en', 'ja')")

    class Config:
        json_schema_extra = {
            "example": {
                "text": "안녕하세요",
                "source_lang": "ko",
                "target_lang": "en"
            }
        }


class TranslationResponse(BaseModel):
    """Translation response model"""
    translated_text: str = Field(..., description="Translated text")
    source_lang: str = Field(..., description="Source language code")
    target_lang: str = Field(..., description="Target language code")
    provider: str = Field(default="m2m100", description="Translation provider used")
    cache_hit: bool = Field(default=False, description="Whether result was from cache")
    latency_ms: float = Field(..., description="Processing time in milliseconds")

    class Config:
        json_schema_extra = {
            "example": {
                "translated_text": "Hello",
                "source_lang": "ko",
                "target_lang": "en",
                "provider": "m2m100",
                "cache_hit": False,
                "latency_ms": 125.5
            }
        }


class BatchTranslationRequest(BaseModel):
    """Batch translation request model"""
    texts: List[str] = Field(..., min_length=1, max_length=50, description="List of texts to translate")
    source_lang: str = Field(..., min_length=2, max_length=5, description="Source language code")
    target_lang: str = Field(..., min_length=2, max_length=5, description="Target language code")


class BatchTranslationResponse(BaseModel):
    """Batch translation response model"""
    translations: List[str] = Field(..., description="List of translated texts")
    source_lang: str
    target_lang: str
    provider: str = "m2m100"
    cache_hits: int = Field(default=0, description="Number of cache hits")
    latency_ms: float


class HealthResponse(BaseModel):
    """Health check response model"""
    status: str = Field(..., description="Service status")
    model_loaded: bool = Field(..., description="Whether translation model is loaded")
    gpu_available: bool = Field(..., description="Whether GPU is available")
    redis_connected: bool = Field(..., description="Whether Redis is connected")
    version: str = Field(..., description="API version")


class LanguagesResponse(BaseModel):
    """Supported languages response model"""
    supported: List[str] = Field(..., description="List of supported language codes")
    primary: List[str] = Field(..., description="Primary supported languages (ko, en, zh, ja)")
    pairs: int = Field(..., description="Number of supported translation pairs")


class ErrorResponse(BaseModel):
    """Error response model"""
    error: str = Field(..., description="Error type")
    message: str = Field(..., description="Error message")
    detail: Optional[str] = Field(None, description="Additional error details")
