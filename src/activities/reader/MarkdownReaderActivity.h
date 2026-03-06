#pragma once

#include <Markdown.h>
#include <Markdown/Section.h>

#include "CrossPointSettings.h"
#include "activities/Activity.h"

class MarkdownReaderActivity final : public Activity {
  std::shared_ptr<Markdown> markdown;
  std::unique_ptr<MarkdownSection> section = nullptr;
  int nextPageNumber = 0;
  int pagesUntilFullRefresh = 0;

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar() const;
  void saveProgress() const;
  void loadProgress();

 public:
  explicit MarkdownReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  std::unique_ptr<Markdown> markdown)
      : Activity("MarkdownReader", renderer, mappedInput), markdown(std::move(markdown)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
};
