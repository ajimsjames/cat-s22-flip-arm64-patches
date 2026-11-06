#!/bin/bash
# AOT / ART Profile-Guided Optimization for Cat S22 Flip (Snapdragon 215 / 2GB RAM)
# Compiles core UI components ahead of time to eliminate jank and frame drops.

echo "Optimizing core system apps with full AOT (speed)..."
cmd package compile -m speed -f com.android.systemui
cmd package compile -m speed -f com.android.launcher3
cmd package compile -m speed -f com.android.settings
cmd package compile -m speed -f com.android.dialer

echo "Optimizing heavy apps with speed-profile (prevents OOM during dex2oat on 2GB RAM)..."
cmd package compile -m speed-profile -f com.phonepe.app

echo "Optimization complete!"
