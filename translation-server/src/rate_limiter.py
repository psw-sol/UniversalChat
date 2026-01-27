"""
Redis-based rate limiter using sliding window algorithm
"""
import time
from typing import Optional, Tuple
from dataclasses import dataclass
import redis.asyncio as redis
from .config import settings


@dataclass
class RateLimitInfo:
    """Rate limit 정보"""
    allowed: bool
    limit: int
    remaining: int
    reset_time: int  # Unix timestamp
    window: str  # "minute", "hour", "day"
    retry_after: Optional[int] = None  # 초


class RateLimiter:
    """
    Redis 기반 Sliding Window Rate Limiter

    제한:
    - 분당 30회
    - 시간당 500회
    - 일당 5,000회
    """

    def __init__(self, redis_client: redis.Redis):
        self.redis = redis_client

        # 설정 로드
        self.limits = {
            "minute": settings.rate_limit_per_minute,
            "hour": settings.rate_limit_per_hour,
            "day": settings.rate_limit_per_day,
        }

        # 윈도우 크기 (초)
        self.windows = {
            "minute": 60,
            "hour": 3600,
            "day": 86400,
        }

    def _get_key(self, user_id: str, window: str) -> str:
        """Rate limit 키 생성"""
        return f"ratelimit:{window}:{user_id}"

    async def check_rate_limit(self, user_id: str) -> RateLimitInfo:
        """
        Rate limit 체크 (가장 제한적인 윈도우 반환)

        Args:
            user_id: 사용자 ID 또는 IP

        Returns:
            RateLimitInfo: 허용 여부 및 제한 정보
        """
        now = int(time.time())

        # 모든 윈도우 체크
        for window in ["minute", "hour", "day"]:
            result = await self._check_window(user_id, window, now)
            if not result.allowed:
                return result

        # 모든 윈도우 통과 시 가장 세밀한 (분) 정보 반환
        return await self._check_window(user_id, "minute", now)

    async def _check_window(
        self,
        user_id: str,
        window: str,
        now: int
    ) -> RateLimitInfo:
        """
        특정 윈도우의 rate limit 체크

        Sliding Window Log 알고리즘 사용:
        - 현재 윈도우 내의 요청 타임스탬프를 저장
        - 만료된 타임스탬프 제거
        - 요청 수 카운트
        """
        key = self._get_key(user_id, window)
        window_size = self.windows[window]
        limit = self.limits[window]
        window_start = now - window_size

        # Lua 스크립트로 원자적 연산
        script = """
        local key = KEYS[1]
        local now = tonumber(ARGV[1])
        local window_start = tonumber(ARGV[2])
        local limit = tonumber(ARGV[3])
        local window_size = tonumber(ARGV[4])

        -- 만료된 엔트리 제거
        redis.call('ZREMRANGEBYSCORE', key, 0, window_start)

        -- 현재 카운트
        local count = redis.call('ZCARD', key)

        if count < limit then
            -- 새 요청 추가
            redis.call('ZADD', key, now, now .. '-' .. math.random(1000000))
            redis.call('EXPIRE', key, window_size)
            return {1, count + 1}  -- allowed, new_count
        else
            -- 가장 오래된 엔트리의 만료 시간
            local oldest = redis.call('ZRANGE', key, 0, 0, 'WITHSCORES')
            if #oldest > 0 then
                local oldest_time = tonumber(oldest[2])
                local retry_after = oldest_time + window_size - now
                return {0, count, retry_after}  -- denied, count, retry_after
            end
            return {0, count, window_size}
        end
        """

        result = await self.redis.eval(
            script,
            1,  # number of keys
            key,  # KEYS[1]
            now,  # ARGV[1]
            window_start,  # ARGV[2]
            limit,  # ARGV[3]
            window_size  # ARGV[4]
        )

        allowed = result[0] == 1
        count = result[1]
        retry_after = result[2] if len(result) > 2 else None

        reset_time = now + window_size

        return RateLimitInfo(
            allowed=allowed,
            limit=limit,
            remaining=max(0, limit - count),
            reset_time=reset_time,
            window=window,
            retry_after=int(retry_after) if retry_after else None
        )

    async def increment(self, user_id: str) -> RateLimitInfo:
        """
        요청 카운트 증가 및 rate limit 체크

        check_rate_limit과 동일하지만 명시적으로 카운트를 증가시킴
        """
        return await self.check_rate_limit(user_id)

    async def get_usage(self, user_id: str) -> dict:
        """
        사용자의 현재 사용량 조회

        Returns:
            dict: 윈도우별 사용량
        """
        now = int(time.time())
        usage = {}

        for window, window_size in self.windows.items():
            key = self._get_key(user_id, window)
            window_start = now - window_size

            # 만료된 엔트리 제거
            await self.redis.zremrangebyscore(key, 0, window_start)

            # 현재 카운트
            count = await self.redis.zcard(key)

            usage[window] = {
                "used": count,
                "limit": self.limits[window],
                "remaining": max(0, self.limits[window] - count),
            }

        return usage

    async def reset(self, user_id: str, window: Optional[str] = None):
        """
        사용자의 rate limit 리셋

        Args:
            user_id: 사용자 ID
            window: 특정 윈도우만 리셋 (None이면 모두 리셋)
        """
        if window:
            key = self._get_key(user_id, window)
            await self.redis.delete(key)
        else:
            for w in self.windows:
                key = self._get_key(user_id, w)
                await self.redis.delete(key)


def get_rate_limit_headers(info: RateLimitInfo) -> dict:
    """
    HTTP 응답 헤더용 rate limit 정보

    Returns:
        dict: X-RateLimit-* 헤더
    """
    headers = {
        "X-RateLimit-Limit": str(info.limit),
        "X-RateLimit-Remaining": str(info.remaining),
        "X-RateLimit-Reset": str(info.reset_time),
        "X-RateLimit-Window": info.window,
    }

    if info.retry_after:
        headers["Retry-After"] = str(info.retry_after)

    return headers
