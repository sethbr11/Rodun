#!/bin/bash

# bundle.mac.sh
# This script automates the process of building, bundling, code-signing,
# and creating a macOS Disk Image (DMG) for the Rodun application.

# Exit immediately if any command fails.
set -e

# --- Configuration Variables ---
readonly BASE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
readonly DIST_DIR="$BASE_DIR/dist"
readonly BUILD_DIR="$BASE_DIR/cmake-build-debug"
readonly APP_NAME="Rodun"
readonly APP_BUNDLE_PATH="$DIST_DIR/$APP_NAME.app"
readonly DMG_NAME="$DIST_DIR/$APP_NAME-Installer.dmg"
readonly STAGING_DIR="$DIST_DIR/dmg_staging"
readonly DMG_VOLNAME="$APP_NAME Installer" # Volume name for the DMG

# Libharu paths (determined from previous debugging)
# ORIGINAL_LIBHPDF_DEPENDENCY_PATH: The path Rodun's executable is linked against (from 'otool -L').
# ACTUAL_LIBHPDF_FILE_TO_COPY: The actual path of the libharu dylib installed by Homebrew.
readonly ORIGINAL_LIBHPDF_DEPENDENCY_PATH="/opt/homebrew/opt/libharu/lib/libhpdf.2.4.dylib"
readonly ACTUAL_LIBHPDF_FILE_TO_COPY="/opt/homebrew/Cellar/libharu/2.4.5/lib/libhpdf.2.4.5.dylib"
readonly BUNDLED_LIBHPDF_NAME="libhpdf.2.4.5.dylib" # The name of the dylib when copied into the bundle

# Code Signing Identity:
# Use "-" for ad-hoc signing (development/testing).
# Use "Developer ID Application: Your Name (XXXXXXXXXX)" for distribution.
readonly SIGNING_IDENTITY="-" # Ad-hoc signing for development

# --- Global Flags ---
BUILD_HARD=false # True if --hard flag is passed
SILENT_MODE=false # True if --silent flag is passed

# --- Helper Functions ---

# Log a message to stdout unless SILENT_MODE is true.
log() {
    if [ "$SILENT_MODE" = false ]; then
        echo "--> $@"
    fi
}

# Log an error message to stderr and exit.
log_error() {
    echo "--- ERROR: $@" >&2
    exit 1
}

# Parse command line arguments.
parse_args() {
    for arg in "$@"; do
        case $arg in
            --hard)
                BUILD_HARD=true
                shift
                ;;
            --silent)
                SILENT_MODE=true
                shift
                ;;
            --help)
                echo "Usage: $(basename "$0") [OPTIONS]"
                echo ""
                echo "Options:"
                echo "  --hard      Rebuild the project from scratch (cleans cmake-build-debug)."
                echo "  --silent    Suppress most output messages (only errors are shown)."
                echo "  --help      Display this help message."
                exit 0
                ;;
            *)
                echo "Unknown option: $arg"
                echo "Use --help for usage information."
                exit 1
                ;;
        esac
    done
}

# Perform initial cleanup: remove old build artifacts and mount points.
cleanup() {
    log "Initiating cleanup of old build artifacts and lingering mount points..."

    # Remove existing distribution directory to ensure a clean slate
    if [ -d "$DIST_DIR" ]; then
        log "  Removing existing distribution directory: $DIST_DIR"
        rm -rf "$DIST_DIR"
    fi
    mkdir -p "$DIST_DIR"

    # Clean up any temporary read-write DMGs from previous failed attempts
    find "$DIST_DIR" -name "rw.*.dmg" -type f -delete 2>/dev/null || true
    
    # Check for and detach any lingering create-dmg temporary mount points
    log "  Checking for and detaching lingering temporary DMG mount points..."
    # Filter for paths that look like '/Volumes/dmg.XXXXXXXX'
    hdiutil info | grep -E '/Volumes/dmg\.[a-zA-Z0-9]+' | awk '{print $NF}' | while read -r mount_path; do
        if [ -n "$mount_path" ]; then
            log "    Found and detaching: $mount_path"
            hdiutil detach "$mount_path" -force > /dev/null 2>&1 || log "    Warning: Could not detach $mount_path. Manual unmount might be required."
        fi
    done

    # Check for and unmount the final DMG volume if it's still mounted
    local final_mount_point="/Volumes/$DMG_VOLNAME"
    if hdiutil info | grep -q "$final_mount_point"; then
        log "  Found and detaching final DMG mount point: $final_mount_point"
        hdiutil detach "$final_mount_point" -force > /dev/null 2>&1 || log "    Warning: Could not detach $final_mount_point. Manual unmount might be required."
    fi
    log "Cleanup complete."
}

# Build the project using CMake and Make.
build_project() {
    log "Building project (cmake-build-debug)..."
    if [ "$BUILD_HARD" = true ]; then
        log "  --hard flag detected: Deleting existing build directory and rebuilding."
        if [ -d "$BUILD_DIR" ]; then
            rm -rf "$BUILD_DIR"
        fi
        mkdir -p "$BUILD_DIR"
        cd "$BUILD_DIR"
        cmake .. > /dev/null # Suppress cmake output
        make -j "$(sysctl -n hw.ncpu)" # Use all available CPU cores for faster build
        log "  Project rebuilt successfully."
    else
        log "  Skipping full rebuild. Assuming project is already built."
        log "  (Use --hard flag to force a clean rebuild)"
        if [ ! -f "$BUILD_DIR/$APP_NAME" ]; then
            log_error "Executable not found at $BUILD_DIR/$APP_NAME. Please build the project or use --hard."
        fi
    fi
    cd "$BASE_DIR" # Return to base directory
}

# Scaffold the .app bundle directory structure.
setup_app_bundle() {
    log "Scaffolding $APP_NAME.app bundle structure..."
    mkdir -p "$APP_BUNDLE_PATH/Contents/MacOS"
    mkdir -p "$APP_BUNDLE_PATH/Contents/Resources"
    mkdir -p "$APP_BUNDLE_PATH/Contents/Frameworks"
    cp "$BASE_DIR/distResources/Info.plist" "$APP_BUNDLE_PATH/Contents" \
        || log_error "Failed to copy Info.plist."
    log "  App directory structure created at $APP_BUNDLE_PATH."
}

# Copy necessary files into the .app bundle.
copy_files() {
    log "Copying application executable, icon, and dynamic libraries into bundle..."

    # Copy executable
    cp "$BUILD_DIR/$APP_NAME" "$APP_BUNDLE_PATH/Contents/MacOS/" \
        || log_error "Failed to copy $APP_NAME executable."
    log "  Executable copied: $APP_NAME."

    # Copy icon
    cp "$BASE_DIR/distResources/icons/icon.icns" "$APP_BUNDLE_PATH/Contents/Resources/" \
        || log_error "Failed to copy icon.icns."
    log "  Icon copied: icon.icns."

    # Copy Libharu dynamic library
    cp "$ACTUAL_LIBHPDF_FILE_TO_COPY" "$APP_BUNDLE_PATH/Contents/Frameworks/$BUNDLED_LIBHPDF_NAME" \
        || log_error "Failed to copy $ACTUAL_LIBHPDF_FILE_TO_COPY."
    log "  Libharu dylib copied: $BUNDLED_LIBHPDF_NAME."

    # Update dynamic library paths within the executable and the library itself
    log "  Updating dynamic library paths with install_name_tool..."
    # Change the path that Rodun.app's executable looks for libhpdf
    install_name_tool -change "$ORIGINAL_LIBHPDF_DEPENDENCY_PATH" \
      "@executable_path/../Frameworks/$BUNDLED_LIBHPDF_NAME" \
      "$APP_BUNDLE_PATH/Contents/MacOS/$APP_NAME" \
      || log_error "Failed to update install_name for $APP_NAME executable."
    log "    Executable's libhpdf path updated."

    # Set the internal ID of the copied libhpdf dylib
    install_name_tool -id "@rpath/$BUNDLED_LIBHPDF_NAME" \
      "$APP_BUNDLE_PATH/Contents/Frameworks/$BUNDLED_LIBHPDF_NAME" \
      &> /dev/null || log_error "Failed to set install_id for $BUNDLED_LIBHPDF_NAME."
    log "    Bundled libhpdf.dylib's ID set."
    log "File copying and path adjustments complete."
}

# Code sign the application bundle.
codesign_app() {
    log "Initiating code signing for the application bundle..."

    # Sign bundled frameworks/libraries first (deepest first)
    log "  Signing bundled dynamic library: $BUNDLED_LIBHPDF_NAME..."
    codesign --force --deep --sign "$SIGNING_IDENTITY" \
      "$APP_BUNDLE_PATH/Contents/Frameworks/$BUNDLED_LIBHPDF_NAME" \
      || log_error "Failed to sign $BUNDLED_LIBHPDF_NAME."

    # Sign the main executable
    log "  Signing main executable: $APP_NAME..."
    codesign --force --deep --sign "$SIGNING_IDENTITY" \
      "$APP_BUNDLE_PATH/Contents/MacOS/$APP_NAME" \
      || log_error "Failed to sign $APP_NAME executable."

    # Sign the entire application bundle
    log "  Signing the entire application bundle: $APP_BUNDLE_PATH..."
    codesign --force --deep --sign "$SIGNING_IDENTITY" \
      "$APP_BUNDLE_PATH" \
      || log_error "Failed to sign $APP_NAME.app bundle."

    log "Code signing process complete."
}

# Create the Disk Image (DMG).
create_dmg() {
    log "Creating Disk Image ($DMG_NAME)..."

    # Ensure create-dmg is installed
    if ! command -v create-dmg &> /dev/null; then
        log_error "The 'create-dmg' tool was not found. Please install it (e.g., 'brew install create-dmg') and try again."
    fi

    # Set up temporary staging directory for DMG layout
    log "  Setting up temporary DMG staging directory: $STAGING_DIR"
    mkdir -p "$STAGING_DIR"
    cp -R "$APP_BUNDLE_PATH" "$STAGING_DIR/" \
        || log_error "Failed to copy $APP_NAME.app to staging directory."

    log "  Running create-dmg command..."
    if [ "$SILENT_MODE" = true ]; then
        create-dmg \
        --volname "$DMG_VOLNAME" \
        --app-drop-link 550 150 \
        --icon "$APP_NAME.app" 200 150 \
        --window-pos 200 120 \
        --window-size 800 400 \
        --hide-extension "$APP_NAME.app" \
        --disk-image-size 10000 \
        "$DMG_NAME" \
        "$STAGING_DIR" > /dev/null 2>&1 # Redirect all output to null
    else
        create-dmg \
        --volname "$DMG_VOLNAME" \
        --app-drop-link 550 150 \
        --icon "$APP_NAME.app" 200 150 \
        --window-pos 200 120 \
        --window-size 800 400 \
        --hide-extension "$APP_NAME.app" \
        --disk-image-size 10000 \
        "$DMG_NAME" \
        "$STAGING_DIR" # Let output go to console
    fi

    # Check exit status of create-dmg
    if [ $? -ne 0 ]; then
        log_error "create-dmg failed. Check console output for details."
    else
        log "  DMG created successfully."
    fi

    # Check for errors from create-dmg
    if echo "$create_dmg_output" | grep -q "ERROR"; then
        log_error "create-dmg failed: $create_dmg_output"
    else
        log "  DMG created successfully."
        if [ "$SILENT_MODE" = false ]; then
            echo "$create_dmg_output" # Print create-dmg output if not in silent mode
        fi
    fi

    log "  Cleaning up temporary staging folder: $STAGING_DIR"
    rm -rf "$STAGING_DIR"
    log "DMG creation process complete."
}

# --- Main Script Execution ---
main() {
    log "--- Starting Rodun macOS Application Bundling and DMG Creation Process ---"
    log "Base Directory: $BASE_DIR"
    log "Distribution Directory: $DIST_DIR"
    log "Application Name: $APP_NAME"

    parse_args "$@"

    cleanup
    build_project
    setup_app_bundle
    copy_files
    codesign_app
    create_dmg

    log "--- Rodun macOS Application Bundling and DMG Creation Completed Successfully! ---"
    log "Final DMG available at: $DMG_NAME"
}

# Call the main function
main "$@"