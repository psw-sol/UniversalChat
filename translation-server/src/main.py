"""
M2M-100 Translation Server - FastAPI Application
"""
import time
import logging
from contextlib import asynccontextmanager
from typing import Optional

from fastapi import FastAPI, HTTPException, Request, Depends
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse

from .config import get_settings, PRIMARY_LANGUAGES, SUPPORTED_LANGUAGES
from .models import (
    TranslationRequest,
    TranslationResponse,
    BatchTranslationRequest,
    BatchTranslationResponse,
    HealthResponse,
    LanguagesResponse,
    ErrorResponse
)
from .translator import get_translator
from .cache import get_cache

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application lifespan events"""
    # Startup
    logger.info("Starting Translation Server...")

    # Load translation model
    translator = get_translator()
    if not translator.load_model():
        logger.error("Failed to load translation model!")

    # Connect to Redis cache
    cache = get_cache()
    await cache.connect()

    logger.info("Translation Server started successfully")

    yield

    # Shutdown
    logger.info("Shutting down Translation Server...")
    await cache.disconnect()
    logger.info("Translation Server stopped")


# Create FastAPI app
app = FastAPI(
    title="M2M-100 Translation API",
    description="High-performance translation API using Meta's M2M-100 model",
    version="1.0.0",
    lifespan=lifespan
)

# CORS middleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # Configure appropriately for production
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.exception_handler(Exception)
async def global_exception_handler(request: Request, exc: Exception):
    """Global exception handler"""
    logger.error(f"Unhandled exception: {exc}")
    return JSONResponse(
        status_code=500,
        content=ErrorResponse(
            error="internal_error",
            message="An internal error occurred",
            detail=str(exc) if get_settings().log_level == "debug" else None
        ).model_dump()
    )


@app.get("/health", response_model=HealthResponse, tags=["System"])
async def health_check():
    """
    Health check endpoint

    Returns system status including model loading state and service connectivity.
    """
    translator = get_translator()
    cache = get_cache()

    return HealthResponse(
        status="healthy" if translator.is_loaded() else "degraded",
        model_loaded=translator.is_loaded(),
        gpu_available=translator.is_gpu_available(),
        redis_connected=cache.is_connected(),
        version="1.0.0"
    )


@app.get("/languages", response_model=LanguagesResponse, tags=["System"])
async def get_languages():
    """
    Get supported languages

    Returns list of all supported language codes and primary language pairs.
    """
    # Calculate number of translation pairs
    primary_count = len(PRIMARY_LANGUAGES)
    pairs = primary_count * (primary_count - 1)  # All pairs, no self-translation

    return LanguagesResponse(
        supported=SUPPORTED_LANGUAGES,
        primary=PRIMARY_LANGUAGES,
        pairs=pairs
    )


@app.post("/translate", response_model=TranslationResponse, tags=["Translation"])
async def translate(request: TranslationRequest):
    """
    Translate text

    Translates text from source language to target language.
    Uses M2M-100 model with Redis caching for optimal performance.

    **Supported languages**: ko (Korean), en (English), zh (Chinese), ja (Japanese)

    **Example**:
    ```json
    {
        "text": "안녕하세요",
        "source_lang": "ko",
        "target_lang": "en"
    }
    ```
    """
    start_time = time.time()
    translator = get_translator()
    cache = get_cache()

    # Validate model is loaded
    if not translator.is_loaded():
        raise HTTPException(
            status_code=503,
            detail="Translation model not loaded"
        )

    # Validate languages
    if request.source_lang not in SUPPORTED_LANGUAGES:
        raise HTTPException(
            status_code=400,
            detail=f"Unsupported source language: {request.source_lang}"
        )
    if request.target_lang not in SUPPORTED_LANGUAGES:
        raise HTTPException(
            status_code=400,
            detail=f"Unsupported target language: {request.target_lang}"
        )

    # Same language check
    if request.source_lang == request.target_lang:
        return TranslationResponse(
            translated_text=request.text,
            source_lang=request.source_lang,
            target_lang=request.target_lang,
            provider="passthrough",
            cache_hit=False,
            latency_ms=round((time.time() - start_time) * 1000, 2)
        )

    # Check cache first
    cached_result = await cache.get(
        request.text,
        request.source_lang,
        request.target_lang
    )

    if cached_result:
        return TranslationResponse(
            translated_text=cached_result,
            source_lang=request.source_lang,
            target_lang=request.target_lang,
            provider="m2m100",
            cache_hit=True,
            latency_ms=round((time.time() - start_time) * 1000, 2)
        )

    # Perform translation
    try:
        translated = translator.translate(
            request.text,
            request.source_lang,
            request.target_lang
        )

        # Store in cache (fire and forget)
        await cache.set(
            request.text,
            request.source_lang,
            request.target_lang,
            translated
        )

        return TranslationResponse(
            translated_text=translated,
            source_lang=request.source_lang,
            target_lang=request.target_lang,
            provider="m2m100",
            cache_hit=False,
            latency_ms=round((time.time() - start_time) * 1000, 2)
        )

    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))
    except RuntimeError as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.post("/translate/batch", response_model=BatchTranslationResponse, tags=["Translation"])
async def translate_batch(request: BatchTranslationRequest):
    """
    Batch translate multiple texts

    Translates multiple texts at once for better performance.
    Maximum 50 texts per request.
    """
    start_time = time.time()
    translator = get_translator()
    cache = get_cache()

    if not translator.is_loaded():
        raise HTTPException(
            status_code=503,
            detail="Translation model not loaded"
        )

    # Validate languages
    if request.source_lang not in SUPPORTED_LANGUAGES:
        raise HTTPException(
            status_code=400,
            detail=f"Unsupported source language: {request.source_lang}"
        )
    if request.target_lang not in SUPPORTED_LANGUAGES:
        raise HTTPException(
            status_code=400,
            detail=f"Unsupported target language: {request.target_lang}"
        )

    translations = []
    cache_hits = 0
    texts_to_translate = []
    text_indices = []

    # Check cache for each text
    for i, text in enumerate(request.texts):
        cached = await cache.get(text, request.source_lang, request.target_lang)
        if cached:
            translations.append(cached)
            cache_hits += 1
        else:
            translations.append(None)
            texts_to_translate.append(text)
            text_indices.append(i)

    # Batch translate uncached texts
    if texts_to_translate:
        try:
            new_translations = translator.translate_batch(
                texts_to_translate,
                request.source_lang,
                request.target_lang
            )

            # Fill in results and cache
            for idx, translated in zip(text_indices, new_translations):
                translations[idx] = translated
                await cache.set(
                    request.texts[idx],
                    request.source_lang,
                    request.target_lang,
                    translated
                )

        except Exception as e:
            raise HTTPException(status_code=500, detail=str(e))

    return BatchTranslationResponse(
        translations=translations,
        source_lang=request.source_lang,
        target_lang=request.target_lang,
        provider="m2m100",
        cache_hits=cache_hits,
        latency_ms=round((time.time() - start_time) * 1000, 2)
    )


@app.get("/cache/stats", tags=["System"])
async def get_cache_stats():
    """Get cache statistics"""
    cache = get_cache()
    return cache.get_stats()


# Run with: uvicorn src.main:app --reload
if __name__ == "__main__":
    import uvicorn
    settings = get_settings()
    uvicorn.run(
        "src.main:app",
        host=settings.host,
        port=settings.port,
        reload=True
    )
