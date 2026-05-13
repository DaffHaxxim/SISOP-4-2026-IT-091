#!/bin/bash
set -e

# Compile
make

# Create mount point if it doesn't exist
mkdir -p mnt

# Run FUSE in foreground mode
echo "Mounting FUSE filesystem at mnt/"
echo "Press Ctrl+C to unmount"
./kenz_rescue -f mnt
