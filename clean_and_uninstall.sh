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

# Function to check for and remove library artifacts
check_and_remove_artifacts() {
    LIB_DIR="/usr/local/lib"
    ARTIFACT_PATTERN="libgnuradio-ieee802_11ah.so.*"
    log "Checking for library artifacts in $LIB_DIR..."
    if ls $LIB_DIR/$ARTIFACT_PATTERN >/dev/null 2>&1; then
        log "Found the following artifacts:"
        ls -l $LIB_DIR/$ARTIFACT_PATTERN
        if confirm "Do you want to remove these artifacts?"; then
            log "Removing artifacts (requires sudo)..."
            sudo rm -f $LIB_DIR/$ARTIFACT_PATTERN || { echo "[ERROR] Failed to remove artifacts"; exit 1; }
            log "Artifacts removed successfully."
        else
            log "Artifacts will not be removed."
        fi
    else
        log "No artifacts matching $ARTIFACT_PATTERN found in $LIB_DIR."
    fi
}

# Main script logic
clean_build
check_and_remove_artifacts