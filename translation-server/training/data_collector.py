"""
Data Collector for M2M-100 Fine-tuning

Collects translation pairs from:
1. Chat logs (if available)
2. Commercial API translations (Azure/Gemini)
3. Manual curated data
"""
import json
import asyncio
import hashlib
from pathlib import Path
from datetime import datetime
from typing import Optional, Dict, List, Tuple
from dataclasses import dataclass, asdict
import logging

import httpx

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


@dataclass
class TranslationPair:
    """Single translation pair for training"""
    source_text: str
    target_text: str
    source_lang: str
    target_lang: str
    source: str  # "chat_log", "azure", "gemini", "manual"
    quality_score: Optional[float] = None  # 0.0 - 1.0
    created_at: Optional[str] = None

    def __post_init__(self):
        if self.created_at is None:
            self.created_at = datetime.utcnow().isoformat()

    @property
    def hash_id(self) -> str:
        """Unique identifier for deduplication"""
        content = f"{self.source_text}|{self.source_lang}|{self.target_lang}"
        return hashlib.md5(content.encode()).hexdigest()[:12]


class DataCollector:
    """
    Collects and manages translation pairs for fine-tuning

    Supports multiple data sources:
    - Chat log extraction
    - Azure Translator API
    - Gemini API
    - Manual input
    """

    SUPPORTED_LANGS = ["ko", "en", "zh", "ja"]

    def __init__(
        self,
        output_dir: str = "data/collected",
        azure_key: Optional[str] = None,
        azure_region: str = "koreacentral",
        gemini_key: Optional[str] = None
    ):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

        self.azure_key = azure_key
        self.azure_region = azure_region
        self.gemini_key = gemini_key

        self.collected_pairs: Dict[str, TranslationPair] = {}
        self._load_existing()

    def _load_existing(self):
        """Load existing collected data"""
        for json_file in self.output_dir.glob("*.jsonl"):
            with open(json_file, "r", encoding="utf-8") as f:
                for line in f:
                    try:
                        data = json.loads(line.strip())
                        pair = TranslationPair(**data)
                        self.collected_pairs[pair.hash_id] = pair
                    except Exception as e:
                        logger.warning(f"Failed to load line: {e}")

        logger.info(f"Loaded {len(self.collected_pairs)} existing pairs")

    def add_pair(self, pair: TranslationPair) -> bool:
        """
        Add a translation pair (with deduplication)

        Returns:
            True if pair was added, False if duplicate
        """
        if pair.hash_id in self.collected_pairs:
            return False

        self.collected_pairs[pair.hash_id] = pair
        return True

    def add_from_dict(
        self,
        source_text: str,
        target_text: str,
        source_lang: str,
        target_lang: str,
        source: str = "manual",
        quality_score: Optional[float] = None
    ) -> bool:
        """Convenience method to add a pair from parameters"""
        pair = TranslationPair(
            source_text=source_text,
            target_text=target_text,
            source_lang=source_lang,
            target_lang=target_lang,
            source=source,
            quality_score=quality_score
        )
        return self.add_pair(pair)

    async def collect_from_azure(
        self,
        texts: List[str],
        source_lang: str,
        target_lang: str
    ) -> List[TranslationPair]:
        """
        Collect translations using Azure Translator API

        Args:
            texts: Source texts to translate
            source_lang: Source language code
            target_lang: Target language code

        Returns:
            List of collected translation pairs
        """
        if not self.azure_key:
            raise ValueError("Azure API key not configured")

        # Azure language code mapping
        lang_map = {"zh": "zh-Hans"}
        azure_target = lang_map.get(target_lang, target_lang)
        azure_source = lang_map.get(source_lang, source_lang)

        url = "https://api.cognitive.microsofttranslator.com/translate"
        headers = {
            "Ocp-Apim-Subscription-Key": self.azure_key,
            "Ocp-Apim-Subscription-Region": self.azure_region,
            "Content-Type": "application/json"
        }
        params = {
            "api-version": "3.0",
            "from": azure_source,
            "to": azure_target
        }

        collected = []

        async with httpx.AsyncClient(timeout=30.0) as client:
            # Batch by 100 (Azure limit)
            for i in range(0, len(texts), 100):
                batch = texts[i:i+100]
                body = [{"text": t} for t in batch]

                try:
                    response = await client.post(
                        url,
                        headers=headers,
                        params=params,
                        json=body
                    )
                    response.raise_for_status()

                    results = response.json()
                    for text, result in zip(batch, results):
                        if result.get("translations"):
                            translated = result["translations"][0]["text"]
                            pair = TranslationPair(
                                source_text=text,
                                target_text=translated,
                                source_lang=source_lang,
                                target_lang=target_lang,
                                source="azure",
                                quality_score=0.9  # Azure quality baseline
                            )
                            if self.add_pair(pair):
                                collected.append(pair)

                    logger.info(f"Azure batch {i//100 + 1}: {len(collected)} new pairs")

                except Exception as e:
                    logger.error(f"Azure API error: {e}")

        return collected

    async def collect_from_gemini(
        self,
        texts: List[str],
        source_lang: str,
        target_lang: str
    ) -> List[TranslationPair]:
        """
        Collect translations using Gemini API

        Args:
            texts: Source texts to translate
            source_lang: Source language code
            target_lang: Target language code

        Returns:
            List of collected translation pairs
        """
        if not self.gemini_key:
            raise ValueError("Gemini API key not configured")

        lang_names = {
            "ko": "Korean",
            "en": "English",
            "zh": "Chinese",
            "ja": "Japanese"
        }

        url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash-lite:generateContent?key={self.gemini_key}"

        collected = []

        async with httpx.AsyncClient(timeout=60.0) as client:
            for text in texts:
                prompt = f"""Translate the following text from {lang_names.get(source_lang, source_lang)} to {lang_names.get(target_lang, target_lang)}.
Only output the translation, nothing else.

Text: {text}"""

                body = {
                    "contents": [{"parts": [{"text": prompt}]}],
                    "generationConfig": {
                        "temperature": 0.1,
                        "maxOutputTokens": 1024
                    }
                }

                try:
                    response = await client.post(url, json=body)
                    response.raise_for_status()

                    result = response.json()
                    candidates = result.get("candidates", [])
                    if candidates:
                        translated = candidates[0]["content"]["parts"][0]["text"].strip()
                        pair = TranslationPair(
                            source_text=text,
                            target_text=translated,
                            source_lang=source_lang,
                            target_lang=target_lang,
                            source="gemini",
                            quality_score=0.85  # Gemini quality baseline
                        )
                        if self.add_pair(pair):
                            collected.append(pair)

                    # Rate limiting
                    await asyncio.sleep(0.5)

                except Exception as e:
                    logger.error(f"Gemini API error for '{text[:50]}...': {e}")

        logger.info(f"Gemini collected: {len(collected)} new pairs")
        return collected

    def collect_from_chat_log(
        self,
        log_file: str,
        source_lang: str,
        target_lang: str
    ) -> List[TranslationPair]:
        """
        Extract translation pairs from chat logs

        Expected format: JSON Lines with 'original' and 'translated' fields
        """
        collected = []
        log_path = Path(log_file)

        if not log_path.exists():
            logger.warning(f"Chat log file not found: {log_file}")
            return collected

        with open(log_path, "r", encoding="utf-8") as f:
            for line in f:
                try:
                    data = json.loads(line.strip())
                    if "original" in data and "translated" in data:
                        pair = TranslationPair(
                            source_text=data["original"],
                            target_text=data["translated"],
                            source_lang=source_lang,
                            target_lang=target_lang,
                            source="chat_log",
                            quality_score=0.7  # Needs validation
                        )
                        if self.add_pair(pair):
                            collected.append(pair)
                except Exception:
                    continue

        logger.info(f"Chat log collected: {len(collected)} new pairs")
        return collected

    def save(self, filename: Optional[str] = None) -> str:
        """
        Save collected data to JSONL file

        Returns:
            Path to saved file
        """
        if filename is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"collected_{timestamp}.jsonl"

        output_path = self.output_dir / filename

        with open(output_path, "w", encoding="utf-8") as f:
            for pair in self.collected_pairs.values():
                f.write(json.dumps(asdict(pair), ensure_ascii=False) + "\n")

        logger.info(f"Saved {len(self.collected_pairs)} pairs to {output_path}")
        return str(output_path)

    def get_stats(self) -> Dict:
        """Get collection statistics"""
        stats = {
            "total_pairs": len(self.collected_pairs),
            "by_source": {},
            "by_lang_pair": {},
            "avg_quality": 0.0
        }

        quality_sum = 0
        quality_count = 0

        for pair in self.collected_pairs.values():
            # By source
            stats["by_source"][pair.source] = stats["by_source"].get(pair.source, 0) + 1

            # By language pair
            lang_pair = f"{pair.source_lang}->{pair.target_lang}"
            stats["by_lang_pair"][lang_pair] = stats["by_lang_pair"].get(lang_pair, 0) + 1

            # Quality average
            if pair.quality_score is not None:
                quality_sum += pair.quality_score
                quality_count += 1

        if quality_count > 0:
            stats["avg_quality"] = round(quality_sum / quality_count, 3)

        return stats

    def export_for_training(
        self,
        output_path: str,
        min_quality: float = 0.7,
        languages: Optional[List[str]] = None
    ) -> int:
        """
        Export data in training format

        Format: JSONL with 'source', 'target', 'source_lang', 'target_lang'

        Args:
            output_path: Output file path
            min_quality: Minimum quality score filter
            languages: Filter by language codes

        Returns:
            Number of exported pairs
        """
        exported = 0

        with open(output_path, "w", encoding="utf-8") as f:
            for pair in self.collected_pairs.values():
                # Quality filter
                if pair.quality_score is not None and pair.quality_score < min_quality:
                    continue

                # Language filter
                if languages:
                    if pair.source_lang not in languages or pair.target_lang not in languages:
                        continue

                record = {
                    "source": pair.source_text,
                    "target": pair.target_text,
                    "source_lang": pair.source_lang,
                    "target_lang": pair.target_lang
                }
                f.write(json.dumps(record, ensure_ascii=False) + "\n")
                exported += 1

        logger.info(f"Exported {exported} pairs for training to {output_path}")
        return exported


# Example usage and CLI
if __name__ == "__main__":
    import argparse
    import os

    parser = argparse.ArgumentParser(description="Collect translation data")
    parser.add_argument("--azure-key", default=os.getenv("AZURE_TRANSLATOR_KEY"))
    parser.add_argument("--gemini-key", default=os.getenv("GEMINI_API_KEY"))
    parser.add_argument("--output-dir", default="data/collected")
    parser.add_argument("--source-lang", default="ko")
    parser.add_argument("--target-lang", default="en")
    parser.add_argument("--input-file", help="Input file with source texts (one per line)")
    parser.add_argument("--provider", choices=["azure", "gemini"], default="azure")

    args = parser.parse_args()

    collector = DataCollector(
        output_dir=args.output_dir,
        azure_key=args.azure_key,
        gemini_key=args.gemini_key
    )

    if args.input_file:
        with open(args.input_file, "r", encoding="utf-8") as f:
            texts = [line.strip() for line in f if line.strip()]

        if args.provider == "azure":
            asyncio.run(collector.collect_from_azure(
                texts, args.source_lang, args.target_lang
            ))
        else:
            asyncio.run(collector.collect_from_gemini(
                texts, args.source_lang, args.target_lang
            ))

    collector.save()
    print("\nStatistics:")
    print(json.dumps(collector.get_stats(), indent=2))
