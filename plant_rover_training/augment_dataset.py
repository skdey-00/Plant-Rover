#!/usr/bin/env python3
"""
SCRIPT 3: Augment Dataset

This script:
1. Applies 7 different albumentations augmentations to each training image
2. Correctly transforms YOLO bounding boxes for each augmentation
3. Saves augmented images with matching label files
4. Prints before/after image counts

Augmentations:
1. Horizontal flip
2. Vertical flip
3. Random brightness/contrast (limit=0.3)
4. HueSaturationValue shift
5. Random 90° rotation
6. Gaussian blur (blur_limit=3)
7. Random crop 90% then resize back
"""

import os
import sys
from pathlib import Path
from typing import List, Tuple

import cv2
import numpy as np
import albumentations as A
from albumentations.core.serialization import to_json


# ============================================================
# Configuration
# ============================================================
AUGMENT_COUNT = 7

# ============================================================
# Augmentation transforms
# ============================================================
AUGMENTATIONS = [
    ("HorizontalFlip", A.HorizontalFlip(p=1.0)),
    ("VerticalFlip", A.VerticalFlip(p=1.0)),
    ("RandomBrightnessContrast", A.RandomBrightnessContrast(limit=0.3, p=1.0)),
    ("HueSaturationValue", A.HueSaturationValue(
        hue_shift_limit=20, sat_shift_limit=30, val_shift_limit=20, p=1.0)),
    ("Rotate90", A.RandomRotate90(p=1.0)),
    ("GaussianBlur", A.GaussianBlur(blur_limit=3, p=1.0)),
    ("RandomCrop", A.RandomResizedCrop(
        height_scale=0.9, width_scale=0.9,
        height=None, width=None,
        p=1.0
    )),
]


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


# ============================================================
# YOLO Box Utilities
# ============================================================

def yolo_to_albumentations(yolo_boxes: List[List[float]], img_width: int, img_height: int):
    """
    Convert YOLO format (x_center, y_center, width, height) normalized to 0-1
    to albumentations format (x_min, y_min, x_max, y_max) in pixels.
    """
    alb_boxes = []
    for box in yolo_boxes:
        x_center, y_center, width, height = box

        # Convert to pixel coordinates
        x_center_px = x_center * img_width
        y_center_px = y_center * img_height
        width_px = width * img_width
        height_px = height * img_height

        # Convert to x_min, y_min, x_max, y_max
        x_min = x_center_px - (width_px / 2)
        y_min = y_center_px - (height_px / 2)
        x_max = x_center_px + (width_px / 2)
        y_max = y_center_px + (height_px / 2)

        alb_boxes.append([x_min, y_min, x_max, y_max])

    return np.array(alb_boxes)


def albumentations_to_yolo(alb_boxes: np.ndarray, img_width: int, img_height: int):
    """
    Convert albumentations format (x_min, y_min, x_max, y_max) in pixels
    to YOLO format (x_center, y_center, width, height) normalized to 0-1.
    """
    yolo_boxes = []
    for box in alb_boxes:
        x_min, y_min, x_max, y_max = box

        # Calculate center and dimensions
        x_center = (x_min + x_max) / 2
        y_center = (y_min + y_max) / 2
        width = x_max - x_min
        height = y_max - y_min

        # Normalize to 0-1
        x_center /= img_width
        y_center /= img_height
        width /= img_width
        height /= img_height

        yolo_boxes.append([x_center, y_center, width, height])

    return yolo_boxes


def load_yolo_labels(label_path: Path) -> Tuple[List[List[float]], List[int]]:
    """
    Load YOLO format labels from .txt file.
    Returns (boxes, class_ids) where boxes are in YOLO format.
    """
    boxes = []
    class_ids = []

    if not label_path.exists():
        return boxes, class_ids

    with open(label_path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) >= 5:
                class_id = int(parts[0])
                coords = [float(x) for x in parts[1:5]]
                boxes.append(coords)
                class_ids.append(class_id)

    return boxes, class_ids


def save_yolo_labels(label_path: Path, boxes: List[List[float]], class_ids: List[int]):
    """Save YOLO format labels to .txt file."""
    with open(label_path, 'w') as f:
        for class_id, box in zip(class_ids, boxes):
            x_center, y_center, width, height = box
            # Clamp values to 0-1 range
            x_center = max(0.0, min(1.0, x_center))
            y_center = max(0.0, min(1.0, y_center))
            width = max(0.0, min(1.0, width))
            height = max(0.0, min(1.0, height))

            f.write(f"{class_id} {x_center:.6f} {y_center:.6f} {width:.6f} {height:.6f}\n")


# ============================================================
# Augmentation
# ============================================================

def augment_image_and_labels(
    image_path: Path,
    label_path: Path,
    output_image_dir: Path,
    output_label_dir: Path
) -> int:
    """
    Apply all augmentations to an image and its labels.
    Returns number of augmentations created.
    """
    # Load image
    image = cv2.imread(str(image_path))
    if image is None:
        print(f"Warning: Could not read {image_path}")
        return 0

    img_height, img_width = image.shape[:2]

    # Load labels
    yolo_boxes, class_ids = load_yolo_labels(label_path)

    if not yolo_boxes:
        # No labels, just copy original without augmentation
        return 0

    # Convert to albumentations format
    alb_boxes = yolo_to_albumentations(yolo_boxes, img_width, img_height)

    # Get base filename
    base_name = image_path.stem

    augmentations_created = 0

    # Apply each augmentation
    for aug_idx, (aug_name, transform) in enumerate(AUGMENTATIONS, 1):
        # Apply augmentation
        transformed = transform(image=image, bboxes=alb_boxes)
        aug_image = transformed['image']
        aug_boxes = transformed['bboxes']

        if not aug_boxes:
            # All boxes were clipped out, skip
            continue

        # Convert back to YOLO format
        aug_yolo_boxes = albumentations_to_yolo(
            np.array(aug_boxes),
            aug_image.shape[1],
            aug_image.shape[0]
        )

        # Save augmented image
        aug_name = f"{base_name}_aug{aug_idx}.jpg"
        aug_image_path = output_image_dir / aug_name
        cv2.imwrite(str(aug_image_path), aug_image,
                   [cv2.IMWRITE_JPEG_QUALITY, 95])

        # Save augmented labels
        aug_label_path = output_label_dir / aug_name
        save_yolo_labels(aug_label_path, aug_yolo_boxes, class_ids)

        augmentations_created += 1

    return augmentations_created


def augment_dataset():
    """Augment all training images."""
    dataset_root = Path("plant_dataset")
    train_images_dir = dataset_root / "images" / "train"
    train_labels_dir = dataset_root / "labels" / "train"

    if not train_images_dir.exists():
        print(f"Error: {train_images_dir} not found. Run prepare_dataset.py first.")
        return

    # Get all training images
    image_files = []
    for ext in ['.jpg', '.jpeg', '.png', '.bmp', '.webp']:
        image_files.extend(train_images_dir.glob(f"*{ext}"))
        image_files.extend(train_images_dir.glob(f"*{ext.upper()}"))

    if not image_files:
        print("No training images found.")
        return

    print(f"Found {len(image_files)} training images")
    print(f"Applying {AUGMENT_COUNT} augmentations to each image...\n")

    total_augmentations = 0
    processed = 0

    for image_path in image_files:
        label_path = train_labels_dir / (image_path.stem + '.txt')

        if not label_path.exists():
            print(f"Skipping {image_path.name} (no label)")
            continue

        aug_count = augment_image_and_labels(
            image_path,
            label_path,
            train_images_dir,
            train_labels_dir
        )

        total_augmentations += aug_count
        processed += 1

        if processed % 50 == 0:
            print(f"  Processed {processed}/{len(image_files)} images...")

    print_success(f"Augmentation complete!")

    # Count final images
    final_image_files = []
    for ext in ['.jpg', '.jpeg', '.png', '.bmp', '.webp']:
        final_image_files.extend(train_images_dir.glob(f"*{ext}"))
        final_image_files.extend(train_images_dir.glob(f"*{ext.upper()}"))

    print(f"\n{'─'*60}")
    print(f"  Original images:    {len(image_files)}")
    print(f"  Augmentations added: {total_augmentations}")
    print(f"  Total training images: {len(final_image_files)}")
    print(f"{'─'*60}")

    print(f"\n{Colors.GREEN}✓ Dataset augmented successfully!{Colors.END}")
    print(f"\n{Colors.YELLOW}Next step: Train the model{Colors.END}")
    print(f"{Colors.CYAN}  python train_model.py{Colors.END}\n")


# ============================================================
# Main
# ============================================================
def main():
    print_header("PLANT ROVER TRAINING PIPELINE - STEP 3: AUGMENT DATASET")

    augment_dataset()


if __name__ == "__main__":
    main()
