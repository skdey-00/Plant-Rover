#!/usr/bin/env python3
"""
SCRIPT 1: Setup Labeling Environment

This script:
1. Creates a virtual environment called "rover-train"
2. Installs required packages
3. Creates folder structure for dataset
4. Creates classes.txt file
5. Prints instructions for launching labelImg
"""

import os
import sys
import subprocess
import platform
from pathlib import Path


# ============================================================
# Configuration
# ============================================================
VENV_NAME = "rover-train"
PROJECT_ROOT = Path(__file__).parent.absolute()

# Dataset folders
DATASET_DIRS = [
    "plant_dataset/raw/fungus",
    "plant_dataset/raw/pest",
    "plant_dataset/images/train",
    "plant_dataset/images/val",
    "plant_dataset/labels/train",
    "plant_dataset/labels/val",
]

# Additional folders
OTHER_DIRS = [
    "models",
    "detections",
]

# Classes for detection
CLASSES = ["fungus", "pest"]


# ============================================================
# Colors for terminal output
# ============================================================
class Colors:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    END = '\033[0m'
    BOLD = '\033[1m'


def print_header(text):
    print(f"\n{Colors.HEADER}{Colors.BOLD}{'='*60}{Colors.END}")
    print(f"{Colors.HEADER}{Colors.BOLD}{text.center(60)}{Colors.END}")
    print(f"{Colors.HEADER}{Colors.BOLD}{'='*60}{Colors.END}\n")


def print_step(num, text):
    print(f"{Colors.CYAN}{Colors.BOLD}[STEP {num}]{Colors.END} {text}")


def print_success(text):
    print(f"{Colors.GREEN}✓ {text}{Colors.END}")


def print_warning(text):
    print(f"{Colors.YELLOW}⚠ {text}{Colors.END}")


def print_error(text):
    print(f"{Colors.RED}✗ {text}{Colors.END}")


def create_virtual_environment():
    """Create virtual environment."""
    print_step(1, "Creating virtual environment")

    venv_path = PROJECT_ROOT / VENV_NAME

    if venv_path.exists():
        print_warning(f"Virtual environment '{VENV_NAME}' already exists")
        response = input("Delete and recreate? (y/N): ").strip().lower()
        if response == 'y':
            import shutil
            shutil.rmtree(venv_path)
            print_success("Deleted existing virtual environment")
        else:
            print_success("Using existing virtual environment")
            return True

    try:
        subprocess.run(
            [sys.executable, "-m", "venv", str(venv_path)],
            check=True,
            capture_output=True
        )
        print_success(f"Created virtual environment: {venv_path}")
        return True
    except subprocess.CalledProcessError as e:
        print_error(f"Failed to create virtual environment: {e}")
        return False


def get_python_executable():
    """Get the Python executable in the venv."""
    system = platform.system()
    if system == "Windows":
        return PROJECT_ROOT / VENV_NAME / "Scripts" / "python.exe"
    else:
        return PROJECT_ROOT / VENV_NAME / "bin" / "python"


def get_pip_executable():
    """Get the pip executable in the venv."""
    system = platform.system()
    if system == "Windows":
        return PROJECT_ROOT / VENV_NAME / "Scripts" / "pip.exe"
    else:
        return PROJECT_ROOT / VENV_NAME / "bin" / "pip"


def install_packages():
    """Install required packages."""
    print_step(2, "Installing required packages")

    pip_exec = get_pip_executable()

    if not pip_exec.exists():
        print_error(f"Pip not found at {pip_exec}")
        return False

    packages = [
        "labelImg",
        "ultralytics>=8.0.0",
        "opencv-python>=4.8.0",
        "pillow>=10.0.0",
        "scikit-learn>=1.3.0",
        "pyyaml>=6.0",
        "albumentations>=1.3.0",
        "onnx>=1.15.0",
        "onnxruntime>=1.16.0",
    ]

    for package in packages:
        print(f"  Installing {package}...")
        try:
            subprocess.run(
                [str(pip_exec), "install", package],
                check=True,
                capture_output=True
            )
            print_success(f"Installed {package}")
        except subprocess.CalledProcessError as e:
            print_error(f"Failed to install {package}: {e}")
            return False

    return True


def create_folder_structure():
    """Create dataset folder structure."""
    print_step(3, "Creating folder structure")

    for dir_path in DATASET_DIRS + OTHER_DIRS:
        full_path = PROJECT_ROOT / dir_path
        full_path.mkdir(parents=True, exist_ok=True)
        print_success(f"Created: {dir_path}")

    return True


def create_classes_file():
    """Create classes.txt file."""
    print_step(4, "Creating classes.txt")

    classes_file = PROJECT_ROOT / "plant_dataset" / "classes.txt"

    with open(classes_file, 'w') as f:
        for cls in CLASSES:
            f.write(f"{cls}\n")

    print_success(f"Created classes.txt: {', '.join(CLASSES)}")
    return True


def print_labelimg_instructions():
    """Print instructions for launching labelImg."""
    print_step(5, "LabelImg Instructions")

    labelimg_exec = get_python_executable().parent / "labelImg.exe"
    if not labelimg_exec.exists():
        # Try Scripts directory
        labelimg_exec = get_python_executable().parent / "Scripts" / "labelImg.exe"

    print(f"""
{Colors.BOLD}How to use labelImg:{Colors.END}

1. {Colors.YELLOW}Copy your raw photos to the appropriate folders:{Colors.END}
   - Fungus photos → plant_dataset/raw/fungus/
   - Pest photos → plant_dataset/raw/pest/

2. {Colors.YELLOW}Launch labelImg:{Colors.END}
   {Colors.CYAN}{'    ' if platform.system() != 'Windows' else ''}{get_python_executable()} -m labelImg{Colors.END}

   Or directly:
   {Colors.CYAN}{'    ' if platform.system() != 'Windows' else ''}{labelimg_exec}{Colors.END}

3. {Colors.YELLOW}Configure labelImg:{Colors.END}
   - Click 'Open Dir' → select plant_dataset/raw/fungus/
   - Click 'Change Save Dir' → select plant_dataset/raw/fungus/
   - Click 'PascalVOC' button → change to 'YOLO'
   - Click 'Create RectBox'
   - Draw bounding boxes around the fungus/pest
   - Select the class label
   - Press Ctrl+S to save (creates .txt file next to image)
   - Press 'Next Image' (d) or 'Prev Image' (a) to navigate

4. {Colors.YELLOW}Repeat for both classes:{Colors.END}
   - Label all fungus images in plant_dataset/raw/fungus/
   - Label all pest images in plant_dataset/raw/pest/

5. {Colors.YELLOW}After labeling is complete:{Colors.END}
   Run: {Colors.GREEN}python prepare_dataset.py{Colors.END}

{Colors.BOLD}YOLO Format (.txt files):{Colors.END}
Each line represents one object:
  <class_id> <x_center> <y_center> <width> <height>

All values normalized (0.0 to 1.0):
  - class_id: 0 for fungus, 1 for pest
  - x_center, y_center: center of bounding box
  - width, height: bounding box dimensions

{Colors.BOLD}Example (fungus detection):{Colors.END}
  0 0.500 0.500 0.300 0.400

{Colors.BOLD}Keyboard Shortcuts in labelImg:{Colors.END}
  Ctrl+S  - Save
  d       - Next image
  a       - Previous image
  w       - Create rect box
  del     - Delete selected box
  Ctrl+R  - Rotate (useful for phone photos)
""")


def print_next_steps():
    """Print next steps."""
    print(f"""
{Colors.GREEN}{Colors.BOLD}{'='*60}{Colors.END}
{Colors.GREEN}{Colors.BOLD}SETUP COMPLETE!{Colors.END}
{Colors.GREEN}{Colors.BOLD}{'='*60}{Colors.END}

{Colors.BOLD}Next Steps:{Colors.END}
1. Copy your reference photos to:
   {Colors.CYAN}plant_dataset/raw/fungus/{Colors.END}
   {Colors.CYAN}plant_dataset/raw/pest/{Colors.END}

2. Launch labelImg and label all images

3. Run dataset preparation:
   {Colors.YELLOW}python prepare_dataset.py{Colors.END}

4. Run augmentation:
   {Colors.YELLOW}python augment_dataset.py{Colors.END}

5. Train the model:
   {Colors.YELLOW}python train_model.py{Colors.END}

6. Verify the model:
   {Colors.YELLOW}python verify_model.py ./test_images/{Colors.END}
""")


# ============================================================
# Main
# ============================================================
def main():
    print_header("PLANT ROVER TRAINING PIPELINE - STEP 1: SETUP")

    # Change to script directory
    os.chdir(PROJECT_ROOT)

    # Step 1: Create venv
    if not create_virtual_environment():
        print_error("Setup failed at virtual environment creation")
        sys.exit(1)

    # Step 2: Install packages
    if not install_packages():
        print_error("Setup failed at package installation")
        sys.exit(1)

    # Step 3: Create folders
    if not create_folder_structure():
        print_error("Setup failed at folder creation")
        sys.exit(1)

    # Step 4: Create classes file
    if not create_classes_file():
        print_error("Setup failed at classes file creation")
        sys.exit(1)

    # Step 5: Print instructions
    print_labelimg_instructions()
    print_next_steps()

    print_success("Setup complete!")
    print()


if __name__ == "__main__":
    main()
