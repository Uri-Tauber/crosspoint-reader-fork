#pragma once
#include <Arduino.h>
#include <SPI.h>

class EInkDisplay {
 public:
  // Constructor with pin configuration
  EInkDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy);

  // Destructor
  ~EInkDisplay() = default;

  // Refresh modes (guarded to avoid redefinition in test builds)
  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh (1720ms) - balanced quality and speed
    FAST_REFRESH   // Fast refresh using custom LUT
  };

  // Set X3 panel geometry and mode (must be called before begin())
  void setDisplayX3();

  // Initialize the display hardware and driver
  void begin();

  // Legacy compile-time dimensions kept for compatibility.
  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;
  static constexpr uint16_t X3_DISPLAY_WIDTH = 792;
  static constexpr uint16_t X3_DISPLAY_HEIGHT = 528;
  static constexpr uint16_t X3_DISPLAY_WIDTH_BYTES = X3_DISPLAY_WIDTH / 8;
  static constexpr uint32_t X3_BUFFER_SIZE = X3_DISPLAY_WIDTH_BYTES * X3_DISPLAY_HEIGHT;
  static constexpr uint32_t MAX_BUFFER_SIZE = 52272;  // max(800x480, 792x528) / 8

  // Runtime dimensions
  uint16_t getDisplayWidth() const { return displayWidth; }
  uint16_t getDisplayHeight() const { return displayHeight; }
  uint16_t getDisplayWidthBytes() const { return displayWidthBytes; }
  uint32_t getBufferSize() const { return bufferSize; }

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool fromProgmem = false) const;
  void drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool fromProgmem = false) const;
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  void swapBuffers();
#endif
  void setFramebuffer(const uint8_t* bwBuffer) const;

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);

  // Stream one horizontal band of a grayscale plane straight to controller RAM
  // so the caller never holds a full plane buffer in MCU heap (the heap win
  // behind tiled grayscale). `plane` selects LSB/MSB RAM; `rows` points at
  // `numRows` physical rows (displayWidthBytes wide) whose top is logical
  // `yStart`. X4 writes each band as an independent windowed RAM write via
  // setRamArea; X3 (UC81xx) windows each band via PTL. Either way bands may be
  // streamed in any order.
  enum GrayPlane { GRAY_PLANE_LSB, GRAY_PLANE_MSB };
  void writeGrayscalePlaneStrip(GrayPlane plane, const uint8_t* rows, uint16_t yStart, uint16_t numRows);

  // True when the tiled/strip grayscale path is supported. X4 (SSD1677) windows
  // each band via setRamArea; X3 (UC81xx) windows via PTL. Both implemented.
  bool supportsStripGrayscale() const { return true; }
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);
#endif

  void displayBuffer(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false);
  void displayBufferDriveAll(bool turnOffScreen = false);
  // EXPERIMENTAL: Windowed update - display only a rectangular region
  void displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool turnOffScreen = false, bool partialRedSync = false);
  void displayGrayBuffer(bool turnOffScreen = false, const unsigned char* lut = nullptr, bool factoryMode = false);

  void refreshDisplay(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false);

  // Hint the X3 policy to run a one-shot full resync on next update.
  void requestResync(uint8_t settlePasses = 0);

  // Zero the X3 initial-full-sync counter and mark the RED RAM as already
  // synced. Call after a warm restart (the panel was active ms ago); without
  // this, the first two paints after begin() are promoted to FULL (~770ms
  // each) regardless of the requested mode.
  void skipInitialResync();

  // debug function
  void grayscaleRevert();

  // LUT control
  void setCustomLUT(bool enabled, const unsigned char* lutData = nullptr);

  // Power management
  void deepSleep();

  // Access to frame buffer
  uint8_t* getFrameBuffer() const {
    return frameBuffer;
  }

  // Save the current framebuffer to a PBM file (desktop/test builds only)
  void saveFrameBufferAsPBM(const char* filename);

 private:
  // Internal geometry setter used by setDisplayX3().
  void setDisplayDimensions(uint16_t width, uint16_t height);

  // Pin configuration
  int8_t _sclk, _mosi, _cs, _dc, _rst, _busy;

  // Runtime display geometry
  uint16_t displayWidth = DISPLAY_WIDTH;
  uint16_t displayHeight = DISPLAY_HEIGHT;
  uint16_t displayWidthBytes = DISPLAY_WIDTH_BYTES;
  uint32_t bufferSize = BUFFER_SIZE;
  bool _x3Mode = false;
  bool _x3RedRamSynced = false;
  struct X3GrayState {
    bool lastBaseWasPartial = false;
    bool lsbValid = false;
  };
  X3GrayState _x3GrayState;
  uint8_t _x3InitialFullSyncsRemaining = 0;
  bool _x3ForceFullSyncNext = false;
  uint8_t _x3ForcedConditionPassesNext = 0;
  // Frame buffer (statically allocated)
  uint8_t frameBuffer0[MAX_BUFFER_SIZE];
  uint8_t* frameBuffer;
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  uint8_t frameBuffer1[MAX_BUFFER_SIZE];
  uint8_t* frameBufferActive;
#endif

  // SPI settings
  SPISettings spiSettings;

  // State
  bool isScreenOn = false;
  bool customLutActive = false;
  bool inGrayscaleMode = false;
  bool drawGrayscale = false;

  // Low-level display control
  void resetDisplay();
  void sendCommand(uint8_t command);
  void sendData(uint8_t data);
  void sendData(const uint8_t* data, uint16_t length);
  void waitForRefresh(const char* comment = nullptr);
  void waitWhileBusy(const char* comment = nullptr);
  // Shared body for the two waits above. X4 (SSD1677) and X3 (UC81xx-class)
  // use opposite BUSY-line polarities:
  //   X4: active HIGH. BUSY HIGH while working, drops LOW when done.
  //   X3: active LOW.  BUSY HIGH when idle, drops LOW while working, returns
  //                    HIGH when done.
  // The per-panel polling logic therefore stays gated; consolidation here
  // is the function body only.
  void pollBusy(const char* comment, const char* completeWord);
  void initDisplayController();

  // Low-level display operations
  void setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void writeRamBuffer(uint8_t ramBuffer, const uint8_t* data, uint32_t size);
  void writeRamBufferInverted(uint8_t ramBuffer, const uint8_t* data, uint32_t size);

  // X3 (UC81xx) primitives. Promoted from inline lambdas that used to be
  // redefined in displayBuffer / displayGrayBuffer / grayscaleRevert. These
  // fuse a command byte and a short data payload into one CS-low SPI
  // transaction — used for LUT register / mode-select / partial-window
  // writes where the payload is small. Bulk plane writes go through
  // sendPlaneX3/fillPlaneX3 (separated sendCommand+sendData). Not an
  // atomicity requirement, just convenience.
  void sendCommandDataX3(uint8_t cmd, const uint8_t* data, uint16_t len);
  void sendCommandDataByteX3(uint8_t cmd, uint8_t d0);
  void sendCommandDataByteX3(uint8_t cmd, uint8_t d0, uint8_t d1);
  // Bulk-write a pixel plane to one of the DTM RAM commands. Y-flips rows
  // in-place (X3 controller scans gates upward), optionally inverts bits
  // before sending, then restores the buffer.
  void sendPlaneX3(uint8_t ramCmd, uint8_t* buf, bool invert);
  // Fill an entire RAM plane with a single byte (e.g., 0xFF for white).
  // Streams a small row buffer repeatedly so the framebuffer isn't touched.
  void fillPlaneX3(uint8_t ramCmd, uint8_t fillByte);
  // Load all 5 LUT registers (VCOM/WW/BW/WB/BB) in one call. Each pointer
  // must reference a 42-byte LUT bank in PROGMEM/DRAM.
  void loadLutBankX3(const uint8_t* vcom, const uint8_t* ww,
                     const uint8_t* bw, const uint8_t* wb,
                     const uint8_t* bb);
  void loadLutBankX3WithCdi(uint8_t cdi0, const uint8_t* vcom,
                            const uint8_t* ww, const uint8_t* bw,
                            const uint8_t* wb, const uint8_t* bb);
  void loadLutBankX3WithCdi(uint8_t cdi0, uint8_t cdi1,
                            const uint8_t* vcom, const uint8_t* ww,
                            const uint8_t* bw, const uint8_t* wb,
                            const uint8_t* bb);
  // Power-on if needed, trigger refresh, optionally power-off. The `tag`
  // string is included verbatim in busy-wait log lines.
  void triggerRefreshX3(bool turnOffScreen, const char* tag);
};

// Factory LUTs extracted from firmware V3.1.9_CH_X4_0117.bin.
// Uses absolute 2-bit pixel encoding for single-pass grayscale refresh.
// See EInkDisplay.cpp for encoding details.
extern const unsigned char lut_factory_fast[];    // 110 bytes, 60 frames, FR=0x44
extern const unsigned char lut_factory_quality[];  // 110 bytes, 50 frames, FR=0x22
