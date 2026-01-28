"""
M2M-100 Translation Engine with Fine-tuned LoRA Support
"""
import logging
from pathlib import Path
from typing import Optional, List
import torch
from transformers import M2M100ForConditionalGeneration, M2M100Tokenizer, BitsAndBytesConfig
from peft import PeftModel

from .config import get_settings, LANGUAGE_MAP

logger = logging.getLogger(__name__)


class TranslationEngine:
    """M2M-100 based translation engine"""

    _instance: Optional["TranslationEngine"] = None

    def __init__(self):
        self.settings = get_settings()
        self.model: Optional[M2M100ForConditionalGeneration] = None
        self.tokenizer: Optional[M2M100Tokenizer] = None
        self.device: str = "cpu"
        self._loaded = False

    @classmethod
    def get_instance(cls) -> "TranslationEngine":
        """Get singleton instance"""
        if cls._instance is None:
            cls._instance = cls()
        return cls._instance

    def load_model(self) -> bool:
        """Load the translation model (with optional fine-tuned LoRA adapter)"""
        if self._loaded:
            return True

        try:
            # Check GPU availability
            if self.settings.device == "cuda" and torch.cuda.is_available():
                self.device = "cuda"
                logger.info(f"Using GPU: {torch.cuda.get_device_name(0)}")
            else:
                self.device = "cpu"
                logger.info("Using CPU")

            # Check if fine-tuned model path is set
            finetuned_path = self.settings.finetuned_model_path
            use_finetuned = finetuned_path and Path(finetuned_path).exists()

            if use_finetuned:
                logger.info(f"Loading fine-tuned model from: {finetuned_path}")

                # Load tokenizer from fine-tuned model
                logger.info("Loading tokenizer...")
                self.tokenizer = M2M100Tokenizer.from_pretrained(finetuned_path)

                # Setup quantization config for memory efficiency
                if self.settings.use_quantization and self.device == "cuda":
                    logger.info("Using 4-bit quantization...")
                    bnb_config = BitsAndBytesConfig(
                        load_in_4bit=True,
                        bnb_4bit_quant_type="nf4",
                        bnb_4bit_compute_dtype=torch.float16,
                        bnb_4bit_use_double_quant=True
                    )
                    # Load base model with quantization
                    logger.info(f"Loading base model: {self.settings.model_name}")
                    base_model = M2M100ForConditionalGeneration.from_pretrained(
                        self.settings.model_name,
                        quantization_config=bnb_config,
                        device_map="auto",
                        torch_dtype=torch.float16
                    )
                else:
                    # Load base model without quantization
                    logger.info(f"Loading base model: {self.settings.model_name}")
                    # Use float32 for CPU (float16 not supported on CPU)
                    dtype = torch.float32 if self.device == "cpu" else torch.float16
                    base_model = M2M100ForConditionalGeneration.from_pretrained(
                        self.settings.model_name,
                        device_map="auto" if self.device == "cuda" else None,
                        torch_dtype=dtype
                    )
                    if self.device == "cpu":
                        base_model = base_model.to(self.device)

                # Load LoRA adapter
                logger.info("Loading LoRA adapter...")
                self.model = PeftModel.from_pretrained(base_model, finetuned_path)
                logger.info("Fine-tuned model loaded successfully")

            else:
                logger.info(f"Loading base model: {self.settings.model_name}")

                # Load tokenizer
                logger.info("Loading tokenizer...")
                self.tokenizer = M2M100Tokenizer.from_pretrained(self.settings.model_name)

                # Load model
                logger.info("Loading model weights...")
                self.model = M2M100ForConditionalGeneration.from_pretrained(
                    self.settings.model_name
                ).to(self.device)

            # Set to evaluation mode
            self.model.eval()

            self._loaded = True
            logger.info("Model loaded successfully")
            return True

        except Exception as e:
            logger.error(f"Failed to load model: {e}")
            return False

    def is_loaded(self) -> bool:
        """Check if model is loaded"""
        return self._loaded

    def is_gpu_available(self) -> bool:
        """Check if GPU is being used"""
        return self.device == "cuda"

    def translate(
        self,
        text: str,
        source_lang: str,
        target_lang: str
    ) -> str:
        """
        Translate text from source language to target language

        Args:
            text: Text to translate
            source_lang: Source language code (e.g., 'ko')
            target_lang: Target language code (e.g., 'en')

        Returns:
            Translated text

        Raises:
            RuntimeError: If model is not loaded
            ValueError: If language is not supported
        """
        if not self._loaded:
            raise RuntimeError("Model not loaded. Call load_model() first.")

        # Validate languages
        if source_lang not in LANGUAGE_MAP:
            raise ValueError(f"Unsupported source language: {source_lang}")
        if target_lang not in LANGUAGE_MAP:
            raise ValueError(f"Unsupported target language: {target_lang}")

        # Map to M2M-100 language codes
        src_lang = LANGUAGE_MAP[source_lang]
        tgt_lang = LANGUAGE_MAP[target_lang]

        try:
            # Set source language
            self.tokenizer.src_lang = src_lang

            # Tokenize input
            encoded = self.tokenizer(
                text,
                return_tensors="pt",
                max_length=self.settings.max_length,
                truncation=True
            ).to(self.device)

            # Generate translation
            with torch.no_grad():
                generated_tokens = self.model.generate(
                    **encoded,
                    forced_bos_token_id=self.tokenizer.get_lang_id(tgt_lang),
                    max_length=self.settings.max_length,
                    num_beams=5,
                    early_stopping=True
                )

            # Decode output
            translated = self.tokenizer.decode(
                generated_tokens[0],
                skip_special_tokens=True
            )

            return translated

        except Exception as e:
            logger.error(f"Translation error: {e}")
            raise RuntimeError(f"Translation failed: {e}")

    def translate_batch(
        self,
        texts: List[str],
        source_lang: str,
        target_lang: str
    ) -> List[str]:
        """
        Translate multiple texts in batch

        Args:
            texts: List of texts to translate
            source_lang: Source language code
            target_lang: Target language code

        Returns:
            List of translated texts
        """
        if not self._loaded:
            raise RuntimeError("Model not loaded. Call load_model() first.")

        # Validate languages
        if source_lang not in LANGUAGE_MAP:
            raise ValueError(f"Unsupported source language: {source_lang}")
        if target_lang not in LANGUAGE_MAP:
            raise ValueError(f"Unsupported target language: {target_lang}")

        src_lang = LANGUAGE_MAP[source_lang]
        tgt_lang = LANGUAGE_MAP[target_lang]

        try:
            self.tokenizer.src_lang = src_lang

            # Tokenize batch
            encoded = self.tokenizer(
                texts,
                return_tensors="pt",
                max_length=self.settings.max_length,
                truncation=True,
                padding=True
            ).to(self.device)

            # Generate translations
            with torch.no_grad():
                generated_tokens = self.model.generate(
                    **encoded,
                    forced_bos_token_id=self.tokenizer.get_lang_id(tgt_lang),
                    max_length=self.settings.max_length,
                    num_beams=5,
                    early_stopping=True
                )

            # Decode outputs
            translations = self.tokenizer.batch_decode(
                generated_tokens,
                skip_special_tokens=True
            )

            return translations

        except Exception as e:
            logger.error(f"Batch translation error: {e}")
            raise RuntimeError(f"Batch translation failed: {e}")


# Convenience function
def get_translator() -> TranslationEngine:
    """Get the translation engine instance"""
    return TranslationEngine.get_instance()
