# Script to copy plant images for training
Set-Location "C:/Users/sanme/desktop/Daanish LY Project/plant_rover_training"

# Create folders
New-Item -ItemType Directory -Force -Path "plant_dataset/raw/fungus" | Out-Null
New-Item -ItemType Directory -Force -Path "plant_dataset/raw/pest" | Out-Null

# Source path - use absolute path
$src = "C:/Users/sanme/desktop/Daanish LY Project/Matter"

# Copy fungus images
Get-ChildItem "$src/*/Fungus" -Directory | ForEach-Object {
    Get-ChildItem $_.FullName -File | ForEach-Object {
        Copy-Item -Force $_.FullName "plant_dataset/raw/fungus/"
    }
}

# Copy pest images
Get-ChildItem "$src/*/Pest" -Directory | ForEach-Object {
    Get-ChildItem $_.FullName -File | ForEach-Object {
        Copy-Item -Force $_.FullName "plant_dataset/raw/pest/"
    }
}

# Count results
$fungusCount = (Get-ChildItem "plant_dataset/raw/fungus" -File).Count
$pestCount = (Get-ChildItem "plant_dataset/raw/pest" -File).Count

Write-Host "================================"
Write-Host "Images copied successfully!"
Write-Host "Fungus images: $fungusCount"
Write-Host "Pest images: $pestCount"
Write-Host "================================"
