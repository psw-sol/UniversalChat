"""
Prometheus metrics for monitoring
"""
from prometheus_client import Counter, Histogram, Gauge, Info
from prometheus_client import generate_latest, CONTENT_TYPE_LATEST

# 요청 수 카운터
translation_requests_total = Counter(
    "translation_requests_total",
    "Total number of translation requests",
    ["provider", "source_lang", "target_lang", "status"]
)

# 응답 시간 히스토그램
translation_latency_seconds = Histogram(
    "translation_latency_seconds",
    "Translation request latency in seconds",
    ["provider"],
    buckets=[0.01, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0]
)

# 캐시 히트율 게이지
translation_cache_hits = Counter(
    "translation_cache_hits_total",
    "Total number of cache hits"
)

translation_cache_misses = Counter(
    "translation_cache_misses_total",
    "Total number of cache misses"
)

# 폴백 사용 카운터
translation_fallback_total = Counter(
    "translation_fallback_total",
    "Total number of fallback uses",
    ["from_provider", "to_provider"]
)

# 에러 카운터
translation_errors_total = Counter(
    "translation_errors_total",
    "Total number of translation errors",
    ["error_type", "provider"]
)

# Rate limit 카운터
rate_limit_exceeded_total = Counter(
    "rate_limit_exceeded_total",
    "Total number of rate limit exceeded",
    ["window"]  # minute, hour, day
)

# 활성 요청 게이지
active_requests = Gauge(
    "active_translation_requests",
    "Number of active translation requests"
)

# 모델 정보
model_info = Info(
    "translation_model",
    "Translation model information"
)

# API 사용량 (폴백용)
api_usage_chars = Counter(
    "translation_api_usage_chars_total",
    "Total characters sent to external APIs",
    ["provider"]
)


class MetricsCollector:
    """메트릭 수집 헬퍼 클래스"""

    @staticmethod
    def record_request(
        provider: str,
        source_lang: str,
        target_lang: str,
        success: bool
    ):
        """요청 메트릭 기록"""
        status = "success" if success else "error"
        translation_requests_total.labels(
            provider=provider,
            source_lang=source_lang,
            target_lang=target_lang,
            status=status
        ).inc()

    @staticmethod
    def record_latency(provider: str, latency_seconds: float):
        """지연시간 메트릭 기록"""
        translation_latency_seconds.labels(provider=provider).observe(latency_seconds)

    @staticmethod
    def record_cache_hit(hit: bool):
        """캐시 히트 메트릭 기록"""
        if hit:
            translation_cache_hits.inc()
        else:
            translation_cache_misses.inc()

    @staticmethod
    def record_fallback(from_provider: str, to_provider: str):
        """폴백 사용 메트릭 기록"""
        translation_fallback_total.labels(
            from_provider=from_provider,
            to_provider=to_provider
        ).inc()

    @staticmethod
    def record_error(error_type: str, provider: str = "unknown"):
        """에러 메트릭 기록"""
        translation_errors_total.labels(
            error_type=error_type,
            provider=provider
        ).inc()

    @staticmethod
    def record_rate_limit(window: str):
        """Rate limit 초과 메트릭 기록"""
        rate_limit_exceeded_total.labels(window=window).inc()

    @staticmethod
    def record_api_usage(provider: str, chars: int):
        """API 사용량 기록"""
        api_usage_chars.labels(provider=provider).inc(chars)

    @staticmethod
    def set_model_info(name: str, device: str, version: str = "1.0.0"):
        """모델 정보 설정"""
        model_info.info({
            "name": name,
            "device": device,
            "version": version
        })

    @staticmethod
    def request_started():
        """요청 시작 시 호출"""
        active_requests.inc()

    @staticmethod
    def request_finished():
        """요청 완료 시 호출"""
        active_requests.dec()


def get_metrics() -> bytes:
    """
    Prometheus 메트릭 출력

    Returns:
        Prometheus 형식의 메트릭 텍스트
    """
    return generate_latest()


def get_metrics_content_type() -> str:
    """
    Prometheus 메트릭 Content-Type

    Returns:
        Content-Type 문자열
    """
    return CONTENT_TYPE_LATEST
