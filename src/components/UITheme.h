#pragma once

#include <functional>
#include <memory>

#include "CrossPointSettings.h"
#include "components/themes/BaseTheme.h"

class GfxRenderer;
#include <EpdFontFamily.h>

class UITheme {
  // Static instance
  static UITheme instance;

 public:
  UITheme();
  static UITheme& getInstance() { return instance; }

  const ThemeMetrics& getMetrics() const { return *currentMetrics; }
  const BaseTheme& getTheme() const { return *currentTheme; }
  bool isRtl() const;
  void reload();
  void setTheme(CrossPointSettings::UI_THEME type);
  static int getNumberOfItemsPerPage(const GfxRenderer& renderer, bool hasHeader, bool hasTabBar, bool hasButtonHints,
                                     bool hasSubtitle, int extraReservedHeight = 0);
  static std::string getCoverThumbPath(std::string coverBmpPath, int coverHeight);
  static UIIcon getFileIcon(const std::string& filename);
  static int getStatusBarHeight();
  static int getProgressBarHeight();

  // RTL text rendering helpers
  static bool isRtlUi();
  static void drawTextStart(const GfxRenderer& renderer, const int fontId, const int leftX, const int rightX,
                            const int y, const char* text, const bool black = true,
                            const EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  static void drawTextEnd(const GfxRenderer& renderer, const int fontId, const int leftX, const int rightX, const int y,
                          const char* text, const bool black = true,
                          const EpdFontFamily::Style style = EpdFontFamily::REGULAR);

 private:
  const ThemeMetrics* currentMetrics;
  std::unique_ptr<BaseTheme> currentTheme;
};

// Helper macro to access current theme
#define GUI UITheme::getInstance().getTheme()
