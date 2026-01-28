"""
Continue QLoRA Training from Checkpoint

Resume training from the last checkpoint with additional epochs
"""
import os
import json
import logging
from pathlib import Path

import torch
from datasets import Dataset
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
    TaskType,
    PeftModel
)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)


# Language codes for M2M-100
LANG_CODES = {"ko": "ko", "en": "en", "zh": "zh", "ja": "ja"}


def preprocess_function(examples, tokenizer, max_length=256):
    """Preprocess examples for training"""
    sources = examples["source"]
    targets = examples["target"]
    source_langs = examples["source_lang"]
    target_langs = examples["target_lang"]

    model_inputs = {
        "input_ids": [],
        "attention_mask": [],
        "labels": []
    }

    for source, target, src_lang, tgt_lang in zip(sources, targets, source_langs, target_langs):
        src_code = LANG_CODES.get(src_lang, src_lang)
        tokenizer.src_lang = src_code

        source_tokenized = tokenizer(
            source,
            max_length=max_length,
            padding="max_length",
            truncation=True,
            return_tensors=None
        )

        tgt_code = LANG_CODES.get(tgt_lang, tgt_lang)
        tokenizer.tgt_lang = tgt_code

        with tokenizer.as_target_tokenizer():
            target_tokenized = tokenizer(
                target,
                max_length=max_length,
                padding="max_length",
                truncation=True,
                return_tensors=None
            )

        model_inputs["input_ids"].append(source_tokenized["input_ids"])
        model_inputs["attention_mask"].append(source_tokenized["attention_mask"])

        labels = target_tokenized["input_ids"]
        labels = [-100 if token == tokenizer.pad_token_id else token for token in labels]
        model_inputs["labels"].append(labels)

    return model_inputs


def load_jsonl(path: str):
    """Load dataset from JSONL file"""
    data = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            try:
                data.append(json.loads(line.strip()))
            except json.JSONDecodeError:
                continue
    return Dataset.from_list(data)


def main():
    # Configuration
    base_model_name = "facebook/m2m100_418M"
    checkpoint_dir = "./outputs/m2m100-finetuned"
    output_dir = "./outputs/m2m100-finetuned-v2"
    train_file = "data/cleaned/train.jsonl"
    valid_file = "data/cleaned/valid.jsonl"

    # Training settings
    additional_epochs = 3
    batch_size = 4
    gradient_accumulation_steps = 8
    learning_rate = 1e-4  # Lower LR for continued training
    max_length = 256

    logger.info("=" * 60)
    logger.info("CONTINUING TRAINING FROM CHECKPOINT")
    logger.info("=" * 60)

    # Setup quantization
    bnb_config = BitsAndBytesConfig(
        load_in_4bit=True,
        bnb_4bit_quant_type="nf4",
        bnb_4bit_compute_dtype=torch.bfloat16,
        bnb_4bit_use_double_quant=True
    )

    # Load tokenizer from checkpoint
    logger.info(f"Loading tokenizer from {checkpoint_dir}")
    tokenizer = AutoTokenizer.from_pretrained(checkpoint_dir)

    # Load base model with quantization
    logger.info(f"Loading base model: {base_model_name}")
    base_model = AutoModelForSeq2SeqLM.from_pretrained(
        base_model_name,
        quantization_config=bnb_config,
        device_map="auto",
        trust_remote_code=True
    )

    # Prepare for k-bit training
    base_model = prepare_model_for_kbit_training(
        base_model,
        use_gradient_checkpointing=True
    )

    # Load LoRA weights from checkpoint
    logger.info(f"Loading LoRA weights from {checkpoint_dir}")
    model = PeftModel.from_pretrained(
        base_model,
        checkpoint_dir,
        is_trainable=True  # Enable training mode
    )

    # Print trainable parameters
    trainable_params = sum(p.numel() for p in model.parameters() if p.requires_grad)
    total_params = sum(p.numel() for p in model.parameters())
    logger.info(f"Trainable parameters: {trainable_params:,} / {total_params:,} ({100 * trainable_params / total_params:.2f}%)")

    # Load datasets
    logger.info("Loading datasets...")
    train_dataset = load_jsonl(train_file)
    valid_dataset = load_jsonl(valid_file)

    logger.info(f"Train samples: {len(train_dataset)}")
    logger.info(f"Valid samples: {len(valid_dataset)}")

    # Preprocess
    train_dataset = train_dataset.map(
        lambda x: preprocess_function(x, tokenizer, max_length),
        batched=True,
        remove_columns=train_dataset.column_names,
        desc="Preprocessing train"
    )

    valid_dataset = valid_dataset.map(
        lambda x: preprocess_function(x, tokenizer, max_length),
        batched=True,
        remove_columns=valid_dataset.column_names,
        desc="Preprocessing valid"
    )

    # Training arguments
    training_args = Seq2SeqTrainingArguments(
        output_dir=output_dir,
        num_train_epochs=additional_epochs,
        per_device_train_batch_size=batch_size,
        per_device_eval_batch_size=batch_size,
        gradient_accumulation_steps=gradient_accumulation_steps,
        gradient_checkpointing=True,
        gradient_checkpointing_kwargs={"use_reentrant": False},
        learning_rate=learning_rate,
        weight_decay=0.01,
        warmup_ratio=0.05,  # Lower warmup for continued training
        lr_scheduler_type="cosine",
        optim="paged_adamw_8bit",
        fp16=False,
        bf16=torch.cuda.is_bf16_supported(),
        logging_dir=f"{output_dir}/logs",
        logging_steps=10,
        eval_strategy="steps",
        eval_steps=100,
        save_strategy="steps",
        save_steps=500,
        save_total_limit=3,
        load_best_model_at_end=True,
        metric_for_best_model="eval_loss",
        greater_is_better=False,
        dataloader_num_workers=0,  # Windows compatibility
        dataloader_pin_memory=True,
        remove_unused_columns=False,
        report_to="none"
    )

    # Data collator
    data_collator = DataCollatorForSeq2Seq(
        tokenizer=tokenizer,
        model=model,
        padding=True,
        label_pad_token_id=-100
    )

    # Trainer
    trainer = Seq2SeqTrainer(
        model=model,
        args=training_args,
        train_dataset=train_dataset,
        eval_dataset=valid_dataset,
        tokenizer=tokenizer,
        data_collator=data_collator,
        callbacks=[EarlyStoppingCallback(early_stopping_patience=3)]
    )

    logger.info("Starting continued training...")
    logger.info(f"Additional epochs: {additional_epochs}")
    logger.info(f"Learning rate: {learning_rate}")

    trainer.train()

    # Save final model
    logger.info(f"Saving model to {output_dir}")
    model.save_pretrained(output_dir)
    tokenizer.save_pretrained(output_dir)

    # Save training info
    training_info = {
        "base_model": base_model_name,
        "continued_from": checkpoint_dir,
        "additional_epochs": additional_epochs,
        "total_epochs": 6,  # 3 + 3
        "learning_rate": learning_rate,
        "batch_size": batch_size * gradient_accumulation_steps
    }

    with open(f"{output_dir}/training_info.json", "w") as f:
        json.dump(training_info, f, indent=2)

    logger.info("Continued training complete!")


if __name__ == "__main__":
    main()
