#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "ParsedText.h"

class GfxRenderer;
class PageElement;

// Narrow interface the chapter parser implements so table layout can place
// elements on pages (and break pages) without seeing parser internals.
// TableLayout only ever holds a sink by reference for the duration of a call,
// never owns one, so the destructor is protected and non-virtual.
class TablePageSink {
 public:
  virtual int16_t currentY() const = 0;
  virtual int16_t pageHeight() const = 0;
  // Flush the current page (if it has content) and start a fresh one at y = 0.
  virtual void completePage() = 0;
  virtual void addElement(std::shared_ptr<PageElement> element) = 0;
  virtual void advanceY(int16_t dy) = 0;

 protected:
  ~TablePageSink() = default;
};

// Lays out <table> content as a real grid with drawn cell borders.
//
// Nothing is ever truncated: content that does not fit the grid changes HOW it
// renders, never WHETHER it renders. Tables and rows unsuited to a grid fall
// back to plain full-width paragraphs (Mode::Fallback) rather than losing cells.
//
// Memory strategy (the ~380KB heap is the hard constraint): only a small sample
// of leading rows is buffered to derive column widths; after that every row
// flushes as its </tr> closes. Grid rows must materialize all their lines at
// once (the row height is needed for the borders), so rows heavier than
// GRID_ROW_WORD_LIMIT render as paragraphs instead, bounding the per-row
// transient.
//
// When the table has more columns than the viewport can legibly hold (or is
// single-column), the whole table uses the fallback layout.
class TableLayout {
 public:
  static constexpr size_t MAX_COLS = 8;

  TableLayout(const GfxRenderer& renderer, int fontId, float lineCompression, int16_t originX, uint16_t availableWidth);

  // Presentation policy for a cell's text block: th centered+indent-free,
  // td left-aligned, CSS text-align/dir honored when present.
  static BlockStyle cellBlockStyle(bool isHeaderCell, const CssStyle& cssStyle);

  void startRow();
  // Returns false only when a cell is already open (defensive; the parser's
  // inTableCell gate prevents it). Never drops content otherwise.
  bool startCell(uint8_t colSpan, TablePageSink& sink);
  // Hands ownership of the finished cell's words to the table.
  void endCell(std::unique_ptr<ParsedText> text, TablePageSink& sink);
  void endRow(TablePageSink& sink);
  // Called on </table>: flushes anything still buffered and closes the grid.
  void finish(TablePageSink& sink);

 private:
  static constexpr int16_t BORDER = 1;
  static constexpr int16_t CELL_PAD_X = 4;
  static constexpr int16_t CELL_PAD_Y = 3;
  // Column widths are derived from the first few rows only, so an arbitrarily
  // long table never buffers more than this sample.
  static constexpr size_t SAMPLE_ROW_LIMIT = 6;
  static constexpr size_t SAMPLE_WORD_LIMIT = 600;
  // A grid row's lines exist together in RAM during emitRowColumns (each line
  // is a TextBlock of ~200 bytes); heavier rows render as paragraphs instead.
  static constexpr size_t GRID_ROW_WORD_LIMIT = 320;

  // Sampling: leading rows buffer until column layout can be decided.
  // Grid: rows lay out as bordered columns (per-row degradation for rows that
  // are too heavy or too wide). Fallback: every cell renders as a full-width
  // paragraph the moment it closes.
  enum class Mode : uint8_t { Sampling, Grid, Fallback };

  struct Cell {
    std::unique_ptr<ParsedText> text;
    uint8_t colSpan = 1;
  };
  using Row = std::vector<Cell>;

  const GfxRenderer& renderer;
  const int fontId;
  const int16_t lineHeight;
  const int16_t originX;
  const uint16_t availableWidth;

  Mode mode = Mode::Sampling;
  std::vector<Row> bufferedRows;
  Row currentRow;
  bool rowOpen = false;
  bool cellOpen = false;
  bool currentRowFallback = false;  // grid mode: this row degraded to paragraphs
  uint8_t pendingColSpan = 1;
  size_t currentRowCols = 0;
  size_t currentRowWords = 0;
  size_t sampledWords = 0;

  std::vector<uint16_t> columnWidths;
  std::vector<int16_t> boundaryXs;  // x of each vertical border line, size numCols + 1
  int16_t tableX = 0;
  uint16_t totalWidth = 0;
  bool topBorderPending = true;
  bool emittedAnything = false;

  // Per-row scratch, reused across rows to avoid per-row heap churn
  // (capacity is retained by clear()).
  std::vector<std::vector<std::shared_ptr<TextBlock>>> rowCellLines;
  std::vector<int16_t> rowCellX;
  std::vector<bool> rowBoundarySkipped;
  std::vector<int16_t> rowBoundaries;

  void decideLayout();
  // Abandons sampling for the whole table (content unsuited to a grid):
  // emits everything buffered so far as paragraphs and streams from then on.
  void switchToFallback(TablePageSink& sink);
  // Degrades the in-progress row: emits its buffered cells immediately;
  // subsequent cells of the row emit as they close.
  void convertCurrentRowToFallback(TablePageSink& sink);
  void flushBufferedRows(TablePageSink& sink);
  void emitRow(Row& row, TablePageSink& sink);
  // Lays every cell of the row out into rowCellLines/rowCellX and fills
  // rowBoundaries; returns the row's line count (>= 1).
  size_t layoutRowCells(Row& row);
  void emitRowColumns(Row& row, TablePageSink& sink);
  void emitRowAsParagraphs(Row& row, TablePageSink& sink);
  // Lays `text` out at full table width and emits the lines (the fallback
  // building block).
  void emitCellAsParagraph(ParsedText& text, TablePageSink& sink);
  void addRect(TablePageSink& sink, int16_t x, int16_t y, uint16_t w, uint16_t h) const;
};
