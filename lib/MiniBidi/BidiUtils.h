#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace BidiUtils {

bool startsWithRtl(const char* utf8, int maxStrongChars = 5);

std::string applyBidiVisual(const char* utf8, int paragraphLevel = -1);

bool computeVisualWordOrder(const std::vector<std::string>& words, bool paragraphIsRtl,
                            std::vector<uint16_t>& visualOrder);

}  // namespace BidiUtils
