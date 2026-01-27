"""
Azure Translator API fallback provider
"""
import httpx
import uuid
from typing import Optional
from ..config import settings

# Azure Translator API 언어 코드 매핑
AZURE_LANG_MAP = {
    "ko": "ko",
    "en": "en",
    "zh": "zh-Hans",  # 간체 중국어
    "ja": "ja",
    "es": "es",
    "pt": "pt",
    "fr": "fr",
    "de": "de",
    "ru": "ru",
    "vi": "vi",
    "th": "th",
    "id": "id",
}


class AzureTranslator:
    """
    Azure Translator API client

    무료 티어: 월 200만 자
    가격: $10/백만 자 (유료)
    """

    def __init__(self):
        self.api_key = settings.azure_translator_key
        self.region = settings.azure_translator_region
        self.endpoint = settings.azure_translator_endpoint
        self._monthly_chars = 0
        self._monthly_limit = 2_000_000  # 무료 티어 한도

    @property
    def is_available(self) -> bool:
        """API 키가 설정되어 있고 한도가 남아있는지 확인"""
        return bool(self.api_key) and self._monthly_chars < self._monthly_limit

    @property
    def remaining_chars(self) -> int:
        """남은 무료 문자 수"""
        return max(0, self._monthly_limit - self._monthly_chars)

    def _map_language(self, lang: str) -> str:
        """내부 언어 코드를 Azure 언어 코드로 변환"""
        return AZURE_LANG_MAP.get(lang, lang)

    async def translate(
        self,
        text: str,
        source_lang: str,
        target_lang: str,
        timeout: float = 5.0
    ) -> Optional[str]:
        """
        Azure Translator API를 사용하여 텍스트 번역

        Args:
            text: 번역할 텍스트
            source_lang: 소스 언어 코드
            target_lang: 타겟 언어 코드
            timeout: 요청 타임아웃 (초)

        Returns:
            번역된 텍스트 또는 실패 시 None
        """
        if not self.is_available:
            return None

        # 언어 코드 변환
        src_lang = self._map_language(source_lang)
        tgt_lang = self._map_language(target_lang)

        url = f"{self.endpoint}/translate"
        params = {
            "api-version": "3.0",
            "from": src_lang,
            "to": tgt_lang
        }
        headers = {
            "Ocp-Apim-Subscription-Key": self.api_key,
            "Ocp-Apim-Subscription-Region": self.region,
            "Content-Type": "application/json",
            "X-ClientTraceId": str(uuid.uuid4())
        }
        body = [{"text": text}]

        try:
            async with httpx.AsyncClient() as client:
                response = await client.post(
                    url,
                    params=params,
                    headers=headers,
                    json=body,
                    timeout=timeout
                )

                if response.status_code == 200:
                    result = response.json()
                    if result and len(result) > 0:
                        translations = result[0].get("translations", [])
                        if translations:
                            # 사용량 추적
                            self._monthly_chars += len(text)
                            return translations[0].get("text")

                elif response.status_code == 429:
                    # Rate limit 초과
                    return None
                elif response.status_code == 401:
                    # 인증 실패
                    return None

        except httpx.TimeoutException:
            return None
        except Exception as e:
            print(f"[AzureTranslator] Error: {e}")
            return None

        return None

    def reset_monthly_usage(self):
        """월간 사용량 리셋 (매월 1일에 호출)"""
        self._monthly_chars = 0
