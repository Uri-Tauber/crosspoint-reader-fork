#pragma once

#include <cstdint>

#include "../Activity.h"

// Classic Minesweeper for e-ink. 10x14 grid.
// D-pad moves cursor. Confirm short = reveal, long = toggle flag. Back = exit.
class MinesweeperActivity final : public Activity {
  static constexpr int COLS = 10;
  static constexpr int ROWS = 14;
  static constexpr uint16_t CONTINUOUS_START_MS = 500;
  static constexpr uint16_t CONTINUOUS_INTERVAL_MS = 500;
  static constexpr uint16_t CONFIRM_LONG_PRESS_MS = 500;

  enum class CellState : uint8_t { HIDDEN, REVEALED, FLAGGED };

  // Each cell: adjacent mine count (0-8) or 9 = mine
  uint8_t grid[ROWS][COLS]{};
  CellState state[ROWS][COLS]{};

  int cursorRow = 0;
  int cursorCol = 0;
  int mineCount = 20;
  int flagCount = 0;
  int revealedCount = 0;
  bool gameOver = false;
  bool won = false;
  bool firstMove = true;  // mines placed after first reveal to avoid instant death
  uint32_t lastUpNavTime = 0;
  uint32_t lastDownNavTime = 0;
  uint32_t lastLeftNavTime = 0;
  uint32_t lastRightNavTime = 0;
  bool confirmHeld = false;
  bool confirmLongHandled = false;

  enum class Difficulty : uint8_t { EASY = 0, MEDIUM, HARD };
  Difficulty difficulty = Difficulty::EASY;
  static constexpr int MINE_COUNTS[] = {15, 25, 35};
  static constexpr int DIFFICULTY_COUNT = 3;

  void placeMines(int safeRow, int safeCol);
  void reveal(int row, int col);
  void revealAll();
  int countAdjacentMines(int row, int col) const;
  int countAdjacentFlags(int row, int col) const;
  bool checkWin() const;
  const char* difficultyLabel() const;
  bool toggleFlagAtCursor();
  bool revealAtCursor();
  static void drawFlagIcon(const GfxRenderer& renderer, int x, int y, int size, bool state);
  static void drawBombIcon(const GfxRenderer& renderer, int x, int y, int size, bool state);

 public:
  explicit MinesweeperActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Minesweeper", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
