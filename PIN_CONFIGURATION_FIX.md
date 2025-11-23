# Pin Configuration Fix - Handshake Capture Tool

## Issue Found
The handshake capture tool was initially configured with **incorrect pins** that didn't match your standard ILI9341 setup used in other projects (BLE Scanner, Tetris, etc.).

## ✅ Corrected Pin Configuration

### Before (INCORRECT - Hardware SPI)
```cpp
#define TFT_CS 15    // ❌ Wrong
#define TFT_DC 2     // ❌ Wrong
#define TFT_RST 4    // ✓ Correct

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
```

### After (CORRECT - Software SPI)
```cpp
#define TFT_CS 5     // ✅ Matches your standard
#define TFT_DC 21    // ✅ Matches your standard
#define TFT_RST 4    // ✅ Correct
#define TFT_MOSI 23  // ✅ Matches your standard
#define TFT_SCLK 18  // ✅ Matches your standard

// Use software SPI constructor: (CS, DC, MOSI, SCLK, RST)
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
```

## Standard Pin Mapping (All Your ILI9341 Projects)

```
┌─────────────────────────────────────┐
│  ILI9341 → ESP32 (Software SPI)    │
├─────────────────────────────────────┤
│  VCC   → 3.3V                       │
│  GND   → GND                        │
│  MOSI  → GPIO 23                    │
│  SCK   → GPIO 18                    │
│  CS    → GPIO 5   ⚡ KEY PIN        │
│  DC    → GPIO 21  ⚡ KEY PIN        │
│  RST   → GPIO 4                     │
│  LED   → 3.3V                       │
└─────────────────────────────────────┘
```

## What Was Changed

### 1. **handshake_capture_ili9341.ino**
```diff
- Wiring (Hardware SPI):
+ Wiring (Software SPI - SAME AS TETRIS):

- TFT CS   -> GPIO 15
- TFT DC   -> GPIO 2
+ TFT CS   -> GPIO 5
+ TFT DC   -> GPIO 21

- #define TFT_CS 15
- #define TFT_DC 2
+ #define TFT_CS 5
+ #define TFT_DC 21
+ #define TFT_MOSI 23
+ #define TFT_SCLK 18

- Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
+ Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
```

### 2. **HANDSHAKE_CAPTURE_README.md**
- Updated wiring section to show correct pins
- Changed from "Hardware SPI" to "Software SPI - SAME AS TETRIS"
- Updated all pin references

### 3. **HANDSHAKE_WIRING_GUIDE.txt**
- Updated connection diagram with correct pins
- Updated pin reference table
- Added note about matching BLE Scanner and Tetris configuration
- Updated tips section to reflect software SPI

## Why This Matters

### ⚠️ Using Wrong Pins Would Cause:
1. **Blank display** - No communication with TFT
2. **Garbage on screen** - Incorrect DC/CS signals
3. **No response** - ESP32 talking to wrong pins
4. **Confusion** - Different wiring than your other projects

### ✅ Using Correct Pins Provides:
1. **Consistent wiring** - Same as all your other ILI9341 projects
2. **Easy swapping** - Move display between projects without rewiring
3. **Tested configuration** - Already working in BLE Scanner and Tetris
4. **Less mistakes** - Familiar pin layout

## Verification Checklist

Before uploading, verify your physical connections:

```
Physical Check:
□ VCC connected to 3.3V (NOT 5V!)
□ GND connected to GND
□ MOSI connected to GPIO 23
□ SCK connected to GPIO 18
□ CS connected to GPIO 5   ⚡ Check this carefully!
□ DC connected to GPIO 21  ⚡ Check this carefully!
□ RST connected to GPIO 4
□ LED connected to 3.3V (backlight)

Code Check:
□ #define TFT_CS 5
□ #define TFT_DC 21
□ #define TFT_RST 4
□ #define TFT_MOSI 23
□ #define TFT_SCLK 18
□ Software SPI constructor used
```

## Hardware vs Software SPI

### Hardware SPI (Original Mistake)
- Uses ESP32 built-in SPI hardware
- Faster (but not needed for 240x320 display)
- Fixed pins: MOSI=GPIO 23, SCK=GPIO 18
- Flexible CS, DC, RST pins
- Constructor: `Adafruit_ILI9341(CS, DC, RST)`

### Software SPI (Your Standard)
- Uses any GPIO pins via bit-banging
- Slightly slower (but sufficient for this project)
- **All pins are flexible and assignable**
- Constructor: `Adafruit_ILI9341(CS, DC, MOSI, SCLK, RST)`
- **This is what ALL your other projects use**

## Project Compatibility

Your pin configuration is now consistent across:

✅ **ble_scanner_ili9341.ino** - CS=5, DC=21, RST=4
✅ **infinite_tetris.ino** - CS=5, DC=21, RST=4  
✅ **handshake_capture_ili9341.ino** - CS=5, DC=21, RST=4 ⚡ FIXED!

This means you can:
- Use the same display without rewiring
- Reference any project's wiring as a guide
- Build a permanent breadboard setup
- Create a standardized ESP32+ILI9341 platform

## Testing After Fix

When you upload the corrected code, you should see:

1. ✅ **Matrix rain animation** - Green falling characters
2. ✅ **"HANDSHAKE HUNTER" logo** - Red text with glitch effect
3. ✅ **Boot sequence messages** - Cyan text scrolling
4. ✅ **Main interface** - Header, content area, footer
5. ✅ **"Scanning..." message** - While waiting for APs
6. ✅ **Live WiFi data** - APs appearing in real-time

If you see a **blank screen** or **garbage**, double-check:
- GPIO 5 is connected to CS
- GPIO 21 is connected to DC
- 3.3V power is stable

## Summary

✅ **Fixed**: CS pin changed from GPIO 15 → GPIO 5
✅ **Fixed**: DC pin changed from GPIO 2 → GPIO 21
✅ **Fixed**: Constructor changed to Software SPI version
✅ **Fixed**: All documentation updated
✅ **Consistent**: Now matches your other ILI9341 projects

The handshake capture tool will now work with your standard wiring configuration! 🎉

---

**Note**: The tool will still work perfectly with these pins. Software SPI is more than fast enough for the 320x240 display at the refresh rates used in this project.

