"""
Download Translation Datasets from HuggingFace

More reliable alternative to direct Tatoeba/OPUS downloads
Uses HuggingFace datasets library for consistent access
"""
import json
import logging
import argparse
from pathlib import Path
from typing import List, Set, Tuple
from collections import defaultdict

from datasets import load_dataset
from tqdm import tqdm

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)


class HFDatasetDownloader:
    """Download translation datasets from HuggingFace"""

    # Available dataset configurations
    DATASETS = {
        "opus100": {
            "name": "opus100",
            "pairs": {
                "ko-en": "en-ko",
                "zh-en": "en-zh",
                "ja-en": "en-ja",
            }
        },
        "tatoeba": {
            "name": "tatoeba",
            "pairs": {
                "ko-en": "eng-kor",
                "zh-en": "cmn-eng",
                "ja-en": "eng-jpn",
            }
        },
        "iwslt2017": {
            "name": "iwslt2017",
            "pairs": {
                "ko-en": "iwslt2017-ko-en",
                "zh-en": "iwslt2017-zh-en",
                "ja-en": "iwslt2017-ja-en",
            }
        }
    }

    def __init__(
        self,
        output_dir: str = "data/public",
        cache_dir: str = ".cache/huggingface"
    ):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.cache_dir = cache_dir
        self.seen_hashes: Set[str] = set()

    def _hash_pair(self, source: str, target: str) -> str:
        """Create hash for deduplication"""
        import hashlib
        return hashlib.md5(f"{source}|||{target}".encode()).hexdigest()

    def download_opus100(
        self,
        lang_pair: str,
        max_samples: int = 50000
    ) -> List[dict]:
        """
        Download OPUS-100 dataset

        Args:
            lang_pair: Language pair (e.g., "ko-en")
            max_samples: Maximum samples to download
        """
        pairs = []

        # Map to OPUS-100 config name
        src_lang, tgt_lang = lang_pair.split("-")

        # OPUS-100 uses "en-XX" format
        if "en" in lang_pair:
            other_lang = src_lang if tgt_lang == "en" else tgt_lang
            config = f"en-{other_lang}"
        else:
            config = lang_pair

        logger.info(f"Loading OPUS-100 {config}...")

        try:
            dataset = load_dataset(
                "opus100",
                config,
                split="train",
                cache_dir=self.cache_dir,
                trust_remote_code=True
            )

            logger.info(f"Dataset loaded: {len(dataset)} samples")

            for item in tqdm(dataset, desc=f"Processing {config}"):
                if len(pairs) >= max_samples:
                    break

                translation = item.get("translation", {})

                # Get source and target based on requested direction
                if src_lang == "en":
                    source = translation.get("en", "")
                    target = translation.get(other_lang, "")
                else:
                    source = translation.get(other_lang, "")
                    target = translation.get("en", "")

                if not source or not target:
                    continue

                # Deduplication
                pair_hash = self._hash_pair(source, target)
                if pair_hash in self.seen_hashes:
                    continue
                self.seen_hashes.add(pair_hash)

                pairs.append({
                    "source": source,
                    "target": target,
                    "source_lang": src_lang,
                    "target_lang": tgt_lang,
                    "dataset": "opus100"
                })

            logger.info(f"Collected {len(pairs)} pairs from OPUS-100 {config}")

        except Exception as e:
            logger.error(f"Failed to load OPUS-100 {config}: {e}")

        return pairs

    def download_tatoeba(
        self,
        lang_pair: str,
        max_samples: int = 10000
    ) -> List[dict]:
        """
        Download Tatoeba dataset

        Args:
            lang_pair: Language pair (e.g., "ko-en")
            max_samples: Maximum samples
        """
        pairs = []
        src_lang, tgt_lang = lang_pair.split("-")

        # Tatoeba language codes
        tatoeba_codes = {
            "ko": "kor",
            "en": "eng",
            "zh": "cmn",
            "ja": "jpn"
        }

        src_code = tatoeba_codes.get(src_lang, src_lang)
        tgt_code = tatoeba_codes.get(tgt_lang, tgt_lang)

        # Try both directions
        configs_to_try = [
            f"{src_code}-{tgt_code}",
            f"{tgt_code}-{src_code}"
        ]

        for config in configs_to_try:
            try:
                logger.info(f"Trying Tatoeba config: {config}")

                dataset = load_dataset(
                    "tatoeba",
                    lang1=config.split("-")[0],
                    lang2=config.split("-")[1],
                    split="train",
                    cache_dir=self.cache_dir,
                    trust_remote_code=True
                )

                logger.info(f"Dataset loaded: {len(dataset)} samples")

                for item in tqdm(dataset, desc=f"Processing {config}"):
                    if len(pairs) >= max_samples:
                        break

                    translation = item.get("translation", {})
                    src_text = translation.get(config.split("-")[0], "")
                    tgt_text = translation.get(config.split("-")[1], "")

                    if not src_text or not tgt_text:
                        continue

                    # Swap if needed to match requested direction
                    if config.startswith(tgt_code):
                        src_text, tgt_text = tgt_text, src_text

                    # Deduplication
                    pair_hash = self._hash_pair(src_text, tgt_text)
                    if pair_hash in self.seen_hashes:
                        continue
                    self.seen_hashes.add(pair_hash)

                    pairs.append({
                        "source": src_text,
                        "target": tgt_text,
                        "source_lang": src_lang,
                        "target_lang": tgt_lang,
                        "dataset": "tatoeba"
                    })

                if pairs:
                    logger.info(f"Collected {len(pairs)} pairs from Tatoeba {config}")
                    break

            except Exception as e:
                logger.warning(f"Tatoeba {config} not available: {e}")
                continue

        return pairs

    def download_all(
        self,
        lang_pairs: List[str] = None,
        max_per_pair: int = 20000,
        output_file: str = "training_data.jsonl"
    ) -> int:
        """
        Download from all available datasets

        Args:
            lang_pairs: List of language pairs (default: ko-en, zh-en, ja-en)
            max_per_pair: Max samples per language pair
            output_file: Output filename
        """
        if lang_pairs is None:
            lang_pairs = ["ko-en", "zh-en", "ja-en"]

        all_pairs = []
        stats = defaultdict(lambda: defaultdict(int))

        for lang_pair in lang_pairs:
            logger.info(f"\n=== Downloading {lang_pair} ===")

            # Try OPUS-100 first (larger dataset)
            opus_pairs = self.download_opus100(lang_pair, max_per_pair)
            all_pairs.extend(opus_pairs)
            stats[lang_pair]["opus100"] = len(opus_pairs)

            # Also try reverse direction
            reverse_pair = "-".join(lang_pair.split("-")[::-1])
            opus_reverse = self.download_opus100(reverse_pair, max_per_pair)
            all_pairs.extend(opus_reverse)
            stats[reverse_pair]["opus100"] = len(opus_reverse)

            # Supplement with Tatoeba if needed
            if len(opus_pairs) < max_per_pair // 2:
                remaining = max_per_pair - len(opus_pairs)
                tatoeba_pairs = self.download_tatoeba(lang_pair, remaining)
                all_pairs.extend(tatoeba_pairs)
                stats[lang_pair]["tatoeba"] = len(tatoeba_pairs)

        # Save to file
        output_path = self.output_dir / output_file
        with open(output_path, "w", encoding="utf-8") as f:
            for pair in all_pairs:
                f.write(json.dumps(pair, ensure_ascii=False) + "\n")

        # Print statistics
        print("\n" + "=" * 50)
        print("DOWNLOAD STATISTICS")
        print("=" * 50)

        total = 0
        for lang_pair, sources in stats.items():
            pair_total = sum(sources.values())
            total += pair_total
            print(f"\n{lang_pair}:")
            for source, count in sources.items():
                print(f"  {source}: {count:,}")
            print(f"  Total: {pair_total:,}")

        print(f"\n{'=' * 50}")
        print(f"TOTAL: {total:,} translation pairs")
        print(f"Saved to: {output_path}")
        print("=" * 50)

        return total


def main():
    parser = argparse.ArgumentParser(description="Download HuggingFace translation datasets")
    parser.add_argument(
        "--lang-pairs",
        nargs="+",
        default=["ko-en", "zh-en", "ja-en"],
        help="Language pairs to download"
    )
    parser.add_argument(
        "--max-per-pair",
        type=int,
        default=20000,
        help="Maximum samples per language pair"
    )
    parser.add_argument(
        "--output-dir",
        default="data/public",
        help="Output directory"
    )
    parser.add_argument(
        "--output-file",
        default="training_data.jsonl",
        help="Output filename"
    )

    args = parser.parse_args()

    downloader = HFDatasetDownloader(output_dir=args.output_dir)
    downloader.download_all(
        lang_pairs=args.lang_pairs,
        max_per_pair=args.max_per_pair,
        output_file=args.output_file
    )


if __name__ == "__main__":
    main()
