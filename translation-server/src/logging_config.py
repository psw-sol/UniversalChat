"""
Structured logging configuration using structlog
"""
import sys
import logging
import structlog
from typing import Any
from datetime import datetime, timezone
from .config import settings


def configure_logging():
    """
    structlog 설정

    JSON 포맷으로 로깅하며, ELK/Loki 등에서 파싱 가능
    """
    # 로그 레벨 매핑
    log_levels = {
        "debug": logging.DEBUG,
        "info": logging.INFO,
        "warning": logging.WARNING,
        "error": logging.ERROR,
    }
    level = log_levels.get(settings.log_level, logging.INFO)

    # 표준 logging 설정
    logging.basicConfig(
        format="%(message)s",
        stream=sys.stdout,
        level=level,
    )

    # structlog 프로세서 체인
    processors = [
        # 타임스탬프 추가
        structlog.processors.TimeStamper(fmt="iso"),
        # 로그 레벨 추가
        structlog.stdlib.add_log_level,
        # 위치 정보 추가 (옵션)
        structlog.processors.StackInfoRenderer(),
        # 예외 포맷팅
        structlog.processors.format_exc_info,
        # 최종 JSON 렌더링
        structlog.processors.JSONRenderer()
    ]

    structlog.configure(
        processors=processors,
        wrapper_class=structlog.stdlib.BoundLogger,
        context_class=dict,
        logger_factory=structlog.stdlib.LoggerFactory(),
        cache_logger_on_first_use=True,
    )


def get_logger(name: str = None) -> structlog.stdlib.BoundLogger:
    """
    이름이 지정된 로거 가져오기

    Args:
        name: 로거 이름 (기본: 호출 모듈)

    Returns:
        structlog 로거
    """
    return structlog.get_logger(name)


class RequestLogger:
    """
    HTTP 요청/응답 로깅을 위한 컨텍스트 관리자
    """

    def __init__(self, request_id: str, user_id: str = None):
        self.request_id = request_id
        self.user_id = user_id
        self.logger = get_logger("request")
        self.start_time = None
        self.context: dict[str, Any] = {}

    def __enter__(self):
        self.start_time = datetime.now(timezone.utc)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type:
            self.log_error(str(exc_val), exc_info=(exc_type, exc_val, exc_tb))
        return False

    def bind(self, **kwargs):
        """추가 컨텍스트 바인딩"""
        self.context.update(kwargs)

    def log_request(
        self,
        source_lang: str,
        target_lang: str,
        text_length: int,
        **extra
    ):
        """번역 요청 로깅"""
        self.logger.info(
            "translation_request",
            request_id=self.request_id,
            user_id=self.user_id,
            source_lang=source_lang,
            target_lang=target_lang,
            text_length=text_length,
            **self.context,
            **extra
        )

    def log_response(
        self,
        provider: str,
        cache_hit: bool,
        latency_ms: float,
        success: bool = True,
        **extra
    ):
        """번역 응답 로깅"""
        self.logger.info(
            "translation_response",
            request_id=self.request_id,
            user_id=self.user_id,
            provider=provider,
            cache_hit=cache_hit,
            latency_ms=round(latency_ms, 2),
            success=success,
            **self.context,
            **extra
        )

    def log_error(self, error: str, exc_info=None, **extra):
        """에러 로깅"""
        self.logger.error(
            "translation_error",
            request_id=self.request_id,
            user_id=self.user_id,
            error=error,
            exc_info=exc_info,
            **self.context,
            **extra
        )

    def log_fallback(
        self,
        from_provider: str,
        to_provider: str,
        reason: str,
        **extra
    ):
        """폴백 사용 로깅"""
        self.logger.warning(
            "translation_fallback",
            request_id=self.request_id,
            user_id=self.user_id,
            from_provider=from_provider,
            to_provider=to_provider,
            reason=reason,
            **self.context,
            **extra
        )

    def log_rate_limit(
        self,
        window: str,
        limit: int,
        remaining: int,
        **extra
    ):
        """Rate limit 로깅"""
        self.logger.warning(
            "rate_limit_exceeded",
            request_id=self.request_id,
            user_id=self.user_id,
            window=window,
            limit=limit,
            remaining=remaining,
            **self.context,
            **extra
        )


# 애플리케이션 시작 시 로깅 설정
configure_logging()
