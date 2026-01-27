"""
Model Export for M2M-100 Fine-tuned Models

Exports trained models in various formats for deployment:
- ONNX (cross-platform inference)
- TorchScript (PyTorch native)
- Quantized (INT8/INT4 for edge deployment)
"""
import json
import logging
from pathlib import Path
from typing import Optional, List, Literal
from dataclasses import dataclass

import torch
from transformers import AutoTokenizer, AutoModelForSeq2SeqLM

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)


@dataclass
class ExportConfig:
    """Export configuration"""
    model_path: str
    output_dir: str
    formats: List[str]  # ["onnx", "torchscript", "quantized"]
    quantization_type: str = "dynamic"  # dynamic, static, int4
    opset_version: int = 14  # ONNX opset version
    max_length: int = 256


class ModelExporter:
    """
    Exports translation models for deployment

    Supports multiple export formats for different deployment scenarios:
    - ONNX: Cross-platform, optimized for inference servers
    - TorchScript: Native PyTorch, good for Python deployments
    - Quantized: Reduced precision for edge/mobile deployment
    """

    SUPPORTED_FORMATS = ["onnx", "torchscript", "quantized", "huggingface"]

    def __init__(self, config: ExportConfig):
        """
        Initialize exporter

        Args:
            config: Export configuration
        """
        self.config = config
        self.model_path = Path(config.model_path)
        self.output_dir = Path(config.output_dir)

        self.tokenizer = None
        self.model = None

        # Validate formats
        for fmt in config.formats:
            if fmt not in self.SUPPORTED_FORMATS:
                raise ValueError(f"Unsupported format: {fmt}. Supported: {self.SUPPORTED_FORMATS}")

    def load_model(self):
        """Load model and tokenizer"""
        logger.info(f"Loading model from: {self.model_path}")

        self.tokenizer = AutoTokenizer.from_pretrained(self.model_path)
        self.model = AutoModelForSeq2SeqLM.from_pretrained(
            self.model_path,
            torch_dtype=torch.float16
        )
        self.model.eval()

        logger.info("Model loaded successfully")

    def export_all(self):
        """Export model in all specified formats"""
        if self.model is None:
            self.load_model()

        self.output_dir.mkdir(parents=True, exist_ok=True)

        results = {}

        for fmt in self.config.formats:
            logger.info(f"Exporting to {fmt} format...")

            try:
                if fmt == "onnx":
                    output_path = self._export_onnx()
                elif fmt == "torchscript":
                    output_path = self._export_torchscript()
                elif fmt == "quantized":
                    output_path = self._export_quantized()
                elif fmt == "huggingface":
                    output_path = self._export_huggingface()

                results[fmt] = {
                    "status": "success",
                    "path": str(output_path)
                }
                logger.info(f"Successfully exported to {fmt}")

            except Exception as e:
                logger.error(f"Failed to export {fmt}: {e}")
                results[fmt] = {
                    "status": "failed",
                    "error": str(e)
                }

        # Save export info
        self._save_export_info(results)

        return results

    def _export_onnx(self) -> Path:
        """Export to ONNX format"""
        try:
            from optimum.onnxruntime import ORTModelForSeq2SeqLM
        except ImportError:
            logger.warning("optimum not installed, using basic ONNX export")
            return self._export_onnx_basic()

        output_path = self.output_dir / "onnx"
        output_path.mkdir(parents=True, exist_ok=True)

        # Use optimum for optimized ONNX export
        ort_model = ORTModelForSeq2SeqLM.from_pretrained(
            self.model_path,
            export=True
        )

        ort_model.save_pretrained(output_path)
        self.tokenizer.save_pretrained(output_path)

        return output_path

    def _export_onnx_basic(self) -> Path:
        """Basic ONNX export without optimum"""
        output_path = self.output_dir / "onnx"
        output_path.mkdir(parents=True, exist_ok=True)

        # Create dummy input
        dummy_input = self.tokenizer(
            "Hello world",
            return_tensors="pt",
            max_length=self.config.max_length,
            padding="max_length",
            truncation=True
        )

        # Export encoder
        encoder_path = output_path / "encoder.onnx"

        torch.onnx.export(
            self.model.get_encoder(),
            (dummy_input["input_ids"], dummy_input["attention_mask"]),
            encoder_path,
            input_names=["input_ids", "attention_mask"],
            output_names=["last_hidden_state"],
            dynamic_axes={
                "input_ids": {0: "batch", 1: "sequence"},
                "attention_mask": {0: "batch", 1: "sequence"},
                "last_hidden_state": {0: "batch", 1: "sequence"}
            },
            opset_version=self.config.opset_version
        )

        # Save tokenizer
        self.tokenizer.save_pretrained(output_path)

        # Note: Full seq2seq ONNX export is complex due to decoder
        # For production, use optimum library

        logger.warning("Basic ONNX export completed (encoder only). "
                      "For full model export, install optimum: pip install optimum[onnxruntime]")

        return output_path

    def _export_torchscript(self) -> Path:
        """Export to TorchScript format"""
        output_path = self.output_dir / "torchscript"
        output_path.mkdir(parents=True, exist_ok=True)

        # Create dummy input
        dummy_input = self.tokenizer(
            "Hello world",
            return_tensors="pt",
            max_length=self.config.max_length,
            padding="max_length",
            truncation=True
        )

        # Move to CPU for tracing
        model_cpu = self.model.cpu().float()

        # Trace encoder (decoder tracing is complex for seq2seq)
        encoder = model_cpu.get_encoder()
        traced_encoder = torch.jit.trace(
            encoder,
            (dummy_input["input_ids"], dummy_input["attention_mask"])
        )

        # Save traced encoder
        encoder_path = output_path / "encoder.pt"
        traced_encoder.save(encoder_path)

        # Save tokenizer
        self.tokenizer.save_pretrained(output_path)

        # Save config for loading
        config_dict = {
            "model_type": "m2m100",
            "traced_components": ["encoder"],
            "max_length": self.config.max_length
        }

        with open(output_path / "config.json", "w") as f:
            json.dump(config_dict, f, indent=2)

        logger.warning("TorchScript export completed (encoder only). "
                      "Full seq2seq tracing requires custom implementation.")

        return output_path

    def _export_quantized(self) -> Path:
        """Export quantized model"""
        output_path = self.output_dir / "quantized"
        output_path.mkdir(parents=True, exist_ok=True)

        if self.config.quantization_type == "dynamic":
            return self._export_dynamic_quantized(output_path)
        elif self.config.quantization_type == "int4":
            return self._export_int4_quantized(output_path)
        else:
            raise ValueError(f"Unsupported quantization type: {self.config.quantization_type}")

    def _export_dynamic_quantized(self, output_path: Path) -> Path:
        """Export with dynamic INT8 quantization"""
        # Move to CPU for quantization
        model_cpu = self.model.cpu().float()

        # Apply dynamic quantization
        quantized_model = torch.quantization.quantize_dynamic(
            model_cpu,
            {torch.nn.Linear},
            dtype=torch.qint8
        )

        # Save quantized model
        model_path = output_path / "model_quantized.pt"
        torch.save(quantized_model.state_dict(), model_path)

        # Save tokenizer
        self.tokenizer.save_pretrained(output_path)

        # Save quantization info
        quant_info = {
            "quantization_type": "dynamic",
            "dtype": "qint8",
            "quantized_layers": ["Linear"]
        }

        with open(output_path / "quantization_info.json", "w") as f:
            json.dump(quant_info, f, indent=2)

        return output_path

    def _export_int4_quantized(self, output_path: Path) -> Path:
        """Export with INT4 quantization using bitsandbytes"""
        try:
            from transformers import BitsAndBytesConfig
        except ImportError:
            raise ImportError("bitsandbytes required for INT4 quantization")

        # Load model with INT4 quantization
        bnb_config = BitsAndBytesConfig(
            load_in_4bit=True,
            bnb_4bit_quant_type="nf4",
            bnb_4bit_compute_dtype=torch.float16,
            bnb_4bit_use_double_quant=True
        )

        quantized_model = AutoModelForSeq2SeqLM.from_pretrained(
            self.model_path,
            quantization_config=bnb_config,
            device_map="auto"
        )

        # Save (bitsandbytes models need special handling)
        quantized_model.save_pretrained(output_path)
        self.tokenizer.save_pretrained(output_path)

        # Save quantization info
        quant_info = {
            "quantization_type": "int4",
            "quant_method": "nf4",
            "double_quant": True
        }

        with open(output_path / "quantization_info.json", "w") as f:
            json.dump(quant_info, f, indent=2)

        return output_path

    def _export_huggingface(self) -> Path:
        """Export in HuggingFace format (standard)"""
        output_path = self.output_dir / "huggingface"
        output_path.mkdir(parents=True, exist_ok=True)

        # Save model and tokenizer
        self.model.save_pretrained(output_path, safe_serialization=True)
        self.tokenizer.save_pretrained(output_path)

        return output_path

    def _save_export_info(self, results: dict):
        """Save export summary"""
        export_info = {
            "source_model": str(self.model_path),
            "exports": results,
            "config": {
                "formats": self.config.formats,
                "quantization_type": self.config.quantization_type,
                "max_length": self.config.max_length
            }
        }

        with open(self.output_dir / "export_info.json", "w", encoding="utf-8") as f:
            json.dump(export_info, f, indent=2, ensure_ascii=False)

        # Print summary
        print("\n" + "=" * 60)
        print("EXPORT SUMMARY")
        print("=" * 60)
        print(f"Source: {self.model_path}")
        print(f"Output: {self.output_dir}")
        print("\nResults:")

        for fmt, result in results.items():
            status = "✓" if result["status"] == "success" else "✗"
            print(f"  {status} {fmt}: {result.get('path', result.get('error', 'unknown'))}")

        print("=" * 60)


class DeploymentPackager:
    """
    Creates deployment packages for different environments
    """

    @staticmethod
    def create_docker_package(
        model_path: str,
        output_path: str,
        base_image: str = "python:3.10-slim"
    ):
        """Create Docker deployment package"""
        output_dir = Path(output_path)
        output_dir.mkdir(parents=True, exist_ok=True)

        # Copy model
        import shutil
        model_dir = output_dir / "model"
        shutil.copytree(model_path, model_dir, dirs_exist_ok=True)

        # Create Dockerfile
        dockerfile_content = f"""FROM {base_image}

WORKDIR /app

# Install dependencies
RUN pip install --no-cache-dir \\
    torch \\
    transformers \\
    sentencepiece \\
    fastapi \\
    uvicorn

# Copy model
COPY model /app/model

# Copy inference script
COPY inference_server.py /app/

EXPOSE 8000

CMD ["uvicorn", "inference_server:app", "--host", "0.0.0.0", "--port", "8000"]
"""

        with open(output_dir / "Dockerfile", "w") as f:
            f.write(dockerfile_content)

        # Create inference server script
        inference_script = '''"""
FastAPI Inference Server for Translation Model
"""
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from transformers import AutoTokenizer, AutoModelForSeq2SeqLM
import torch

app = FastAPI(title="Translation API")

# Load model
MODEL_PATH = "/app/model"
tokenizer = AutoTokenizer.from_pretrained(MODEL_PATH)
model = AutoModelForSeq2SeqLM.from_pretrained(
    MODEL_PATH,
    torch_dtype=torch.float16,
    device_map="auto"
)


class TranslationRequest(BaseModel):
    text: str
    source_lang: str
    target_lang: str


class TranslationResponse(BaseModel):
    translated_text: str
    source_lang: str
    target_lang: str


@app.post("/translate", response_model=TranslationResponse)
async def translate(request: TranslationRequest):
    try:
        tokenizer.src_lang = request.source_lang

        inputs = tokenizer(
            request.text,
            return_tensors="pt",
            max_length=256,
            truncation=True
        ).to(model.device)

        forced_bos_token_id = tokenizer.get_lang_id(request.target_lang)

        with torch.no_grad():
            outputs = model.generate(
                **inputs,
                forced_bos_token_id=forced_bos_token_id,
                num_beams=5,
                max_length=256
            )

        translated = tokenizer.decode(outputs[0], skip_special_tokens=True)

        return TranslationResponse(
            translated_text=translated,
            source_lang=request.source_lang,
            target_lang=request.target_lang
        )

    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/health")
async def health():
    return {"status": "healthy"}
'''

        with open(output_dir / "inference_server.py", "w") as f:
            f.write(inference_script)

        # Create docker-compose.yml
        compose_content = """version: '3.8'

services:
  translation:
    build: .
    ports:
      - "8000:8000"
    deploy:
      resources:
        reservations:
          devices:
            - driver: nvidia
              count: 1
              capabilities: [gpu]
    environment:
      - CUDA_VISIBLE_DEVICES=0
"""

        with open(output_dir / "docker-compose.yml", "w") as f:
            f.write(compose_content)

        logger.info(f"Docker package created at: {output_dir}")
        return output_dir


# CLI interface
if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Export translation model")
    parser.add_argument("--model", required=True, help="Path to model directory")
    parser.add_argument("--output", required=True, help="Output directory")
    parser.add_argument(
        "--formats",
        nargs="+",
        default=["huggingface"],
        choices=["onnx", "torchscript", "quantized", "huggingface"],
        help="Export formats"
    )
    parser.add_argument(
        "--quantization",
        default="dynamic",
        choices=["dynamic", "int4"],
        help="Quantization type (for quantized format)"
    )
    parser.add_argument("--docker", action="store_true", help="Create Docker deployment package")

    args = parser.parse_args()

    config = ExportConfig(
        model_path=args.model,
        output_dir=args.output,
        formats=args.formats,
        quantization_type=args.quantization
    )

    exporter = ModelExporter(config)
    results = exporter.export_all()

    if args.docker:
        # Use HuggingFace export for Docker package
        hf_path = Path(args.output) / "huggingface"
        if hf_path.exists():
            DeploymentPackager.create_docker_package(
                str(hf_path),
                str(Path(args.output) / "docker")
            )
