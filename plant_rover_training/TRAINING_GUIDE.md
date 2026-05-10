# Plant Disease Detection Training Guide

## Quick Start

### Step 1: Install Dependencies (if not done)

```bash
cd plant_rover_training
pip install ultralytics opencv-python pyyaml labelImg
```

### Step 2: Verify Your Images

Your images are already copied:
- ✅ Fungus images: 112
- ✅ Pest images: 137

### Step 3: Label Your Images

You have two options:

#### Option A: Manual Labeling (Most Accurate)

1. Open labelImg:
   ```bash
   python -m labelImg
   ```

2. In labelImg:
   - Click "Change Default Dir" → Select `plant_dataset/raw/fungus` or `plant_dataset/raw/pest`
   - Draw boxes around the disease areas
   - Press `w` to save and next
   - Press `d` to skip (no disease)
   - Press `a` to go back

3. Label at least 50-100 images per class for good results

#### Option B: Auto-Label (Fastest, less accurate)

```bash
python auto_label.py
```

Then verify and correct labels in labelImg.

### Step 4: Prepare Dataset

```bash
python prepare_dataset.py
```

This creates:
- `plant_dataset/images/train/`
- `plant_dataset/images/val/`
- `plant_dataset/labels/train/`
- `plant_dataset/labels/val/`
- `plant_dataset/dataset.yaml`

### Step 5: Train Model

```bash
python quick_train.py
```

Training will take 20-60 minutes depending on your computer.

### Step 6: Verify Model

```bash
python verify_model.py plant_dataset/raw/fungus
```

This runs inference and shows detected boxes.

### Step 7: Deploy Model

Copy the trained model to detection server:

```bash
cp plant_rover/detection/weights/best.pt ../detection_server/plant_detector.pt
```

---

## Tips for Best Results

1. **Label Quality**: Take time to draw accurate boxes
2. **Variety**: Include different lighting, angles, and disease stages
3. **Minimum**: Aim for at least 50 labeled images per class
4. **Balance**: Try to have similar numbers of fungus and pest images

---

## Troubleshooting

### "No module named 'ultralytics'"
```bash
pip install ultralytics
```

### "CUDA out of memory"
Use CPU training or smaller batch:
```python
# In quick_train.py, change:
batch=8  # or 4 for less memory
```

### "Low accuracy"
- Label more images
- Check if labels are accurate
- Train for more epochs (change `epochs=100`)
