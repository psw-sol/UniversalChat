"""
Alternative Dataset Downloader

Uses currently available HuggingFace datasets:
- Helsinki-NLP/opus_books
- KoNE/koen-translation
- iwslt2017
"""
import json
import logging
import argparse
from pathlib import Path
from typing import List, Set
from collections import defaultdict
import hashlib

from datasets import load_dataset
from tqdm import tqdm

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)


class AltDatasetDownloader:
    """Download from alternative HuggingFace datasets"""

    def __init__(self, output_dir: str = "data/public"):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.seen_hashes: Set[str] = set()

    def _hash_pair(self, source: str, target: str) -> str:
        return hashlib.md5(f"{source}|||{target}".encode()).hexdigest()

    def download_opus_books(self, lang_pair: str, max_samples: int = 10000) -> List[dict]:
        """Download from Helsinki-NLP/opus_books"""
        pairs = []
        src_lang, tgt_lang = lang_pair.split("-")

        try:
            logger.info(f"Loading Helsinki-NLP/opus_books {lang_pair}...")

            dataset = load_dataset(
                "Helsinki-NLP/opus_books",
                f"{src_lang}-{tgt_lang}",
                split="train"
            )

            logger.info(f"Loaded {len(dataset)} samples")

            for item in tqdm(dataset, desc="Processing", total=min(max_samples, len(dataset))):
                if len(pairs) >= max_samples:
                    break

                translation = item.get("translation", {})
                source = translation.get(src_lang, "")
                target = translation.get(tgt_lang, "")

                if not source or not target:
                    continue

                pair_hash = self._hash_pair(source, target)
                if pair_hash in self.seen_hashes:
                    continue
                self.seen_hashes.add(pair_hash)

                pairs.append({
                    "source": source,
                    "target": target,
                    "source_lang": src_lang,
                    "target_lang": tgt_lang,
                    "dataset": "opus_books"
                })

        except Exception as e:
            logger.warning(f"opus_books {lang_pair} not available: {e}")

        return pairs

    def download_kone_korean(self, max_samples: int = 30000) -> List[dict]:
        """Download Korean-English from KoNE"""
        pairs = []

        try:
            logger.info("Loading KoNE/koen-translation...")

            dataset = load_dataset(
                "KoNE/koen-translation",
                split="train"
            )

            logger.info(f"Loaded {len(dataset)} samples")

            for item in tqdm(dataset, desc="Processing KoNE", total=min(max_samples, len(dataset))):
                if len(pairs) >= max_samples:
                    break

                source = item.get("ko", "")
                target = item.get("en", "")

                if not source or not target:
                    continue

                pair_hash = self._hash_pair(source, target)
                if pair_hash in self.seen_hashes:
                    continue
                self.seen_hashes.add(pair_hash)

                pairs.append({
                    "source": source,
                    "target": target,
                    "source_lang": "ko",
                    "target_lang": "en",
                    "dataset": "kone"
                })

        except Exception as e:
            logger.warning(f"KoNE dataset not available: {e}")

        return pairs

    def download_alt_koen(self, max_samples: int = 20000) -> List[dict]:
        """Try alternative Korean-English datasets"""
        pairs = []

        # Try different Korean-English datasets
        datasets_to_try = [
            ("sae4k/ko-en-translation-dataset", None),
            ("traintogpb/aihub-koen-translation-integrated-base-1m", None),
            ("Saxo/ko_en_translate", None),
        ]

        for dataset_name, config in datasets_to_try:
            if len(pairs) >= max_samples:
                break

            try:
                logger.info(f"Trying {dataset_name}...")

                if config:
                    ds = load_dataset(dataset_name, config, split="train")
                else:
                    ds = load_dataset(dataset_name, split="train")

                logger.info(f"Loaded {len(ds)} samples from {dataset_name}")

                for item in tqdm(ds, desc=f"Processing {dataset_name.split('/')[-1]}"):
                    if len(pairs) >= max_samples:
                        break

                    # Try different column names
                    source = item.get("ko") or item.get("korean") or item.get("source") or ""
                    target = item.get("en") or item.get("english") or item.get("target") or ""

                    if not source or not target:
                        continue

                    pair_hash = self._hash_pair(source, target)
                    if pair_hash in self.seen_hashes:
                        continue
                    self.seen_hashes.add(pair_hash)

                    pairs.append({
                        "source": source,
                        "target": target,
                        "source_lang": "ko",
                        "target_lang": "en",
                        "dataset": dataset_name.split("/")[-1]
                    })

                if pairs:
                    logger.info(f"Got {len(pairs)} pairs from {dataset_name}")

            except Exception as e:
                logger.warning(f"{dataset_name} not available: {e}")

        return pairs

    def download_alt_jaen(self, max_samples: int = 20000) -> List[dict]:
        """Try alternative Japanese-English datasets"""
        pairs = []

        datasets_to_try = [
            ("Mxode/JParaCrawl-Ja-En-Filtered", None),
            ("sudy-super/JNLI", None),
        ]

        for dataset_name, config in datasets_to_try:
            if len(pairs) >= max_samples:
                break

            try:
                logger.info(f"Trying {dataset_name}...")

                if config:
                    ds = load_dataset(dataset_name, config, split="train")
                else:
                    ds = load_dataset(dataset_name, split="train")

                logger.info(f"Loaded {len(ds)} samples from {dataset_name}")

                for item in tqdm(ds, desc=f"Processing {dataset_name.split('/')[-1]}"):
                    if len(pairs) >= max_samples:
                        break

                    source = item.get("ja") or item.get("japanese") or item.get("source") or ""
                    target = item.get("en") or item.get("english") or item.get("target") or ""

                    if not source or not target:
                        continue

                    pair_hash = self._hash_pair(source, target)
                    if pair_hash in self.seen_hashes:
                        continue
                    self.seen_hashes.add(pair_hash)

                    pairs.append({
                        "source": source,
                        "target": target,
                        "source_lang": "ja",
                        "target_lang": "en",
                        "dataset": dataset_name.split("/")[-1]
                    })

            except Exception as e:
                logger.warning(f"{dataset_name} not available: {e}")

        return pairs

    def download_alt_zhen(self, max_samples: int = 20000) -> List[dict]:
        """Try alternative Chinese-English datasets"""
        pairs = []

        datasets_to_try = [
            ("Helsinki-NLP/opus-100", "en-zh"),
            ("iwslt2017", "iwslt2017-en-zh"),
        ]

        for dataset_name, config in datasets_to_try:
            if len(pairs) >= max_samples:
                break

            try:
                logger.info(f"Trying {dataset_name} {config}...")

                ds = load_dataset(dataset_name, config, split="train")

                logger.info(f"Loaded {len(ds)} samples from {dataset_name}")

                for item in tqdm(ds, desc=f"Processing {dataset_name.split('/')[-1]}"):
                    if len(pairs) >= max_samples:
                        break

                    translation = item.get("translation", item)
                    source = translation.get("zh") or translation.get("chinese") or ""
                    target = translation.get("en") or translation.get("english") or ""

                    if not source or not target:
                        continue

                    pair_hash = self._hash_pair(source, target)
                    if pair_hash in self.seen_hashes:
                        continue
                    self.seen_hashes.add(pair_hash)

                    pairs.append({
                        "source": source,
                        "target": target,
                        "source_lang": "zh",
                        "target_lang": "en",
                        "dataset": dataset_name.split("/")[-1]
                    })

            except Exception as e:
                logger.warning(f"{dataset_name} {config} not available: {e}")

        return pairs

    def download_all(self, max_per_lang: int = 15000, output_file: str = "training_data.jsonl"):
        """Download from all available sources"""
        all_pairs = []
        stats = defaultdict(int)

        # Korean-English
        logger.info("\n=== Downloading Korean-English ===")
        koen_pairs = self.download_alt_koen(max_per_lang)
        all_pairs.extend(koen_pairs)
        stats["ko-en"] = len(koen_pairs)

        # Also add reverse
        for pair in koen_pairs[:max_per_lang//2]:
            reverse = {
                "source": pair["target"],
                "target": pair["source"],
                "source_lang": "en",
                "target_lang": "ko",
                "dataset": pair["dataset"]
            }
            all_pairs.append(reverse)
            stats["en-ko"] += 1

        # Japanese-English
        logger.info("\n=== Downloading Japanese-English ===")
        jaen_pairs = self.download_alt_jaen(max_per_lang)
        all_pairs.extend(jaen_pairs)
        stats["ja-en"] = len(jaen_pairs)

        for pair in jaen_pairs[:max_per_lang//2]:
            reverse = {
                "source": pair["target"],
                "target": pair["source"],
                "source_lang": "en",
                "target_lang": "ja",
                "dataset": pair["dataset"]
            }
            all_pairs.append(reverse)
            stats["en-ja"] += 1

        # Chinese-English
        logger.info("\n=== Downloading Chinese-English ===")
        zhen_pairs = self.download_alt_zhen(max_per_lang)
        all_pairs.extend(zhen_pairs)
        stats["zh-en"] = len(zhen_pairs)

        for pair in zhen_pairs[:max_per_lang//2]:
            reverse = {
                "source": pair["target"],
                "target": pair["source"],
                "source_lang": "en",
                "target_lang": "zh",
                "dataset": pair["dataset"]
            }
            all_pairs.append(reverse)
            stats["en-zh"] += 1

        # Save
        output_path = self.output_dir / output_file
        with open(output_path, "w", encoding="utf-8") as f:
            for pair in all_pairs:
                f.write(json.dumps(pair, ensure_ascii=False) + "\n")

        # Print stats
        print("\n" + "=" * 50)
        print("DOWNLOAD STATISTICS")
        print("=" * 50)

        total = 0
        for lang_pair, count in sorted(stats.items()):
            print(f"  {lang_pair}: {count:,}")
            total += count

        print(f"\n  TOTAL: {total:,} translation pairs")
        print(f"  Saved to: {output_path}")
        print("=" * 50)

        return total


def main():
    parser = argparse.ArgumentParser(description="Download alternative translation datasets")
    parser.add_argument("--max-per-lang", type=int, default=15000)
    parser.add_argument("--output-dir", default="data/public")
    parser.add_argument("--output-file", default="training_data.jsonl")

    args = parser.parse_args()

    downloader = AltDatasetDownloader(output_dir=args.output_dir)
    downloader.download_all(
        max_per_lang=args.max_per_lang,
        output_file=args.output_file
    )


if __name__ == "__main__":
    main()
