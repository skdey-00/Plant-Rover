"""
Fix for simple_label.py - ensures proper window focus for mouse events
"""

import cv2
import os
import numpy as np
from pathlib import Path

def get_images_from_dir(directory):
    """Get all image files from directory"""
    extensions = ['.jpg', '.jpeg', '.png', '.JPG', '.PNG']
    images = []
    for ext in extensions:
        images.extend(list(directory.glob(f'*{ext}')))
    return images

def main():
    print("="*60)
    print("  Simple Plant Disease Labeling Tool v2")
    print("="*60)
    print("\n⚠ IMPORTANT:")
    print("   1. Click on the image window FIRST to give it focus")
    print("   2. Then use mouse to draw boxes")
    print()

    # Choose class
    print("Which class are you labeling?")
    print("  1. Fungus (black dots)")
    print("  2. Pest (white dots)")
    choice = input("Enter choice (1 or 2): ")

    if choice == "1":
        class_id = 0
        class_name = "fungus"
        target_dir = Path("plant_dataset/raw/fungus")
    elif choice == "2":
        class_id = 1
        class_name = "pest"
        target_dir = Path("plant_dataset/raw/pest")
    else:
        print("Invalid choice. Exiting.")
        return

    # Get images that need labeling (no .txt file yet)
    all_images = get_images_from_dir(target_dir)
    unlabeled = [img for img in all_images if not img.with_suffix('.txt').exists()]

    print(f"\nFound {len(unlabeled)} unlabeled images")
    print(f"Already labeled: {len(all_images) - len(unabeled)} images")

    if not unlabeled:
        print("\n✓ All images are already labeled!")
        return

    # Select which images to label (start from first unlabeled)
    images_to_label = unlabeled

    print(f"\nWill label {len(images_to_label)} images")
    print("Controls:")
    print("  Click window first! Then:")
    print("  + Draw: Left click + drag")
    print("  Undo: Z key")
    print("  Save & next: W key")
    print("  Skip: S key")
    print("  Quit: Q key")
    print("\nPress any key to start...")
    input()

    # Process each image
    for idx, img_path in enumerate(images_to_label):
        img = cv2.imread(str(img_path))
        if img is None:
            continue

        h, w = img.shape[:2]

        # Scale if too large
        scale = 1.0
        max_dim = 1200
        if max(h, w) > max_dim:
            scale = max_dim / max(h, w)
            img = cv2.resize(img, None, fx=scale, fy=scale)
            h, w = img.shape[:2]

        display_img = img.copy()
        boxes = []
        current_idx = idx

        # Display image info
        window_name = f"Image {current_idx+1}/{len(images_to_label)} - {img_path.name}"
        cv2.imshow(window_name, display_img)

        def mouse_callback(event, x, y, flags, param):
            nonlocal boxes, display_img, img, scale

            if event == cv2.EVENT_LBUTTONDOWN:
                boxes.append([(x, y)])
            elif event == cv2.EVENT_LBUTTONUP:
                if len(boxes) > 0:
                    # Finalize the last box
                    start = boxes[-1][0]
                    end = (x, y)
                    x1, x2 = min(start[0], end[0]), max(start[0], end[0])
                    y1, y2 = min(start[1], end[1]), max(start[1], end[1])

                    # Scale back to original coordinates
                    x1 /= scale
                    x2 /= scale
                    y1 /= scale
                    y2 /= scale

                    # Convert to center, width, height
                    x_center = (x1 + x2) / 2 / w
                    y_center = (y1 + y2) / 2 / h
                    width = (x2 - x1) / w
                    height = (y2 - y1) / h

                    boxes[-1] = [x_center, y_center, width, height]

                    # Redraw all boxes
                    display_img = img.copy()
                    for box in boxes:
                        xc, yc, bw, bh = box
                        x1 = int((xc - bw / 2) * scale)
                        y1 = int((yc - bh / 2) * scale)
                        x2 = int((xc + bw / 2) * scale)
                        y2 = int((yc + bh / 2) * scale)
                        cv2.rectangle(display_img, (x1, y1), (x2, y2), (0, 255, 0), 2)
                    cv2.imshow(window_name, display_img)

            elif event == cv2.EVENT_MOUSEMOVE and flags & cv2.EVENT_FLAG_LBUTTON:
                # Draw current rectangle
                if len(boxes) > 0:
                    start = boxes[-1][0]
                    temp_display = display_img.copy()
                    # Redraw existing boxes
                    for i, box in enumerate(boxes[:-1]):
                        xc, yc, bw, bh = box
                        x1 = int((xc - bw / 2) * scale)
                        y1 = int((yc - bh / 2) * scale)
                        x2 = int((xc + bw / 2) * scale)
                        y2 = int((yc + bh / 2) * scale)
                        cv2.rectangle(temp_display, (x1, y1), (x2, y2), (0, 255, 0), 2)
                    # Draw current rectangle
                    cv2.rectangle(temp_display, start, (x, y), (255, 0, 0), 2)
                    cv2.imshow(window_name, temp_display)

        cv2.setMouseCallback(window_name, mouse_callback)

        # Wait for user input
        key = cv2.waitKey(0) & 0xFF

        if key == ord('w'):  # Save and next
            if boxes:
                # Save labels
                label_path = img_path.with_suffix('.txt')
                with open(label_path, 'w') as f:
                    for box in boxes:
                        f.write(f"{class_id} {box[0]:.6f} {box[1]:.6f} {box[2]:.6f} {box[3]:.6f}\n")
                print(f"✓ Saved {len(boxes)} boxes → {img_path.name}")
            cv2.destroyAllWindows()
        elif key == ord('s'):  # Skip
            print(f"Skipped {img_path.name}")
            cv2.destroyAllWindows()
        elif key == ord('z'):  # Undo
            if boxes:
                boxes.pop()
                # Redraw
                display_img = img.copy()
                for box in boxes:
                    xc, yc, bw, bh = box
                    x1 = int((xc - bw / 2) * scale)
                    y1 = int((yc - bh / 2) * scale)
                    x2 = int((xc + bw / 2) * scale)
                    y2 = int((yc + bh / 2) * scale)
                    cv2.rectangle(display_img, (x1, y1), (x2, y2), (0, 255, 0), 2)
                cv2.imshow(window_name, display_img)
        elif key == ord('q'):  # Quit
            print(f"\n✓ Quitting. Labeled {idx} images this session")
            cv2.destroyAllWindows()
            break
        else:
            continue

        break  # Only process one at a time for safety

    print("\n✓ Labeling session complete!")

if __name__ == "__main__":
    main()
