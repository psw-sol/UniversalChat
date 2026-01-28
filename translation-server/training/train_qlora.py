"""
QLoRA Fine-tuning for M2M-100 Translation Model

4-bit quantization + LoRA for memory-efficient training on RTX 3060
Expected VRAM: ~4-5GB
"""
import os
import json
import logging
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional, Dict, List, Any

import torch
import yaml
from datasets import Dataset, load_dataset
from transformers import (
    AutoTokenizer,
    AutoModelForSeq2SeqLM,
    Seq2SeqTrainingArguments,
    Seq2SeqTrainer,
    DataCollatorForSeq2Seq,
    BitsAndBytesConfig,
    EarlyStoppingCallback
)
from peft import (
    LoraConfig,
    get_peft_model,
    prepare_model_for_kbit_training,
    TaskType
)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)


@dataclass
class TrainingConfig:
    """Configuration for QLoRA training"""
    # Model
    model_name: str = "facebook/m2m100_418M"
    max_length: int = 256

    # QLoRA
    lora_r: int = 16
    lora_alpha: int = 32
    lora_dropout: float = 0.05
    target_modules: List[str] = field(default_factory=lambda: [
        "q_proj", "k_proj", "v_proj", "o_proj", "fc1", "fc2"
    ])

    # Quantization
    load_in_4bit: bool = True
    bnb_4bit_quant_type: str = "nf4"
    bnb_4bit_compute_dtype: str = "bfloat16"

    # Training
    output_dir: str = "./outputs/m2m100-finetuned"
    num_epochs: int = 3
    batch_size: int = 4
    gradient_accumulation_steps: int = 8
    learning_rate: float = 2e-4
    warmup_ratio: float = 0.1

    # Data
    train_file: str = "data/cleaned/train.jsonl"
    valid_file: str = "data/cleaned/valid.jsonl"

    # Logging
    logging_steps: int = 10
    eval_steps: int = 100
    save_steps: int = 500

    @classmethod
    def from_yaml(cls, path: str) -> "TrainingConfig":
        """Load config from YAML file"""
        with open(path, "r", encoding="utf-8") as f:
            config = yaml.safe_load(f)

        # Flatten nested config
        flat_config = {}
        if "model" in config:
            flat_config["model_name"] = config["model"].get("name", cls.model_name)
            flat_config["max_length"] = config["model"].get("max_length", cls.max_length)

        if "qlora" in config:
            flat_config["lora_r"] = config["qlora"].get("lora_r", cls.lora_r)
            flat_config["lora_alpha"] = config["qlora"].get("lora_alpha", cls.lora_alpha)
            flat_config["lora_dropout"] = config["qlora"].get("lora_dropout", cls.lora_dropout)
            flat_config["target_modules"] = config["qlora"].get("target_modules", None)
            flat_config["load_in_4bit"] = config["qlora"].get("load_in_4bit", cls.load_in_4bit)
            flat_config["bnb_4bit_quant_type"] = config["qlora"].get("bnb_4bit_quant_type", cls.bnb_4bit_quant_type)
            flat_config["bnb_4bit_compute_dtype"] = config["qlora"].get("bnb_4bit_compute_dtype", cls.bnb_4bit_compute_dtype)

        if "training" in config:
            flat_config["output_dir"] = config["training"].get("output_dir", cls.output_dir)
            flat_config["num_epochs"] = config["training"].get("num_train_epochs", cls.num_epochs)
            flat_config["batch_size"] = config["training"].get("per_device_train_batch_size", cls.batch_size)
            flat_config["gradient_accumulation_steps"] = config["training"].get("gradient_accumulation_steps", cls.gradient_accumulation_steps)
            flat_config["learning_rate"] = config["training"].get("learning_rate", cls.learning_rate)
            flat_config["warmup_ratio"] = config["training"].get("warmup_ratio", cls.warmup_ratio)
            flat_config["logging_steps"] = config["training"].get("logging_steps", cls.logging_steps)
            flat_config["eval_steps"] = config["training"].get("eval_steps", cls.eval_steps)
            flat_config["save_steps"] = config["training"].get("save_steps", cls.save_steps)

        if "data" in config:
            flat_config["train_file"] = config["data"].get("train_file", cls.train_file)
            flat_config["valid_file"] = config["data"].get("valid_file", cls.valid_file)

        # Filter None values
        flat_config = {k: v for k, v in flat_config.items() if v is not None}

        return cls(**flat_config)


class TranslationDataset:
    """Dataset handler for translation training"""

    # M2M-100 language code mapping
    LANG_CODES = {
        "ko": "ko",
        "en": "en",
        "zh": "zh",
        "ja": "ja"
    }

    def __init__(
        self,
        tokenizer,
        max_length: int = 256,
        source_key: str = "source",
        target_key: str = "target",
        source_lang_key: str = "source_lang",
        target_lang_key: str = "target_lang"
    ):
        self.tokenizer = tokenizer
        self.max_length = max_length
        self.source_key = source_key
        self.target_key = target_key
        self.source_lang_key = source_lang_key
        self.target_lang_key = target_lang_key

    def preprocess_function(self, examples: Dict[str, List]) -> Dict[str, List]:
        """Preprocess examples for training"""
        sources = examples[self.source_key]
        targets = examples[self.target_key]
        source_langs = examples[self.source_lang_key]
        target_langs = examples[self.target_lang_key]

        model_inputs = {
            "input_ids": [],
            "attention_mask": [],
            "labels": []
        }

        for source, target, src_lang, tgt_lang in zip(sources, targets, source_langs, target_langs):
            # Set source language
            src_code = self.LANG_CODES.get(src_lang, src_lang)
            self.tokenizer.src_lang = src_code

            # Tokenize source
            source_tokenized = self.tokenizer(
                source,
                max_length=self.max_length,
                padding="max_length",
                truncation=True,
                return_tensors=None
            )

            # Set target language for labels
            tgt_code = self.LANG_CODES.get(tgt_lang, tgt_lang)
            self.tokenizer.tgt_lang = tgt_code

            # Tokenize target with target language prefix
            with self.tokenizer.as_target_tokenizer():
                target_tokenized = self.tokenizer(
                    target,
                    max_length=self.max_length,
                    padding="max_length",
                    truncation=True,
                    return_tensors=None
                )

            model_inputs["input_ids"].append(source_tokenized["input_ids"])
            model_inputs["attention_mask"].append(source_tokenized["attention_mask"])

            # Replace padding token id with -100 for loss calculation
            labels = target_tokenized["input_ids"]
            labels = [-100 if token == self.tokenizer.pad_token_id else token for token in labels]
            model_inputs["labels"].append(labels)

        return model_inputs

    def load_jsonl(self, path: str) -> Dataset:
        """Load dataset from JSONL file"""
        data = []
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                try:
                    data.append(json.loads(line.strip()))
                except json.JSONDecodeError:
                    continue

        return Dataset.from_list(data)


class QLoRATrainer:
    """QLoRA training pipeline for M2M-100"""

    def __init__(self, config: TrainingConfig):
        self.config = config
        self.tokenizer = None
        self.model = None
        self.peft_model = None

    def setup_quantization(self) -> BitsAndBytesConfig:
        """Setup 4-bit quantization config"""
        compute_dtype = getattr(torch, self.config.bnb_4bit_compute_dtype)

        return BitsAndBytesConfig(
            load_in_4bit=self.config.load_in_4bit,
            bnb_4bit_quant_type=self.config.bnb_4bit_quant_type,
            bnb_4bit_compute_dtype=compute_dtype,
            bnb_4bit_use_double_quant=True
        )

    def setup_lora(self) -> LoraConfig:
        """Setup LoRA config"""
        return LoraConfig(
            r=self.config.lora_r,
            lora_alpha=self.config.lora_alpha,
            lora_dropout=self.config.lora_dropout,
            target_modules=self.config.target_modules,
            bias="none",
            task_type=TaskType.SEQ_2_SEQ_LM
        )

    def load_model(self):
        """Load model with quantization"""
        logger.info(f"Loading model: {self.config.model_name}")

        # Load tokenizer
        self.tokenizer = AutoTokenizer.from_pretrained(
            self.config.model_name,
            use_fast=True
        )

        # Load model with quantization
        bnb_config = self.setup_quantization()

        self.model = AutoModelForSeq2SeqLM.from_pretrained(
            self.config.model_name,
            quantization_config=bnb_config,
            device_map="auto",
            trust_remote_code=True
        )

        # Prepare for k-bit training
        self.model = prepare_model_for_kbit_training(
            self.model,
            use_gradient_checkpointing=True
        )

        # Apply LoRA
        lora_config = self.setup_lora()
        self.peft_model = get_peft_model(self.model, lora_config)

        # Print trainable parameters
        trainable_params = sum(p.numel() for p in self.peft_model.parameters() if p.requires_grad)
        total_params = sum(p.numel() for p in self.peft_model.parameters())
        logger.info(f"Trainable parameters: {trainable_params:,} / {total_params:,} ({100 * trainable_params / total_params:.2f}%)")

    def load_datasets(self) -> tuple:
        """Load training and validation datasets"""
        dataset_handler = TranslationDataset(
            tokenizer=self.tokenizer,
            max_length=self.config.max_length
        )

        # Load datasets
        train_dataset = dataset_handler.load_jsonl(self.config.train_file)
        valid_dataset = dataset_handler.load_jsonl(self.config.valid_file)

        logger.info(f"Train samples: {len(train_dataset)}")
        logger.info(f"Valid samples: {len(valid_dataset)}")

        # Preprocess
        train_dataset = train_dataset.map(
            dataset_handler.preprocess_function,
            batched=True,
            remove_columns=train_dataset.column_names,
            desc="Preprocessing train"
        )

        valid_dataset = valid_dataset.map(
            dataset_handler.preprocess_function,
            batched=True,
            remove_columns=valid_dataset.column_names,
            desc="Preprocessing valid"
        )

        return train_dataset, valid_dataset

    def train(self):
        """Run training"""
        if self.peft_model is None:
            self.load_model()

        train_dataset, valid_dataset = self.load_datasets()

        # Training arguments
        training_args = Seq2SeqTrainingArguments(
            output_dir=self.config.output_dir,
            num_train_epochs=self.config.num_epochs,
            per_device_train_batch_size=self.config.batch_size,
            per_device_eval_batch_size=self.config.batch_size,
            gradient_accumulation_steps=self.config.gradient_accumulation_steps,
            gradient_checkpointing=True,
            gradient_checkpointing_kwargs={"use_reentrant": False},
            learning_rate=self.config.learning_rate,
            weight_decay=0.01,
            warmup_ratio=self.config.warmup_ratio,
            lr_scheduler_type="cosine",
            optim="paged_adamw_8bit",
            fp16=False,
            bf16=torch.cuda.is_bf16_supported(),
            logging_dir=f"{self.config.output_dir}/logs",
            logging_steps=self.config.logging_steps,
            eval_strategy="steps",
            eval_steps=self.config.eval_steps,
            save_strategy="steps",
            save_steps=self.config.save_steps,
            save_total_limit=3,
            load_best_model_at_end=True,
            metric_for_best_model="eval_loss",
            greater_is_better=False,
            dataloader_num_workers=0,  # Windows compatibility with PEFT
            dataloader_pin_memory=True,
            remove_unused_columns=False,
            report_to="none"  # Disable wandb by default
        )

        # Data collator
        data_collator = DataCollatorForSeq2Seq(
            tokenizer=self.tokenizer,
            model=self.peft_model,
            padding=True,
            label_pad_token_id=-100
        )

        # Trainer
        trainer = Seq2SeqTrainer(
            model=self.peft_model,
            args=training_args,
            train_dataset=train_dataset,
            eval_dataset=valid_dataset,
            tokenizer=self.tokenizer,
            data_collator=data_collator,
            callbacks=[EarlyStoppingCallback(early_stopping_patience=3)]
        )

        logger.info("Starting training...")
        trainer.train()

        # Save final model
        logger.info(f"Saving model to {self.config.output_dir}")
        self.peft_model.save_pretrained(self.config.output_dir)
        self.tokenizer.save_pretrained(self.config.output_dir)

        # Save training info
        training_info = {
            "model_name": self.config.model_name,
            "lora_r": self.config.lora_r,
            "lora_alpha": self.config.lora_alpha,
            "epochs": self.config.num_epochs,
            "batch_size": self.config.batch_size * self.config.gradient_accumulation_steps,
            "learning_rate": self.config.learning_rate
        }

        with open(f"{self.config.output_dir}/training_info.json", "w") as f:
            json.dump(training_info, f, indent=2)

        logger.info("Training complete!")


# CLI interface
if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="QLoRA fine-tuning for M2M-100")
    parser.add_argument("--config", default="config.yaml", help="Config file path")
    parser.add_argument("--train-file", help="Override training file")
    parser.add_argument("--valid-file", help="Override validation file")
    parser.add_argument("--output-dir", help="Override output directory")
    parser.add_argument("--epochs", type=int, help="Override number of epochs")
    parser.add_argument("--batch-size", type=int, help="Override batch size")
    parser.add_argument("--learning-rate", type=float, help="Override learning rate")

    args = parser.parse_args()

    # Load config
    if Path(args.config).exists():
        config = TrainingConfig.from_yaml(args.config)
    else:
        config = TrainingConfig()

    # Override from CLI
    if args.train_file:
        config.train_file = args.train_file
    if args.valid_file:
        config.valid_file = args.valid_file
    if args.output_dir:
        config.output_dir = args.output_dir
    if args.epochs:
        config.num_epochs = args.epochs
    if args.batch_size:
        config.batch_size = args.batch_size
    if args.learning_rate:
        config.learning_rate = args.learning_rate

    # Run training
    trainer = QLoRATrainer(config)
    trainer.train()
