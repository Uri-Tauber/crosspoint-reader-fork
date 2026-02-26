#pragma once

#include <Markdown.h>
#include <Markdown/Section.h>

#include "CrossPointSettings.h"
#include "activities/ActivityWithSubactivity.h"

class MarkdownReaderActivity final : public ActivityWithSubactivity {
  std::shared_ptr<Markdown> markdown;
  std::unique_ptr<MarkdownSection> section = nullptr;
  int nextPageNumber = 0;
  int pagesUntilFullRefresh = 0;
  bool pendingGoHome = false;

  const std::function<void()> onGoBack;
  const std::function<void()> onGoHome;

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar() const;
  void saveProgress() const;
  void loadProgress();

 public:
  explicit MarkdownReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  std::unique_ptr<Markdown> markdown, const std::function<void()>& onGoBack,
                                  const std::function<void()>& onGoHome)
      : ActivityWithSubactivity("MarkdownReader", renderer, mappedInput),
        markdown(std::move(markdown)),
        onGoBack(onGoBack),
        onGoHome(onGoHome) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};
