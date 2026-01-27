"""
Public Dataset Downloader for M2M-100 Fine-tuning

Downloads and processes translation datasets:
1. Tatoeba - Sentence pairs from community
2. OPUS-100 - Large-scale parallel corpus
3. CCAligned - Web-crawled parallel sentences
"""
import json
import gzip
import shutil
from pathlib import Path
from datetime import datetime, timezone
from typing import Optional, List, Dict, Iterator, Tuple
from dataclasses import dataclass
import logging
import hashlib

import httpx
from tqdm import tqdm

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


@dataclass
class DatasetInfo:
    """Dataset metadata"""
    name: str
    url: str
    description: str
    license: str
    estimated_size: str
    lang_pairs: List[str]


# Available datasets
DATASETS = {
    "tatoeba": DatasetInfo(
        name="Tatoeba",
        url="https://object.pouta.csc.fi/Tatoeba-Challenge-v2023-09-26",
        description="Community-contributed sentence pairs",
        license="CC BY 2.0",
        estimated_size="~50MB per language pair",
        lang_pairs=["eng-jpn", "eng-kor", "eng-cmn", "jpn-kor", "jpn-cmn", "kor-cmn"]
    ),
    "opus100": DatasetInfo(
        name="OPUS-100",
        url="https://opus.nlpl.eu/OPUS-100/v1.0",
        description="Large-scale parallel corpus from OPUS",
        license="Various (mostly CC)",
        estimated_size="~100MB per language pair",
        lang_pairs=["en-ja", "en-ko", "en-zh", "ja-ko", "ja-zh", "ko-zh"]
    )
}

# Language code mappings
LANG_CODE_MAP = {
    # Tatoeba codes
    "eng": "en",
    "jpn": "ja",
    "kor": "ko",
    "cmn": "zh",
    # OPUS codes (same as our standard)
    "en": "en",
    "ja": "ja",
    "ko": "ko",
    "zh": "zh"
}

REVERSE_LANG_MAP = {
    "en": ["eng", "en"],
    "ja": ["jpn", "ja"],
    "ko": ["kor", "ko"],
    "zh": ["cmn", "zh", "zho"]
}


class DatasetDownloader:
    """
    Downloads and processes public translation datasets

    Supported datasets:
    - Tatoeba: High-quality community sentences
    - OPUS-100: Large-scale parallel corpus
    """

    CACHE_DIR = Path("data/cache")
    OUTPUT_DIR = Path("data/datasets")

    def __init__(self, cache_dir: Optional[str] = None, output_dir: Optional[str] = None):
        self.cache_dir = Path(cache_dir) if cache_dir else self.CACHE_DIR
        self.output_dir = Path(output_dir) if output_dir else self.OUTPUT_DIR

        self.cache_dir.mkdir(parents=True, exist_ok=True)
        self.output_dir.mkdir(parents=True, exist_ok=True)

    async def download_file(
        self,
        url: str,
        output_path: Path,
        chunk_size: int = 8192
    ) -> bool:
        """
        Download file with progress bar

        Args:
            url: Source URL
            output_path: Destination path
            chunk_size: Download chunk size

        Returns:
            True if successful
        """
        if output_path.exists():
            logger.info(f"File already exists: {output_path}")
            return True

        try:
            async with httpx.AsyncClient(follow_redirects=True, timeout=300.0) as client:
                async with client.stream("GET", url) as response:
                    response.raise_for_status()

                    total = int(response.headers.get("content-length", 0))
                    output_path.parent.mkdir(parents=True, exist_ok=True)

                    with open(output_path, "wb") as f:
                        with tqdm(total=total, unit="B", unit_scale=True, desc=output_path.name) as pbar:
                            async for chunk in response.aiter_bytes(chunk_size):
                                f.write(chunk)
                                pbar.update(len(chunk))

            logger.info(f"Downloaded: {output_path}")
            return True

        except Exception as e:
            logger.error(f"Download failed for {url}: {e}")
            if output_path.exists():
                output_path.unlink()
            return False

    def _parse_tatoeba_line(self, line: str) -> Optional[Tuple[str, str]]:
        """Parse a Tatoeba TSV line"""
        parts = line.strip().split("\t")
        if len(parts) >= 2:
            return parts[0], parts[1]
        return None

    async def download_tatoeba(
        self,
        source_lang: str,
        target_lang: str,
        max_pairs: int = 50000
    ) -> List[Dict]:
        """
        Download Tatoeba sentence pairs

        Args:
            source_lang: Source language (en, ja, ko, zh)
            target_lang: Target language
            max_pairs: Maximum number of pairs to download

        Returns:
            List of translation pairs
        """
        # Map to Tatoeba codes
        src_codes = REVERSE_LANG_MAP.get(source_lang, [source_lang])
        tgt_codes = REVERSE_LANG_MAP.get(target_lang, [target_lang])

        # Try different code combinations
        for src_code in src_codes:
            for tgt_code in tgt_codes:
                # Tatoeba uses alphabetical ordering
                codes = sorted([src_code, tgt_code])
                pair_name = f"{codes[0]}-{codes[1]}"

                url = f"{DATASETS['tatoeba'].url}/{pair_name}.txt.gz"
                cache_file = self.cache_dir / f"tatoeba_{pair_name}.txt.gz"

                if await self.download_file(url, cache_file):
                    # Parse the file
                    pairs = []
                    try:
                        with gzip.open(cache_file, "rt", encoding="utf-8") as f:
                            for i, line in enumerate(f):
                                if i >= max_pairs:
                                    break

                                result = self._parse_tatoeba_line(line)
                                if result:
                                    text1, text2 = result
                                    # Determine which is source/target based on order
                                    if codes[0] in src_codes:
                                        src_text, tgt_text = text1, text2
                                    else:
                                        src_text, tgt_text = text2, text1

                                    pairs.append({
                                        "source": src_text,
                                        "target": tgt_text,
                                        "source_lang": source_lang,
                                        "target_lang": target_lang,
                                        "dataset": "tatoeba"
                                    })

                        logger.info(f"Tatoeba {pair_name}: {len(pairs)} pairs")
                        return pairs

                    except Exception as e:
                        logger.error(f"Failed to parse {cache_file}: {e}")

        logger.warning(f"No Tatoeba data found for {source_lang}-{target_lang}")
        return []

    async def download_opus100(
        self,
        source_lang: str,
        target_lang: str,
        split: str = "train",
        max_pairs: int = 50000
    ) -> List[Dict]:
        """
        Download OPUS-100 dataset

        Args:
            source_lang: Source language
            target_lang: Target language
            split: Dataset split (train, dev, test)
            max_pairs: Maximum pairs

        Returns:
            List of translation pairs
        """
        # OPUS-100 uses specific naming convention
        codes = sorted([source_lang, target_lang])
        pair_name = f"{codes[0]}-{codes[1]}"

        base_url = f"https://data.statmt.org/opus-100-corpus/v1.0/supervised/{pair_name}"

        pairs = []

        # Download both source and target files
        src_url = f"{base_url}/opus.{pair_name}-{split}.{codes[0]}"
        tgt_url = f"{base_url}/opus.{pair_name}-{split}.{codes[1]}"

        src_file = self.cache_dir / f"opus100_{pair_name}_{split}.{codes[0]}"
        tgt_file = self.cache_dir / f"opus100_{pair_name}_{split}.{codes[1]}"

        src_ok = await self.download_file(src_url, src_file)
        tgt_ok = await self.download_file(tgt_url, tgt_file)

        if src_ok and tgt_ok:
            try:
                with open(src_file, "r", encoding="utf-8") as sf, \
                     open(tgt_file, "r", encoding="utf-8") as tf:

                    for i, (src_line, tgt_line) in enumerate(zip(sf, tf)):
                        if i >= max_pairs:
                            break

                        src_text = src_line.strip()
                        tgt_text = tgt_line.strip()

                        if src_text and tgt_text:
                            # Match to requested language order
                            if codes[0] == source_lang:
                                pairs.append({
                                    "source": src_text,
                                    "target": tgt_text,
                                    "source_lang": source_lang,
                                    "target_lang": target_lang,
                                    "dataset": "opus100"
                                })
                            else:
                                pairs.append({
                                    "source": tgt_text,
                                    "target": src_text,
                                    "source_lang": source_lang,
                                    "target_lang": target_lang,
                                    "dataset": "opus100"
                                })

                logger.info(f"OPUS-100 {pair_name}: {len(pairs)} pairs")

            except Exception as e:
                logger.error(f"Failed to parse OPUS-100 files: {e}")

        return pairs

    async def download_all_pairs(
        self,
        languages: List[str] = None,
        datasets: List[str] = None,
        max_per_pair: int = 10000
    ) -> Dict[str, List[Dict]]:
        """
        Download all language pairs from specified datasets

        Args:
            languages: List of language codes (default: ko, en, zh, ja)
            datasets: List of dataset names (default: tatoeba, opus100)
            max_per_pair: Maximum pairs per language combination

        Returns:
            Dict mapping language pair to list of translation pairs
        """
        if languages is None:
            languages = ["ko", "en", "zh", "ja"]

        if datasets is None:
            datasets = ["tatoeba", "opus100"]

        all_pairs = {}

        # Generate all language pair combinations
        for i, src_lang in enumerate(languages):
            for tgt_lang in languages[i+1:]:
                pair_key = f"{src_lang}-{tgt_lang}"
                all_pairs[pair_key] = []
                all_pairs[f"{tgt_lang}-{src_lang}"] = []

                for dataset in datasets:
                    if dataset == "tatoeba":
                        pairs = await self.download_tatoeba(
                            src_lang, tgt_lang, max_per_pair // len(datasets)
                        )
                    elif dataset == "opus100":
                        pairs = await self.download_opus100(
                            src_lang, tgt_lang, max_pairs=max_per_pair // len(datasets)
                        )
                    else:
                        continue

                    all_pairs[pair_key].extend(pairs)

                    # Also add reverse pairs
                    for p in pairs:
                        all_pairs[f"{tgt_lang}-{src_lang}"].append({
                            "source": p["target"],
                            "target": p["source"],
                            "source_lang": tgt_lang,
                            "target_lang": src_lang,
                            "dataset": p["dataset"]
                        })

                logger.info(f"Total for {pair_key}: {len(all_pairs[pair_key])} pairs")

        return all_pairs

    def save_dataset(
        self,
        pairs: List[Dict],
        filename: str,
        deduplicate: bool = True
    ) -> str:
        """
        Save dataset to JSONL file

        Args:
            pairs: List of translation pairs
            filename: Output filename
            deduplicate: Remove duplicates

        Returns:
            Path to saved file
        """
        if deduplicate:
            seen = set()
            unique_pairs = []
            for p in pairs:
                key = hashlib.md5(
                    f"{p['source']}|{p['target']}".encode()
                ).hexdigest()
                if key not in seen:
                    seen.add(key)
                    unique_pairs.append(p)
            pairs = unique_pairs

        output_path = self.output_dir / filename

        with open(output_path, "w", encoding="utf-8") as f:
            for p in pairs:
                f.write(json.dumps(p, ensure_ascii=False) + "\n")

        logger.info(f"Saved {len(pairs)} pairs to {output_path}")
        return str(output_path)

    def merge_datasets(
        self,
        input_files: List[str],
        output_file: str,
        shuffle: bool = True
    ) -> str:
        """
        Merge multiple dataset files

        Args:
            input_files: List of input file paths
            output_file: Output file path
            shuffle: Shuffle merged data

        Returns:
            Path to merged file
        """
        import random

        all_pairs = []

        for input_file in input_files:
            with open(input_file, "r", encoding="utf-8") as f:
                for line in f:
                    try:
                        all_pairs.append(json.loads(line.strip()))
                    except json.JSONDecodeError:
                        continue

        if shuffle:
            random.shuffle(all_pairs)

        return self.save_dataset(all_pairs, output_file)


# CLI interface
if __name__ == "__main__":
    import argparse
    import asyncio

    parser = argparse.ArgumentParser(description="Download translation datasets")
    parser.add_argument(
        "--datasets",
        nargs="+",
        default=["tatoeba"],
        choices=["tatoeba", "opus100"],
        help="Datasets to download"
    )
    parser.add_argument(
        "--languages",
        nargs="+",
        default=["ko", "en", "zh", "ja"],
        help="Language codes"
    )
    parser.add_argument(
        "--max-per-pair",
        type=int,
        default=10000,
        help="Maximum pairs per language combination"
    )
    parser.add_argument(
        "--output",
        default="training_data.jsonl",
        help="Output filename"
    )
    parser.add_argument(
        "--cache-dir",
        default="data/cache",
        help="Cache directory"
    )
    parser.add_argument(
        "--output-dir",
        default="data/datasets",
        help="Output directory"
    )

    args = parser.parse_args()

    async def main():
        downloader = DatasetDownloader(
            cache_dir=args.cache_dir,
            output_dir=args.output_dir
        )

        all_pairs = await downloader.download_all_pairs(
            languages=args.languages,
            datasets=args.datasets,
            max_per_pair=args.max_per_pair
        )

        # Flatten all pairs
        merged = []
        for pairs in all_pairs.values():
            merged.extend(pairs)

        # Save
        output_path = downloader.save_dataset(merged, args.output)
        print(f"\nSaved {len(merged)} translation pairs to {output_path}")

        # Print statistics
        print("\nDataset Statistics:")
        stats = {}
        for p in merged:
            key = f"{p['source_lang']}->{p['target_lang']}"
            stats[key] = stats.get(key, 0) + 1

        for key, count in sorted(stats.items()):
            print(f"  {key}: {count:,} pairs")

    asyncio.run(main())
