"""
Simple Manual Labeling Tool for Plant Disease Detection
Press keys to draw boxes and save labels
"""

import cv2
import os
from pathlib import Path

# Simple labeling class
class SimpleLabeler:
    def __init__(self, image_paths, output_dir, class_id):
        self.image_paths = sorted(image_paths)
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)
        self.class_id = class_id
        self.current_idx = 0
        self.boxes = []
        self.drawing = False
        self.start_point = None
        self.current_image = None
        self.display_image = None
        self.scale = 1  # Image scale if too large

    def load_image(self, idx):
        path = self.image_paths[idx]
        self.current_image_path = path
        img = cv2.imread(str(path))
        if img is None:
            print(f"Error loading {path}")
            return False

        # Scale if too large
        h, w = img.shape[:2]
        if max(h, w) > 1000:
            self.scale = 1000 / max(h, w)
            img = cv2.resize(img, None, fx=self.scale, fy=self.scale)

        self.current_image = img.copy()
        self.display_image = img.copy()
        self.boxes = []

        # Load existing boxes if any
        label_path = Path(str(path).replace('.jpg', '.txt').replace('.png', '.txt').replace('.JPG', '.txt').replace('.PNG', '.txt'))
        if label_path.exists():
            with open(label_path, 'r') as f:
                for line in f:
                    parts = line.strip().split()
                    if len(parts) >= 5:
                        # YOLO format: class x_center y_center width height
                        # Convert back to pixel coordinates
                        self.boxes.append([
                            float(parts[1]) * w,  # x_center
                            float(parts[2]) * h,  # y_center
                            float(parts[3]) * w,  # width
                            float(parts[4]) * h   # height
                        ])

        self.redraw()
        return True

    def redraw(self):
        self.display_image = self.current_image.copy()
        for box in self.boxes:
            x_center, y_center, box_w, box_h = box
            x1 = int((x_center - box_w / 2))
            y1 = int((y_center - box_h / 2))
            x2 = int((x_center + box_w / 2))
            y2 = int((y_center + box_h / 2))
            cv2.rectangle(self.display_image, (x1, y1), (x2, y2), (0, 255, 0), 2)
        cv2.imshow('Labeling Tool', self.display_image)

    def save(self):
        if not self.boxes:
            return

        h, w = self.current_image.shape[:2]

        # Get image path (original size, not scaled)
        label_path = self.output_dir / self.image_paths[self.current_idx].name
        label_path = label_path.with_suffix('.txt')

        with open(label_path, 'w') as f:
            for box in self.boxes:
                x_center, y_center, box_w, box_h = box
                # Normalize to 0-1
                xc = x_center / w
                yc = y_center / h
                bw = box_w / w
                bh = box_h / h
                f.write(f"{self.class_id} {xc:.6f} {yc:.6f} {bw:.6f} {bh:.6f}\n")

        print(f"✓ Saved {len(self.boxes)} boxes to {label_path.name}")

    def run(self):
        print("="*60)
        print("  Simple Plant Disease Labeling Tool")
        print("="*60)
        print("\nInstructions:")
        print("  Left Click + Drag: Draw box")
        print("  W: Save and next image")
        print("  S: Skip (no disease)")
        print("  Z: Undo last box")
        print("  A: Previous image")
        print("  Q: Quit and save\n")

        self.load_image(0)

        while self.current_idx < len(self.image_paths):
            key = cv2.waitKey(0) & 0xFF

            if key == ord('q'):
                self.save()
                print("\n✓ Quitting...")
                break
            elif key == ord('w'):
                self.save()
                self.current_idx += 1
                if self.current_idx < len(self.image_paths):
                    self.load_image(self.current_idx)
                else:
                    print("\n✓ All images labeled!")
                    break
            elif key == ord('s'):
                print(f"Skipping {self.image_paths[self.current_idx].name}")
                self.current_idx += 1
                if self.current_idx < len(self.image_paths):
                    self.load_image(self.current_idx)
                else:
                    print("\n✓ All images labeled!")
                    break
            elif key == ord('a'):
                if self.current_idx > 0:
                    self.save()
                    self.current_idx -= 1
                    self.load_image(self.current_idx)
            elif key == ord('z'):
                if self.boxes:
                    self.boxes.pop()
                    self.redraw()

            # Mouse callback for drawing
            cv2.setMouseCallback('Labeling Tool', self.mouse_callback)

        cv2.destroyAllWindows()

    def mouse_callback(self, event, x, y, flags, param):
        if event == cv2.EVENT_LBUTTONDOWN:
            self.drawing = True
            self.start_point = (x, y)
        elif event == cv2.EVENT_LBUTTONUP:
            if self.drawing and self.start_point:
                x1, y1 = self.start_point
                x2, y2 = x, y

                # Ensure proper order
                x1, x2 = min(x1, x2), max(x1, x2)
                y1, y2 = min(y1, y2), max(y1, y2)

                # Convert to center, width, height format
                x_center = (x1 + x2) / 2 / self.scale
                y_center = (y1 + y2) / 2 / self.scale
                box_w = (x2 - x1) / self.scale
                box_h = (y2 - y1) / self.scale

                self.boxes.append([x_center, y_center, box_w, box_h])
                self.drawing = False
                self.start_point = None
                self.redraw()

        elif event == cv2.EVENT_MOUSEMOVE and self.drawing:
            if self.start_point:
                self.display_image = self.current_image.copy()
                for box in self.boxes:
                    x_center, y_center, box_w, box_h = box
                    x1 = int((x_center - box_w / 2))
                    y1 = int((y_center - box_h / 2))
                    x2 = int((x_center + box_w / 2))
                    y2 = int((y_center + box_h / 2))
                    cv2.rectangle(self.display_image, (x1, y1), (x2, y2), (0, 255, 0), 2)

                # Draw current box being drawn
                cv2.rectangle(self.display_image, self.start_point, (x, y), (255, 0, 0), 2)
                cv2.imshow('Labeling Tool', self.display_image)


def main():
    # Setup paths
    base_path = Path("plant_dataset/raw")

    print("="*60)
    print("  Plant Disease Labeling")
    print("="*60)

    # Choose class
    print("\nWhich class are you labeling?")
    print("  1. Fungus")
    print("  2. Pest")
    choice = input("Enter choice (1 or 2): ")

    if choice == "1":
        class_name = "fungus"
        class_id = 0
    elif choice == "2":
        class_name = "pest"
        class_id = 1
    else:
        print("Invalid choice. Exiting.")
        return

    # Get images
    image_dir = base_path / class_name
    images = list(image_dir.glob('*.jpg')) + list(image_dir.glob('*.png')) + list(image_dir.glob('*.JPG')) + list(image_dir.glob('*.PNG'))

    if not images:
        print(f"No images found in {image_dir}")
        return

    print(f"\nFound {len(images)} images")

    # Create labeler
    labeler = SimpleLabeler(images, image_dir, class_id)
    labeler.run()

if __name__ == "__main__":
    main()
