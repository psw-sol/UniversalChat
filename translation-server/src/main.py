"""
M2M-100 Translation Server - FastAPI Application
Phase 2: 폴백 시스템, Rate Limiting, 로깅, 메트릭 통합
"""
import time
import uuid
from contextlib import asynccontextmanager
from typing import Optional

from fastapi import FastAPI, HTTPException, Request, Header
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, Response

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
from .fallback.chain import FallbackChain
from .rate_limiter import RateLimiter, get_rate_limit_headers
from .logging_config import get_logger, RequestLogger
from .metrics import (
    MetricsCollector,
    get_metrics,
    get_metrics_content_type
)

import redis.asyncio as redis_async

# Initialize logger
logger = get_logger(__name__)

# Global instances
_redis_client: Optional[redis_async.Redis] = None
_rate_limiter: Optional[RateLimiter] = None
_fallback_chain: Optional[FallbackChain] = None


async def get_redis() -> redis_async.Redis:
    """Get Redis client"""
    global _redis_client
    if _redis_client is None:
        settings = get_settings()
        _redis_client = redis_async.from_url(settings.redis_url)
    return _redis_client


async def get_rate_limiter() -> RateLimiter:
    """Get Rate Limiter instance"""
    global _rate_limiter
    if _rate_limiter is None:
        redis_client = await get_redis()
        _rate_limiter = RateLimiter(redis_client)
    return _rate_limiter


def get_fallback_chain() -> FallbackChain:
    """Get Fallback Chain instance"""
    global _fallback_chain
    if _fallback_chain is None:
        settings = get_settings()
        translator = get_translator()
        _fallback_chain = FallbackChain(
            primary_translator=translator if translator.is_loaded() else None,
            azure_timeout=settings.azure_timeout,
            gemini_timeout=settings.gemini_timeout,
            primary_timeout=settings.primary_timeout
        )
    return _fallback_chain


@asynccontextmanager
async def lifespan(_app: FastAPI):
    """Application lifespan events"""
    global _fallback_chain

    # Startup
    logger.info("starting_server", message="Starting Translation Server...")

    # Load translation model
    translator = get_translator()
    if not translator.load_model():
        logger.error("model_load_failed", message="Failed to load translation model!")

    # Set model info for metrics
    settings = get_settings()
    MetricsCollector.set_model_info(
        name=settings.model_name,
        device=settings.device,
        version="1.0.0"
    )

    # Initialize fallback chain with loaded translator
    _fallback_chain = FallbackChain(
        primary_translator=translator if translator.is_loaded() else None,
        azure_timeout=settings.azure_timeout,
        gemini_timeout=settings.gemini_timeout,
        primary_timeout=settings.primary_timeout
    )

    # Connect to Redis cache
    cache = get_cache()
    await cache.connect()

    logger.info("server_started", message="Translation Server started successfully")

    yield

    # Shutdown
    logger.info("stopping_server", message="Shutting down Translation Server...")
    await cache.disconnect()

    global _redis_client
    if _redis_client:
        await _redis_client.close()
        _redis_client = None

    logger.info("server_stopped", message="Translation Server stopped")


# Create FastAPI app
app = FastAPI(
    title="M2M-100 Translation API",
    description="High-performance translation API using Meta's M2M-100 model with fallback support",
    version="2.0.0",
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


@app.middleware("http")
async def request_middleware(request: Request, call_next):
    """Request logging and metrics middleware"""
    request_id = request.headers.get("X-Request-ID", str(uuid.uuid4()))
    request.state.request_id = request_id

    # Start metrics tracking
    MetricsCollector.request_started()

    try:
        response = await call_next(request)
        response.headers["X-Request-ID"] = request_id
        return response
    finally:
        MetricsCollector.request_finished()


@app.exception_handler(Exception)
async def global_exception_handler(request: Request, exc: Exception):
    """Global exception handler"""
    request_id = getattr(request.state, "request_id", "unknown")
    logger.error(
        "unhandled_exception",
        request_id=request_id,
        error=str(exc),
        path=request.url.path
    )
    MetricsCollector.record_error("unhandled_exception", "unknown")

    return JSONResponse(
        status_code=500,
        content=ErrorResponse(
            error="internal_error",
            message="An internal error occurred",
            detail=str(exc) if get_settings().log_level == "debug" else None
        ).model_dump()
    )


def extract_user_id(
    request: Request,
    x_user_id: Optional[str] = Header(None)
) -> str:
    """Extract user ID from header or IP address"""
    if x_user_id:
        return x_user_id
    # Fallback to IP address
    forwarded = request.headers.get("X-Forwarded-For")
    if forwarded:
        return forwarded.split(",")[0].strip()
    return request.client.host if request.client else "unknown"


@app.get("/health", response_model=HealthResponse, tags=["System"])
async def health_check():
    """
    Health check endpoint

    Returns system status including model loading state and service connectivity.
    """
    translator = get_translator()
    cache = get_cache()
    fallback = get_fallback_chain()

    return HealthResponse(
        status="healthy" if translator.is_loaded() else "degraded",
        model_loaded=translator.is_loaded(),
        gpu_available=translator.is_gpu_available(),
        redis_connected=cache.is_connected(),
        version="2.0.0",
        available_providers=fallback.get_available_providers()
    )


@app.get("/languages", response_model=LanguagesResponse, tags=["System"])
async def get_languages():
    """
    Get supported languages

    Returns list of all supported language codes and primary language pairs.
    """
    primary_count = len(PRIMARY_LANGUAGES)
    pairs = primary_count * (primary_count - 1)

    return LanguagesResponse(
        supported=SUPPORTED_LANGUAGES,
        primary=PRIMARY_LANGUAGES,
        pairs=pairs
    )


@app.get("/metrics", tags=["System"])
async def metrics():
    """
    Prometheus metrics endpoint

    Returns metrics in Prometheus format for scraping.
    """
    return Response(
        content=get_metrics(),
        media_type=get_metrics_content_type()
    )


@app.post("/translate", response_model=TranslationResponse, tags=["Translation"])
async def translate(
    request: TranslationRequest,
    http_request: Request,
    x_user_id: Optional[str] = Header(None)
):
    """
    Translate text with fallback support

    Translates text from source language to target language.
    Uses M2M-100 model with automatic fallback to Azure/Gemini if needed.

    **Rate Limits**:
    - 30 requests per minute
    - 500 requests per hour
    - 5,000 requests per day
    """
    start_time = time.time()
    request_id = getattr(http_request.state, "request_id", str(uuid.uuid4()))
    user_id = extract_user_id(http_request, x_user_id)

    # Request logging context
    req_logger = RequestLogger(request_id, user_id)
    req_logger.log_request(
        source_lang=request.source_lang,
        target_lang=request.target_lang,
        text_length=len(request.text)
    )

    # Rate limiting
    rate_limiter = await get_rate_limiter()
    rate_info = await rate_limiter.check_rate_limit(user_id)

    if not rate_info.allowed:
        req_logger.log_rate_limit(
            window=rate_info.window,
            limit=rate_info.limit,
            remaining=rate_info.remaining
        )
        MetricsCollector.record_rate_limit(rate_info.window)

        response = JSONResponse(
            status_code=429,
            content=ErrorResponse(
                error="rate_limit_exceeded",
                message=f"Rate limit exceeded. Retry after {rate_info.retry_after} seconds.",
                detail=f"Window: {rate_info.window}, Limit: {rate_info.limit}"
            ).model_dump(),
            headers=get_rate_limit_headers(rate_info)
        )
        return response

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
        latency = (time.time() - start_time) * 1000
        req_logger.log_response(
            provider="passthrough",
            cache_hit=False,
            latency_ms=latency
        )
        return TranslationResponse(
            translated_text=request.text,
            source_lang=request.source_lang,
            target_lang=request.target_lang,
            provider="passthrough",
            cache_hit=False,
            latency_ms=round(latency, 2)
        )

    # Check cache first
    cache = get_cache()
    cached_result = await cache.get(
        request.text,
        request.source_lang,
        request.target_lang
    )

    if cached_result:
        latency = (time.time() - start_time) * 1000
        MetricsCollector.record_cache_hit(True)
        MetricsCollector.record_request(
            provider="cache",
            source_lang=request.source_lang,
            target_lang=request.target_lang,
            success=True
        )
        req_logger.log_response(
            provider="cache",
            cache_hit=True,
            latency_ms=latency
        )
        return TranslationResponse(
            translated_text=cached_result,
            source_lang=request.source_lang,
            target_lang=request.target_lang,
            provider="m2m100",
            cache_hit=True,
            latency_ms=round(latency, 2)
        )

    MetricsCollector.record_cache_hit(False)

    # Use fallback chain for translation
    fallback_chain = get_fallback_chain()
    result = await fallback_chain.translate(
        request.text,
        request.source_lang,
        request.target_lang
    )

    if result.translated_text is None:
        MetricsCollector.record_error("all_providers_failed", "none")
        req_logger.log_error(result.error or "All translation providers failed")
        raise HTTPException(
            status_code=503,
            detail="Translation service temporarily unavailable"
        )

    # Log fallback if used
    if result.fallback_used:
        req_logger.log_fallback(
            from_provider="m2m100",
            to_provider=result.provider.value,
            reason="primary_failed"
        )
        MetricsCollector.record_fallback("m2m100", result.provider.value)

    # Record metrics
    MetricsCollector.record_request(
        provider=result.provider.value,
        source_lang=request.source_lang,
        target_lang=request.target_lang,
        success=True
    )
    MetricsCollector.record_latency(result.provider.value, result.latency_ms / 1000)

    # Store in cache
    await cache.set(
        request.text,
        request.source_lang,
        request.target_lang,
        result.translated_text
    )

    req_logger.log_response(
        provider=result.provider.value,
        cache_hit=False,
        latency_ms=result.latency_ms,
        fallback_used=result.fallback_used
    )

    # Add rate limit headers to response
    response_data = TranslationResponse(
        translated_text=result.translated_text,
        source_lang=request.source_lang,
        target_lang=request.target_lang,
        provider=result.provider.value,
        cache_hit=False,
        latency_ms=round(result.latency_ms, 2),
        fallback_used=result.fallback_used
    )

    return JSONResponse(
        content=response_data.model_dump(),
        headers=get_rate_limit_headers(rate_info)
    )


@app.post("/translate/batch", response_model=BatchTranslationResponse, tags=["Translation"])
async def translate_batch(
    request: BatchTranslationRequest,
    http_request: Request,
    x_user_id: Optional[str] = Header(None)
):
    """
    Batch translate multiple texts

    Translates multiple texts at once for better performance.
    Maximum 50 texts per request.
    """
    start_time = time.time()
    user_id = extract_user_id(http_request, x_user_id)

    # Rate limiting (count as number of texts)
    rate_limiter = await get_rate_limiter()

    for _ in request.texts:
        rate_info = await rate_limiter.check_rate_limit(user_id)
        if not rate_info.allowed:
            MetricsCollector.record_rate_limit(rate_info.window)
            return JSONResponse(
                status_code=429,
                content=ErrorResponse(
                    error="rate_limit_exceeded",
                    message=f"Rate limit exceeded. Retry after {rate_info.retry_after} seconds.",
                    detail=f"Window: {rate_info.window}"
                ).model_dump(),
                headers=get_rate_limit_headers(rate_info)
            )

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
            MetricsCollector.record_cache_hit(True)
        else:
            translations.append(None)
            texts_to_translate.append(text)
            text_indices.append(i)
            MetricsCollector.record_cache_hit(False)

    # Batch translate uncached texts
    if texts_to_translate:
        try:
            new_translations = translator.translate_batch(
                texts_to_translate,
                request.source_lang,
                request.target_lang
            )

            for idx, translated in zip(text_indices, new_translations):
                translations[idx] = translated
                await cache.set(
                    request.texts[idx],
                    request.source_lang,
                    request.target_lang,
                    translated
                )

            MetricsCollector.record_request(
                provider="m2m100",
                source_lang=request.source_lang,
                target_lang=request.target_lang,
                success=True
            )

        except Exception as e:
            MetricsCollector.record_error("batch_translation_failed", "m2m100")
            raise HTTPException(status_code=500, detail=str(e))

    latency = (time.time() - start_time) * 1000
    MetricsCollector.record_latency("m2m100", latency / 1000)

    return BatchTranslationResponse(
        translations=translations,
        source_lang=request.source_lang,
        target_lang=request.target_lang,
        provider="m2m100",
        cache_hits=cache_hits,
        latency_ms=round(latency, 2)
    )


@app.get("/cache/stats", tags=["System"])
async def get_cache_stats():
    """Get cache statistics"""
    cache = get_cache()
    return cache.get_stats()


@app.get("/rate-limit/status", tags=["System"])
async def get_rate_limit_status(
    request: Request,
    x_user_id: Optional[str] = Header(None)
):
    """Get current rate limit status for user"""
    user_id = extract_user_id(request, x_user_id)
    rate_limiter = await get_rate_limiter()
    usage = await rate_limiter.get_usage(user_id)
    return {"user_id": user_id, "usage": usage}


@app.get("/fallback/stats", tags=["System"])
async def get_fallback_stats():
    """Get fallback chain statistics"""
    fallback_chain = get_fallback_chain()
    return {
        "stats": fallback_chain.get_stats(),
        "available_providers": fallback_chain.get_available_providers()
    }


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
