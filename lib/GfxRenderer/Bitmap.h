#pragma once

#include <HalStorage.h>

#include <cstdint>

#include "BitmapHelpers.h"

#pragma pack(push, 1)
struct BmpHeader {
  struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
  } fileHeader;
  struct {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
  } infoHeader;
  struct RgbQuad {
    uint8_t rgbBlue;
    uint8_t rgbGreen;
    uint8_t rgbRed;
    uint8_t rgbReserved;
  };
  RgbQuad colors[2];
};
#pragma pack(pop)

enum class BmpReaderError : uint8_t {
  Ok = 0,
  FileInvalid,
  SeekStartFailed,

  NotBMP,
  DIBTooSmall,

  BadPlanes,
  UnsupportedBpp,
  UnsupportedCompression,

  BadDimensions,
  ImageTooLarge,
  PaletteTooLarge,

  SeekPixelDataFailed,
  BufferTooSmall,
  OomRowBuffer,
  ShortReadRow,
};

class Bitmap {
 public:
  static const char* errorToString(BmpReaderError err);

  explicit Bitmap(HalFile& file, bool dithering = false) : file(file), dithering(dithering) {}
  ~Bitmap();
  BmpReaderError parseHeaders();
  BmpReaderError parseAndLoadAll();
  BmpReaderError readNextRow(uint8_t* data, uint8_t* rowBuffer) const;
  BmpReaderError rewindToData() const;
  int getWidth() const { return width; }
  int getHeight() const { return height; }
  bool isTopDown() const { return topDown; }
  bool hasGreyscale() const { return bpp > 1; }
  int getRowBytes() const { return rowBytes; }
  bool is1Bit() const { return bpp == 1; }
  uint16_t getBpp() const { return bpp; }

  // Optimization hook for renderers: try to slurp every pixel row off SD in
  // ONE big SharedBusLock-protected read.  Without this, drawBitmap fires
  // ~hundreds of small file.read() calls — each one paying the SDFat
  // block-cache lookup + (post-write-storm) SD recovery latency, which
  // adds up to multi-second renders right after a JPEG decode flushes the
  // card cache.  Returns true if the slurp succeeded; subsequent
  // readRow() calls then memcpy from RAM instead of touching SD.
  bool preloadAllRows();
  bool isPreloaded() const { return preloadedRows_ != nullptr; }
  const uint8_t* preloadedRow(int rowIndex) const;

 private:
  static uint16_t readLE16(HalFile& f);
  static uint32_t readLE32(HalFile& f);

  HalFile& file;
  bool dithering = false;
  int width = 0;
  int height = 0;
  bool topDown = false;
  uint32_t bfOffBits = 0;
  uint16_t bpp = 0;
  uint32_t colorsUsed = 0;
  bool nativePalette = false;  // true if all palette entries map to native gray levels
  int rowBytes = 0;
  uint8_t paletteLum[256] = {};

  // Dithering state (mutable for const methods)
  mutable int16_t* errorCurRow = nullptr;
  mutable int16_t* errorNextRow = nullptr;
  mutable int prevRowY = -1;  // Track row progression for error propagation

  mutable AtkinsonDitherer* atkinsonDitherer = nullptr;
  mutable FloydSteinbergDitherer* fsDitherer = nullptr;

  // Optional whole-image preload buffer (heap, owned).  When set, readRow()
  // and the renderer's preloadedRow() helper bypass file.read entirely.
  uint8_t* preloadedRows_ = nullptr;
  uint8_t* preloadedFileStart_ = nullptr;
};
