#!/bin/bash
#
# xv7 Build Script
#
# Usage:
#   ./run.sh              # Build with GCC (default)
#   ./run.sh gcc          # Build with GCC
#   ./run.sh clang        # Build with Clang
#

COMPILER=${1:-gcc}

echo "=========================================="
echo "Building xv7 with COMPILER=$COMPILER"
echo "=========================================="

make clean
make COMPILER=$COMPILER
make COMPILER=$COMPILER fs.img
make COMPILER=$COMPILER qemu NET=user
