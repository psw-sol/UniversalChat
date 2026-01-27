"""
Model Evaluation for M2M-100 Fine-tuned Models

Evaluates translation quality using:
- BLEU score (bilingual evaluation understudy)
- chrF score (character n-gram F-score)
- Custom metrics for chat domain
"""
import json
import logging
from pathlib import Path
from typing import Dict, List, Optional
from dataclasses import dataclass, asdict
from datetime import datetime, timezone

import torch
from tqdm import tqdm
import sacrebleu
from transformers import AutoTokenizer, AutoModelForSeq2SeqLM

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)


@dataclass
class EvaluationResult:
    """Evaluation results for a single language pair"""
    source_lang: str
    target_lang: str
    num_samples: int
    bleu_score: float
    chrf_score: float
    avg_latency_ms: float
    samples: List[Dict] = None  # Example translations

    def __post_init__(self):
        if self.samples is None:
            self.samples = []


@dataclass
class ModelEvaluation:
    """Complete model evaluation results"""
    model_name: str
    evaluation_date: str
    overall_bleu: float
    overall_chrf: float
    by_language_pair: Dict[str, EvaluationResult]
    improvement_over_baseline: Optional[float] = None


class TranslationEvaluator:
    """
    Evaluates translation model quality

    Supports:
    - Baseline M2M-100 model
    - Fine-tuned models (with LoRA adapters)
    - Comparison between models
    """

    LANG_CODES = {
        "ko": "ko",
        "en": "en",
        "zh": "zh",
        "ja": "ja"
    }

    def __init__(
        self,
        model_path: str,
        device: str = "cuda" if torch.cuda.is_available() else "cpu",
        use_peft: bool = False
    ):
        """
        Initialize evaluator

        Args:
            model_path: Path to model or HuggingFace model name
            device: Device to run on
            use_peft: Whether model uses PEFT/LoRA
        """
        self.model_path = model_path
        self.device = device
        self.use_peft = use_peft

        self.tokenizer = None
        self.model = None

        self._load_model()

    def _load_model(self):
        """Load model and tokenizer"""
        logger.info(f"Loading model from: {self.model_path}")

        self.tokenizer = AutoTokenizer.from_pretrained(self.model_path)

        if self.use_peft:
            # Load base model + adapter
            from peft import PeftModel

            # Get base model name from adapter config
            adapter_config_path = Path(self.model_path) / "adapter_config.json"
            if adapter_config_path.exists():
                with open(adapter_config_path) as f:
                    adapter_config = json.load(f)
                base_model_name = adapter_config.get("base_model_name_or_path", "facebook/m2m100_418M")
            else:
                base_model_name = "facebook/m2m100_418M"

            base_model = AutoModelForSeq2SeqLM.from_pretrained(
                base_model_name,
                device_map="auto",
                torch_dtype=torch.float16
            )
            self.model = PeftModel.from_pretrained(base_model, self.model_path)
        else:
            self.model = AutoModelForSeq2SeqLM.from_pretrained(
                self.model_path,
                device_map="auto",
                torch_dtype=torch.float16
            )

        self.model.eval()
        logger.info("Model loaded successfully")

    def translate(
        self,
        text: str,
        source_lang: str,
        target_lang: str,
        num_beams: int = 5,
        max_length: int = 256
    ) -> str:
        """
        Translate a single text

        Args:
            text: Source text
            source_lang: Source language code
            target_lang: Target language code
            num_beams: Beam search size
            max_length: Maximum output length

        Returns:
            Translated text
        """
        # Set source language
        src_code = self.LANG_CODES.get(source_lang, source_lang)
        self.tokenizer.src_lang = src_code

        # Tokenize
        inputs = self.tokenizer(
            text,
            return_tensors="pt",
            max_length=max_length,
            truncation=True
        ).to(self.model.device)

        # Get target language token
        tgt_code = self.LANG_CODES.get(target_lang, target_lang)
        forced_bos_token_id = self.tokenizer.get_lang_id(tgt_code)

        # Generate
        with torch.no_grad():
            outputs = self.model.generate(
                **inputs,
                forced_bos_token_id=forced_bos_token_id,
                num_beams=num_beams,
                max_length=max_length
            )

        # Decode
        translated = self.tokenizer.decode(outputs[0], skip_special_tokens=True)
        return translated

    def evaluate_dataset(
        self,
        test_file: str,
        source_key: str = "source",
        target_key: str = "target",
        source_lang_key: str = "source_lang",
        target_lang_key: str = "target_lang",
        max_samples: Optional[int] = None,
        save_predictions: bool = True
    ) -> ModelEvaluation:
        """
        Evaluate model on test dataset

        Args:
            test_file: Path to test JSONL file
            source_key: Key for source text
            target_key: Key for reference translation
            source_lang_key: Key for source language
            target_lang_key: Key for target language
            max_samples: Maximum samples to evaluate
            save_predictions: Save predictions to file

        Returns:
            ModelEvaluation with results
        """
        import time

        # Load test data
        test_data = []
        with open(test_file, "r", encoding="utf-8") as f:
            for i, line in enumerate(f):
                if max_samples and i >= max_samples:
                    break
                try:
                    test_data.append(json.loads(line.strip()))
                except json.JSONDecodeError:
                    continue

        logger.info(f"Evaluating on {len(test_data)} samples")

        # Group by language pair
        by_lang_pair: Dict[str, Dict] = {}

        for item in test_data:
            src_lang = item.get(source_lang_key, "")
            tgt_lang = item.get(target_lang_key, "")
            key = f"{src_lang}->{tgt_lang}"

            if key not in by_lang_pair:
                by_lang_pair[key] = {
                    "sources": [],
                    "references": [],
                    "predictions": [],
                    "latencies": []
                }

            by_lang_pair[key]["sources"].append(item.get(source_key, ""))
            by_lang_pair[key]["references"].append(item.get(target_key, ""))

        # Generate predictions
        for lang_pair, data in by_lang_pair.items():
            src_lang, tgt_lang = lang_pair.split("->")
            logger.info(f"Translating {lang_pair}: {len(data['sources'])} samples")

            for source in tqdm(data["sources"], desc=f"Translating {lang_pair}"):
                start_time = time.time()
                prediction = self.translate(source, src_lang, tgt_lang)
                latency = (time.time() - start_time) * 1000

                data["predictions"].append(prediction)
                data["latencies"].append(latency)

        # Calculate metrics
        results_by_pair = {}
        all_references = []
        all_predictions = []

        for lang_pair, data in by_lang_pair.items():
            src_lang, tgt_lang = lang_pair.split("->")

            references = data["references"]
            predictions = data["predictions"]

            # BLEU
            bleu = sacrebleu.corpus_bleu(predictions, [references])

            # chrF
            chrf = sacrebleu.corpus_chrf(predictions, [references])

            # Average latency
            avg_latency = sum(data["latencies"]) / len(data["latencies"])

            # Sample examples
            samples = []
            for i in range(min(5, len(data["sources"]))):
                samples.append({
                    "source": data["sources"][i],
                    "reference": references[i],
                    "prediction": predictions[i]
                })

            results_by_pair[lang_pair] = EvaluationResult(
                source_lang=src_lang,
                target_lang=tgt_lang,
                num_samples=len(references),
                bleu_score=round(bleu.score, 2),
                chrf_score=round(chrf.score, 2),
                avg_latency_ms=round(avg_latency, 2),
                samples=samples
            )

            all_references.extend(references)
            all_predictions.extend(predictions)

        # Overall metrics
        overall_bleu = sacrebleu.corpus_bleu(all_predictions, [all_references])
        overall_chrf = sacrebleu.corpus_chrf(all_predictions, [all_references])

        evaluation = ModelEvaluation(
            model_name=self.model_path,
            evaluation_date=datetime.now(timezone.utc).isoformat(),
            overall_bleu=round(overall_bleu.score, 2),
            overall_chrf=round(overall_chrf.score, 2),
            by_language_pair={k: asdict(v) for k, v in results_by_pair.items()}
        )

        # Save predictions
        if save_predictions:
            output_dir = Path("outputs/evaluations")
            output_dir.mkdir(parents=True, exist_ok=True)

            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            output_path = output_dir / f"evaluation_{timestamp}.json"

            with open(output_path, "w", encoding="utf-8") as f:
                json.dump(asdict(evaluation), f, indent=2, ensure_ascii=False)

            logger.info(f"Results saved to: {output_path}")

        return evaluation

    def print_report(self, evaluation: ModelEvaluation):
        """Print human-readable evaluation report"""
        print("\n" + "=" * 60)
        print("TRANSLATION MODEL EVALUATION REPORT")
        print("=" * 60)
        print(f"\nModel: {evaluation.model_name}")
        print(f"Date: {evaluation.evaluation_date}")
        print(f"\nOverall Scores:")
        print(f"  BLEU:  {evaluation.overall_bleu:.2f}")
        print(f"  chrF:  {evaluation.overall_chrf:.2f}")

        if evaluation.improvement_over_baseline:
            print(f"  Improvement over baseline: +{evaluation.improvement_over_baseline:.2f}%")

        print(f"\nBy Language Pair:")
        print("-" * 60)

        for lang_pair, result in evaluation.by_language_pair.items():
            if isinstance(result, dict):
                print(f"\n  {lang_pair}:")
                print(f"    Samples: {result['num_samples']}")
                print(f"    BLEU:    {result['bleu_score']:.2f}")
                print(f"    chrF:    {result['chrf_score']:.2f}")
                print(f"    Latency: {result['avg_latency_ms']:.1f}ms")

                if result.get('samples'):
                    print(f"    Example:")
                    sample = result['samples'][0]
                    print(f"      Source:     {sample['source'][:50]}...")
                    print(f"      Reference:  {sample['reference'][:50]}...")
                    print(f"      Prediction: {sample['prediction'][:50]}...")

        print("\n" + "=" * 60)


def compare_models(
    baseline_path: str,
    finetuned_path: str,
    test_file: str,
    max_samples: int = 500
) -> Dict:
    """
    Compare baseline and fine-tuned models

    Returns:
        Comparison results with improvement metrics
    """
    logger.info("Evaluating baseline model...")
    baseline_evaluator = TranslationEvaluator(baseline_path, use_peft=False)
    baseline_results = baseline_evaluator.evaluate_dataset(
        test_file, max_samples=max_samples, save_predictions=False
    )

    logger.info("Evaluating fine-tuned model...")
    finetuned_evaluator = TranslationEvaluator(finetuned_path, use_peft=True)
    finetuned_results = finetuned_evaluator.evaluate_dataset(
        test_file, max_samples=max_samples, save_predictions=True
    )

    # Calculate improvement
    bleu_improvement = finetuned_results.overall_bleu - baseline_results.overall_bleu
    chrf_improvement = finetuned_results.overall_chrf - baseline_results.overall_chrf

    finetuned_results.improvement_over_baseline = bleu_improvement

    comparison = {
        "baseline": {
            "bleu": baseline_results.overall_bleu,
            "chrf": baseline_results.overall_chrf
        },
        "finetuned": {
            "bleu": finetuned_results.overall_bleu,
            "chrf": finetuned_results.overall_chrf
        },
        "improvement": {
            "bleu": round(bleu_improvement, 2),
            "chrf": round(chrf_improvement, 2),
            "bleu_percent": round(100 * bleu_improvement / baseline_results.overall_bleu, 2) if baseline_results.overall_bleu > 0 else 0
        }
    }

    print("\n" + "=" * 60)
    print("MODEL COMPARISON RESULTS")
    print("=" * 60)
    print(f"\nBaseline ({baseline_path}):")
    print(f"  BLEU: {baseline_results.overall_bleu:.2f}")
    print(f"  chrF: {baseline_results.overall_chrf:.2f}")
    print(f"\nFine-tuned ({finetuned_path}):")
    print(f"  BLEU: {finetuned_results.overall_bleu:.2f}")
    print(f"  chrF: {finetuned_results.overall_chrf:.2f}")
    print(f"\nImprovement:")
    print(f"  BLEU: +{bleu_improvement:.2f} ({comparison['improvement']['bleu_percent']:.1f}%)")
    print(f"  chrF: +{chrf_improvement:.2f}")
    print("=" * 60)

    return comparison


# CLI interface
if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Evaluate translation model")
    parser.add_argument("--model", required=True, help="Model path or name")
    parser.add_argument("--test-file", required=True, help="Test data JSONL file")
    parser.add_argument("--peft", action="store_true", help="Model uses PEFT/LoRA")
    parser.add_argument("--max-samples", type=int, help="Maximum samples to evaluate")
    parser.add_argument("--compare-baseline", help="Baseline model to compare against")

    args = parser.parse_args()

    if args.compare_baseline:
        compare_models(
            baseline_path=args.compare_baseline,
            finetuned_path=args.model,
            test_file=args.test_file,
            max_samples=args.max_samples or 500
        )
    else:
        evaluator = TranslationEvaluator(args.model, use_peft=args.peft)
        results = evaluator.evaluate_dataset(
            args.test_file,
            max_samples=args.max_samples
        )
        evaluator.print_report(results)
