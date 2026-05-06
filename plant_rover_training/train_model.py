#!/usr/bin/env python3
"""
SCRIPT 4: Train YOLOv8 Model

This script:
1. Loads base model yolov8n.pt
2. Trains on plant_dataset/dataset.yaml
3. Saves best model to models/plant_detector.pt
4. Exports to ONNX at models/plant_detector.onnx
5. Saves confusion matrix to models/confusion_matrix.png
6. Prints mAP50, precision, recall per class
"""

import os
import sys
import shutil
import torch
from pathlib import Path
import csv

from ultralytics import YOLO
import cv2
import numpy as np


# ============================================================
# Configuration
# ============================================================
BASE_MODEL = "yolov8n.pt"
DATASET_YAML = "plant_dataset/dataset.yaml"
PROJECT_NAME = "plant_rover"
RUN_NAME = "v1"

# Training parameters
EPOCHS = 80
IMAGE_SIZE = 416
BATCH_SIZE = 8
BATCH_SIZE_GPU = 16  # Used if CUDA available

# Output paths
MODELS_DIR = Path("models")
MODEL_OUTPUT = MODELS_DIR / "plant_detector.pt"
ONNX_OUTPUT = MODELS_DIR / "plant_detector.onnx"
CONFUSION_MATRIX_OUTPUT = MODELS_DIR / "confusion_matrix.png"
RESULTS_CSV = MODELS_DIR / "training_results.csv"


# ============================================================
# Colors
# ============================================================
class Colors:
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    CYAN = '\033[96m'
    BOLD = '\033[1m'
    END = '\033[0m'


def print_header(text):
    print(f"\n{'='*60}")
    print(f"{text.center(60)}")
    print(f"{'='*60}\n")


def print_success(text):
    print(f"{Colors.GREEN}✓ {text}{Colors.END}")


def print_info(text):
    print(f"{Colors.CYAN}  {text}{Colors.END}")


def detect_device():
    """Auto-detect if CUDA GPU is available."""
    if torch.cuda.is_available():
        print_success(f"CUDA GPU detected: {torch.cuda.get_device_name(0)}")
        device = "0"  # GPU 0
        batch_size = BATCH_SIZE_GPU
    else:
        print_info("No CUDA GPU detected, using CPU")
        device = "cpu"
        batch_size = BATCH_SIZE
    return device, batch_size


def print_training_config(device, batch_size):
    """Print training configuration."""
    print(f"\n{Colors.BOLD}Training Configuration:{Colors.END}")
    print(f"  Base Model:      {BASE_MODEL}")
    print(f"  Dataset:         {DATASET_YAML}")
    print(f"  Epochs:          {EPOCHS}")
    print(f"  Image Size:      {IMAGE_SIZE}")
    print(f"  Batch Size:      {batch_size}")
    print(f"  Device:          {device}")
    print(f"  Project:         {PROJECT_NAME}")
    print(f"  Run Name:        {RUN_NAME}")
    print()


def train_model(model, device, batch_size):
    """Train the YOLOv8 model."""
    print_header("STARTING TRAINING")

    results = model.train(
        data=DATASET_YAML,
        epochs=EPOCHS,
        imgsz=IMAGE_SIZE,
        batch=batch_size,
        device=device,
        project=PROJECT_NAME,
        name=RUN_NAME,
        patience=15,  # Early stopping if no improvement for 15 epochs
        save=True,
        plots=True,
        verbose=True,
    )

    return results


def save_best_model(run_dir):
    """Copy best model to models directory."""
    print_header("SAVING BEST MODEL")

    MODELS_DIR.mkdir(parents=True, exist_ok=True)

    best_pt = run_dir / "weights" / "best.pt"
    if best_pt.exists():
        shutil.copy2(best_pt, MODEL_OUTPUT)
        print_success(f"Best model saved to: {MODEL_OUTPUT}")
    else:
        print(f"Warning: best.pt not found at {best_pt}")

    return best_pt


def export_to_onnx(model):
    """Export model to ONNX format."""
    print_header("EXPORTING TO ONNX")

    try:
        model.export(
            format="onnx",
            imgsz=IMAGE_SIZE,
            simplify=True,
            opset=12
        )

        # Copy ONNX model to models directory
        onnx_source = Path(f"{PROJECT_NAME}/{RUN_NAME}/weights/best.onnx")
        if onnx_source.exists():
            shutil.copy2(onnx_source, ONNX_OUTPUT)
            print_success(f"ONNX model saved to: {ONNX_OUTPUT}")
        else:
            print_warning("ONNX export not found in run directory")

    except Exception as e:
        print(f"Error exporting to ONNX: {e}")


def save_confusion_matrix(run_dir):
    """Save confusion matrix image."""
    print_header("SAVING CONFUSION MATRIX")

    # YOLOv8 saves confusion_matrix.png in the results folder
    confusion_source = run_dir / "confusion_matrix.png"

    if confusion_source.exists():
        shutil.copy2(confusion_source, CONFUSION_MATRIX_OUTPUT)
        print_success(f"Confusion matrix saved to: {CONFUSION_MATRIX_OUTPUT}")
    else:
        # Try alternate locations
        for possible_path in [
            run_dir / "results.png",
            run_dir / "confusion_matrix.png",
            Path(f"{PROJECT_NAME}/{RUN_NAME}/confusion_matrix.png"),
        ]:
            if possible_path.exists():
                shutil.copy2(possible_path, CONFUSION_MATRIX_OUTPUT)
                print_success(f"Confusion matrix saved to: {CONFUSION_MATRIX_OUTPUT}")
                return

        print_warning("Confusion matrix not found")


def print_results_table(model, run_dir):
    """Print training results in a clean table format."""
    print_header("TRAINING RESULTS")

    # Load results.csv from the run
    results_csv = run_dir / "results.csv"

    if results_csv.exists():
        # Read the results
        import pandas as pd
        df = pd.read_csv(results_csv)

        # Get the final row (last epoch)
        final = df.iloc[-1]

        print(f"\n{Colors.BOLD}Final Epoch Results:{Colors.END}")
        print(f"{'─'*60}")
        print(f"  Epoch:                  {int(final.get('epoch', 0)) + 1}/{EPOCHS}")
        print(f"  Train Box Loss:         {final.get('train/box_loss', 'N/A'):.4f}")
        print(f"  Train Cls Loss:         {final.get('train/cls_loss', 'N/A'):.4f}")
        print(f"  Validation Box Loss:    {final.get('val/box_loss', 'N/A'):.4f}")
        print(f"  Validation Cls Loss:    {final.get('val/cls_loss', 'N/A'):.4f}")
        print(f"{'─'*60}\n")

    # Validate model to get metrics
    print("Running validation...")
    metrics = model.val()

    print(f"\n{Colors.BOLD}Overall Metrics:{Colors.END}")
    print(f"{'─'*60}")
    print(f"  mAP50:                  {metrics.box.map50:.4f}")
    print(f"  mAP50-95:               {metrics.box.map:.4f}")
    print(f"  Precision:              {metrics.box.mp:.4f}")
    print(f"  Recall:                 {metrics.box.mr:.4f}")
    print(f"{'─'*60}\n")

    # Per-class metrics
    if hasattr(metrics, 'classes') and metrics.classes is not None:
        print(f"{Colors.BOLD}Per-Class Results:{Colors.END}")
        print(f"{'─'*60}")
        print(f"{'Class':<15} {'mAP50':<10} {'Precision':<10} {'Recall':<10}")
        print(f"{'─'*60}")

        # Get per-class metrics
        maps = metrics.box.maps  # mAP50 per class
        precision = metrics.box.p  # Precision per class
        recall = metrics.box.r  # Recall per class

        class_names = metrics.names

        for i, name in class_names.items():
            map50 = maps[i] if i < len(maps) else 0
            prec = precision[0][i] if len(precision) > 0 and i < len(precision[0]) else 0
            rec = recall[0][i] if len(recall) > 0 and i < len(recall[0]) else 0
            print(f"{name:<15} {map50:<10.4f} {prec:<10.4f} {rec:<10.4f}")

        print(f"{'─'*60}\n")

    # Save results to CSV
    save_results_to_csv(metrics)


def save_results_to_csv(metrics):
    """Save training results to CSV file."""
    MODELS_DIR.mkdir(parents=True, exist_ok=True)

    with open(RESULTS_CSV, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['Metric', 'Value'])

        # Overall metrics
        writer.writerow(['mAP50', f"{metrics.box.map50:.4f}"])
        writer.writerow(['mAP50-95', f"{metrics.box.map:.4f}"])
        writer.writerow(['Precision', f"{metrics.box.mp:.4f}"])
        writer.writerow(['Recall', f"{metrics.box.mr:.4f}"])

        # Per-class metrics
        if hasattr(metrics, 'classes') and metrics.classes is not None:
            class_names = metrics.names
            maps = metrics.box.maps
            precision = metrics.box.p
            recall = metrics.box.r

            for i, name in class_names.items():
                writer.writerow([f'{name}_mAP50', f"{maps[i]:.4f}" if i < len(maps) else "N/A"])
                if len(precision) > 0 and i < len(precision[0]):
                    writer.writerow([f'{name}_Precision', f"{precision[0][i]:.4f}"])
                if len(recall) > 0 and i < len(recall[0]):
                    writer.writerow([f'{name}_Recall', f"{recall[0][i]:.4f}"])

    print_success(f"Results saved to: {RESULTS_CSV}")


def print_next_steps():
    """Print next steps."""
    print(f"\n{Colors.GREEN}{Colors.BOLD}{'='*60}{Colors.END}")
    print(f"{Colors.GREEN}{Colors.BOLD}TRAINING COMPLETE!{Colors.END}")
    print(f"{Colors.GREEN}{Colors.BOLD}{'='*60}{Colors.END}")

    print(f"\n{Colors.BOLD}Model files:{Colors.END}")
    print(f"  PyTorch:   {MODEL_OUTPUT}")
    print(f"  ONNX:      {ONNX_OUTPUT}")
    print(f"  Results:   {RESULTS_CSV}")

    print(f"\n{Colors.BOLD}Next step: Verify the model{Colors.END}")
    print(f"{Colors.CYAN}  python verify_model.py ./test_images/{Colors.END}\n")


# ============================================================
# Main
# ============================================================
def main():
    print_header("PLANT ROVER TRAINING PIPELINE - STEP 4: TRAIN MODEL")

    # Check if dataset exists
    dataset_path = Path(DATASET_YAML)
    if not dataset_path.exists():
        print(f"Error: Dataset config not found at {dataset_path}")
        print("Please run prepare_dataset.py first.")
        sys.exit(1)

    # Detect device and set batch size
    device, batch_size = detect_device()
    print_training_config(device, batch_size)

    # Load base model
    print(f"\nLoading base model: {BASE_MODEL}")
    model = YOLO(BASE_MODEL)

    # Train
    train_model(model, device, batch_size)

    # Get run directory
    run_dir = Path(f"{PROJECT_NAME}/{RUN_NAME}")
    if not run_dir.exists():
        run_dir = Path(f"{PROJECT_NAME}") / "train"

    # Save best model
    save_best_model(run_dir)

    # Load best model for validation/export
    best_model = YOLO(str(MODEL_OUTPUT))

    # Export to ONNX
    export_to_onnx(best_model)

    # Save confusion matrix
    save_confusion_matrix(run_dir)

    # Print results table
    print_results_table(best_model, run_dir)

    # Print next steps
    print_next_steps()


if __name__ == "__main__":
    main()
