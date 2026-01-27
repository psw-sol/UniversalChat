"""
LoRA Adapter Merger for M2M-100 Fine-tuned Models

Merges LoRA adapters with the base model to create a standalone model
that can be used without the PEFT library.
"""
import json
import logging
import shutil
from pathlib import Path
from typing import Optional

import torch
from transformers import AutoTokenizer, AutoModelForSeq2SeqLM
from peft import PeftModel

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)


class AdapterMerger:
    """
    Merges LoRA adapters with base model

    Creates a standalone model that:
    - Does not require PEFT library at inference time
    - Has slightly larger size but faster inference
    - Can be quantized separately for deployment
    """

    def __init__(
        self,
        adapter_path: str,
        output_path: str,
        base_model_name: Optional[str] = None,
        device: str = "cuda" if torch.cuda.is_available() else "cpu"
    ):
        """
        Initialize merger

        Args:
            adapter_path: Path to LoRA adapter directory
            output_path: Path to save merged model
            base_model_name: Override base model name (auto-detected from adapter config)
            device: Device to use for merging
        """
        self.adapter_path = Path(adapter_path)
        self.output_path = Path(output_path)
        self.device = device

        # Get base model name from adapter config
        if base_model_name:
            self.base_model_name = base_model_name
        else:
            self.base_model_name = self._get_base_model_name()

        self.tokenizer = None
        self.base_model = None
        self.peft_model = None
        self.merged_model = None

    def _get_base_model_name(self) -> str:
        """Extract base model name from adapter config"""
        config_path = self.adapter_path / "adapter_config.json"

        if not config_path.exists():
            logger.warning(f"Adapter config not found at {config_path}")
            return "facebook/m2m100_418M"

        with open(config_path, "r", encoding="utf-8") as f:
            config = json.load(f)

        base_model = config.get("base_model_name_or_path", "facebook/m2m100_418M")
        logger.info(f"Detected base model: {base_model}")
        return base_model

    def load_models(self):
        """Load base model and apply adapter"""
        logger.info(f"Loading base model: {self.base_model_name}")

        # Load tokenizer
        self.tokenizer = AutoTokenizer.from_pretrained(self.adapter_path)

        # Load base model in full precision for merging
        self.base_model = AutoModelForSeq2SeqLM.from_pretrained(
            self.base_model_name,
            torch_dtype=torch.float16,
            device_map="auto"
        )

        logger.info(f"Loading adapter from: {self.adapter_path}")

        # Load PEFT model with adapter
        self.peft_model = PeftModel.from_pretrained(
            self.base_model,
            self.adapter_path
        )

        logger.info("Models loaded successfully")

    def merge(self):
        """Merge adapter weights into base model"""
        if self.peft_model is None:
            self.load_models()

        logger.info("Merging adapter weights into base model...")

        # Merge and unload - this creates a standalone model
        self.merged_model = self.peft_model.merge_and_unload()

        logger.info("Merge complete")

    def save(self, safe_serialization: bool = True):
        """
        Save merged model

        Args:
            safe_serialization: Use safetensors format (recommended)
        """
        if self.merged_model is None:
            self.merge()

        # Create output directory
        self.output_path.mkdir(parents=True, exist_ok=True)

        logger.info(f"Saving merged model to: {self.output_path}")

        # Save model
        self.merged_model.save_pretrained(
            self.output_path,
            safe_serialization=safe_serialization
        )

        # Save tokenizer
        self.tokenizer.save_pretrained(self.output_path)

        # Copy training info if exists
        training_info_path = self.adapter_path / "training_info.json"
        if training_info_path.exists():
            shutil.copy(training_info_path, self.output_path / "training_info.json")

        # Save merge info
        merge_info = {
            "base_model": self.base_model_name,
            "adapter_path": str(self.adapter_path),
            "merged": True,
            "safe_serialization": safe_serialization
        }

        with open(self.output_path / "merge_info.json", "w", encoding="utf-8") as f:
            json.dump(merge_info, f, indent=2)

        logger.info("Model saved successfully")

        # Print model size info
        self._print_model_info()

    def _print_model_info(self):
        """Print information about the merged model"""
        total_size = 0
        for file in self.output_path.iterdir():
            if file.is_file():
                total_size += file.stat().st_size

        size_mb = total_size / (1024 * 1024)
        size_gb = total_size / (1024 * 1024 * 1024)

        print("\n" + "=" * 50)
        print("MERGED MODEL INFO")
        print("=" * 50)
        print(f"Output path: {self.output_path}")
        print(f"Total size: {size_mb:.2f} MB ({size_gb:.2f} GB)")
        print(f"Base model: {self.base_model_name}")
        print("=" * 50)


def merge_multiple_adapters(
    adapter_paths: list,
    output_path: str,
    base_model_name: str = "facebook/m2m100_418M",
    weights: Optional[list] = None
):
    """
    Merge multiple LoRA adapters (experimental)

    This allows combining adapters trained on different data
    (e.g., different language pairs or domains)

    Args:
        adapter_paths: List of adapter directory paths
        output_path: Path to save merged model
        base_model_name: Base model name
        weights: Optional weights for each adapter (default: equal weights)
    """
    logger.info(f"Merging {len(adapter_paths)} adapters...")

    if weights is None:
        weights = [1.0 / len(adapter_paths)] * len(adapter_paths)

    if len(weights) != len(adapter_paths):
        raise ValueError("Number of weights must match number of adapters")

    # Load base model
    tokenizer = AutoTokenizer.from_pretrained(adapter_paths[0])
    base_model = AutoModelForSeq2SeqLM.from_pretrained(
        base_model_name,
        torch_dtype=torch.float16,
        device_map="auto"
    )

    # Load and merge each adapter
    merged_state_dict = None

    for i, (adapter_path, weight) in enumerate(zip(adapter_paths, weights)):
        logger.info(f"Processing adapter {i+1}/{len(adapter_paths)}: {adapter_path}")

        # Load adapter
        peft_model = PeftModel.from_pretrained(base_model, adapter_path)
        merged = peft_model.merge_and_unload()

        # Get state dict
        current_state = merged.state_dict()

        if merged_state_dict is None:
            # First adapter - initialize with weighted state dict
            merged_state_dict = {
                k: v * weight for k, v in current_state.items()
            }
        else:
            # Add weighted state dict
            for k, v in current_state.items():
                merged_state_dict[k] = merged_state_dict[k] + v * weight

        # Reload base model for next adapter
        if i < len(adapter_paths) - 1:
            base_model = AutoModelForSeq2SeqLM.from_pretrained(
                base_model_name,
                torch_dtype=torch.float16,
                device_map="auto"
            )

    # Load final merged weights
    final_model = AutoModelForSeq2SeqLM.from_pretrained(
        base_model_name,
        torch_dtype=torch.float16,
        device_map="auto"
    )
    final_model.load_state_dict(merged_state_dict)

    # Save
    output_dir = Path(output_path)
    output_dir.mkdir(parents=True, exist_ok=True)

    final_model.save_pretrained(output_dir, safe_serialization=True)
    tokenizer.save_pretrained(output_dir)

    # Save merge info
    merge_info = {
        "base_model": base_model_name,
        "adapters": [str(p) for p in adapter_paths],
        "weights": weights,
        "merged": True
    }

    with open(output_dir / "merge_info.json", "w", encoding="utf-8") as f:
        json.dump(merge_info, f, indent=2)

    logger.info(f"Multi-adapter merge complete: {output_path}")


# CLI interface
if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Merge LoRA adapter with base model")
    parser.add_argument("--adapter", required=True, help="Path to adapter directory")
    parser.add_argument("--output", required=True, help="Output path for merged model")
    parser.add_argument("--base-model", help="Override base model name")
    parser.add_argument("--no-safe", action="store_true", help="Disable safetensors format")

    args = parser.parse_args()

    merger = AdapterMerger(
        adapter_path=args.adapter,
        output_path=args.output,
        base_model_name=args.base_model
    )

    merger.merge()
    merger.save(safe_serialization=not args.no_safe)
