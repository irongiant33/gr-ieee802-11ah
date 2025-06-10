#!/bin/bash

# Script to clean and uninstall a CMake-based project

# Exit on any error
set -e

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

# Function to log messages
log() {
    echo "[INFO] $1"
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
clean_build