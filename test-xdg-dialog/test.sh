#!/bin/bash

# Test script for XDG Dialog v1 client
echo "=== XDG Dialog v1 Test ==="
echo ""

# Build if needed
if [ ! -f "build-xdg-dialog/test-xdg-dialog" ]; then
    echo "Building..."
    cmake -S test-xdg-dialog -B build-xdg-dialog
    cmake --build build-xdg-dialog
fi

# Run with timeout
echo "Running test (3 seconds)..."
SDL_VIDEODRIVER=wayland timeout 3 ./build-xdg-dialog/test-xdg-dialog 2>&1 | grep -E "init|dialog|Interfaces|===|Interface|Modal|Non|xdg_wm_dialog"

echo ""
echo "Test completed."