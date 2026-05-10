"""
Quick Training Script for Plant Disease Detection
Trains YOLOv8n on fungus and pest detection
"""

from ultralytics import YOLO
from pathlib import Path
import yaml

def main():
    print("="*60)
    print("  Training Plant Disease Detection Model")
    print("="*60)

    # Check if dataset.yaml exists
    yaml_path = Path("plant_dataset/dataset.yaml")

    if not yaml_path.exists():
        print("\n❌ dataset.yaml not found!")
        print("   Run prepare_dataset.py first")
        return

    # Load dataset config
    with open(yaml_path) as f:
        dataset_config = yaml.safe_load(f)

    nc = dataset_config.get('nc', 2)
    names = dataset_config.get('names', ['fungus', 'pest'])

    print(f"\n📊 Dataset:")
    print(f"   Classes: {nc}")
    print(f"   Names: {names}")

    print(f"\n🚀 Starting training...")
    print(f"   - Model: YOLOv8n (nano - fast)")
    print(f"   - Epochs: 50")
    print(f"   - Image size: 416")
    print(f"   - Batch: Auto")

    # Load pre-trained YOLOv8n
    model = YOLO('yolov8n.pt')

    # Train
    results = model.train(
        data=str(yaml_path),
        epochs=50,
        imgsz=416,
        batch=16,
        project='plant_rover',
        name='detection',
        patience=10,  # Early stopping
        save=True,
        plots=True,
        verbose=True
    )

    print(f"\n✓ Training complete!")
    print(f"   Best model: plant_rover/detection/weights/best.pt")
    print(f"\n📊 Run 'python verify_model.py' to test the model")

if __name__ == "__main__":
    main()
