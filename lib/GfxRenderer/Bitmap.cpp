#include "Bitmap.h"

#include <cstdlib>
#include <cstring>
#include <new>

// ============================================================================
// IMAGE PROCESSING OPTIONS
// ============================================================================
// Dithering is applied when converting high-color BMPs to the display's native
// 2-bit (4-level) grayscale. Images whose palette entries all map to native
// gray levels (0, 85, 170, 255 ±21) are mapped directly without dithering.
// For cover images, dithering is done in JpegToBmpConverter.cpp instead.
constexpr bool USE_ATKINSON = true;  // Use Atkinson dithering instead of Floyd-Steinberg
// ============================================================================

Bitmap::~Bitmap() {
  delete[] errorCurRow;
  delete[] errorNextRow;

  delete atkinsonDitherer;
  delete fsDitherer;

  if (preloadedFileStart_) {
    delete[] preloadedFileStart_;
  } else if (preloadedRows_) {
    delete[] preloadedRows_;
  }
  preloadedRows_ = nullptr;
  preloadedFileStart_ = nullptr;
}

uint16_t Bitmap::readLE16(HalFile& f) {
  const int c0 = f.read();
  const int c1 = f.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  return static_cast<uint16_t>(b0) | (static_cast<uint16_t>(b1) << 8);
}

uint32_t Bitmap::readLE32(HalFile& f) {
  const int c0 = f.read();
  const int c1 = f.read();
  const int c2 = f.read();
  const int c3 = f.read();

  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  const auto b2 = static_cast<uint8_t>(c2 < 0 ? 0 : c2);
  const auto b3 = static_cast<uint8_t>(c3 < 0 ? 0 : c3);

  return static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8) | (static_cast<uint32_t>(b2) << 16) |
         (static_cast<uint32_t>(b3) << 24);
}

const char* Bitmap::errorToString(BmpReaderError err) {
  switch (err) {
    case BmpReaderError::Ok:
      return "Ok";
    case BmpReaderError::FileInvalid:
      return "FileInvalid";
    case BmpReaderError::SeekStartFailed:
      return "SeekStartFailed";
    case BmpReaderError::NotBMP:
      return "NotBMP (missing 'BM')";
    case BmpReaderError::DIBTooSmall:
      return "DIBTooSmall (<40 bytes)";
    case BmpReaderError::BadPlanes:
      return "BadPlanes (!= 1)";
    case BmpReaderError::UnsupportedBpp:
      return "UnsupportedBpp (expected 1, 2, 4, 8, 24, or 32)";
    case BmpReaderError::UnsupportedCompression:
      return "UnsupportedCompression (expected BI_RGB or BI_BITFIELDS for 32bpp)";
    case BmpReaderError::BadDimensions:
      return "BadDimensions";
    case BmpReaderError::ImageTooLarge:
      return "ImageTooLarge (max 2048x3072)";
    case BmpReaderError::PaletteTooLarge:
      return "PaletteTooLarge";

    case BmpReaderError::SeekPixelDataFailed:
      return "SeekPixelDataFailed";
    case BmpReaderError::BufferTooSmall:
      return "BufferTooSmall";

    case BmpReaderError::OomRowBuffer:
      return "OomRowBuffer";
    case BmpReaderError::ShortReadRow:
      return "ShortReadRow";
  }
  return "Unknown";
}

BmpReaderError Bitmap::parseHeaders() {
  if (!file) return BmpReaderError::FileInvalid;
  if (!file.seek(0)) return BmpReaderError::SeekStartFailed;

  uint8_t hdr[54];
  if (file.read(hdr, sizeof(hdr)) != static_cast<int>(sizeof(hdr))) {
    return BmpReaderError::FileInvalid;
  }

  auto leU16 = [](const uint8_t* p) -> uint16_t {
    return static_cast<uint16_t>(p[0] | (uint16_t(p[1]) << 8));
  };
  auto leU32 = [](const uint8_t* p) -> uint32_t {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
  };
  auto leI32 = [&leU32](const uint8_t* p) -> int32_t { return static_cast<int32_t>(leU32(p)); };

  const uint16_t bfType = leU16(hdr + 0);
  if (bfType != 0x4D42) return BmpReaderError::NotBMP;
  bfOffBits = leU32(hdr + 10);

  const uint32_t biSize = leU32(hdr + 14);
  if (biSize < 40) return BmpReaderError::DIBTooSmall;
  width = leI32(hdr + 18);
  const int32_t rawHeight = leI32(hdr + 22);
  topDown = rawHeight < 0;
  height = topDown ? -rawHeight : rawHeight;
  const uint16_t planes = leU16(hdr + 26);
  bpp = leU16(hdr + 28);
  const uint32_t comp = leU32(hdr + 30);
  colorsUsed = leU32(hdr + 46);

  const bool validBpp = bpp == 1 || bpp == 2 || bpp == 4 || bpp == 8 || bpp == 24 || bpp == 32;

  if (planes != 1) return BmpReaderError::BadPlanes;
  if (!validBpp) return BmpReaderError::UnsupportedBpp;
  if (!(comp == 0 || (bpp == 32 && comp == 3))) return BmpReaderError::UnsupportedCompression;

  if (colorsUsed == 0 && bpp <= 8) colorsUsed = 1u << bpp;
  if (colorsUsed > 256u) return BmpReaderError::PaletteTooLarge;

  if (width <= 0 || height <= 0) return BmpReaderError::BadDimensions;

  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;
  if (width > MAX_IMAGE_WIDTH || height > MAX_IMAGE_HEIGHT) {
    return BmpReaderError::ImageTooLarge;
  }

  rowBytes = (width * bpp + 31) / 32 * 4;

  for (int i = 0; i < 256; i++) paletteLum[i] = static_cast<uint8_t>(i);
  if (colorsUsed > 0) {
    uint8_t paletteBuf[256 * 4];
    const int paletteBytes = static_cast<int>(colorsUsed * 4);
    if (file.read(paletteBuf, paletteBytes) != paletteBytes) {
      return BmpReaderError::FileInvalid;
    }
    for (uint32_t i = 0; i < colorsUsed; i++) {
      const uint8_t* rgb = paletteBuf + i * 4;
      paletteLum[i] = (77u * rgb[2] + 150u * rgb[1] + 29u * rgb[0]) >> 8;
    }
  }

  if (!file.seek(bfOffBits)) {
    return BmpReaderError::SeekPixelDataFailed;
  }

  // Check if palette luminances map cleanly to the display's 4 native gray levels.
  // Native levels are 0, 85, 170, 255 — i.e. values where (lum >> 6) is lossless.
  // If all palette entries are near a native level, we can skip dithering entirely.
  nativePalette = bpp <= 2;  // 1-bit and 2-bit are always native
  if (!nativePalette && colorsUsed > 0) {
    nativePalette = true;
    for (uint32_t i = 0; i < colorsUsed; i++) {
      const uint8_t lum = paletteLum[i];
      const uint8_t level = lum >> 6;            // quantize to 0-3
      const uint8_t reconstructed = level * 85;  // back to 0, 85, 170, 255
      if (lum > reconstructed + 21 || lum + 21 < reconstructed) {
        nativePalette = false;  // luminance is too far from any native level
        break;
      }
    }
  }

  // Decide pixel processing strategy:
  //  - Native palette → direct mapping, no processing needed
  //  - High-color + dithering enabled → error-diffusion dithering (Atkinson or Floyd-Steinberg)
  //  - High-color + dithering disabled → simple quantization (no error diffusion)
  const bool highColor = !nativePalette;
  if (highColor && dithering) {
    if (USE_ATKINSON) {
      atkinsonDitherer = new AtkinsonDitherer(width);
    } else {
      fsDitherer = new FloydSteinbergDitherer(width);
    }
  }

  return BmpReaderError::Ok;
}

// packed 2bpp output, 0 = black, 1 = dark gray, 2 = light gray, 3 = white
BmpReaderError Bitmap::readNextRow(uint8_t* data, uint8_t* rowBuffer) const {
  if (preloadedRows_) {
    const uint8_t* pRow = preloadedRow(prevRowY + 1);
    if (!pRow) return BmpReaderError::ShortReadRow;
    memcpy(rowBuffer, pRow, rowBytes);
  } else {
    if (file.read(rowBuffer, rowBytes) != rowBytes) return BmpReaderError::ShortReadRow;
  }

  prevRowY += 1;

  uint8_t* outPtr = data;
  uint8_t currentOutByte = 0;
  int bitShift = 6;
  int currentX = 0;

  // Helper lambda to pack 2bpp color into the output stream
  auto packPixel = [&](const uint8_t lum) {
    uint8_t color;
    if (atkinsonDitherer) {
      color = atkinsonDitherer->processPixel(adjustPixel(lum), currentX);
    } else if (fsDitherer) {
      color = fsDitherer->processPixel(adjustPixel(lum), currentX);
    } else {
      if (nativePalette) {
        // Palette matches native gray levels: direct mapping (still apply brightness/contrast/gamma)
        color = static_cast<uint8_t>(adjustPixel(lum) >> 6);
      } else {
        // Non-native palette with dithering disabled: simple quantization
        color = quantize(adjustPixel(lum), currentX, prevRowY);
      }
    }
    currentOutByte |= (color << bitShift);
    if (bitShift == 0) {
      *outPtr++ = currentOutByte;
      currentOutByte = 0;
      bitShift = 6;
    } else {
      bitShift -= 2;
    }
    currentX++;
  };

  uint8_t lum;

  switch (bpp) {
    case 32: {
      const uint8_t* p = rowBuffer;
      for (int x = 0; x < width; x++) {
        lum = (77u * p[2] + 150u * p[1] + 29u * p[0]) >> 8;
        packPixel(lum);
        p += 4;
      }
      break;
    }
    case 24: {
      const uint8_t* p = rowBuffer;
      for (int x = 0; x < width; x++) {
        lum = (77u * p[2] + 150u * p[1] + 29u * p[0]) >> 8;
        packPixel(lum);
        p += 3;
      }
      break;
    }
    case 8: {
      for (int x = 0; x < width; x++) {
        packPixel(paletteLum[rowBuffer[x]]);
      }
      break;
    }
    case 4: {
      for (int x = 0; x < width; x++) {
        const uint8_t nibble = (x & 1) ? (rowBuffer[x >> 1] & 0x0F) : (rowBuffer[x >> 1] >> 4);
        packPixel(paletteLum[nibble]);
      }
      break;
    }
    case 2: {
      // Fast path: when the palette is the standard 4-level grayscale
      // (0=black, 0x55=dgray, 0xAA=lgray, 0xFF=white) the entire round-trip
      // collapses to identity — paletteLum[i] >> 6 == i for the four palette
      // indices, and the inner packPixel() loop's only job for 2bpp input is
      // `color = lum >> 6`.  All BMPs we generate (JpegToBmpConverter,
      // PngToBmpConverter, BitmapHelpers thumbnail) use exactly this palette,
      // so we can drop ~8 s of unpack/repack/relookup churn off a 500×400
      // image render and just memcpy the bytes through.  Detect by comparing
      // the four indices' luminance values against the canonical palette.
      const bool stdPalette = paletteLum[0] == 0x00 && paletteLum[1] == 0x55 && paletteLum[2] == 0xAA &&
                              paletteLum[3] == 0xFF && !atkinsonDitherer && !fsDitherer;
      if (stdPalette) {
        // Source row is already 2bpp packed in BMP order (MSB-first), output
        // row uses the same packing — straight copy of rowBytes bytes.  The
        // outPtr / currentOutByte / bitShift state below would normally
        // accumulate the values one pixel at a time, so we have to disable
        // the trailing flush (set bitShift to 6 to mark "no partial byte").
        const int bytesIn = (width * 2 + 7) / 8;  // packed pixel bytes (no row-end padding)
        memcpy(data, rowBuffer, bytesIn);
        bitShift = 6;
        outPtr = data + bytesIn;
        if (atkinsonDitherer) atkinsonDitherer->nextRow();
        else if (fsDitherer)  fsDitherer->nextRow();
        return BmpReaderError::Ok;
      }
      // Slow path for non-standard palettes — full unpack/repack via packPixel.
      for (int x = 0; x < width; x++) {
        lum = paletteLum[(rowBuffer[x >> 2] >> (6 - ((x & 3) << 1))) & 0x03];
        packPixel(lum);
      }
      break;
    }
    case 1: {
      for (int x = 0; x < width; x++) {
        // Get palette index (0 or 1) from bit at position x
        const uint8_t palIndex = (rowBuffer[x >> 3] & (0x80 >> (x & 7))) ? 1 : 0;
        // Use palette lookup for proper black/white mapping
        lum = paletteLum[palIndex];
        packPixel(lum);
      }
      break;
    }
    default:
      return BmpReaderError::UnsupportedBpp;
  }

  if (atkinsonDitherer)
    atkinsonDitherer->nextRow();
  else if (fsDitherer)
    fsDitherer->nextRow();

  // Flush remaining bits if width is not a multiple of 4
  if (bitShift != 6) *outPtr = currentOutByte;

  return BmpReaderError::Ok;
}

BmpReaderError Bitmap::rewindToData() const {
  if (!file.seek(bfOffBits)) {
    return BmpReaderError::SeekPixelDataFailed;
  }

  // Reset dithering when rewinding
  if (fsDitherer) fsDitherer->reset();
  if (atkinsonDitherer) atkinsonDitherer->reset();

  return BmpReaderError::Ok;
}

BmpReaderError Bitmap::parseAndLoadAll() {
  if (!file) return BmpReaderError::FileInvalid;
  if (preloadedFileStart_) {
    delete[] preloadedFileStart_;
    preloadedFileStart_ = nullptr;
    preloadedRows_ = nullptr;
  } else if (preloadedRows_) {
    delete[] preloadedRows_;
    preloadedRows_ = nullptr;
  }

  if (!file.seek(0)) return BmpReaderError::SeekStartFailed;
  const uint32_t fileSize = file.size();
  if (fileSize < 62) return BmpReaderError::FileInvalid;

  constexpr uint32_t kMaxLoadAll = 256 * 1024;
  if (fileSize > kMaxLoadAll) return BmpReaderError::ImageTooLarge;

  preloadedRows_ = new (std::nothrow) uint8_t[fileSize];
  if (!preloadedRows_) return BmpReaderError::OomRowBuffer;

  if (!file.seek(0)) {
    delete[] preloadedRows_;
    preloadedRows_ = nullptr;
    return BmpReaderError::FileInvalid;
  }
  if (file.read(preloadedRows_, fileSize) != static_cast<int>(fileSize)) {
    delete[] preloadedRows_;
    preloadedRows_ = nullptr;
    return BmpReaderError::FileInvalid;
  }

  const uint8_t* hdr = preloadedRows_;
  auto leU16 = [](const uint8_t* p) -> uint16_t {
    return static_cast<uint16_t>(p[0] | (uint16_t(p[1]) << 8));
  };
  auto leU32 = [](const uint8_t* p) -> uint32_t {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
  };
  auto leI32 = [&leU32](const uint8_t* p) -> int32_t { return static_cast<int32_t>(leU32(p)); };

  if (leU16(hdr + 0) != 0x4D42) {
    delete[] preloadedRows_;
    preloadedRows_ = nullptr;
    return BmpReaderError::NotBMP;
  }
  bfOffBits = leU32(hdr + 10);
  const uint32_t biSize = leU32(hdr + 14);
  if (biSize < 40) {
    delete[] preloadedRows_;
    preloadedRows_ = nullptr;
    return BmpReaderError::DIBTooSmall;
  }
  width = leI32(hdr + 18);
  const int32_t rawHeight = leI32(hdr + 22);
  topDown = rawHeight < 0;
  height = topDown ? -rawHeight : rawHeight;
  const uint16_t planes = leU16(hdr + 26);
  bpp = leU16(hdr + 28);
  const uint32_t comp = leU32(hdr + 30);
  const uint32_t colorsUsed = leU32(hdr + 46);
  const bool validBpp = bpp == 1 || bpp == 2 || bpp == 4 || bpp == 8 || bpp == 24 || bpp == 32;

  auto fail = [&](BmpReaderError e) {
    delete[] preloadedRows_;
    preloadedRows_ = nullptr;
    return e;
  };
  if (planes != 1) return fail(BmpReaderError::BadPlanes);
  if (!validBpp) return fail(BmpReaderError::UnsupportedBpp);
  if (!(comp == 0 || (bpp == 32 && comp == 3))) return fail(BmpReaderError::UnsupportedCompression);
  if (colorsUsed > 256u) return fail(BmpReaderError::PaletteTooLarge);
  if (width <= 0 || height <= 0) return fail(BmpReaderError::BadDimensions);

  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;
  if (width > MAX_IMAGE_WIDTH || height > MAX_IMAGE_HEIGHT) return fail(BmpReaderError::ImageTooLarge);

  rowBytes = (width * bpp + 31) / 32 * 4;
  if (bfOffBits + static_cast<uint32_t>(rowBytes) * static_cast<uint32_t>(height) > fileSize) {
    return fail(BmpReaderError::FileInvalid);
  }

  for (int i = 0; i < 256; i++) paletteLum[i] = static_cast<uint8_t>(i);
  if (colorsUsed > 0) {
    const uint8_t* pal = hdr + 54;
    if (54 + colorsUsed * 4 > fileSize) return fail(BmpReaderError::FileInvalid);
    for (uint32_t i = 0; i < colorsUsed; i++) {
      const uint8_t* rgb = pal + i * 4;
      paletteLum[i] = (77u * rgb[2] + 150u * rgb[1] + 29u * rgb[0]) >> 8;
    }
  }

  delete atkinsonDitherer;
  atkinsonDitherer = nullptr;
  delete fsDitherer;
  fsDitherer = nullptr;

  preloadedFileStart_ = preloadedRows_;
  preloadedRows_ = preloadedRows_ + bfOffBits;
  return BmpReaderError::Ok;
}

bool Bitmap::preloadAllRows() {
  if (preloadedRows_) return true;
  if (rowBytes <= 0 || height <= 0) return false;

  const size_t total = static_cast<size_t>(rowBytes) * static_cast<size_t>(height);
  preloadedRows_ = new (std::nothrow) uint8_t[total];
  if (!preloadedRows_) return false;

  if (!file.seek(bfOffBits)) {
    delete[] preloadedRows_;
    preloadedRows_ = nullptr;
    return false;
  }
  if (file.read(preloadedRows_, total) != static_cast<int>(total)) {
    delete[] preloadedRows_;
    preloadedRows_ = nullptr;
    return false;
  }
  return true;
}

const uint8_t* Bitmap::preloadedRow(int rowIndex) const {
  if (!preloadedRows_ || rowIndex < 0 || rowIndex >= height) return nullptr;
  return preloadedRows_ + static_cast<size_t>(rowIndex) * static_cast<size_t>(rowBytes);
}
