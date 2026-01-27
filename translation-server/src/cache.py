"""
Redis caching layer for translations
"""
import hashlib
import json
import logging
from typing import Optional
import redis.asyncio as redis

from .config import get_settings

logger = logging.getLogger(__name__)


class TranslationCache:
    """Redis-based translation cache"""

    _instance: Optional["TranslationCache"] = None

    def __init__(self):
        self.settings = get_settings()
        self.client: Optional[redis.Redis] = None
        self._connected = False

        # Statistics
        self._hits = 0
        self._misses = 0

    @classmethod
    def get_instance(cls) -> "TranslationCache":
        """Get singleton instance"""
        if cls._instance is None:
            cls._instance = cls()
        return cls._instance

    async def connect(self) -> bool:
        """Connect to Redis"""
        if self._connected:
            return True

        try:
            self.client = redis.from_url(
                self.settings.redis_url,
                encoding="utf-8",
                decode_responses=True
            )
            # Test connection
            await self.client.ping()
            self._connected = True
            logger.info("Connected to Redis")
            return True
        except Exception as e:
            logger.error(f"Redis connection failed: {e}")
            return False

    async def disconnect(self):
        """Disconnect from Redis"""
        if self.client:
            await self.client.close()
            self._connected = False
            logger.info("Disconnected from Redis")

    def is_connected(self) -> bool:
        """Check if connected to Redis"""
        return self._connected

    def _generate_key(self, text: str, source_lang: str, target_lang: str) -> str:
        """
        Generate cache key from translation parameters

        Key format: trans:{hash}
        Hash is first 16 chars of SHA256(text + source + target)
        """
        content = f"{text}|{source_lang}|{target_lang}"
        hash_value = hashlib.sha256(content.encode()).hexdigest()[:16]
        return f"trans:{hash_value}"

    async def get(
        self,
        text: str,
        source_lang: str,
        target_lang: str
    ) -> Optional[str]:
        """
        Get cached translation

        Args:
            text: Original text
            source_lang: Source language code
            target_lang: Target language code

        Returns:
            Cached translation or None if not found
        """
        if not self._connected:
            return None

        try:
            key = self._generate_key(text, source_lang, target_lang)
            result = await self.client.get(key)

            if result:
                self._hits += 1
                logger.debug(f"Cache hit: {key}")
                return result
            else:
                self._misses += 1
                logger.debug(f"Cache miss: {key}")
                return None

        except Exception as e:
            logger.error(f"Cache get error: {e}")
            return None

    async def set(
        self,
        text: str,
        source_lang: str,
        target_lang: str,
        translated: str,
        ttl: Optional[int] = None
    ) -> bool:
        """
        Store translation in cache

        Args:
            text: Original text
            source_lang: Source language code
            target_lang: Target language code
            translated: Translated text
            ttl: Time to live in seconds (default from settings)

        Returns:
            True if stored successfully
        """
        if not self._connected:
            return False

        try:
            key = self._generate_key(text, source_lang, target_lang)
            ttl = ttl or self.settings.cache_ttl

            await self.client.setex(key, ttl, translated)
            logger.debug(f"Cache set: {key} (TTL: {ttl}s)")
            return True

        except Exception as e:
            logger.error(f"Cache set error: {e}")
            return False

    async def delete(
        self,
        text: str,
        source_lang: str,
        target_lang: str
    ) -> bool:
        """Delete cached translation"""
        if not self._connected:
            return False

        try:
            key = self._generate_key(text, source_lang, target_lang)
            await self.client.delete(key)
            return True
        except Exception as e:
            logger.error(f"Cache delete error: {e}")
            return False

    def get_stats(self) -> dict:
        """Get cache statistics"""
        total = self._hits + self._misses
        hit_rate = (self._hits / total * 100) if total > 0 else 0

        return {
            "hits": self._hits,
            "misses": self._misses,
            "total": total,
            "hit_rate": round(hit_rate, 2)
        }

    def reset_stats(self):
        """Reset statistics"""
        self._hits = 0
        self._misses = 0


# Convenience function
def get_cache() -> TranslationCache:
    """Get the cache instance"""
    return TranslationCache.get_instance()
