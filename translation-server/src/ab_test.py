"""
A/B Testing for Translation Models

Provides infrastructure for comparing translation model quality:
- Traffic splitting between baseline and experimental models
- Metrics collection (BLEU, chrF, latency, user feedback)
- Statistical significance testing
- Automatic rollback on degradation
"""
import json
import logging
import random
import time
from dataclasses import dataclass, field, asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Optional, Literal
from collections import defaultdict
import hashlib
import threading
from enum import Enum

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)


class ABTestStatus(Enum):
    """A/B test status"""
    DRAFT = "draft"
    RUNNING = "running"
    PAUSED = "paused"
    COMPLETED = "completed"
    ROLLED_BACK = "rolled_back"


@dataclass
class ModelVariant:
    """Model variant configuration"""
    name: str
    model_path: str
    traffic_percentage: float  # 0.0 to 1.0
    is_control: bool = False  # True for baseline/control group


@dataclass
class TranslationResult:
    """Single translation result for analysis"""
    variant_name: str
    user_id: str
    source_text: str
    translated_text: str
    source_lang: str
    target_lang: str
    latency_ms: float
    timestamp: str
    bleu_score: Optional[float] = None
    user_rating: Optional[int] = None  # 1-5 scale
    reference_text: Optional[str] = None


@dataclass
class VariantMetrics:
    """Aggregated metrics for a variant"""
    variant_name: str
    total_requests: int = 0
    total_latency_ms: float = 0.0
    bleu_scores: List[float] = field(default_factory=list)
    user_ratings: List[int] = field(default_factory=list)
    errors: int = 0

    @property
    def avg_latency_ms(self) -> float:
        return self.total_latency_ms / self.total_requests if self.total_requests > 0 else 0

    @property
    def avg_bleu(self) -> float:
        return sum(self.bleu_scores) / len(self.bleu_scores) if self.bleu_scores else 0

    @property
    def avg_rating(self) -> float:
        return sum(self.user_ratings) / len(self.user_ratings) if self.user_ratings else 0

    @property
    def error_rate(self) -> float:
        return self.errors / self.total_requests if self.total_requests > 0 else 0


@dataclass
class ABTestConfig:
    """A/B test configuration"""
    test_id: str
    name: str
    description: str
    variants: List[ModelVariant]
    start_time: Optional[str] = None
    end_time: Optional[str] = None
    min_samples_per_variant: int = 1000
    significance_level: float = 0.05  # p-value threshold
    max_error_rate: float = 0.05  # Auto-rollback threshold
    max_latency_increase: float = 0.2  # 20% latency increase threshold


class ABTestManager:
    """
    Manages A/B tests for translation models

    Features:
    - Consistent user assignment (same user always gets same variant)
    - Real-time metrics collection
    - Statistical significance testing
    - Automatic rollback on quality degradation
    """

    def __init__(self, config: ABTestConfig, results_dir: str = "ab_test_results"):
        """
        Initialize A/B test manager

        Args:
            config: Test configuration
            results_dir: Directory for storing results
        """
        self.config = config
        self.results_dir = Path(results_dir)
        self.results_dir.mkdir(parents=True, exist_ok=True)

        self.status = ABTestStatus.DRAFT
        self.metrics: Dict[str, VariantMetrics] = {}
        self.results: List[TranslationResult] = []
        self.user_assignments: Dict[str, str] = {}

        self._lock = threading.Lock()

        # Initialize metrics for each variant
        for variant in config.variants:
            self.metrics[variant.name] = VariantMetrics(variant_name=variant.name)

        # Validate traffic percentages
        total_traffic = sum(v.traffic_percentage for v in config.variants)
        if abs(total_traffic - 1.0) > 0.001:
            raise ValueError(f"Traffic percentages must sum to 1.0, got {total_traffic}")

        # Ensure exactly one control group
        controls = [v for v in config.variants if v.is_control]
        if len(controls) != 1:
            raise ValueError(f"Exactly one control variant required, got {len(controls)}")

        self.control_variant = controls[0]

    def start(self):
        """Start the A/B test"""
        self.status = ABTestStatus.RUNNING
        self.config.start_time = datetime.now(timezone.utc).isoformat()
        logger.info(f"A/B test '{self.config.name}' started")

    def pause(self):
        """Pause the A/B test"""
        self.status = ABTestStatus.PAUSED
        logger.info(f"A/B test '{self.config.name}' paused")

    def resume(self):
        """Resume the A/B test"""
        self.status = ABTestStatus.RUNNING
        logger.info(f"A/B test '{self.config.name}' resumed")

    def complete(self):
        """Mark test as completed"""
        self.status = ABTestStatus.COMPLETED
        self.config.end_time = datetime.now(timezone.utc).isoformat()
        self._save_results()
        logger.info(f"A/B test '{self.config.name}' completed")

    def rollback(self, reason: str):
        """Rollback to control variant"""
        self.status = ABTestStatus.ROLLED_BACK
        self.config.end_time = datetime.now(timezone.utc).isoformat()

        # Set all traffic to control
        for variant in self.config.variants:
            if variant.is_control:
                variant.traffic_percentage = 1.0
            else:
                variant.traffic_percentage = 0.0

        self._save_results()
        logger.warning(f"A/B test '{self.config.name}' rolled back: {reason}")

    def assign_variant(self, user_id: str) -> ModelVariant:
        """
        Assign a user to a variant (consistently)

        Uses deterministic hashing to ensure same user always gets same variant
        """
        if self.status != ABTestStatus.RUNNING:
            # Return control variant if test not running
            return self.control_variant

        with self._lock:
            # Check existing assignment
            if user_id in self.user_assignments:
                variant_name = self.user_assignments[user_id]
                return next(v for v in self.config.variants if v.name == variant_name)

            # Deterministic assignment based on user_id hash
            hash_value = int(hashlib.md5(
                f"{self.config.test_id}:{user_id}".encode()
            ).hexdigest(), 16)

            # Normalize to 0-1 range
            normalized = (hash_value % 10000) / 10000

            # Assign based on traffic percentages
            cumulative = 0.0
            for variant in self.config.variants:
                cumulative += variant.traffic_percentage
                if normalized < cumulative:
                    self.user_assignments[user_id] = variant.name
                    return variant

            # Fallback to last variant
            self.user_assignments[user_id] = self.config.variants[-1].name
            return self.config.variants[-1]

    def record_result(self, result: TranslationResult):
        """Record a translation result"""
        with self._lock:
            self.results.append(result)

            # Update metrics
            metrics = self.metrics[result.variant_name]
            metrics.total_requests += 1
            metrics.total_latency_ms += result.latency_ms

            if result.bleu_score is not None:
                metrics.bleu_scores.append(result.bleu_score)

            if result.user_rating is not None:
                metrics.user_ratings.append(result.user_rating)

        # Check for auto-rollback conditions
        self._check_rollback_conditions()

    def record_error(self, variant_name: str):
        """Record an error for a variant"""
        with self._lock:
            self.metrics[variant_name].errors += 1

        self._check_rollback_conditions()

    def _check_rollback_conditions(self):
        """Check if automatic rollback is needed"""
        if self.status != ABTestStatus.RUNNING:
            return

        control_metrics = self.metrics[self.control_variant.name]

        for variant in self.config.variants:
            if variant.is_control:
                continue

            metrics = self.metrics[variant.name]

            # Check error rate
            if metrics.total_requests >= 100 and metrics.error_rate > self.config.max_error_rate:
                self.rollback(f"Error rate too high for {variant.name}: {metrics.error_rate:.2%}")
                return

            # Check latency increase
            if (control_metrics.total_requests >= 100 and
                metrics.total_requests >= 100 and
                control_metrics.avg_latency_ms > 0):

                latency_increase = (metrics.avg_latency_ms - control_metrics.avg_latency_ms) / control_metrics.avg_latency_ms

                if latency_increase > self.config.max_latency_increase:
                    self.rollback(
                        f"Latency increase too high for {variant.name}: "
                        f"{latency_increase:.2%} (threshold: {self.config.max_latency_increase:.2%})"
                    )
                    return

    def get_statistics(self) -> Dict:
        """Get current test statistics"""
        stats = {
            "test_id": self.config.test_id,
            "name": self.config.name,
            "status": self.status.value,
            "start_time": self.config.start_time,
            "end_time": self.config.end_time,
            "variants": {}
        }

        for variant_name, metrics in self.metrics.items():
            stats["variants"][variant_name] = {
                "total_requests": metrics.total_requests,
                "avg_latency_ms": round(metrics.avg_latency_ms, 2),
                "avg_bleu": round(metrics.avg_bleu, 4),
                "avg_rating": round(metrics.avg_rating, 2),
                "error_rate": round(metrics.error_rate, 4),
                "bleu_sample_size": len(metrics.bleu_scores),
                "rating_sample_size": len(metrics.user_ratings)
            }

        # Add statistical significance if enough samples
        if self._has_sufficient_samples():
            stats["significance"] = self._compute_significance()

        return stats

    def _has_sufficient_samples(self) -> bool:
        """Check if we have enough samples for significance testing"""
        for metrics in self.metrics.values():
            if metrics.total_requests < self.config.min_samples_per_variant:
                return False
        return True

    def _compute_significance(self) -> Dict:
        """Compute statistical significance using t-test"""
        try:
            from scipy import stats
        except ImportError:
            logger.warning("scipy not installed, skipping significance testing")
            return {"error": "scipy not installed"}

        control_metrics = self.metrics[self.control_variant.name]
        significance_results = {}

        for variant in self.config.variants:
            if variant.is_control:
                continue

            variant_metrics = self.metrics[variant.name]

            # BLEU significance test
            if control_metrics.bleu_scores and variant_metrics.bleu_scores:
                t_stat, p_value = stats.ttest_ind(
                    control_metrics.bleu_scores,
                    variant_metrics.bleu_scores
                )

                bleu_diff = variant_metrics.avg_bleu - control_metrics.avg_bleu

                significance_results[variant.name] = {
                    "bleu_difference": round(bleu_diff, 4),
                    "bleu_p_value": round(p_value, 4),
                    "bleu_significant": p_value < self.config.significance_level,
                    "bleu_improved": bleu_diff > 0 and p_value < self.config.significance_level
                }

            # Latency significance test
            # (Note: We don't have individual latency samples in this implementation,
            # so we can't do a proper t-test. In production, store individual latencies.)

        return significance_results

    def _save_results(self):
        """Save test results to file"""
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"ab_test_{self.config.test_id}_{timestamp}.json"
        filepath = self.results_dir / filename

        data = {
            "config": {
                "test_id": self.config.test_id,
                "name": self.config.name,
                "description": self.config.description,
                "variants": [
                    {
                        "name": v.name,
                        "model_path": v.model_path,
                        "traffic_percentage": v.traffic_percentage,
                        "is_control": v.is_control
                    }
                    for v in self.config.variants
                ],
                "start_time": self.config.start_time,
                "end_time": self.config.end_time
            },
            "status": self.status.value,
            "statistics": self.get_statistics(),
            "sample_results": [asdict(r) for r in self.results[-100:]]  # Last 100 results
        }

        with open(filepath, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)

        logger.info(f"Results saved to: {filepath}")

    def print_report(self):
        """Print human-readable test report"""
        stats = self.get_statistics()

        print("\n" + "=" * 70)
        print("A/B TEST REPORT")
        print("=" * 70)
        print(f"Test: {stats['name']} ({stats['test_id']})")
        print(f"Status: {stats['status']}")
        print(f"Started: {stats['start_time']}")
        if stats['end_time']:
            print(f"Ended: {stats['end_time']}")

        print("\n" + "-" * 70)
        print("VARIANT METRICS")
        print("-" * 70)

        for variant_name, variant_stats in stats["variants"].items():
            is_control = any(v.is_control and v.name == variant_name for v in self.config.variants)
            control_marker = " (CONTROL)" if is_control else ""

            print(f"\n{variant_name}{control_marker}:")
            print(f"  Requests:     {variant_stats['total_requests']:,}")
            print(f"  Avg Latency:  {variant_stats['avg_latency_ms']:.2f} ms")
            print(f"  Avg BLEU:     {variant_stats['avg_bleu']:.4f} ({variant_stats['bleu_sample_size']} samples)")
            print(f"  Avg Rating:   {variant_stats['avg_rating']:.2f} ({variant_stats['rating_sample_size']} samples)")
            print(f"  Error Rate:   {variant_stats['error_rate']:.2%}")

        if "significance" in stats and not stats["significance"].get("error"):
            print("\n" + "-" * 70)
            print("STATISTICAL SIGNIFICANCE")
            print("-" * 70)

            for variant_name, sig_stats in stats["significance"].items():
                status = "✓ IMPROVED" if sig_stats["bleu_improved"] else "✗ NOT IMPROVED"
                print(f"\n{variant_name} vs Control:")
                print(f"  BLEU Difference: {sig_stats['bleu_difference']:+.4f}")
                print(f"  p-value:         {sig_stats['bleu_p_value']:.4f}")
                print(f"  Significant:     {'Yes' if sig_stats['bleu_significant'] else 'No'}")
                print(f"  Result:          {status}")

        print("\n" + "=" * 70)


# Convenience function for quick A/B test setup
def create_ab_test(
    test_name: str,
    control_model_path: str,
    experimental_model_path: str,
    control_traffic: float = 0.5,
    description: str = ""
) -> ABTestManager:
    """
    Create a simple A/B test between two models

    Args:
        test_name: Name of the test
        control_model_path: Path to baseline model
        experimental_model_path: Path to experimental model
        control_traffic: Traffic percentage for control (default 50%)
        description: Test description

    Returns:
        Configured ABTestManager
    """
    test_id = f"ab_{datetime.now().strftime('%Y%m%d_%H%M%S')}"

    config = ABTestConfig(
        test_id=test_id,
        name=test_name,
        description=description,
        variants=[
            ModelVariant(
                name="control",
                model_path=control_model_path,
                traffic_percentage=control_traffic,
                is_control=True
            ),
            ModelVariant(
                name="experimental",
                model_path=experimental_model_path,
                traffic_percentage=1.0 - control_traffic,
                is_control=False
            )
        ]
    )

    return ABTestManager(config)


# CLI interface
if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="A/B Test Management")
    parser.add_argument("--control", required=True, help="Control model path")
    parser.add_argument("--experimental", required=True, help="Experimental model path")
    parser.add_argument("--name", default="Translation A/B Test", help="Test name")
    parser.add_argument("--traffic-split", type=float, default=0.5, help="Control traffic percentage")
    parser.add_argument("--simulate", action="store_true", help="Run simulation with random data")
    parser.add_argument("--simulate-samples", type=int, default=1000, help="Number of simulation samples")

    args = parser.parse_args()

    # Create test
    manager = create_ab_test(
        test_name=args.name,
        control_model_path=args.control,
        experimental_model_path=args.experimental,
        control_traffic=args.traffic_split
    )

    if args.simulate:
        # Simulation mode for testing
        manager.start()

        for i in range(args.simulate_samples):
            user_id = f"user_{i % 100}"  # 100 unique users
            variant = manager.assign_variant(user_id)

            # Simulate translation result
            result = TranslationResult(
                variant_name=variant.name,
                user_id=user_id,
                source_text="테스트 문장입니다.",
                translated_text="This is a test sentence.",
                source_lang="ko",
                target_lang="en",
                latency_ms=50 + random.gauss(0, 10) + (5 if variant.name == "experimental" else 0),
                timestamp=datetime.now(timezone.utc).isoformat(),
                bleu_score=0.35 + random.gauss(0, 0.05) + (0.02 if variant.name == "experimental" else 0)
            )

            manager.record_result(result)

            # Occasional user rating
            if random.random() < 0.1:
                result.user_rating = random.randint(3, 5)

        manager.complete()
        manager.print_report()
    else:
        print(f"A/B test '{args.name}' created with ID: {manager.config.test_id}")
        print(f"Control: {args.control}")
        print(f"Experimental: {args.experimental}")
        print(f"Traffic split: {args.traffic_split:.0%} / {1-args.traffic_split:.0%}")
        print("\nCall manager.start() to begin the test")
