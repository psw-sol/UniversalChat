"""
Model Evaluation Script

Evaluate fine-tuned M2M-100 model with BLEU scores on test set
"""
import json
import logging
from pathlib import Path
from typing import List, Dict, Tuple

import torch
from transformers import AutoTokenizer, AutoModelForSeq2SeqLM
from peft import PeftModel
from tqdm import tqdm

# Try to import sacrebleu, fall back to simple BLEU if not available
try:
    import sacrebleu
    HAS_SACREBLEU = True
except ImportError:
    HAS_SACREBLEU = False
    print("Warning: sacrebleu not installed. Using simple BLEU calculation.")

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)


class SimpleBleu:
    """Simple BLEU implementation for fallback"""

    @staticmethod
    def ngrams(tokens: List[str], n: int) -> Dict[Tuple, int]:
        ngram_counts = {}
        for i in range(len(tokens) - n + 1):
            ngram = tuple(tokens[i:i+n])
            ngram_counts[ngram] = ngram_counts.get(ngram, 0) + 1
        return ngram_counts

    @staticmethod
    def compute(hypothesis: str, reference: str) -> float:
        """Compute simple BLEU score"""
        hyp_tokens = hypothesis.lower().split()
        ref_tokens = reference.lower().split()

        if len(hyp_tokens) == 0:
            return 0.0

        # Compute n-gram precisions
        precisions = []
        for n in range(1, 5):  # 1-gram to 4-gram
            hyp_ngrams = SimpleBleu.ngrams(hyp_tokens, n)
            ref_ngrams = SimpleBleu.ngrams(ref_tokens, n)

            if not hyp_ngrams:
                precisions.append(0.0)
                continue

            matches = 0
            total = 0
            for ngram, count in hyp_ngrams.items():
                total += count
                if ngram in ref_ngrams:
                    matches += min(count, ref_ngrams[ngram])

            precisions.append(matches / total if total > 0 else 0.0)

        # Geometric mean
        import math
        if 0 in precisions:
            return 0.0

        log_precision = sum(math.log(p) for p in precisions) / 4

        # Brevity penalty
        bp = 1.0 if len(hyp_tokens) >= len(ref_tokens) else math.exp(1 - len(ref_tokens) / len(hyp_tokens))

        return bp * math.exp(log_precision) * 100


class ModelEvaluator:
    """Evaluate translation model"""

    LANG_CODES = {
        "ko": "ko",
        "en": "en",
        "zh": "zh",
        "ja": "ja"
    }

    def __init__(
        self,
        base_model_name: str = "facebook/m2m100_418M",
        finetuned_path: str = "./outputs/m2m100-finetuned",
        device: str = None
    ):
        self.base_model_name = base_model_name
        self.finetuned_path = finetuned_path
        self.device = device or ("cuda" if torch.cuda.is_available() else "cpu")

        self.tokenizer = None
        self.model = None

    def load_model(self):
        """Load fine-tuned model"""
        logger.info(f"Loading tokenizer from {self.finetuned_path}")
        self.tokenizer = AutoTokenizer.from_pretrained(self.finetuned_path)

        logger.info(f"Loading base model: {self.base_model_name}")
        base_model = AutoModelForSeq2SeqLM.from_pretrained(
            self.base_model_name,
            device_map="auto",
            torch_dtype=torch.float16
        )

        logger.info(f"Loading LoRA weights from {self.finetuned_path}")
        self.model = PeftModel.from_pretrained(base_model, self.finetuned_path)
        self.model.eval()

        logger.info("Model loaded successfully")

    def translate(
        self,
        text: str,
        source_lang: str,
        target_lang: str,
        max_length: int = 256
    ) -> str:
        """Translate single text"""
        src_code = self.LANG_CODES.get(source_lang, source_lang)
        tgt_code = self.LANG_CODES.get(target_lang, target_lang)

        self.tokenizer.src_lang = src_code

        inputs = self.tokenizer(
            text,
            return_tensors="pt",
            max_length=max_length,
            truncation=True
        )
        inputs = {k: v.to(self.model.device) for k, v in inputs.items()}

        # Get target language token id
        forced_bos_token_id = self.tokenizer.get_lang_id(tgt_code)

        with torch.no_grad():
            outputs = self.model.generate(
                **inputs,
                forced_bos_token_id=forced_bos_token_id,
                max_length=max_length,
                num_beams=5,
                early_stopping=True
            )

        return self.tokenizer.decode(outputs[0], skip_special_tokens=True)

    def load_test_data(self, test_file: str) -> List[Dict]:
        """Load test data from JSONL"""
        data = []
        with open(test_file, "r", encoding="utf-8") as f:
            for line in f:
                try:
                    data.append(json.loads(line.strip()))
                except json.JSONDecodeError:
                    continue
        return data

    def evaluate(
        self,
        test_file: str,
        max_samples: int = None,
        show_examples: int = 5
    ) -> Dict:
        """Evaluate model on test set"""
        if self.model is None:
            self.load_model()

        test_data = self.load_test_data(test_file)
        if max_samples:
            test_data = test_data[:max_samples]

        logger.info(f"Evaluating on {len(test_data)} samples")

        # Group by language pair
        results_by_pair = {}
        examples = []

        for item in tqdm(test_data, desc="Translating"):
            source = item["source"]
            target = item["target"]
            source_lang = item["source_lang"]
            target_lang = item["target_lang"]

            pair_key = f"{source_lang}-{target_lang}"

            if pair_key not in results_by_pair:
                results_by_pair[pair_key] = {
                    "hypotheses": [],
                    "references": []
                }

            # Translate
            hypothesis = self.translate(source, source_lang, target_lang)

            results_by_pair[pair_key]["hypotheses"].append(hypothesis)
            results_by_pair[pair_key]["references"].append(target)

            # Collect examples
            if len(examples) < show_examples:
                examples.append({
                    "source": source,
                    "source_lang": source_lang,
                    "target_lang": target_lang,
                    "reference": target,
                    "hypothesis": hypothesis
                })

        # Calculate BLEU scores
        bleu_scores = {}

        for pair_key, data in results_by_pair.items():
            hypotheses = data["hypotheses"]
            references = data["references"]

            if HAS_SACREBLEU:
                bleu = sacrebleu.corpus_bleu(hypotheses, [references])
                bleu_scores[pair_key] = {
                    "bleu": bleu.score,
                    "samples": len(hypotheses)
                }
            else:
                # Simple average BLEU
                scores = [SimpleBleu.compute(h, r) for h, r in zip(hypotheses, references)]
                avg_bleu = sum(scores) / len(scores) if scores else 0
                bleu_scores[pair_key] = {
                    "bleu": avg_bleu,
                    "samples": len(hypotheses)
                }

        # Overall BLEU
        if HAS_SACREBLEU:
            all_hyp = []
            all_ref = []
            for data in results_by_pair.values():
                all_hyp.extend(data["hypotheses"])
                all_ref.extend(data["references"])
            overall_bleu = sacrebleu.corpus_bleu(all_hyp, [all_ref]).score
        else:
            total_bleu = 0
            total_samples = 0
            for pair_key, data in bleu_scores.items():
                total_bleu += data["bleu"] * data["samples"]
                total_samples += data["samples"]
            overall_bleu = total_bleu / total_samples if total_samples > 0 else 0

        return {
            "overall_bleu": overall_bleu,
            "bleu_by_pair": bleu_scores,
            "examples": examples,
            "total_samples": len(test_data)
        }


def main():
    import argparse

    parser = argparse.ArgumentParser(description="Evaluate fine-tuned translation model")
    parser.add_argument("--base-model", default="facebook/m2m100_418M")
    parser.add_argument("--finetuned-path", default="./outputs/m2m100-finetuned")
    parser.add_argument("--test-file", default="data/cleaned/test.jsonl")
    parser.add_argument("--max-samples", type=int, default=None)
    parser.add_argument("--examples", type=int, default=10)

    args = parser.parse_args()

    evaluator = ModelEvaluator(
        base_model_name=args.base_model,
        finetuned_path=args.finetuned_path
    )

    results = evaluator.evaluate(
        test_file=args.test_file,
        max_samples=args.max_samples,
        show_examples=args.examples
    )

    # Print results
    print("\n" + "=" * 60)
    print("EVALUATION RESULTS")
    print("=" * 60)

    print(f"\n📊 Overall BLEU Score: {results['overall_bleu']:.2f}")
    print(f"📝 Total Samples: {results['total_samples']}")

    print("\n📈 BLEU by Language Pair:")
    for pair, data in sorted(results["bleu_by_pair"].items()):
        print(f"   {pair}: {data['bleu']:.2f} ({data['samples']} samples)")

    print("\n📋 Translation Examples:")
    print("-" * 60)
    for i, ex in enumerate(results["examples"], 1):
        print(f"\n[{i}] {ex['source_lang']} → {ex['target_lang']}")
        print(f"   Source:     {ex['source'][:80]}...")
        print(f"   Reference:  {ex['reference'][:80]}...")
        print(f"   Hypothesis: {ex['hypothesis'][:80]}...")

    print("\n" + "=" * 60)

    # Save results
    output_file = Path(args.finetuned_path) / "evaluation_results.json"
    with open(output_file, "w", encoding="utf-8") as f:
        json.dump(results, f, ensure_ascii=False, indent=2)

    print(f"\n✅ Results saved to: {output_file}")


if __name__ == "__main__":
    main()
