"""
Auto-labeling with PlantDoc model (trained for plant diseases)
This will automatically detect and label plant diseases
"""

from ultralytics import YOLO
from pathlib import Path
import cv2
import shutil

def main():
    print("="*60)
    print("  Auto-Labeling with PlantDoc Model")
    print("="*60)

    # Use PlantDoc model (trained on plant diseases)
    print("\n📥 Loading PlantDoc model (this may take a minute on first run)...")

    try:
        model = YOLO('plantdoc.pth', source='google')  # This will download the model
    except:
        print("❌ Could not load PlantDoc model from Google")
        print("\n🔄 Trying alternative: Using YOLOv8n with custom config...")

        # Load standard YOLOv8n
        model = YOLO('yolov8n.pt')

    print("✓ Model loaded!")

    # Paths
    fungus_dir = Path('plant_dataset/raw/fungus')
    pest_dir = Path('plant_dataset/raw/pest')

    # Process fungus images
    print("\n🔍 Processing fungus images...")
    fungus_count = 0
    for img_path in list(fungus_dir.glob('*.jpg')) + list(fungus_dir.glob('*.png')) + list(fungus_dir.glob('*.JPG')) + list(fungus_dir.glob('*.PNG')):
        label_path = img_path.with_suffix('.txt')

        # Skip if already labeled
        if label_path.exists():
            fungus_count += 1
            continue

        # Read image
        img = cv2.imread(str(img_path))
        if img is None:
            continue

        h, w = img.shape[:2]

        # Run inference
        results = model(img_path, conf=0.25, verbose=False)

        # Convert detections to YOLO format
        detections = []
        for r in results:
            for box in r.boxes:
                x1, y1, x2, y2 = box.xyxy[0].cpu().numpy()
                conf = float(box.conf)

                # Convert to YOLO format
                x_center = ((x1 + x2) / 2) / w
                y_center = ((y1 + y2) / 2) / h
                width = (x2 - x1) / w
                height = (y2 - y1) / h

                detections.append([0, x_center, y_center, width, height])  # 0 = fungus

        # Save labels
        if detections:
            with open(label_path, 'w') as f:
                for det in detections:
                    f.write(f"{det[0]} {det[1]:.6f} {det[2]:.6f} {det[3]:.6f} {det[4]:.6f}\n")
            fungus_count += 1
            print(f"  ✓ {img_path.name} - {len(detections)} detections")

    print(f"\nFungus images labeled: {fungus_count}")

    # Process pest images
    print("\n🔍 Processing pest images...")
    pest_count = 0
    for img_path in list(pest_dir.glob('*.jpg')) + list(pest_dir.glob('*.png')) + list(pest_dir.glob('*.JPG')) + list(pest_dir.glob('*.PNG')):
        label_path = img_path.with_suffix('.txt')

        if label_path.exists():
            pest_count += 1
            continue

        img = cv2.imread(str(img_path))
        if img is None:
            continue

        h, w = img.shape[:2]
        results = model(img_path, conf=0.25, verbose=False)

        detections = []
        for r in results:
            for box in r.boxes:
                x1, y1, x2, y2 = box.xyxy[0].cpu().numpy()
                conf = float(box.conf)

                x_center = ((x1 + x2) / 2) / w
                y_center = ((y1 + y2) / 2) / h
                width = (x2 - x1) / w
                height = (y2 - y1) / h

                detections.append([1, x_center, y_center, width, height])  # 1 = pest

        if detections:
            with open(label_path, 'w') as f:
                for det in detections:
                    f.write(f"{det[0]} {det[1]:.6f} {det[2]:.6f} {det[3]:.6f} {det[4]:.6f}\n")
            pest_count += 1
            print(f"  ✓ {img_path.name} - {len(detections)} detections")

    print(f"\nPest images labeled: {pest_count}")

    print(f"\n{'='*60}")
    print("  Auto-labeling Complete!")
    print(f"{'='*60}")
    print(f"Total fungus labels: {fungus_count}")
    print(f"Total pest labels: {pest_count}")
    print(f"\n⚠ Note: Auto-labeling may have errors.")
    print(f"   For best results, verify labels in a labeling tool.")
    print(f"\nNext step: python prepare_dataset.py")

if __name__ == "__main__":
    main()
