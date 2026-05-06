# Quick Start Guide

## Windows

```bash
# Step 1: Setup
python setup_labeling.py

# Step 2: Add your photos to plant_dataset/raw/fungus/ and plant_dataset/raw/pest/

# Step 3: Launch labelImg (from step 1 output)
# Or manually:
rover-train\Scripts\python -m labelImg

# Step 4: Prepare dataset
python prepare_dataset.py

# Step 5: Augment
python augment_dataset.py

# Step 6: Train
python train_model.py

# Step 7: Verify
python verify_model.py test_images\
```

## Linux/Mac

```bash
# Step 1: Setup
python3 setup_labeling.py

# Step 2: Add photos
cp your_photos/fungus/* plant_dataset/raw/fungus/
cp your_photos/pest/* plant_dataset/raw/pest/

# Step 3: Label (from step 1 output)
# Or manually:
source rover-train/bin/activate
labelImg

# Step 4-7: Same as Windows
python3 prepare_dataset.py
python3 augment_dataset.py
python3 train_model.py
python3 verify_model.py test_images/
```

## Minimum Dataset Recommendation

- **50 images per class** for baseline model
- **100+ images per class** for good results
- **Diverse lighting and angles**

## Timeline (50 images/class, CPU training)

| Step | Time |
|------|------|
| Setup | 2 min |
| Labeling | 30-60 min |
| Prepare | 10 sec |
| Augment | 1 min |
| Train | 30-60 min |
| Verify | 2 min |

**Total: ~1-2 hours**
