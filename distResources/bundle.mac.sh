#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

# --- Configuration ---
BASE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="$BASE_DIR/dist"
BUILD_DIR="$BASE_DIR/cmake-build-debug"
APP_NAME="Rodun"
APP_DIR="$DIST_DIR/$APP_NAME.app"
DMG_NAME="$DIST_DIR/$APP_NAME-Installer.dmg"
STAGING_DIR="$DIST_DIR/dmg_staging" # New: Temporary staging directory
VOLNAME="$APP_NAME Installer" # Volume name for the DMG

# --- Parse Arguments ---
SILENT=false
for arg in "$@"; do
    case $arg in
        --silent)
            SILENT=true
            ;;
        --help)
            echo "Usage: bundle.mac.sh [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --hard      Rebuild the project from scratch."
            echo "  --silent    Suppress all output except errors."
            echo "  --help      Display this help message."
            exit 0
            ;;
    esac
done

# --- Helper Function for Conditional Echo ---
log() {
    if [ "$SILENT" = false ]; then
        echo "$@"
    fi
}

echo "--- Starting DMG Creation Process ---"
echo "Base Directory: $BASE_DIR"
echo "Distribution Directory: $DIST_DIR"
echo "Application Name: $APP_NAME"

# --- Step 1: Create dist directory if it doesn't exist ---
log "1. Ensuring dist directory exists..."
mkdir -p "$DIST_DIR"

# --- Step 2: Delete cmake-build-debug folder and re-run make commands (if --hard flag is used) ---
if [[ "$1" == "--hard" ]]; then
    log "2. --hard flag detected: Cleaning and rebuilding project..."
    if [ -d "$BUILD_DIR" ]; then
        log "   Deleting existing build directory: $BUILD_DIR"
        rm -rf "$BUILD_DIR"
    fi
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake .. > /dev/null 2>&1
    make > /dev/null 2>&1
    log "   Project built successfully."
fi

# --- Step 3: Delete any existing Rodun.app folders and DMG files ---
log "3. Cleaning up old .app bundles and .dmg files..."
if [ -d "$APP_DIR" ]; then
    log "   Deleting existing .app bundle: $APP_DIR"
    rm -rf "$APP_DIR"
fi
find "$DIST_DIR" -name "*.dmg" -type f -delete
# Also clean up any temporary read-write DMGs from previous failed attempts
find "$DIST_DIR" -name "rw.*.dmg" -type f -delete
log "   Old files cleaned."

# --- Clean up any potential previous create-dmg temporary mount points ---
log "3.1. Cleaning up any lingering temporary create-dmg mount points..."
hdiutil info | grep -E '/Volumes/dmg\.[a-zA-Z0-9]+' | awk '{print $NF}' | while read -r mount_path; do
    log "    Found lingering mount: $mount_path. Attempting to detach..."
    hdiutil detach "$mount_path" -force > /dev/null 2>&1 || log "    Warning: Could not detach $mount_path. You may need to unmount it manually."
done
log "   Temporary mount points cleaned."

# --- Step 4: Scaffold Rodun.app directory structure ---
log "4. Scaffolding $APP_NAME.app directory structure..."
mkdir -p "$APP_DIR/Contents/MacOS"
mkdir -p "$APP_DIR/Contents/Resources"
cp "$BASE_DIR/distResources/Info.plist" "$APP_DIR/Contents"
log "   App directory structure created."

# --- Step 5: Copy .icns file into Resources folder ---
log "5. Copying icon file..."
cp "$BASE_DIR/distResources/icons/icon.icns" "$APP_DIR/Contents/Resources"
log "   Icon copied."

# --- Step 6: Copy executable into MacOS folder ---
log "6. Copying executable into MacOS folder..."
cp "$BUILD_DIR/$APP_NAME" "$APP_DIR/Contents/MacOS"
log "   Executable copied."

# --- Step 7: Create temporary staging folder for DMG layout and populate it ---
log "7. Setting up temporary DMG staging directory: $STAGING_DIR"
# Ensure the staging directory is clean before populating
if [ -d "$STAGING_DIR" ]; then
    log "   Cleaning existing staging directory: $STAGING_DIR"
    rm -rf "$STAGING_DIR"
fi
mkdir -p "$STAGING_DIR"
cp -R "$APP_DIR" "$STAGING_DIR/" # Copy your .app into the staging directory

log "   Staging directory prepared (Applications symlink handled by create-dmg)."


# --- Step 8: Create the DMG ---
log "8. Creating the Disk Image ($DMG_NAME)..."
# Ensure create-dmg is installed
if ! command -v create-dmg &> /dev/null; then
    echo "Error: create-dmg could not be found."
    echo "Please install it (e.g., brew install create-dmg) and try again."
    exit 1
fi

if [ "$SILENT" = true ]; then
    create-dmg \
      --volname "$VOLNAME" \
      --app-drop-link 550 150 \
      --icon "$APP_NAME.app" 200 150 \
      --window-pos 200 120 \
      --window-size 800 400 \
      --hide-extension "$APP_NAME.app" \
      --disk-image-size 10000 \
      "$DMG_NAME" \
      "$STAGING_DIR" > /dev/null 2>&1
else
    create-dmg \
      --volname "$VOLNAME" \
      --app-drop-link 550 150 \
      --icon "$APP_NAME.app" 200 150 \
      --window-pos 200 120 \
      --window-size 800 400 \
      --hide-extension "$APP_NAME.app" \
      --disk-image-size 10000 \
      "$DMG_NAME" \
      "$STAGING_DIR"
fi
log "   DMG creation command executed."

# --- Step 9: Clean up the temporary staging folder ---
log "9. Cleaning up temporary staging folder: $STAGING_DIR"
rm -rf "$STAGING_DIR"
log "   Staging folder removed."

# --- Step 10: Unmount the disk image (if it's still mounted) ---
log "10. Checking for and unmounting the final disk image if mounted..."
MOUNT_POINT="/Volumes/$VOLNAME"
if hdiutil info | grep -q "$MOUNT_POINT"; then
    log "    Disk image '$VOLNAME' found mounted at '$MOUNT_POINT'. Attempting to unmount..."
    hdiutil detach "$MOUNT_POINT" -force > /dev/null 2>&1 || log "    Warning: Could not force unmount '$MOUNT_POINT'. You may need to unmount it manually."
else
    log "    Disk image '$VOLNAME' not found mounted. No unmount needed."
fi

log "--- DMG Creation Process Completed ---"
log "Final DMG created at: $DMG_NAME"