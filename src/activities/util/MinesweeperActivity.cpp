#include "MinesweeperActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "components/UITheme.h"
#include "fontIds.h"

int MinesweeperActivity::countAdjacentMines(int row, int col) const {
  int count = 0;
  for (int dr = -1; dr <= 1; dr++)
    for (int dc = -1; dc <= 1; dc++) {
      int r = row + dr, c = col + dc;
      if (r >= 0 && r < ROWS && c >= 0 && c < COLS && grid[r][c] == 9) count++;
    }
  return count;
}

int MinesweeperActivity::countAdjacentFlags(int row, int col) const {
  int count = 0;
  for (int dr = -1; dr <= 1; dr++)
    for (int dc = -1; dc <= 1; dc++) {
      int r = row + dr, c = col + dc;
      if (r >= 0 && r < ROWS && c >= 0 && c < COLS && state[r][c] == CellState::FLAGGED) count++;
    }
  return count;
}

void MinesweeperActivity::placeMines(int safeRow, int safeCol) {
  randomSeed(millis());
  int placed = 0;
  while (placed < mineCount) {
    int r = random(0, ROWS);
    int c = random(0, COLS);
    // Keep safe zone around first click (3x3)
    if (abs(r - safeRow) <= 1 && abs(c - safeCol) <= 1) continue;
    if (grid[r][c] == 9) continue;
    grid[r][c] = 9;
    placed++;
  }
  // Calculate adjacent mine counts for non-mine cells
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (grid[r][c] != 9) grid[r][c] = (uint8_t)countAdjacentMines(r, c);
}

void MinesweeperActivity::reveal(int row, int col) {
  if (row < 0 || row >= ROWS || col < 0 || col >= COLS) return;
  if (state[row][col] != CellState::HIDDEN) return;

  state[row][col] = CellState::REVEALED;
  revealedCount++;

  // Flood-fill for empty cells (0 adjacent mines)
  if (grid[row][col] == 0) {
    for (int dr = -1; dr <= 1; dr++)
      for (int dc = -1; dc <= 1; dc++) reveal(row + dr, col + dc);
  }
}

void MinesweeperActivity::revealAll() {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) state[r][c] = CellState::REVEALED;
}

bool MinesweeperActivity::checkWin() const { return revealedCount == (ROWS * COLS - mineCount); }

const char* MinesweeperActivity::difficultyLabel() const {
  switch (difficulty) {
    case Difficulty::EASY:
      return tr(STR_MINES_EASY);
    case Difficulty::MEDIUM:
      return tr(STR_MINES_MEDIUM);
    case Difficulty::HARD:
      return tr(STR_MINES_HARD);
  }
  return "";
}

bool MinesweeperActivity::toggleFlagAtCursor() {
  if (state[cursorRow][cursorCol] == CellState::HIDDEN) {
    state[cursorRow][cursorCol] = CellState::FLAGGED;
    flagCount++;
    return true;
  }
  if (state[cursorRow][cursorCol] == CellState::FLAGGED) {
    state[cursorRow][cursorCol] = CellState::HIDDEN;
    flagCount--;
    return true;
  }
  return false;
}

bool MinesweeperActivity::revealAtCursor() {
  if (state[cursorRow][cursorCol] == CellState::HIDDEN) {
    if (firstMove) {
      placeMines(cursorRow, cursorCol);
      firstMove = false;
    }
    if (grid[cursorRow][cursorCol] == 9) {
      revealAll();
      gameOver = true;
    } else {
      reveal(cursorRow, cursorCol);
      if (checkWin()) {
        won = true;
        revealAll();
      }
    }
    return true;
  }

  if (state[cursorRow][cursorCol] == CellState::REVEALED && grid[cursorRow][cursorCol] > 0) {
    // Chord: if flags match number, reveal all hidden neighbors.
    if (countAdjacentFlags(cursorRow, cursorCol) == grid[cursorRow][cursorCol]) {
      for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++) {
          int r = cursorRow + dr, c = cursorCol + dc;
          if (r >= 0 && r < ROWS && c >= 0 && c < COLS && state[r][c] == CellState::HIDDEN) {
            if (grid[r][c] == 9) {
              revealAll();
              gameOver = true;
            } else
              reveal(r, c);
          }
        }
      if (!gameOver && checkWin()) {
        won = true;
        revealAll();
      }
      return true;
    }
  }

  return false;
}

void MinesweeperActivity::drawFlagIcon(const GfxRenderer& renderer, const int x, const int y, const int size,
                                       const bool state) {
  static constexpr int LINE_WIDTH = 3;
  const int poleX = x + size / 2 - 4;
  const int topY = y + size / 5;
  const int bottomY = y + size - std::max(4, size / 6);
  const int flagW = std::max(8, size / 3);
  const int flagH = std::max(6, size / 4);

  renderer.drawLine(poleX, topY, poleX, bottomY, LINE_WIDTH, state);
  renderer.fillRect(poleX + LINE_WIDTH, topY + 1, flagW, flagH, state);
  renderer.drawLine(poleX - 4, bottomY, poleX + 4, bottomY, LINE_WIDTH, state);
}

void MinesweeperActivity::drawBombIcon(const GfxRenderer& renderer, const int x, const int y, const int size,
                                       const bool state) {
  static constexpr int LINE_WIDTH = 3;  // thicker spines

  const int cx = x + size / 2;
  const int cy = y + size / 2;

  const int r = std::max(4, size * 15 / 48);       // circle radius
  const int sc = std::max(r + 2, size * 21 / 48);  // cardinal spine reach from center
  const int sd = std::max(r + 2, size * 18 / 48);  // diagonal spine reach from center

  const int ri = r * 707 / 1000;   // circle edge at 45°
  const int di = sd * 707 / 1000;  // spine tip at 45°

  // Circle (4 quarter-arcs)
  renderer.drawArc(r, cx, cy, -1, -1, LINE_WIDTH, state);
  renderer.drawArc(r, cx, cy, 1, -1, LINE_WIDTH, state);
  renderer.drawArc(r, cx, cy, 1, 1, LINE_WIDTH, state);
  renderer.drawArc(r, cx, cy, -1, 1, LINE_WIDTH, state);

  // Cardinal spines: from circle edge outward
  renderer.drawLine(cx, cy - r, cx, cy - sc, LINE_WIDTH, state);  // top
  renderer.drawLine(cx + r, cy, cx + sc, cy, LINE_WIDTH, state);  // right
  renderer.drawLine(cx, cy + r, cx, cy + sc, LINE_WIDTH, state);  // bottom
  renderer.drawLine(cx - r, cy, cx - sc, cy, LINE_WIDTH, state);  // left

  // Diagonal spines: from circle edge at 45° outward
  renderer.drawLine(cx + ri, cy - ri, cx + di, cy - di, LINE_WIDTH, state);  // top-right
  renderer.drawLine(cx + ri, cy + ri, cx + di, cy + di, LINE_WIDTH, state);  // bottom-right
  renderer.drawLine(cx - ri, cy + ri, cx - di, cy + di, LINE_WIDTH, state);  // bottom-left
  renderer.drawLine(cx - ri, cy - ri, cx - di, cy - di, LINE_WIDTH, state);  // top-left
}

void MinesweeperActivity::onEnter() {
  Activity::onEnter();
  mineCount = MINE_COUNTS[(int)difficulty];
  memset(grid, 0, sizeof(grid));
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) state[r][c] = CellState::HIDDEN;
  cursorRow = ROWS / 2;
  cursorCol = COLS / 2;
  flagCount = 0;
  revealedCount = 0;
  gameOver = false;
  won = false;
  firstMove = true;
  lastUpNavTime = 0;
  lastDownNavTime = 0;
  lastLeftNavTime = 0;
  lastRightNavTime = 0;
  confirmHeld = false;
  confirmLongHandled = false;
  requestUpdate();
}

void MinesweeperActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  // Game over / won: Confirm = new game, Left/Right = difficulty
  if (gameOver || won) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      onEnter();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      difficulty = (Difficulty)(((int)difficulty + 1) % DIFFICULTY_COUNT);
      requestUpdate();
    }
    return;
  }

  bool changed = false;

  const auto handleDirectionalInput = [&](const MappedInputManager::Button button, uint32_t& lastNavTime,
                                          const int deltaRow, const int deltaCol) {
    if (mappedInput.wasPressed(button)) {
      cursorRow = (cursorRow + deltaRow + ROWS) % ROWS;
      cursorCol = (cursorCol + deltaCol + COLS) % COLS;
      lastNavTime = millis();
      changed = true;
    }

    if (mappedInput.isPressed(button) && mappedInput.getHeldTime() > CONTINUOUS_START_MS &&
        (millis() - lastNavTime) > CONTINUOUS_INTERVAL_MS) {
      cursorRow = (cursorRow + deltaRow + ROWS) % ROWS;
      cursorCol = (cursorCol + deltaCol + COLS) % COLS;
      lastNavTime = millis();
      changed = true;
    }

    if (mappedInput.wasReleased(button)) {
      lastNavTime = 0;
    }
  };

  handleDirectionalInput(MappedInputManager::Button::Up, lastUpNavTime, -1, 0);
  handleDirectionalInput(MappedInputManager::Button::Down, lastDownNavTime, 1, 0);
  handleDirectionalInput(MappedInputManager::Button::Left, lastLeftNavTime, 0, -1);
  handleDirectionalInput(MappedInputManager::Button::Right, lastRightNavTime, 0, 1);

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmHeld = true;
    confirmLongHandled = false;
  }

  if (confirmHeld && !confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > CONFIRM_LONG_PRESS_MS) {
    changed = toggleFlagAtCursor() || changed;
    confirmLongHandled = true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (confirmHeld && !confirmLongHandled) {
      changed = revealAtCursor() || changed;
    }
    confirmHeld = false;
    confirmLongHandled = false;
  }

  if (changed) requestUpdate();
}

void MinesweeperActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MINESWEEPER));

  const int contentTop = metrics.topPadding + metrics.headerHeight + 4;
  const int contentBot = pageHeight - metrics.buttonHintsHeight - 4;
  const int statusAreaHeight = renderer.getLineHeight(SMALL_FONT_ID) + 6;
  const int gridAreaHeight = std::max(ROWS, contentBot - contentTop - statusAreaHeight);
  const int gridAreaWidth = std::max(COLS, pageWidth - 4);
  const int cellSize = std::max(14, std::min(gridAreaWidth / COLS, gridAreaHeight / ROWS));
  const int gridW = cellSize * COLS;
  const int gridH = cellSize * ROWS;
  const int gridX = (pageWidth - gridW) / 2;
  const int gridY = contentTop + (gridAreaHeight - gridH) / 2;

  // Draw cells
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      int cx = gridX + c * cellSize;
      int cy = gridY + r * cellSize;
      bool isCursor = (r == cursorRow && c == cursorCol);

      if (state[r][c] == CellState::HIDDEN || state[r][c] == CellState::FLAGGED) {
        // Hidden cell: filled rectangle
        renderer.fillRect(cx + 1, cy + 1, cellSize - 2, cellSize - 2);
        if (state[r][c] == CellState::FLAGGED) {
          drawFlagIcon(renderer, cx, cy, cellSize, false);
        }
        if (isCursor) {
          // Draw cursor border in white on the filled cell
          renderer.drawRect(cx + 2, cy + 2, cellSize - 4, cellSize - 4, false);
          renderer.drawRect(cx + 3, cy + 3, cellSize - 6, cellSize - 6, false);
        }
      } else {
        // Revealed cell
        renderer.drawRect(cx, cy, cellSize, cellSize);
        if (isCursor) {
          renderer.drawRect(cx + 1, cy + 1, cellSize - 2, cellSize - 2);
        }

        if (grid[r][c] == 9) {
          drawBombIcon(renderer, cx, cy, cellSize, true);
        } else if (grid[r][c] > 0) {
          const char buf[2] = {(char)('0' + grid[r][c]), 0};
          int tw = renderer.getTextWidth(SMALL_FONT_ID, buf);
          renderer.drawText(SMALL_FONT_ID, cx + (cellSize - tw) / 2,
                            cy + (cellSize - renderer.getLineHeight(SMALL_FONT_ID)) / 2, buf);
        }
      }
    }
  }

  // Status line below grid
  const int statusTop = gridY + gridH;
  const int statusY = statusTop + std::max(2, (contentBot - statusTop - renderer.getLineHeight(SMALL_FONT_ID)) / 2);
  if (gameOver) {
    renderer.drawCenteredText(SMALL_FONT_ID, statusY, tr(STR_GAME_OVER), true, EpdFontFamily::BOLD);
  } else if (won) {
    renderer.drawCenteredText(SMALL_FONT_ID, statusY, tr(STR_YOU_WIN), true, EpdFontFamily::BOLD);
  } else {
    char info[32];
    snprintf(info, sizeof(info), tr(STR_MINES_INFO_FMT), mineCount - flagCount);
    renderer.drawCenteredText(SMALL_FONT_ID, statusY, info);
  }

  // Button hints
  const char* btn1 = tr(STR_BACK);
  const char* btn2 = "";
  const char* btn3 = "";
  const char* btn4 = "";

  if (gameOver || won) {
    btn2 = tr(STR_NEW_GAME);
    btn4 = difficultyLabel();
  } else {
    btn2 = tr(STR_TAP);
    btn3 = tr(STR_DIR_LEFT);
    btn4 = tr(STR_DIR_RIGHT);
  }

  const auto labels = mappedInput.mapLabels(btn1, btn2, btn3, btn4);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
