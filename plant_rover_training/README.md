# Plant Rover YOLOv8 Training Pipeline

Complete training pipeline for custom plant disease and pest detection using YOLOv8.

## Overview

This pipeline consists of 5 scripts that run sequentially to train a custom YOLOv8 model:

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│  1. Setup &     │───▶│  2. Prepare     │───▶│  3. Augment     │
│     Labeling    │    │     Dataset     │    │     Dataset     │
└─────────────────┘    └─────────────────┘    └─────────────────┘
                                                      │
┌─────────────────┐    ┌─────────────────┐           │
│  5. Verify      │◀───│  4. Train       │◀──────────┘
│     Model       │    │     Model       │
└─────────────────┘    └─────────────────┘
```

## Prerequisites

- Python 3.8 or higher
- (Optional) NVIDIA GPU with CUDA for faster training

## Quick Start

```bash
cd plant_rover_training

# Step 1: Setup environment and folders
python setup_labeling.py

# Step 2: Label your images with labelImg (launched by step 1)
# - Copy reference photos to plant_dataset/raw/fungus/ and plant_dataset/raw/pest/
# - Draw bounding boxes and save labels

# Step 3: Prepare dataset (split train/val)
python prepare_dataset.py

# Step 4: Augment training data
python augment_dataset.py

# Step 5: Train model
python train_model.py

# Step 6: Verify model on test images
python verify_model.py ./test_images/
```

## Script Details

### SCRIPT 1: setup_labeling.py

Creates the training environment:

- Creates virtual environment `rover-train`
- Installs: labelImg, ultralytics, opencv-python, pillow, scikit-learn, pyyaml, albumentations
- Creates folder structure for dataset
- Creates `plant_dataset/classes.txt` with class names

**Output folders:**
```
plant_dataset/
├── raw/
│   ├── fungus/     # Put raw fungus photos here
│   └── pest/       # Put raw pest photos here
├── images/
│   ├── train/
│   └── val/
├── labels/
│   ├── train/
│   └── val/
└── classes.txt
models/
detections/
```

**After running this script:**
1. Copy your reference photos to `plant_dataset/raw/fungus/` or `plant_dataset/raw/pest/`
2. Launch labelImg (instructions printed by script)
3. Draw bounding boxes around each disease/pest
4. Press Ctrl+S to save labels

### SCRIPT 2: prepare_dataset.py

Prepares the dataset for training:

- Scans for images with matching `.txt` label files
- Warns about unlabeled images
- Performs 80/20 train/val split (stratified by class)
- Creates `plant_dataset/dataset.yaml`

**Output:**
```
Train/val split summary
Total images: X
Train: X (80%)
Val: X (20%)

Per-class breakdown:
  fungus: X train, X val
  pest: X train, X val
```

### SCRIPT 3: augment_dataset.py

Applies 7 augmentations to each training image:

1. Horizontal flip
2. Vertical flip
3. Random brightness/contrast
4. HueSaturationValue shift
5. Random 90° rotation
6. Gaussian blur
7. Random crop + resize

All bounding boxes are correctly transformed for each augmentation.

**Output:**
```
Original images:    X
Augmentations added: X*7
Total training images: X*8
```

### SCRIPT 4: train_model.py

Trains the YOLOv8 model:

- Base model: `yolov8n.pt`
- Epochs: 80
- Image size: 416
- Batch size: 8 (CPU) / 16 (GPU)
- Auto-detects CUDA GPU

**Output files:**
```
models/
├── plant_detector.pt          # Best PyTorch model
├── plant_detector.onnx        # ONNX export
├── confusion_matrix.png       # Confusion matrix visualization
└── training_results.csv       # Metrics summary
```

**Metrics printed:**
- mAP50, mAP50-95
- Precision, Recall (overall and per-class)
- Confusion matrix

### SCRIPT 5: verify_model.py

Verifies the trained model:

```bash
python verify_model.py ./test_images/
```

**Output:**
- Annotated images saved to `models/verification_output/`
- Console table with detections:
  ```
  ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━┳━━━━━━━━━━┳━━━━━━━━━━━━━┳━━━━━━━━━━━━┓
  ┃ Filename                   ┃ Class    ┃ Confidence ┃ Status    ┃
  ┡━━━━━━━━━━━━━━━━━━━━━━━━━━━━╇━━━━━━━━━━╇━━━━━━━━━━━━━╇━━━━━━━━━━━━┩
  │ image1.jpg                 │ fungus   │ 0.8734     │ ✓ DETECTED│
  │ image2.jpg                 │ pest     │ 0.9123     │ ✓ DETECTED│
  └────────────────────────────┴──────────┴─────────────┴───────────┘
  ```

## Deploying to Detection Server

After training, update the detection server config:

```python
# detection_server/config.py
MODEL_PATH = "../plant_rover_training/models/plant_detector.pt"
```

Or copy the model:
```bash
cp models/plant_detector.pt ../detection_server/
```

## Folder Structure After Training

```
plant_rover_training/
├── setup_labeling.py
├── prepare_dataset.py
├── augment_dataset.py
├── train_model.py
├── verify_model.py
├── plant_dataset/
│   ├── raw/                  # Original images
│   ├── images/               # Split images
│   ├── labels/               # YOLO format labels
│   ├── dataset.yaml          # Dataset config
│   └── classes.txt
├── models/
│   ├── plant_detector.pt     # Trained model
│   ├── plant_detector.onnx   # ONNX export
│   ├── confusion_matrix.png
│   ├── training_results.csv
│   └── verification_output/  # Verification images
├── plant_rover/              # Training run directory
│   └── v1/
│       └── weights/
└── rover-train/              # Virtual environment
```

## YOLO Label Format

Labels are saved in YOLO format (normalized 0-1):

```
<class_id> <x_center> <y_center> <width> <height>
```

Example:
```
0 0.500 0.500 0.300 0.400    # fungus at center, 30% width, 40% height
1 0.250 0.750 0.150 0.200    # pest at bottom-left
```

## Training Tips

**For better results:**

1. **Collect diverse images**: Different lighting, angles, backgrounds
2. **Balance classes**: Similar number of images per class
3. **Label accurately**: Tight bounding boxes around the disease/pest
4. **Start with 50-100 images per class**: More is better
5. **Use augmentation**: Increases effective dataset size 8x

**If mAP is low:**

- Add more training images
- Check label quality (common issue)
- Increase epochs to 100-150
- Try larger model (yolov8s.pt instead of yolov8n.pt)

## Troubleshooting

**labelImg won't launch:**
- Make sure venv is activated
- Install tkinter: `sudo apt-get install python3-tk` (Linux)

**Training is slow:**
- Reduce image size to 320
- Use GPU if available
- Reduce batch size

**Poor detections:**
- Verify labels are correct
- Check for class imbalance
- Increase training epochs
- Collect more training data

**Out of memory:**
- Reduce batch size: edit `BATCH_SIZE = 4` in train_model.py
- Reduce image size: edit `IMAGE_SIZE = 320` in train_model.py
