"""
Data Cleaner for M2M-100 Fine-tuning

Cleans and validates translation data:
1. Language detection and filtering
2. Text normalization
3. Profanity/spam filtering
4. Length and quality checks
5. Deduplication
"""
import json
import re
import hashlib
from pathlib import Path
from typing import Optional, List, Dict, Set, Tuple
from dataclasses import dataclass
from collections import Counter
import logging

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


@dataclass
class CleaningStats:
    """Statistics from cleaning process"""
    total_input: int = 0
    passed: int = 0
    failed_empty: int = 0
    failed_length: int = 0
    failed_language: int = 0
    failed_quality: int = 0
    failed_profanity: int = 0
    failed_duplicate: int = 0


class LanguageDetector:
    """
    Simple language detection based on character sets

    For production, consider using langdetect or fasttext
    """

    # Character ranges for different scripts
    PATTERNS = {
        "ko": re.compile(r'[\uAC00-\uD7AF\u1100-\u11FF\u3130-\u318F]'),  # Korean Hangul
        "ja": re.compile(r'[\u3040-\u309F\u30A0-\u30FF]'),  # Japanese Hiragana/Katakana
        "zh": re.compile(r'[\u4E00-\u9FFF]'),  # Chinese characters (also used in Japanese)
        "en": re.compile(r'[a-zA-Z]'),  # Latin alphabet
    }

    @classmethod
    def detect(cls, text: str) -> Optional[str]:
        """
        Detect language from text

        Returns:
            Detected language code or None
        """
        if not text:
            return None

        scores = {}

        for lang, pattern in cls.PATTERNS.items():
            matches = len(pattern.findall(text))
            scores[lang] = matches

        if not any(scores.values()):
            return None

        # Special handling for CJK
        # Japanese uses kanji (Chinese chars) + kana
        # Korean uses only Hangul
        # Chinese uses only hanzi

        if scores["ja"] > 0:
            return "ja"
        if scores["ko"] > 0:
            return "ko"
        if scores["zh"] > scores["en"]:
            return "zh"
        if scores["en"] > 0:
            return "en"

        return max(scores, key=lambda k: scores[k])

    @classmethod
    def verify(cls, text: str, expected_lang: str) -> bool:
        """Verify text matches expected language"""
        detected = cls.detect(text)
        return detected == expected_lang


class TextNormalizer:
    """Text normalization utilities"""

    # Unicode normalization patterns
    WHITESPACE_PATTERN = re.compile(r'\s+')
    CONTROL_CHARS = re.compile(r'[\x00-\x1F\x7F-\x9F]')

    # Common chat abbreviations to expand (optional)
    CHAT_ABBREVS = {
        "ㅋㅋ": "",  # Korean laughter - keep as is
        "ㅎㅎ": "",  # Korean laughter
        "www": "",  # Japanese laughter
        "lol": "",  # English laughter
    }

    @classmethod
    def normalize(cls, text: str, aggressive: bool = False) -> str:
        """
        Normalize text for training

        Args:
            text: Input text
            aggressive: Apply aggressive cleaning

        Returns:
            Normalized text
        """
        if not text:
            return ""

        # Remove control characters
        text = cls.CONTROL_CHARS.sub('', text)

        # Normalize whitespace
        text = cls.WHITESPACE_PATTERN.sub(' ', text)

        # Strip
        text = text.strip()

        if aggressive:
            # Remove URLs
            text = re.sub(r'https?://\S+', '', text)
            # Remove mentions
            text = re.sub(r'@\w+', '', text)
            # Normalize repeated characters (e.g., "헐헐헐헐" -> "헐헐")
            text = re.sub(r'(.)\1{3,}', r'\1\1', text)

        return text


class ProfanityFilter:
    """
    Profanity and spam filter

    Note: In production, use a proper profanity filter library
    This is a basic implementation for demonstration
    """

    # Basic patterns (extend as needed)
    SPAM_PATTERNS = [
        re.compile(r'(http[s]?://|www\.)\S+', re.IGNORECASE),  # URLs
        re.compile(r'[^\s]+\.(com|net|org|io)', re.IGNORECASE),  # Domain names
        re.compile(r'[\d\W]{10,}'),  # Long sequences of numbers/symbols
    ]

    # Placeholder for profanity words (should be loaded from file in production)
    PROFANITY_WORDS: Set[str] = set()

    @classmethod
    def load_profanity_list(cls, filepath: str):
        """Load profanity words from file"""
        try:
            with open(filepath, "r", encoding="utf-8") as f:
                cls.PROFANITY_WORDS = set(line.strip().lower() for line in f)
            logger.info(f"Loaded {len(cls.PROFANITY_WORDS)} profanity words")
        except FileNotFoundError:
            logger.warning(f"Profanity list not found: {filepath}")

    @classmethod
    def is_spam(cls, text: str) -> bool:
        """Check if text is spam"""
        for pattern in cls.SPAM_PATTERNS:
            if pattern.search(text):
                return True
        return False

    @classmethod
    def contains_profanity(cls, text: str) -> bool:
        """Check if text contains profanity"""
        if not cls.PROFANITY_WORDS:
            return False

        text_lower = text.lower()
        for word in cls.PROFANITY_WORDS:
            if word in text_lower:
                return True
        return False


class DataCleaner:
    """
    Main data cleaning pipeline

    Applies multiple filters and validations to translation pairs
    """

    def __init__(
        self,
        min_length: int = 2,
        max_length: int = 512,
        min_ratio: float = 0.3,
        max_ratio: float = 3.0,
        verify_language: bool = True,
        filter_profanity: bool = True,
        filter_spam: bool = True,
        profanity_list: Optional[str] = None
    ):
        """
        Initialize cleaner

        Args:
            min_length: Minimum text length (chars)
            max_length: Maximum text length (chars)
            min_ratio: Minimum target/source length ratio
            max_ratio: Maximum target/source length ratio
            verify_language: Verify detected language matches label
            filter_profanity: Filter profanity
            filter_spam: Filter spam content
            profanity_list: Path to profanity word list
        """
        self.min_length = min_length
        self.max_length = max_length
        self.min_ratio = min_ratio
        self.max_ratio = max_ratio
        self.verify_language = verify_language
        self.filter_profanity = filter_profanity
        self.filter_spam = filter_spam

        if profanity_list:
            ProfanityFilter.load_profanity_list(profanity_list)

        self.seen_hashes: Set[str] = set()
        self.stats = CleaningStats()

    def _compute_hash(self, source: str, target: str) -> str:
        """Compute hash for deduplication"""
        content = f"{source}|||{target}"
        return hashlib.md5(content.encode()).hexdigest()

    def clean_pair(
        self,
        source: str,
        target: str,
        source_lang: str,
        target_lang: str
    ) -> Optional[Tuple[str, str]]:
        """
        Clean a single translation pair

        Args:
            source: Source text
            target: Target text
            source_lang: Source language code
            target_lang: Target language code

        Returns:
            Cleaned (source, target) tuple or None if filtered
        """
        self.stats.total_input += 1

        # Normalize
        source = TextNormalizer.normalize(source)
        target = TextNormalizer.normalize(target)

        # Empty check
        if not source or not target:
            self.stats.failed_empty += 1
            return None

        # Length check
        if len(source) < self.min_length or len(target) < self.min_length:
            self.stats.failed_length += 1
            return None

        if len(source) > self.max_length or len(target) > self.max_length:
            self.stats.failed_length += 1
            return None

        # Length ratio check
        ratio = len(target) / len(source)
        if ratio < self.min_ratio or ratio > self.max_ratio:
            self.stats.failed_quality += 1
            return None

        # Language verification
        if self.verify_language:
            if not LanguageDetector.verify(source, source_lang):
                self.stats.failed_language += 1
                return None
            if not LanguageDetector.verify(target, target_lang):
                self.stats.failed_language += 1
                return None

        # Spam check
        if self.filter_spam:
            if ProfanityFilter.is_spam(source) or ProfanityFilter.is_spam(target):
                self.stats.failed_profanity += 1
                return None

        # Profanity check
        if self.filter_profanity:
            if ProfanityFilter.contains_profanity(source) or \
               ProfanityFilter.contains_profanity(target):
                self.stats.failed_profanity += 1
                return None

        # Deduplication
        pair_hash = self._compute_hash(source, target)
        if pair_hash in self.seen_hashes:
            self.stats.failed_duplicate += 1
            return None

        self.seen_hashes.add(pair_hash)
        self.stats.passed += 1

        return source, target

    def clean_file(
        self,
        input_path: str,
        output_path: str,
        source_key: str = "source",
        target_key: str = "target",
        source_lang_key: str = "source_lang",
        target_lang_key: str = "target_lang"
    ) -> CleaningStats:
        """
        Clean an entire JSONL file

        Args:
            input_path: Input file path
            output_path: Output file path
            source_key: Key for source text in JSON
            target_key: Key for target text in JSON
            source_lang_key: Key for source language
            target_lang_key: Key for target language

        Returns:
            Cleaning statistics
        """
        # Reset stats
        self.stats = CleaningStats()
        self.seen_hashes.clear()

        output = Path(output_path)
        output.parent.mkdir(parents=True, exist_ok=True)

        with open(input_path, "r", encoding="utf-8") as fin, \
             open(output_path, "w", encoding="utf-8") as fout:

            for line in fin:
                try:
                    data = json.loads(line.strip())
                except json.JSONDecodeError:
                    continue

                result = self.clean_pair(
                    source=data.get(source_key, ""),
                    target=data.get(target_key, ""),
                    source_lang=data.get(source_lang_key, ""),
                    target_lang=data.get(target_lang_key, "")
                )

                if result:
                    cleaned_source, cleaned_target = result
                    output_data = {
                        source_key: cleaned_source,
                        target_key: cleaned_target,
                        source_lang_key: data.get(source_lang_key),
                        target_lang_key: data.get(target_lang_key)
                    }
                    # Preserve additional metadata
                    for key in data:
                        if key not in output_data:
                            output_data[key] = data[key]

                    fout.write(json.dumps(output_data, ensure_ascii=False) + "\n")

        logger.info(f"Cleaning complete: {self.stats.passed}/{self.stats.total_input} passed")
        return self.stats

    def get_stats_report(self) -> str:
        """Generate human-readable stats report"""
        total = self.stats.total_input
        if total == 0:
            return "No data processed"

        report = [
            "Data Cleaning Report",
            "=" * 40,
            f"Total Input:      {total:,}",
            f"Passed:           {self.stats.passed:,} ({100*self.stats.passed/total:.1f}%)",
            "",
            "Filtered:",
            f"  Empty:          {self.stats.failed_empty:,}",
            f"  Length:         {self.stats.failed_length:,}",
            f"  Language:       {self.stats.failed_language:,}",
            f"  Quality:        {self.stats.failed_quality:,}",
            f"  Profanity/Spam: {self.stats.failed_profanity:,}",
            f"  Duplicate:      {self.stats.failed_duplicate:,}",
        ]

        return "\n".join(report)


def split_dataset(
    input_path: str,
    train_path: str,
    valid_path: str,
    test_path: str,
    train_ratio: float = 0.9,
    valid_ratio: float = 0.05,
    seed: int = 42
) -> Dict[str, int]:
    """
    Split dataset into train/valid/test

    Args:
        input_path: Input JSONL file
        train_path: Training set output path
        valid_path: Validation set output path
        test_path: Test set output path
        train_ratio: Training set ratio
        valid_ratio: Validation set ratio
        seed: Random seed

    Returns:
        Dict with counts for each split
    """
    import random
    random.seed(seed)

    # Load all data
    data = []
    with open(input_path, "r", encoding="utf-8") as f:
        for line in f:
            try:
                data.append(json.loads(line.strip()))
            except json.JSONDecodeError:
                continue

    # Shuffle
    random.shuffle(data)

    # Calculate split points
    total = len(data)
    train_end = int(total * train_ratio)
    valid_end = train_end + int(total * valid_ratio)

    train_data = data[:train_end]
    valid_data = data[train_end:valid_end]
    test_data = data[valid_end:]

    # Write splits
    for path, split_data in [(train_path, train_data),
                              (valid_path, valid_data),
                              (test_path, test_data)]:
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            for item in split_data:
                f.write(json.dumps(item, ensure_ascii=False) + "\n")

    counts = {
        "train": len(train_data),
        "valid": len(valid_data),
        "test": len(test_data)
    }

    logger.info(f"Split complete: train={counts['train']}, valid={counts['valid']}, test={counts['test']}")
    return counts


# CLI interface
if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Clean translation data")
    parser.add_argument("input", help="Input JSONL file")
    parser.add_argument("output", help="Output JSONL file")
    parser.add_argument("--min-length", type=int, default=2, help="Minimum text length")
    parser.add_argument("--max-length", type=int, default=512, help="Maximum text length")
    parser.add_argument("--no-language-check", action="store_true", help="Disable language verification")
    parser.add_argument("--profanity-list", help="Path to profanity word list")
    parser.add_argument("--split", action="store_true", help="Also split into train/valid/test")
    parser.add_argument("--train-ratio", type=float, default=0.9, help="Training set ratio")
    parser.add_argument("--valid-ratio", type=float, default=0.05, help="Validation set ratio")

    args = parser.parse_args()

    # Clean data
    cleaner = DataCleaner(
        min_length=args.min_length,
        max_length=args.max_length,
        verify_language=not args.no_language_check,
        profanity_list=args.profanity_list
    )

    cleaner.clean_file(args.input, args.output)
    print(cleaner.get_stats_report())

    # Split if requested
    if args.split:
        output_dir = Path(args.output).parent
        counts = split_dataset(
            args.output,
            str(output_dir / "train.jsonl"),
            str(output_dir / "valid.jsonl"),
            str(output_dir / "test.jsonl"),
            train_ratio=args.train_ratio,
            valid_ratio=args.valid_ratio
        )
        print(f"\nSplit: {counts}")
