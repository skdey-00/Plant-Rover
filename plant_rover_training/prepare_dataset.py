#!/usr/bin/env python3
"""
SCRIPT 2: Prepare Dataset

This script:
1. Scans raw folders for images with matching .txt label files
2. Warns about images without labels
3. Performs 80/20 train/val split (stratified, shuffled, seed=42)
4. Copies images and labels to train/val folders
5. Creates dataset.yaml for YOLOv8 training
"""

import os
import sys
import shutil
import random
from pathlib import Path
from collections import defaultdict

import yaml


# ============================================================
# Configuration
# ============================================================
RANDOM_SEED = 42
TRAIN_SPLIT = 0.8
IMAGE_EXTENSIONS = {'.jpg', '.jpeg', '.png', '.bmp', '.webp'}

# Class mapping
CLASS_DIRS = {
    'fungus': 0,
    'pest': 1
}


# ============================================================
# Colors
# ============================================================
class Colors:
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    CYAN = '\033[96m'
    BOLD = '\033[1m'
    END = '\033[0m'


def print_header(text):
    print(f"\n{'='*60}")
    print(f"{text.center(60)}")
    print(f"{'='*60}\n")


def print_success(text):
    print(f"{Colors.GREEN}✓ {text}{Colors.END}")


def print_warning(text):
    print(f"{Colors.YELLOW}⚠ {text}{Colors.END}")


def print_error(text):
    print(f"{Colors.RED}✗ {text}{Colors.END}")


def scan_raw_images():
    """Scan raw folders for images and their label files."""
    print("Scanning raw images...")

    dataset_root = Path("plant_dataset")
    raw_root = dataset_root / "raw"

    labeled_images = []
    unlabeled_images = []

    for class_name, class_id in CLASS_DIRS.items():
        class_dir = raw_root / class_name
        if not class_dir.exists():
            print_warning(f"Directory not found: {class_dir}")
            continue

        image_files = []
        for ext in IMAGE_EXTENSIONS:
            image_files.extend(class_dir.glob(f"*{ext}"))
            image_files.extend(class_dir.glob(f"*{ext.upper()}"))

        for img_path in image_files:
            label_path = img_path.with_suffix('.txt')
            if label_path.exists():
                labeled_images.append({
                    'image': img_path,
                    'label': label_path,
                    'class': class_name,
                    'class_id': class_id
                })
            else:
                unlabeled_images.append(img_path)

        print(f"  {class_name}: {len(image_files)} images, "
              f"{len([i for i in image_files if i.with_suffix('.txt').exists()])} labeled")

    # Warn about unlabeled images
    if unlabeled_images:
        print_warning(f"\nFound {len(unlabeled_images)} images without labels:")
        for img in unlabeled_images[:10]:
            print(f"  - {img}")
        if len(unlabeled_images) > 10:
            print(f"  ... and {len(unlabeled_images) - 10} more")
        print()

    return labeled_images


def split_dataset(labeled_images):
    """Split dataset into train/val with stratification."""
    print(f"\nSplitting dataset ({TRAIN_SPLIT*100:.0f}% train / "
          f"{(1-TRAIN_SPLIT)*100:.0f}% val)...")

    # Group by class
    by_class = defaultdict(list)
    for item in labeled_images:
        by_class[item['class']].append(item)

    train_items = []
    val_items = []

    # Split each class
    for class_name, items in by_class.items():
        random.seed(RANDOM_SEED)
        random.shuffle(items)

        split_idx = int(len(items) * TRAIN_SPLIT)
        train_items.extend(items[:split_idx])
        val_items.extend(items[split_idx:])

        print(f"  {class_name}: {len(items[:split_idx])} train, "
              f"{len(items[split_idx:])} val")

    return train_items, val_items


def copy_files(items, split_name):
    """Copy images and labels to their respective folders."""
    dataset_root = Path("plant_dataset")
    images_dir = dataset_root / "images" / split_name
    labels_dir = dataset_root / "labels" / split_name

    # Clear existing
    if images_dir.exists():
        shutil.rmtree(images_dir)
    if labels_dir.exists():
        shutil.rmtree(labels_dir)

    images_dir.mkdir(parents=True, exist_ok=True)
    labels_dir.mkdir(parents=True, exist_ok=True)

    for item in items:
        # Copy image
        dst_image = images_dir / item['image'].name
        shutil.copy2(item['image'], dst_image)

        # Copy label
        dst_label = labels_dir / item['label'].name
        shutil.copy2(item['label'], dst_label)

    print(f"\nCopied {len(items)} {split_name} items")


def create_dataset_yaml(train_count, val_count, class_counts):
    """Create dataset.yaml for YOLOv8 training."""
    print("\nCreating dataset.yaml...")

    dataset_config = {
        'path': str(Path('./plant_dataset').absolute()),
        'train': 'images/train',
        'val': 'images/val',
        'nc': len(CLASS_DIRS),
        'names': list(CLASS_DIRS.keys())
    }

    yaml_path = Path("plant_dataset/dataset.yaml")
    with open(yaml_path, 'w') as f:
        yaml.dump(dataset_config, f, default_flow_style=False, sort_keys=False)

    print_success(f"Created: {yaml_path}")
    print(f"""
  path: {dataset_config['path']}
  train: {dataset_config['train']}
  val: {dataset_config['val']}
  nc: {dataset_config['nc']}
  names: {dataset_config['names']}
""")


def print_summary(train_items, val_items):
    """Print summary statistics."""
    print_header("DATASET PREPARATION SUMMARY")

    total = len(train_items) + len(val_items)

    print(f"{Colors.BOLD}Total Images:{Colors.END} {total}")
    print(f"{Colors.BOLD}Train:{Colors.END} {len(train_items)} ({len(train_items)/total*100:.1f}%)")
    print(f"{Colors.BOLD}Validation:{Colors.END} {len(val_items)} ({len(val_items)/total*100:.1f}%)")

    print(f"\n{Colors.BOLD}Per-Class Breakdown:{Colors.END}")

    # Count by class
    train_by_class = defaultdict(int)
    val_by_class = defaultdict(int)

    for item in train_items:
        train_by_class[item['class']] += 1
    for item in val_items:
        val_by_class[item['class']] += 1

    for class_name in CLASS_DIRS.keys():
        train_c = train_by_class[class_name]
        val_c = val_by_class[class_name]
        total_c = train_c + val_c
        print(f"  {class_name}: {train_c} train, {val_c} val (total: {total_c})")

    print(f"\n{Colors.GREEN}✓ Dataset ready for training!{Colors.END}")
    print(f"\n{Colors.YELLOW}Next step: Run augmentation{Colors.END}")
    print(f"{Colors.CYAN}  python augment_dataset.py{Colors.END}\n")


# ============================================================
# Main
# ============================================================
def main():
    print_header("PLANT ROVER TRAINING PIPELINE - STEP 2: PREPARE DATASET")

    # Set random seed
    random.seed(RANDOM_SEED)

    # Scan for labeled images
    labeled_images = scan_raw_images()

    if not labeled_images:
        print_error("No labeled images found!")
        print("\nPlease:")
        print("1. Copy images to plant_dataset/raw/fungus/ or plant_dataset/raw/pest/")
        print("2. Use labelImg to create .txt label files for each image")
        sys.exit(1)

    print(f"\n{Colors.GREEN}✓ Found {len(labeled_images)} labeled images{Colors.END}")

    # Split dataset
    train_items, val_items = split_dataset(labeled_images)

    # Copy files
    copy_files(train_items, 'train')
    copy_files(val_items, 'val')

    # Create YAML
    create_dataset_yaml(train_items, val_items, CLASS_DIRS)

    # Print summary
    print_summary(train_items, val_items)


if __name__ == "__main__":
    main()
