#!/bin/bash

# Script to build, install, and optionally clean a CMake-based project

# Exit on any error
set -e

# Function to display usage information
usage() {
    echo "Usage: $0 [-j JOBS] [-c] [-h]"
    echo "Options:"
    echo "  -j JOBS   Number of parallel jobs for make (default: 4)"
    echo "  -c        Clean the build directory and uninstall before building"
    echo "  -h        Display this help message"
    exit 1
}

# Default values
JOBS=4
CLEAN_BUILD=false

# Parse command-line options
while getopts "j:ch" opt; do
    case $opt in
        j) JOBS="$OPTARG" ;;
        c) CLEAN_BUILD=true ;;
        h) usage ;;
        ?) usage ;;
    esac
done

# Function to log messages
log() {
    echo "[INFO] $1"
}

# Function to prompt user for confirmation
confirm() {
    read -r -p "$1 [y/N]: " response
    case "$response" in
        [yY][eE][sS]|[yY])
            true
            ;;
        *)
            false
            ;;
    esac
}

# Function to clean up build directory and uninstall
clean_build() {
    BUILD_DIR="build"
    if [ -d "$BUILD_DIR" ]; then
        log "Navigating to build directory for uninstall..."
        cd "$BUILD_DIR"
        if [ -f "Makefile" ]; then
            log "Running 'make uninstall' (requires sudo)..."
            sudo make uninstall || { echo "[ERROR] Uninstall failed"; exit 1; }
        else
            log "No Makefile found in build directory, skipping uninstall."
        fi
        cd ..
        log "Removing build directory..."
        sudo rm -rf "$BUILD_DIR" || { echo "[ERROR] Failed to remove build directory"; exit 1; }
    else
        log "No build directory found, nothing to clean."
    fi
}

# Main script logic

# If cleaning is requested, perform cleanup and artifact removal
if [ "$CLEAN_BUILD" = true ]; then
    clean_build
fi

# Check if build directory exists, create it if necessary
BUILD_DIR="build"
if [ ! -d "$BUILD_DIR" ]; then
    log "Creating build directory..."
    mkdir "$BUILD_DIR" || { echo "[ERROR] Failed to create build directory"; exit 1; }
else
    log "Using existing build directory."
fi

# Navigate to build directory
log "Navigating to build directory..."
cd "$BUILD_DIR"

# Run CMake
log "Configuring project with CMake..."
cmake .. || { echo "[ERROR] CMake configuration failed"; exit 1; }

# Run make
log "Building project with $JOBS parallel jobs..."
make -j"$JOBS" || { echo "[ERROR] Build failed"; exit 1; }

# Install
log "Installing project (requires sudo)..."
sudo make install || { echo "[ERROR] Installation failed"; exit 1; }

# Update shared library cache
log "Updating shared library cache (requires sudo)..."
sudo ldconfig || { echo "[ERROR] ldconfig failed"; exit 1; }

# Navigate back to original directory
log "Navigating back to original directory..."
cd ..

log "Build and installation completed successfully!"
