#!/usr/bin/env python3
"""
SCRIPT 5: Verify Model

This script:
1. Loads ./models/plant_detector.pt
2. Accepts a folder path as CLI argument
3. Runs inference on every image in that folder
4. Draws bounding boxes and labels on images with confidence > 0.70
5. Saves annotated images to ./models/verification_output/
6. Prints summary table: filename, class, confidence

Usage:
    python verify_model.py ./test_images/
    python verify_model.py ../some_folder/
"""

import os
import sys
from pathlib import Path
from typing import List, Tuple

import cv2
import numpy as np
from ultralytics import YOLO


# ============================================================
# Configuration
# ============================================================
MODEL_PATH = Path("./models/plant_detector.pt")
CONFIDENCE_THRESHOLD = 0.70
OUTPUT_DIR = Path("./models/verification_output")

# Class names (must match training)
CLASS_NAMES = {
    0: "fungus",
    1: "pest"
}

# Colors for drawing (BGR format)
CLASS_COLORS = {
    "fungus": (139, 0, 139),    # Purple
    "pest": (0, 165, 255),       # Orange
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
    print(f"\n{'='*70}")
    print(f"{text.center(70)}")
    print(f"{'='*70}\n")


def print_success(text):
    print(f"{Colors.GREEN}✓ {text}{Colors.END}")


def print_warning(text):
    print(f"{Colors.YELLOW}⚠ {text}{Colors.END}")


def print_error(text):
    print(f"{Colors.RED}✗ {text}{Colors.END}")


def get_image_files(folder: Path) -> List[Path]:
    """Get all image files from a folder."""
    image_files = []
    extensions = ['.jpg', '.jpeg', '.png', '.bmp', '.webp', '.JPG', '.JPEG', '.PNG']

    for ext in extensions:
        image_files.extend(folder.glob(f"*{ext}"))

    return sorted(image_files)


def draw_detections(image: np.ndarray, boxes, classes, confidences) -> np.ndarray:
    """Draw bounding boxes and labels on image."""
    result = image.copy()

    for box, cls_id, conf in zip(boxes, classes, confidences):
        if conf < CONFIDENCE_THRESHOLD:
            continue

        x1, y1, x2, y2 = map(int, box)

        class_name = CLASS_NAMES.get(int(cls_id), f"class_{int(cls_id)}")
        color = CLASS_COLORS.get(class_name, (0, 255, 0))

        # Draw bounding box
        cv2.rectangle(result, (x1, y1), (x2, y2), color, 2)

        # Create label text
        label = f"{class_name}: {conf:.2f}"

        # Get text size for background
        (text_width, text_height), baseline = cv2.getTextSize(
            label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1
        )

        # Draw label background
        cv2.rectangle(
            result,
            (x1, y1 - text_height - baseline - 5),
            (x1 + text_width, y1),
            color,
            -1
        )

        # Draw label text
        cv2.putText(
            result,
            label,
            (x1, y1 - baseline - 2),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            (255, 255, 255),
            1
        )

    return result


def run_inference(model: YOLO, image_path: Path) -> Tuple[np.ndarray, List[dict]]:
    """Run inference on an image and return annotated image + detections."""
    # Read image
    image = cv2.imread(str(image_path))
    if image is None:
        print_error(f"Could not read: {image_path}")
        return None, []

    # Run inference
    results = model(image, verbose=False)

    # Extract detections
    detections = []

    for result in results:
        boxes = result.boxes
        if boxes is None:
            continue

        for box in boxes:
            cls_id = int(box.cls[0])
            conf = float(box.conf[0])
            xyxy = box.xyxy[0].cpu().numpy()

            detections.append({
                'class': CLASS_NAMES.get(cls_id, f"class_{cls_id}"),
                'confidence': conf,
                'box': xyxy
            })

    # Draw detections above threshold
    if detections:
        boxes_arr = [d['box'] for d in detections]
        classes_arr = [list(CLASS_NAMES.keys())[list(CLASS_NAMES.values()).index(d['class'])] for d in detections]
        confs_arr = [d['confidence'] for d in detections]

        # Need class IDs for draw function
        class_ids = []
        for d in detections:
            for cid, cname in CLASS_NAMES.items():
                if cname == d['class']:
                    class_ids.append(cid)
                    break

        annotated = draw_detections(image, boxes_arr, class_ids, confs_arr)
    else:
        annotated = image

    return annotated, detections


def print_results_table(all_results):
    """Print results in a formatted table."""
    print_header("VERIFICATION RESULTS")

    if not all_results:
        print("No detections found above threshold.")
        return

    # Print header
    print(f"\n{Colors.BOLD}{'Filename':<30} {'Class':<10} {'Confidence':<12} {'Status':<10}{Colors.END}")
    print(f"{'─'*70}")

    # Print each detection
    total_detections = 0
    for filename, detections in all_results:
        if detections:
            for det in detections:
                status = "✓ DETECTED" if det['confidence'] >= CONFIDENCE_THRESHOLD else "Below threshold"
                print(f"{filename:<30} {det['class']:<10} {det['confidence']:<12.4f} {status:<10}")
                total_detections += 1
        else:
            print(f"{filename:<30} {'-':<10} {'-':<12} No detection")

    print(f"{'─'*70}")
    print(f"\n{Colors.BOLD}Total detections above threshold: {total_detections}{Colors.END}\n")


def verify_model(test_folder: Path):
    """Verify model on test images."""
    print_header("PLANT ROVER TRAINING PIPELINE - STEP 5: VERIFY MODEL")

    # Check if model exists
    if not MODEL_PATH.exists():
        print_error(f"Model not found: {MODEL_PATH}")
        print("Please train the model first: python train_model.py")
        sys.exit(1)

    # Check if test folder exists
    if not test_folder.exists():
        print_error(f"Test folder not found: {test_folder}")
        print("Usage: python verify_model.py <path_to_test_images>")
        sys.exit(1)

    # Create output directory
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Get test images
    image_files = get_image_files(test_folder)

    if not image_files:
        print_error(f"No images found in {test_folder}")
        sys.exit(1)

    print(f"Found {len(image_files)} test images")
    print(f"Model: {MODEL_PATH}")
    print(f"Confidence threshold: {CONFIDENCE_THRESHOLD}")
    print(f"Output directory: {OUTPUT_DIR}\n")

    # Load model
    print("Loading model...")
    model = YOLO(str(MODEL_PATH))
    print_success("Model loaded\n")

    # Run inference on each image
    all_results = []

    for i, image_path in enumerate(image_files, 1):
        print(f"[{i}/{len(image_files)}] Processing {image_path.name}...")

        annotated, detections = run_inference(model, image_path)

        if annotated is not None:
            # Save annotated image
            output_path = OUTPUT_DIR / image_path.name
            cv2.imwrite(str(output_path), annotated)

            # Store results
            high_conf_detections = [
                d for d in detections if d['confidence'] >= CONFIDENCE_THRESHOLD
            ]
            all_results.append((image_path.name, high_conf_detections))

            if high_conf_detections:
                for det in high_conf_detections:
                    print_success(f"  {det['class']}: {det['confidence']:.4f}")
            else:
                print_warning(f"  No detections above threshold")

    # Print results table
    print_results_table(all_results)

    # Print summary
    print_success("Verification complete!")
    print(f"\nAnnotated images saved to: {OUTPUT_DIR}")
    print(f"\n{Colors.YELLOW}Open the images in {OUTPUT_DIR} to visually verify detections{Colors.END}\n")


# ============================================================
# Main
# ============================================================
def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="Verify trained YOLOv8 model on test images"
    )
    parser.add_argument(
        "test_folder",
        type=str,
        nargs="?",
        default="./test_images",
        help="Path to folder containing test images"
    )

    args = parser.parse_args()

    test_folder = Path(args.test_folder)

    # If folder doesn't exist, try to create it for convenience
    if not test_folder.exists():
        print_warning(f"Test folder not found: {test_folder}")
        print("Creating it for you. Add some test images and run again.")
        test_folder.mkdir(parents=True, exist_ok=True)
        print(f"Created: {test_folder}")
        print("\nAdd some test images to this folder, then run:")
        print(f"  python verify_model.py {test_folder}")
        sys.exit(0)

    verify_model(test_folder)


if __name__ == "__main__":
    main()
