/*
 * Configuration file for TFT Bot
 * Adjust these values to optimize for your system
 */

#ifndef CONFIG_H
#define CONFIG_H

// === PERFORMANCE TUNING ===

// Mouse click delay (milliseconds)
// Lower = faster, but may cause missed clicks
// Recommended: 3-5ms
#define CLICK_DELAY_MS 3

// Delay between scans (milliseconds)
// Lower = more CPU usage but faster response
// Recommended: 30-50ms
#define SCAN_DELAY_MS 40

// Image scaling factor for OCR
// Higher = better accuracy but slower
// Recommended: 1.2 - 2.0
#define SCALE_FACTOR 1.2

// === TESSERACT SETTINGS ===

// Tesseract executable path
#ifndef TESSERACT_PATH
#define TESSERACT_PATH "C:\\Program Files\\Tesseract-OCR\\tesseract.exe"
#endif

// Tesseract data path
#ifndef TESSDATA_PREFIX
#define TESSDATA_PREFIX "C:\\Program Files\\Tesseract-OCR\\tessdata"
#endif

// Tesseract language (usually "eng")
#define TESS_LANGUAGE "eng"

// Page segmentation mode
// PSM_AUTO (3) = Automatic
// PSM_SINGLE_LINE (7) = Single line
// PSM_SINGLE_WORD (8) = Single word
#define TESS_PSM PSM_AUTO

// OCR Engine Mode
// OEM_DEFAULT (3) = Default
// OEM_LSTM_ONLY (1) = Neural nets LSTM only
#define TESS_OEM OEM_DEFAULT

// === SCREEN CAPTURE ===

// Shop region as percentage of screen height
// Default: bottom 18% of screen starting at 82%
#define SHOP_REGION_START_Y 0.82
#define SHOP_REGION_HEIGHT 0.18

// === GUI SETTINGS ===

// Main window dimensions
#define WINDOW_WIDTH 750
#define WINDOW_HEIGHT 800

// Preview image dimensions
#define PREVIEW_WIDTH 400
#define PREVIEW_HEIGHT 80

// Log text maximum lines before auto-clear
#define MAX_LOG_LINES 200

// === COLORS (RGB) ===

// Background colors
#define COLOR_BG_DARK RGB(30, 30, 30)
#define COLOR_BG_MEDIUM RGB(45, 45, 48)
#define COLOR_BG_LIGHT RGB(60, 60, 60)

// Champion button colors
#define COLOR_BTN_OFF RGB(60, 60, 60)          // Not selected
#define COLOR_BTN_2STAR RGB(255, 215, 0)       // 2⭐ (Gold)
#define COLOR_BTN_3STAR RGB(255, 68, 68)       // 3⭐ (Red)
#define COLOR_BTN_COMPLETED RGB(14, 124, 14)   // Completed (Green)

// Text colors
#define COLOR_TEXT_NORMAL RGB(204, 204, 204)
#define COLOR_TEXT_ACTIVE RGB(0, 255, 0)
#define COLOR_TEXT_ERROR RGB(255, 68, 68)
#define COLOR_TEXT_WARNING RGB(255, 215, 0)

// === CHAMPION LIMITS ===

// Maximum champions to track
#define MAX_CHAMPIONS 100

// Maximum length of champion name
#define MAX_NAME_LEN 50

// === DEFAULT VALUES ===

// Default roll speed (seconds)
#define DEFAULT_ROLL_SPEED 0.3

// Auto-roll enabled by default?
#define DEFAULT_AUTO_ROLL false

// Preview enabled by default?
#define DEFAULT_PREVIEW true

// === ADVANCED ===

// Number of threads for parallel processing (future use)
#define NUM_THREADS 1

// Enable debug logging?
#define DEBUG_MODE 0

// Enable performance metrics?
#define ENABLE_METRICS 0

// Retry count for failed clicks
#define CLICK_RETRY_COUNT 1

// Threshold value for image binarization (0-255)
#define BINARY_THRESHOLD 127

// === HOTKEYS ===

// Virtual key codes for hotkeys
#define HOTKEY_START VK_F2
#define HOTKEY_STOP VK_F1

// === MEMORY OPTIMIZATION ===

// Use memory pool for frequent allocations?
#define USE_MEMORY_POOL 0

// Pre-allocate OCR buffers?
#define PREALLOCATE_BUFFERS 1

#endif // CONFIG_H
