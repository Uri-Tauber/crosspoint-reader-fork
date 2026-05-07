#pragma once

#include <HalStorage.h>
#include <functional>

class Print;
class ZipFile;

class JpegToBmpConverter {
  static bool jpegFileToBmpStreamInternal(HalFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight,
                                          bool oneBit, bool quickMode, const std::function<bool()>& shouldAbort);

 public:
  static bool jpegFileToBmpStream(HalFile& jpegFile, Print& bmpOut, bool crop = true);
  static bool jpegFileToBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight, const std::function<bool()>& shouldAbort = nullptr);
  static bool jpegFileTo1BitBmpStream(HalFile& jpegFile, Print& bmpOut);
  static bool jpegFileTo1BitBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  static bool jpegFileToBmpStreamQuick(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight, const std::function<bool()>& shouldAbort = nullptr);
  static bool peekDimensions(HalFile& jpegFile, int& outWidth, int& outHeight);
  static bool jpegFileToBmpStreamPreview(HalFile& jpegFile, Print& bmpOut);
};
