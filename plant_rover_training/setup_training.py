"""
Plant Rover ML Training Setup
Quick setup for training fungus and pest detection model
"""

import os
import subprocess
import sys
from pathlib import Path

def print_header(title):
    print("\n" + "="*60)
    print(f"  {title}")
    print("="*60 + "\n")

def run_command(cmd, description=""):
    if description:
        print(f"▶ {description}...")
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"❌ Error: {result.stderr}")
        return False
    if result.stdout:
        print(result.stdout.strip())
    return True

def main():
    print_header("Plant Rover ML Training Setup")

    # Check if we're in the right directory
    if not Path("plant_dataset").exists():
        print("❌ Error: plant_dataset folder not found!")
        print("   Please run this script from the plant_rover_training directory")
        sys.exit(1)

    # Check images
    fungus_imgs = list(Path("plant_dataset/raw/fungus").glob("*"))
    pest_imgs = list(Path("plant_dataset/raw/pest").glob("*"))

    print("📊 Dataset Summary:")
    print(f"   Fungus images: {len(fungus_imgs)}")
    print(f"   Pest images:  {len(pest_imgs)}")
    print(f"   Total images:  {len(fungus_imgs) + len(pest_imgs)}")

    print("\n" + "="*60)
    print("  NEXT STEPS:")
    print("="*60)
    print("""
For training YOLOv8, we need labeled bounding boxes.

Option 1: Auto-label with pre-trained model (Fastest)
1. Run: python auto_label.py
2. Verify and correct labels in labelImg

Option 2: Manual labeling (Most accurate)
1. Run: python labelImg_setup.py
2. Label each image with bounding boxes
3. When done, run: python prepare_dataset.py

After labeling, run:
  python prepare_dataset.py    # Organize images
  python augment_dataset.py    # Increase dataset size
  python train_model.py         # Train YOLOv8 model
  python verify_model.py        # Test the model

Starting labelImg setup now...
    """)

    # Create labelImg setup
    if Path("plant_dataset/labels").exists():
        print("✓ Labels folder already exists")

    # Try to open labelImg
    print("\n📸 Launching labelImg...")
    print("   Instructions:")
    print("   1. Change 'Save Dir' to: plant_dataset/raw/fungus or pest")
    print("   2. Draw boxes around fungus/pest (PascalVOC format)")
    print("   3. Press 'w' to save and go to next image")
    print("   4. Press 'd' to go to next image without saving")
    print("   5. Press 'a' to go to previous image")
    print("   5. Close when done labeling")
    print()

    try:
        subprocess.run(["python", "-m", "labelImg"])
    except KeyboardInterrupt:
        print("\n✓ Labeling session ended")

    print_header("Setup Complete!")
    print("Run these commands next:")
    print("  python prepare_dataset.py    # Prepare train/val split")
    print("  python train_model.py         # Start training")

if __name__ == "__main__":
    main()
