"""
Gemini API fallback provider for translation
"""
import httpx
from typing import Optional
from ..config import settings

# 언어 이름 매핑 (프롬프트용)
LANG_NAMES = {
    "ko": "Korean",
    "en": "English",
    "zh": "Chinese",
    "ja": "Japanese",
    "es": "Spanish",
    "pt": "Portuguese",
    "fr": "French",
    "de": "German",
    "ru": "Russian",
    "vi": "Vietnamese",
    "th": "Thai",
    "id": "Indonesian",
}


class GeminiTranslator:
    """
    Gemini API client for translation fallback

    가격: $0.08/백만 입력 토큰 (Gemini 2.0 Flash Lite)
    """

    def __init__(self):
        self.api_key = settings.gemini_api_key
        self.model = settings.gemini_model
        self.endpoint = f"https://generativelanguage.googleapis.com/v1beta/models/{self.model}:generateContent"

    @property
    def is_available(self) -> bool:
        """API 키가 설정되어 있는지 확인"""
        return bool(self.api_key)

    def _get_language_name(self, lang_code: str) -> str:
        """언어 코드를 언어 이름으로 변환"""
        return LANG_NAMES.get(lang_code, lang_code)

    def _build_prompt(self, text: str, source_lang: str, target_lang: str) -> str:
        """번역 프롬프트 생성"""
        src_name = self._get_language_name(source_lang)
        tgt_name = self._get_language_name(target_lang)

        return f"""Translate the following text from {src_name} to {tgt_name}.
Only output the translation, nothing else. Do not include any explanations or additional text.

Text: {text}"""

    async def translate(
        self,
        text: str,
        source_lang: str,
        target_lang: str,
        timeout: float = 10.0
    ) -> Optional[str]:
        """
        Gemini API를 사용하여 텍스트 번역

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

        prompt = self._build_prompt(text, source_lang, target_lang)

        headers = {
            "Content-Type": "application/json",
        }

        body = {
            "contents": [
                {
                    "parts": [
                        {"text": prompt}
                    ]
                }
            ],
            "generationConfig": {
                "temperature": 0.1,  # 번역은 낮은 temperature가 적합
                "maxOutputTokens": 512,
                "topP": 0.8,
            },
            "safetySettings": [
                {
                    "category": "HARM_CATEGORY_HARASSMENT",
                    "threshold": "BLOCK_NONE"
                },
                {
                    "category": "HARM_CATEGORY_HATE_SPEECH",
                    "threshold": "BLOCK_NONE"
                },
                {
                    "category": "HARM_CATEGORY_SEXUALLY_EXPLICIT",
                    "threshold": "BLOCK_NONE"
                },
                {
                    "category": "HARM_CATEGORY_DANGEROUS_CONTENT",
                    "threshold": "BLOCK_NONE"
                }
            ]
        }

        url = f"{self.endpoint}?key={self.api_key}"

        try:
            async with httpx.AsyncClient() as client:
                response = await client.post(
                    url,
                    headers=headers,
                    json=body,
                    timeout=timeout
                )

                if response.status_code == 200:
                    result = response.json()
                    candidates = result.get("candidates", [])
                    if candidates:
                        content = candidates[0].get("content", {})
                        parts = content.get("parts", [])
                        if parts:
                            translated = parts[0].get("text", "").strip()
                            # 불필요한 따옴표 제거
                            if translated.startswith('"') and translated.endswith('"'):
                                translated = translated[1:-1]
                            return translated

                elif response.status_code == 429:
                    # Rate limit 초과
                    return None
                elif response.status_code == 400:
                    # Bad request (blocked content 등)
                    return None

        except httpx.TimeoutException:
            return None
        except Exception as e:
            print(f"[GeminiTranslator] Error: {e}")
            return None

        return None
