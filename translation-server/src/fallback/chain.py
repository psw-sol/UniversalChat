"""
Fallback chain manager for translation providers
"""
import asyncio
import time
from typing import Optional, Any
from dataclasses import dataclass
from enum import Enum

from .azure import AzureTranslator
from .gemini import GeminiTranslator


class Provider(str, Enum):
    """번역 프로바이더"""
    M2M100 = "m2m100"
    AZURE = "azure"
    GEMINI = "gemini"
    NONE = "none"


@dataclass
class FallbackResult:
    """폴백 체인 결과"""
    translated_text: Optional[str]
    provider: Provider
    fallback_used: bool
    latency_ms: float
    error: Optional[str] = None


class FallbackChain:
    """
    번역 폴백 체인 매니저

    우선순위:
    1. M2M-100 (로컬, 무료)
    2. Azure Translator (무료 티어 200만 자/월)
    3. Gemini (저렴한 API)

    실패 조건:
    - 타임아웃
    - API 에러
    - 한도 초과
    """

    def __init__(
        self,
        primary_translator=None,  # M2M-100 TranslationEngine 인스턴스
        azure_timeout: float = 5.0,
        gemini_timeout: float = 10.0,
        primary_timeout: float = 5.0
    ):
        self.primary = primary_translator
        self.azure = AzureTranslator()
        self.gemini = GeminiTranslator()

        self.azure_timeout = azure_timeout
        self.gemini_timeout = gemini_timeout
        self.primary_timeout = primary_timeout

        # 통계
        self._stats = {
            Provider.M2M100: {"success": 0, "fail": 0},
            Provider.AZURE: {"success": 0, "fail": 0},
            Provider.GEMINI: {"success": 0, "fail": 0},
        }

    def set_primary_translator(self, translator):
        """Primary translator (M2M-100) 설정"""
        self.primary = translator

    async def translate(
        self,
        text: str,
        source_lang: str,
        target_lang: str
    ) -> FallbackResult:
        """
        폴백 체인을 통한 번역

        Args:
            text: 번역할 텍스트
            source_lang: 소스 언어 코드
            target_lang: 타겟 언어 코드

        Returns:
            FallbackResult: 번역 결과 및 메타데이터
        """
        start_time = time.time()
        fallback_used = False

        # 1. M2M-100 시도
        if self.primary is not None:
            result = await self._try_primary(text, source_lang, target_lang)
            if result is not None:
                latency = (time.time() - start_time) * 1000
                self._stats[Provider.M2M100]["success"] += 1
                return FallbackResult(
                    translated_text=result,
                    provider=Provider.M2M100,
                    fallback_used=False,
                    latency_ms=latency
                )
            self._stats[Provider.M2M100]["fail"] += 1
            fallback_used = True

        # 2. Azure 폴백
        if self.azure.is_available:
            result = await self._try_azure(text, source_lang, target_lang)
            if result is not None:
                latency = (time.time() - start_time) * 1000
                self._stats[Provider.AZURE]["success"] += 1
                return FallbackResult(
                    translated_text=result,
                    provider=Provider.AZURE,
                    fallback_used=fallback_used,
                    latency_ms=latency
                )
            self._stats[Provider.AZURE]["fail"] += 1
            fallback_used = True

        # 3. Gemini 폴백
        if self.gemini.is_available:
            result = await self._try_gemini(text, source_lang, target_lang)
            if result is not None:
                latency = (time.time() - start_time) * 1000
                self._stats[Provider.GEMINI]["success"] += 1
                return FallbackResult(
                    translated_text=result,
                    provider=Provider.GEMINI,
                    fallback_used=fallback_used,
                    latency_ms=latency
                )
            self._stats[Provider.GEMINI]["fail"] += 1

        # 모든 프로바이더 실패
        latency = (time.time() - start_time) * 1000
        return FallbackResult(
            translated_text=None,
            provider=Provider.NONE,
            fallback_used=True,
            latency_ms=latency,
            error="All translation providers failed"
        )

    async def _try_primary(
        self,
        text: str,
        source_lang: str,
        target_lang: str
    ) -> Optional[str]:
        """M2M-100 번역 시도"""
        try:
            result = await asyncio.wait_for(
                asyncio.to_thread(
                    self.primary.translate,
                    text,
                    source_lang,
                    target_lang
                ),
                timeout=self.primary_timeout
            )
            return result
        except asyncio.TimeoutError:
            return None
        except Exception as e:
            print(f"[FallbackChain] M2M-100 error: {e}")
            return None

    async def _try_azure(
        self,
        text: str,
        source_lang: str,
        target_lang: str
    ) -> Optional[str]:
        """Azure Translator 번역 시도"""
        try:
            return await self.azure.translate(
                text, source_lang, target_lang,
                timeout=self.azure_timeout
            )
        except Exception as e:
            print(f"[FallbackChain] Azure error: {e}")
            return None

    async def _try_gemini(
        self,
        text: str,
        source_lang: str,
        target_lang: str
    ) -> Optional[str]:
        """Gemini 번역 시도"""
        try:
            return await self.gemini.translate(
                text, source_lang, target_lang,
                timeout=self.gemini_timeout
            )
        except Exception as e:
            print(f"[FallbackChain] Gemini error: {e}")
            return None

    def get_stats(self) -> dict:
        """프로바이더별 통계 반환"""
        return {
            provider.value: stats
            for provider, stats in self._stats.items()
        }

    def get_available_providers(self) -> list[str]:
        """사용 가능한 프로바이더 목록"""
        providers = []
        if self.primary is not None:
            providers.append(Provider.M2M100.value)
        if self.azure.is_available:
            providers.append(Provider.AZURE.value)
        if self.gemini.is_available:
            providers.append(Provider.GEMINI.value)
        return providers
