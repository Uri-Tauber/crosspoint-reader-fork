#include "EInkDisplay.h"

#include <cstring>
#include <fstream>
#include <vector>

// SSD1677 command definitions
// Initialization and reset
#define CMD_SOFT_RESET 0x12            // Soft reset
#define CMD_BOOSTER_SOFT_START 0x0C    // Booster soft-start control
#define CMD_DRIVER_OUTPUT_CONTROL 0x01 // Driver output control
#define CMD_BORDER_WAVEFORM 0x3C       // Border waveform control
#define CMD_TEMP_SENSOR_CONTROL 0x18   // Temperature sensor control

// RAM and buffer management
#define CMD_DATA_ENTRY_MODE 0x11    // Data entry mode
#define CMD_SET_RAM_X_RANGE 0x44    // Set RAM X address range
#define CMD_SET_RAM_Y_RANGE 0x45    // Set RAM Y address range
#define CMD_SET_RAM_X_COUNTER 0x4E  // Set RAM X address counter
#define CMD_SET_RAM_Y_COUNTER 0x4F  // Set RAM Y address counter
#define CMD_WRITE_RAM_BW 0x24       // Write to BW RAM (current frame)
#define CMD_WRITE_RAM_RED 0x26      // Write to RED RAM (used for fast refresh)
#define CMD_AUTO_WRITE_BW_RAM 0x46  // Auto write BW RAM
#define CMD_AUTO_WRITE_RED_RAM 0x47 // Auto write RED RAM

// Display update and refresh
#define CMD_DISPLAY_UPDATE_CTRL1 0x21 // Display update control 1
#define CMD_DISPLAY_UPDATE_CTRL2 0x22 // Display update control 2
#define CMD_MASTER_ACTIVATION 0x20    // Master activation
#define CTRL1_NORMAL 0x00     // Normal mode - compare RED vs BW for partial
#define CTRL1_BYPASS_RED 0x40 // Bypass RED RAM (treat as 0) - for full refresh

// LUT and voltage settings
#define CMD_WRITE_LUT 0x32      // Write LUT
#define CMD_GATE_VOLTAGE 0x03   // Gate voltage
#define CMD_SOURCE_VOLTAGE 0x04 // Source voltage
#define CMD_WRITE_VCOM 0x2C     // Write VCOM
#define CMD_WRITE_TEMP 0x1A     // Write temperature

// Power management
#define CMD_DEEP_SLEEP 0x10 // Deep sleep

// UC81xx-class command definitions (X3 controller)
// Opcodes overlap with SSD1677 but have different meanings; keep the
// CMD_X3_ prefix when referencing from X3-only code paths.
//
// Initialization
#define CMD_X3_PANEL_SETTING      0x00 // PSR
#define CMD_X3_POWER_SETTING      0x01 // PWR
#define CMD_X3_POWER_OFF          0x02 // POF
#define CMD_X3_POWER_OFF_SEQ      0x03 // PFS
#define CMD_X3_POWER_ON           0x04 // PON
#define CMD_X3_BOOSTER_SOFT_START 0x06 // BTST
// RAM data transfer
#define CMD_X3_DTM1               0x10 // Display Start Transmission 1 ("old" RAM plane)
#define CMD_X3_DATA_STOP          0x11 // DSP — commit the preceding DTMx data stream
#define CMD_X3_DTM2               0x13 // Display Start Transmission 2 ("new" RAM plane)
// Refresh control
#define CMD_X3_DISPLAY_REFRESH    0x12 // DRF — trigger refresh, implicitly closes DTM2
// LUT register bank
#define CMD_X3_LUT_VCOM           0x20 // LUTC
#define CMD_X3_LUT_WW             0x21 // LUTWW
#define CMD_X3_LUT_BW             0x22 // LUTBW
#define CMD_X3_LUT_WB             0x23 // LUTWB
#define CMD_X3_LUT_BB             0x24 // LUTBB
// Configuration
#define CMD_X3_PLL_CONTROL        0x30 // PLL
#define CMD_X3_VCOM_DATA_INTERVAL 0x50 // CDI — VCOM and data interval setting (mode select)
#define CMD_X3_RESOLUTION         0x61 // TRES
#define CMD_X3_GATE_SOURCE_START  0x65 // GSST
#define CMD_X3_VCOM_DC            0x82 // VDCS
#define CMD_X3_LV_SELECTION       0xE1 // Source LV / FT_GS selection
// Partial update window
#define CMD_X3_PARTIAL_WINDOW     0x90 // PTL — set partial window coords
#define CMD_X3_PARTIAL_IN         0x91 // PTIN — enter partial mode
#define CMD_X3_PARTIAL_OUT        0x92 // PTOUT — exit partial mode

// Custom LUT for fast refresh (differential 3-pass mode, 12 frames)
const unsigned char lut_grayscale[] PROGMEM = {
    // 00 black/white
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 01 light gray
    0x54, 0x54, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 10 gray
    0xAA, 0xA0, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 11 dark gray
    0xA2, 0x22, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // L4 (VCOM)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // TP/RP groups (global timing)
    0x01, 0x01, 0x01, 0x01, 0x00, // G0: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00, // G1: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00, // G2: A=0 B=0 C=0 D=0 RP=0 (4 frames)
    0x00, 0x00, 0x00, 0x00, 0x00, // G3: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00, // G4: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00, // G5: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00, // G6: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00, // G7: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00, // G8: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00, // G9: A=0 B=0 C=0 D=0 RP=0

    // Frame rate
    0x8F, 0x8F, 0x8F, 0x8F, 0x8F,

    // Voltages (VGH, VSH1, VSH2, VSL, VCOM)
    0x17, 0x41, 0xA8, 0x32, 0x30,

    // Reserved
    0x00, 0x00};

const unsigned char lut_grayscale_revert[] PROGMEM = {
    // 00 black/white
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 10 gray
    0x54, 0x54, 0x54, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 01 light gray
    0xA8, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 11 dark gray
    0xFC, 0xFC, 0xFC, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // L4 (VCOM)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // TP/RP groups (global timing)
    0x01, 0x01, 0x01, 0x01, 0x01, // G0: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x01, // G1: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00, // G2: A=0 B=0 C=0 D=0 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00, // G3: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00, // G4: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00, // G5: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00, // G6: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00, // G7: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00, // G8: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00, // G9: A=0 B=0 C=0 D=0 RP=0

    // Frame rate
    0x8F, 0x8F, 0x8F, 0x8F, 0x8F,

    // Voltages (VGH, VSH1, VSH2, VSL, VCOM)
    0x17, 0x41, 0xA8, 0x32, 0x30,

    // Reserved
    0x00, 0x00};

// X3 differential BW page-turn LUTs — community-authored.
// Required because loading the OEM img bank for full-sync/grayscale leaves
// absolute-mode waveforms in the controller's LUT registers. Subsequent
// fast-diff triggers reuse those registers, producing grey overlay artifacts.
// Loading this bank before fast-diff overwrites the absolute waveforms with
// differential B→W / W→B transitions, restoring clean page turns.
// Values mirror the OEM V5.6.21 X3 firmware LUT bank at flash offset
// 0x402ad0 (mode-2 entry per command 0x20..0x24). Timing parameters are
// slightly tighter than our prior values (byte 2: 02→01, byte 7: 05→04,
// byte 9: 00→01) and bb's transition pattern differs structurally
// (header 0x10→0x00 + byte 6 0x00→0x04).
const uint8_t lut_x3_vcom_normal[] PROGMEM = {
    0x00, 0x06, 0x01, 0x06, 0x06, 0x01, 0x00, 0x04, 0x01, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_ww_normal[] PROGMEM = {
    0x20, 0x06, 0x01, 0x06, 0x06, 0x01, 0x00, 0x04, 0x01, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bw_normal[] PROGMEM = {
    0xAA, 0x06, 0x01, 0x06, 0x06, 0x01, 0xA0, 0x04, 0x01, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_wb_normal[] PROGMEM = {
    0x55, 0x06, 0x01, 0x06, 0x06, 0x01, 0x50, 0x04, 0x01, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bb_normal[] PROGMEM = {
    0x00, 0x06, 0x01, 0x06, 0x06, 0x01, 0x04, 0x04, 0x01, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// X3 scrub LUTs — extracted from OEM V5.6.21 firmware at flash offset
// 0x402ad0 (mode 1 of the bank). Distinguishing feature vs `_full`: the
// WW/BW pair (cmds 0x21/0x22) and the WB/BB pair (cmds 0x23/0x24) are
// byte-identical, which collapses the controller's per-state LUT selection
// to "drive every pixel that DTM2 says should be white using one strong
// waveform; drive every pixel DTM2 says should be black using another."
// DTM1's contents become irrelevant. Used to scrub the panel back to a
// clean state after differential grayscale (AA), where DTM1 holds the AA
// LSB plane and is no longer a valid "previous BW frame" for diffing.
const uint8_t lut_x3_vcom_half[] PROGMEM = {
    0x00, 0x06, 0x01, 0x06, 0x06, 0x01, 0x00, 0x04, 0x01, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_ww_half[] PROGMEM = {
    0xAA, 0x06, 0x01, 0x06, 0x06, 0x01, 0xA0, 0x04, 0x01, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bw_half[] PROGMEM = {
    0xAA, 0x06, 0x01, 0x06, 0x06, 0x01, 0xA0, 0x04, 0x01, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_wb_half[] PROGMEM = {
    0x55, 0x06, 0x01, 0x06, 0x06, 0x01, 0x50, 0x04, 0x01, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bb_half[] PROGMEM = {
    0x55, 0x06, 0x01, 0x06, 0x06, 0x01, 0x50, 0x04, 0x01, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// X3 turbo LUTs from papyrix-reader: same voltage patterns as full,
// shortened timing for fast differential updates.
const uint8_t lut_x3_vcom_fast[] PROGMEM = {
    0x00, 0x04, 0x02, 0x04, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_ww_fast[] PROGMEM = {
    0x20, 0x04, 0x02, 0x04, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bw_fast[] PROGMEM = {
    0xAA, 0x04, 0x02, 0x04, 0x04, 0x01, 0x80, 0x04, 0x01, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_wb_fast[] PROGMEM = {
    0x55, 0x04, 0x02, 0x04, 0x04, 0x01, 0x40, 0x04, 0x01, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bb_fast[] PROGMEM = {
    0x10, 0x04, 0x02, 0x04, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// X3 differential grayscale LUTs — mechanical port of the X4 lut_grayscale
// VS patterns into the X3's 5-cell bank format. Used for text-only AA pages
// where the BW content is already on screen and grey levels overlay it.
// GRAYSCALE encoding cell mapping: BB=no change, WW=dark gray, BW=medium gray.
// WB is never selected by GRAYSCALE encoding but populated with state 01
// (light gray) for completeness.
const uint8_t lut_x3_vcom_grayscale[] PROGMEM = {
    0x00, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_ww_grayscale[] PROGMEM = {
    // State 11 (dark gray): single phase, weak drive matching original X3
    // behavior
    0x20, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bw_grayscale[] PROGMEM = {
    // State 10 (medium gray): single phase, moderate drive matching original X3
    // behavior
    0x80, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_wb_grayscale[] PROGMEM = {
    // State 01 (light gray): single phase, X4 VS[0] = 0x54 — never selected
    0x54, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bb_grayscale[] PROGMEM = {
    // State 00 (no change): VS = 0x00 — pixels stay at their existing BW state
    0x00, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// X3 stock full/quality image-write LUTs — extracted from OEM firmware
// V5.6.21-X3-EN-PROD-0519_180550.bin at flash offset 0x402b28.
// OEM loaders set CDI 0x29,0x07 before loading this bank.
const uint8_t lut_x3_vcom_full[] PROGMEM = {
    0x00, 0x18, 0x04, 0x0E, 0x0A, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_ww_full[] PROGMEM = {
    0x4A, 0x18, 0x04, 0x0E, 0x0A, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bw_full[] PROGMEM = {
    0x0A, 0x18, 0x04, 0x0E, 0x0A, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_wb_full[] PROGMEM = {
    0x04, 0x18, 0x04, 0x0E, 0x0A, 0x01, 0x40, 0x0A, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bb_full[] PROGMEM = {
    0x84, 0x18, 0x04, 0x0E, 0x0A, 0x01, 0x40, 0x0A, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// X3 community 4-level grayscale LUTs — mechanical port of the X4
// lut_grayscale VS patterns into the X3's 5-cell bank format. Used for both
// reader AA text overlay and sleep-screen cover rendering. Drives 4 visibly-
// distinct end states from the reader's (LSB→DTM1, MSB→DTM2) 4-plane encoding
// by giving each cell a different net charge transfer:
//   ww (1,1)  VS=0x20: brief +V              → near white
//   bw (0,1)  VS=0x80: brief +V (later)      → light gray
//   wb (1,0)  VS=0x54: triple -V pulses      → dark gray
//   bb (0,0)  VS=0x00: no drive              → preserves prior state
// NOT an OEM-derived bank. OEM V5.1.6 has no 4-level differential bank — its
// "_full" and "short" banks are both 2-level transition LUTs that cannot
// produce 4 distinct end states from a 2-plane absolute encoding. The
// community LUT solves what the reader needs even though OEM does not solve
// that problem the same way (OEM dithers covers to BW). The previous V5.6.21
// "_gc" bytes loaded here (~52-frame anti-aliased text touchup waveform)
// collapsed mid-grays toward black on full-frame content — see fix history.
const uint8_t lut_x3_vcom_gc[] PROGMEM = {
    0x00, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_ww_gc[] PROGMEM = {
    0x20, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bw_gc[] PROGMEM = {
    0x80, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_wb_gc[] PROGMEM = {
    0x54, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bb_gc[] PROGMEM = {
    0x00, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void EInkDisplay::setDisplayDimensions(uint16_t width, uint16_t height) {
  displayWidth = width;
  displayHeight = height;
  displayWidthBytes = width / 8;
  bufferSize = displayWidthBytes * height;
  _x3Mode = false;
}

void EInkDisplay::setDisplayX3() {
  setDisplayDimensions(X3_DISPLAY_WIDTH, X3_DISPLAY_HEIGHT);
  _x3Mode = true;
}

void EInkDisplay::requestResync(uint8_t settlePasses) {
  _x3ForceFullSyncNext = _x3Mode;
  _x3ForcedConditionPassesNext = _x3Mode ? settlePasses : 0;
}

void EInkDisplay::skipInitialResync() {
  if (!_x3Mode) return;
  _x3InitialFullSyncsRemaining = 0;
  _x3RedRamSynced = true;
}

// Factory LUT extracted from firmware V3.1.9_CH_X4_0117.bin by CrazyCoder.
// Uses absolute 2-bit pixel encoding: BW RAM = bit0 (LSB), RED RAM = bit1
// (MSB). Pixel states: {RED=0,BW=0}=black, {RED=0,BW=1}=dark gray,
//               {RED=1,BW=0}=light gray, {RED=1,BW=1}=white.

// Fast mode (LUT1): 60 waveform frames, FR=0x44, VCOM=-2.0V.
// Used for XTH reading in container mode. ~40% faster than quality mode.
const unsigned char lut_factory_fast[] PROGMEM = {
    // VS patterns (LUT0-LUT3 + VCOM), 10 bytes each
    0x00, 0x4A, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, // LUT0: state 00 (black)
    0x80, 0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, // LUT1: state 01 (dark gray)
    0x88, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, // LUT2: state 10 (light gray)
    0xA8, 0x44, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, // LUT3: state 11 (white)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT4: VCOM
    // TP/RP timing groups (G0-G9), 5 bytes each
    0x09, 0x0C, 0x03, 0x03, 0x00, // G0: 27 frames
    0x0F, 0x03, 0x07, 0x03, 0x00, // G1: 28 frames
    0x03, 0x00, 0x02, 0x00, 0x00, // G2:  5 frames
    0x00, 0x00, 0x00, 0x00, 0x00, // G3
    0x00, 0x00, 0x00, 0x00, 0x00, // G4
    0x00, 0x00, 0x00, 0x00, 0x00, // G5
    0x00, 0x00, 0x00, 0x00, 0x00, // G6
    0x00, 0x00, 0x00, 0x00, 0x00, // G7
    0x00, 0x00, 0x00, 0x00, 0x00, // G8
    0x00, 0x00, 0x00, 0x00, 0x00, // G9
    // Frame rate (higher = faster clock): 0x44 = 68
    0x44, 0x44, 0x44, 0x44, 0x44,
    // Voltages: VGH, VSH1, VSH2, VSL, VCOM
    0x17, 0x41, 0xA8, 0x32, 0x50};

// Quality mode (LUT2): 50 waveform frames, FR=0x22, VCOM=-1.2V.
// Used for standalone XTH wallpapers/covers. Less ghosting, ~67% slower than
// fast mode.
const unsigned char lut_factory_quality[] PROGMEM = {
    // VS patterns (LUT0-LUT3 + VCOM), 10 bytes each
    0x00, 0x4A, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, // LUT0: state 00 (black)
    0x80, 0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, // LUT1: state 01 (dark gray)
    0x88, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, // LUT2: state 10 (light gray)
    0xA8, 0x44, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, // LUT3: state 11 (white)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT4: VCOM
    // TP/RP timing groups (G0-G9), 5 bytes each
    0x08, 0x0B, 0x02, 0x03, 0x00, // G0: 24 frames
    0x0C, 0x02, 0x07, 0x02, 0x00, // G1: 23 frames
    0x01, 0x00, 0x02, 0x00, 0x00, // G2:  3 frames
    0x00, 0x00, 0x00, 0x00, 0x00, // G3
    0x00, 0x00, 0x00, 0x00, 0x00, // G4
    0x00, 0x00, 0x00, 0x00, 0x00, // G5
    0x00, 0x00, 0x00, 0x00, 0x00, // G6
    0x00, 0x00, 0x00, 0x00, 0x00, // G7
    0x00, 0x00, 0x00, 0x00, 0x00, // G8
    0x00, 0x00, 0x00, 0x00,
    0x01, // G9 (RP[9]=1, no practical effect: all-zero timing)
    // Frame rate (lower = slower clock): 0x22 = 34
    0x22, 0x22, 0x22, 0x22, 0x22,
    // Voltages: VGH, VSH1, VSH2, VSL, VCOM
    0x17, 0x41, 0xA8, 0x32, 0x30};

EInkDisplay::EInkDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc,
                         int8_t rst, int8_t busy)
    : _sclk(sclk), _mosi(mosi), _cs(cs), _dc(dc), _rst(rst), _busy(busy),
      frameBuffer(nullptr),
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
      frameBufferActive(nullptr),
#endif
      customLutActive(false) {
  if (Serial)
    Serial.printf("[%lu] EInkDisplay: Constructor called\n", millis());
  if (Serial)
    Serial.printf("[%lu]   SCLK=%d, MOSI=%d, CS=%d, DC=%d, RST=%d, BUSY=%d\n",
                  millis(), sclk, mosi, cs, dc, rst, busy);
}

void EInkDisplay::begin() {
  if (Serial)
    Serial.printf("[%lu] EInkDisplay: begin() called\n", millis());

  isScreenOn = false;
  customLutActive = false;
  inGrayscaleMode = false;
  drawGrayscale = false;

  frameBuffer = frameBuffer0;
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  frameBufferActive = frameBuffer1;
#endif

  // Initialize to white
  memset(frameBuffer0, 0xFF, bufferSize);
  _x3RedRamSynced = false;
  _x3InitialFullSyncsRemaining = _x3Mode ? 2 : 0;
  _x3ForceFullSyncNext = false;
  _x3ForcedConditionPassesNext = 0;
  _x3GrayState = {};
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  if (Serial)
    Serial.printf("[%lu]   Static frame buffer (%lu bytes)\n", millis(),
                  bufferSize);
#else
  memset(frameBuffer1, 0xFF, bufferSize);
  if (Serial)
    Serial.printf("[%lu]   Static frame buffers (2 x %lu bytes)\n", millis(),
                  bufferSize);
#endif

  if (Serial)
    Serial.printf("[%lu]   Initializing e-ink display driver...\n", millis());

  // Initialize SPI with custom pins
  SPI.begin(_sclk, -1, _mosi, _cs);
  const uint32_t spiHz = _x3Mode ? 16000000 : 40000000;
  spiSettings = SPISettings(spiHz, MSBFIRST, SPI_MODE0);
  if (Serial)
    Serial.printf("[%lu]   SPI initialized at %lu Hz, Mode 0\n", millis(),
                  spiHz);

  // Setup GPIO pins
  pinMode(_cs, OUTPUT);
  pinMode(_dc, OUTPUT);
  pinMode(_rst, OUTPUT);
  pinMode(_busy, INPUT);

  digitalWrite(_cs, HIGH);
  digitalWrite(_dc, HIGH);

  if (Serial)
    Serial.printf("[%lu]   GPIO pins configured\n", millis());

  // Reset display
  resetDisplay();

  // Initialize display controller
  initDisplayController();

  if (Serial)
    Serial.printf("[%lu]   E-ink display driver initialized\n", millis());
}

// ============================================================================
// Low-level display control methods
// ============================================================================

void EInkDisplay::resetDisplay() {
  if (Serial)
    Serial.printf("[%lu]   Resetting display...\n", millis());
  digitalWrite(_rst, HIGH);
  delay(20);
  digitalWrite(_rst, LOW);
  delay(2);
  digitalWrite(_rst, HIGH);
  delay(20);
  if (Serial)
    Serial.printf("[%lu]   Display reset complete\n", millis());
  if (_x3Mode) {
    delay(50);
    return;
  }
}

void EInkDisplay::waitForRefresh(const char *comment) {
  pollBusy(comment, "Refresh done");
}

void EInkDisplay::pollBusy(const char *comment, const char *completeWord) {
  unsigned long start = millis();
  if (!_x3Mode) {
    // X4: BUSY held HIGH while busy, drops LOW when done.
    while (digitalRead(_busy) == HIGH) {
      delay(1);
      if (millis() - start > 30000)
        break;
    }
  } else {
    // X3 (UC81xx-class): BUSY is active LOW. Idle = HIGH, working = LOW.
    // After a command that does work, BUSY transitions HIGH -> LOW (work
    // starts) -> HIGH (work done). We poll up to 1s for the HIGH -> LOW
    // edge (race protection: the controller may not assert BUSY until
    // shortly after the trigger returns), then up to 30s for the
    // LOW -> HIGH edge. If we never observe the LOW phase the operation
    // either completed faster than we could see or was a no-op, and we
    // skip the completion log line.
    bool sawLow = false;
    while (digitalRead(_busy) == HIGH) {
      delay(1);
      if (millis() - start > 1000)
        break;
    }
    if (digitalRead(_busy) == LOW) {
      sawLow = true;
      while (digitalRead(_busy) == LOW) {
        delay(1);
        if (millis() - start > 30000)
          break;
      }
    }
    if (!sawLow)
      return;
  }
  if (comment && Serial)
    Serial.printf("[%lu]   %s: %s (%lu ms)\n", millis(), completeWord, comment,
                  millis() - start);
}

void EInkDisplay::sendCommand(uint8_t command) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, LOW); // Command mode
  digitalWrite(_cs, LOW); // Select chip
  SPI.transfer(command);
  digitalWrite(_cs, HIGH); // Deselect chip
  SPI.endTransaction();
}

void EInkDisplay::sendData(uint8_t data) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH); // Data mode
  digitalWrite(_cs, LOW);  // Select chip
  SPI.transfer(data);
  digitalWrite(_cs, HIGH); // Deselect chip
  SPI.endTransaction();
}

void EInkDisplay::sendData(const uint8_t *data, uint16_t length) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);      // Data mode
  digitalWrite(_cs, LOW);       // Select chip
  SPI.writeBytes(data, length); // Transfer all bytes
  digitalWrite(_cs, HIGH);      // Deselect chip
  SPI.endTransaction();
}

// ---- X3 (UC81xx) primitives ----------------------------------------------
// `sendCommandDataX3` / `sendCommandDataByteX3` bundle a command byte and a
// short data payload into a single CS-low SPI transaction. Used for LUT
// register writes (cmd 0x20-0x24 + 42 bytes), mode select (cmd 0x50 + 2
// bytes), and partial-window descriptors (cmd 0x90 + 9 bytes). Saves one
// CS toggle vs the separated form.
//
// The bulk plane-write helpers (`sendPlaneX3`, `fillPlaneX3`) and the init
// RAM-clear use the separated `sendCommand()` + `sendData()` form instead.
// UC81xx accepts both for DTM1/DTM2 streams; the separation makes the
// in-place Y-flip and row-streaming patterns simpler to express. This is
// not a hard atomicity requirement of the controller.

void EInkDisplay::sendCommandDataX3(uint8_t cmd, const uint8_t *data,
                                    uint16_t len) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_cs, LOW);
  digitalWrite(_dc, LOW);
  SPI.transfer(cmd);
  if (len > 0 && data != nullptr) {
    digitalWrite(_dc, HIGH);
    SPI.writeBytes(data, len);
  }
  digitalWrite(_cs, HIGH);
  SPI.endTransaction();
}

void EInkDisplay::sendCommandDataByteX3(uint8_t cmd, uint8_t d0) {
  const uint8_t d[1] = {d0};
  sendCommandDataX3(cmd, d, 1);
}

void EInkDisplay::sendCommandDataByteX3(uint8_t cmd, uint8_t d0, uint8_t d1) {
  const uint8_t d[2] = {d0, d1};
  sendCommandDataX3(cmd, d, 2);
}

void EInkDisplay::sendPlaneX3(uint8_t ramCmd, uint8_t *buf, bool invert) {
  // The X3 controller scans gates upward (UD=1), so the first byte sent
  // maps to the bottom-left pixel. Our framebuffer stores row 0 at offset
  // 0 (top), so we Y-flip rows before sending and restore after. Avoids
  // allocating a transposed copy.
  auto flipRowsInPlace = [&](uint8_t *p) {
    uint8_t rowTmp[128];
    for (uint16_t top = 0, bot = displayHeight - 1; top < bot; top++, bot--) {
      uint8_t *rowA = p + static_cast<uint32_t>(top) * displayWidthBytes;
      uint8_t *rowB = p + static_cast<uint32_t>(bot) * displayWidthBytes;
      memcpy(rowTmp, rowA, displayWidthBytes);
      memcpy(rowA, rowB, displayWidthBytes);
      memcpy(rowB, rowTmp, displayWidthBytes);
    }
  };
  auto invertBuffer = [&](uint8_t *p) {
    auto *w = reinterpret_cast<uint32_t *>(p);
    for (uint32_t i = 0; i < bufferSize / 4; i++)
      w[i] = ~w[i];
  };
  if (invert) invertBuffer(buf);
  flipRowsInPlace(buf);
  sendCommand(ramCmd);
  sendData(buf, static_cast<uint16_t>(bufferSize));
  flipRowsInPlace(buf);
  if (invert) invertBuffer(buf);
}

void EInkDisplay::fillPlaneX3(uint8_t ramCmd, uint8_t fillByte) {
  // Fill an entire RAM plane with a constant byte. Streams a small stack
  // row buffer repeatedly inside a single SPI transaction so the
  // framebuffer (~50 KB) doesn't need to be touched or memset.
  uint8_t rowBuf[128];
  memset(rowBuf, fillByte, displayWidthBytes);
  sendCommand(ramCmd);
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);
  digitalWrite(_cs, LOW);
  for (uint16_t y = 0; y < displayHeight; y++) {
    SPI.writeBytes(rowBuf, displayWidthBytes);
  }
  digitalWrite(_cs, HIGH);
  SPI.endTransaction();
}

void EInkDisplay::loadLutBankX3(const uint8_t *vcom, const uint8_t *ww,
                                const uint8_t *bw, const uint8_t *wb,
                                const uint8_t *bb) {
  sendCommandDataX3(CMD_X3_LUT_VCOM, vcom, 42);
  sendCommandDataX3(CMD_X3_LUT_WW,   ww,   42);
  sendCommandDataX3(CMD_X3_LUT_BW,   bw,   42);
  sendCommandDataX3(CMD_X3_LUT_WB,   wb,   42);
  sendCommandDataX3(CMD_X3_LUT_BB,   bb,   42);
}

void EInkDisplay::loadLutBankX3WithCdi(uint8_t cdi0, const uint8_t *vcom,
                                       const uint8_t *ww, const uint8_t *bw,
                                       const uint8_t *wb, const uint8_t *bb) {
  sendCommandDataByteX3(CMD_X3_VCOM_DATA_INTERVAL, cdi0);
  loadLutBankX3(vcom, ww, bw, wb, bb);
}

void EInkDisplay::loadLutBankX3WithCdi(uint8_t cdi0, uint8_t cdi1,
                                       const uint8_t *vcom,
                                       const uint8_t *ww, const uint8_t *bw,
                                       const uint8_t *wb, const uint8_t *bb) {
  sendCommandDataByteX3(CMD_X3_VCOM_DATA_INTERVAL, cdi0, cdi1);
  loadLutBankX3(vcom, ww, bw, wb, bb);
}

void EInkDisplay::triggerRefreshX3(bool turnOffScreen, const char *tag) {
  if (!isScreenOn) {
    sendCommand(CMD_X3_POWER_ON);
    char buf[32];
    snprintf(buf, sizeof(buf), " X3_PON%s", tag);
    waitForRefresh(buf);
    isScreenOn = true;
  }
  if (Serial)
    Serial.printf("[%lu]   X3_OEM_TRIGGER=DRF%s\n", millis(), tag);
  sendCommand(CMD_X3_DISPLAY_REFRESH);
  {
    char buf[32];
    snprintf(buf, sizeof(buf), " X3_DRF%s", tag);
    waitForRefresh(buf);
  }
  if (turnOffScreen) {
    sendCommand(CMD_X3_POWER_OFF);
    char buf[32];
    snprintf(buf, sizeof(buf), " X3_POF%s", tag);
    waitForRefresh(buf);
    isScreenOn = false;
  }
}

void EInkDisplay::waitWhileBusy(const char *comment) {
  pollBusy(comment, "Wait complete");
}

void EInkDisplay::initDisplayController() {
#ifndef X3_USE_X4_INIT
  if (_x3Mode) {
    sendCommand(CMD_X3_PANEL_SETTING);
    sendData(0x3F); // OEM value
    sendData(0x0A); // OEM value (was 0x08)
    sendCommand(CMD_X3_RESOLUTION);
    sendData(0x03);
    sendData(0x18);
    sendData(0x02);
    sendData(0x58);
    sendCommand(CMD_X3_GATE_SOURCE_START);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendCommand(CMD_X3_POWER_OFF_SEQ);
    sendData(0x20); // OEM value (was 0x1D)
    sendCommand(CMD_X3_POWER_SETTING);
    sendData(0x07);
    sendData(0x17);
    sendData(0x3F);
    sendData(0x3F);
    sendData(0x17);
    sendCommand(CMD_X3_VCOM_DC);
    sendData(0x24); // OEM value (was 0x1D)
    sendCommand(CMD_X3_BOOSTER_SOFT_START);
    sendData(0x25);
    sendData(0x25);
    sendData(0x3C);
    sendData(0x37);
    sendCommand(CMD_X3_PLL_CONTROL);
    sendData(0x09);
    sendCommand(CMD_X3_LV_SELECTION);
    sendData(0x02);

    // Match the X4 init's RAM-clear step. The X3 panel runs a UC81xx-class
    // controller, not the SSD1677 we drive on X4, so the convenient
    // AUTO_WRITE_BW_RAM (0x47) / AUTO_WRITE_RED_RAM (0x48) built-ins X4 uses
    // to fill both planes with white don't exist here — those opcodes aren't
    // defined in UC81xx. We do the bulk SPI write manually using the
    // existing 0x10 (old) / 0x13 (new) RAM plane write commands. Without
    // this, RAM retains whatever the panel was showing before reset and
    // the first differential refresh diffs against that stale content,
    // letting the prior screen bleed through the first user-rendered frame.
    if (frameBuffer) {
      memset(frameBuffer, 0xFF, bufferSize);
      sendCommand(CMD_X3_DTM1);
      sendData(frameBuffer, static_cast<uint16_t>(bufferSize));
      sendCommand(CMD_X3_DATA_STOP); // commit DTM1 — required because no
                                     // refresh follows this RAM-clear
      sendCommand(CMD_X3_DTM2);
      sendData(frameBuffer, static_cast<uint16_t>(bufferSize));
      sendCommand(CMD_X3_DATA_STOP); // commit DTM2 — same reason
      // Leave frameBuffer at 0xFF (white) so it matches the RAM state we
      // just wrote and matches begin()'s earlier memset(frameBuffer0, 0xFF).
    }

    isScreenOn = false;
    return;
  }
#endif

  if (Serial)
    Serial.printf("[%lu]   Initializing SSD1677 controller...\n", millis());

  const uint8_t TEMP_SENSOR_INTERNAL = 0x80;

  // Soft reset
  sendCommand(CMD_SOFT_RESET);
  waitWhileBusy(" CMD_SOFT_RESET");

  // Temperature sensor control (internal)
  sendCommand(CMD_TEMP_SENSOR_CONTROL);
  sendData(TEMP_SENSOR_INTERNAL);

  // Booster soft-start control (GDEQ0426T82 specific values)
  sendCommand(CMD_BOOSTER_SOFT_START);
  sendData(0xAE);
  sendData(0xC7);
  sendData(0xC3);
  sendData(0xC0);
  sendData(0x40);

  // Driver output control: set display height and scan direction
  sendCommand(CMD_DRIVER_OUTPUT_CONTROL);
  sendData((displayHeight - 1) % 256);
  sendData((displayHeight - 1) / 256);
  sendData(0x02); // SM=1 (interlaced), TB=0

  // Border waveform control
  sendCommand(CMD_BORDER_WAVEFORM);
  sendData(0x01);

  // Set up full screen RAM area
  setRamArea(0, 0, displayWidth, displayHeight);

  if (Serial)
    Serial.printf("[%lu]   Clearing RAM buffers...\n", millis());
  sendCommand(CMD_AUTO_WRITE_BW_RAM); // Auto write BW RAM
  sendData(0xF7);
  waitWhileBusy(" CMD_AUTO_WRITE_BW_RAM");

  sendCommand(CMD_AUTO_WRITE_RED_RAM); // Auto write RED RAM
  sendData(0xF7);                      // Fill with white pattern
  waitWhileBusy(" CMD_AUTO_WRITE_RED_RAM");

  if (Serial)
    Serial.printf("[%lu]   SSD1677 controller initialized\n", millis());
}

void EInkDisplay::setRamArea(const uint16_t x, uint16_t y, uint16_t w,
                             uint16_t h) {
  constexpr uint8_t DATA_ENTRY_X_INC_Y_DEC = 0x01;

  // Reverse Y coordinate (gates are reversed on this display)
  y = displayHeight - y - h;

  // Set data entry mode (X increment, Y decrement for reversed gates)
  sendCommand(CMD_DATA_ENTRY_MODE);
  sendData(DATA_ENTRY_X_INC_Y_DEC);

  // Set RAM X address range (start, end) - X is in PIXELS
  sendCommand(CMD_SET_RAM_X_RANGE);
  sendData(x % 256);           // start low byte
  sendData(x / 256);           // start high byte
  sendData((x + w - 1) % 256); // end low byte
  sendData((x + w - 1) / 256); // end high byte

  // Set RAM Y address range (start, end) - Y is in PIXELS
  sendCommand(CMD_SET_RAM_Y_RANGE);
  sendData((y + h - 1) % 256); // start low byte
  sendData((y + h - 1) / 256); // start high byte
  sendData(y % 256);           // end low byte
  sendData(y / 256);           // end high byte

  // Set RAM X address counter - X is in PIXELS
  sendCommand(CMD_SET_RAM_X_COUNTER);
  sendData(x % 256); // low byte
  sendData(x / 256); // high byte

  // Set RAM Y address counter - Y is in PIXELS
  sendCommand(CMD_SET_RAM_Y_COUNTER);
  sendData((y + h - 1) % 256); // low byte
  sendData((y + h - 1) / 256); // high byte
}

void EInkDisplay::clearScreen(const uint8_t color) const {
  memset(frameBuffer, color, bufferSize);
}

void EInkDisplay::drawImage(const uint8_t *imageData, const uint16_t x,
                            const uint16_t y, const uint16_t w,
                            const uint16_t h, const bool fromProgmem) const {
  if (!frameBuffer) {
    if (Serial)
      Serial.printf("[%lu]   ERROR: Frame buffer not allocated!\n", millis());
    return;
  }

  // Calculate bytes per line for the image
  const uint16_t imageWidthBytes = w / 8;

  // Copy image data to frame buffer
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= displayHeight)
      break;

    const uint16_t destOffset = destY * displayWidthBytes + (x / 8);
    const uint16_t srcOffset = row * imageWidthBytes;

    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((x / 8 + col) >= displayWidthBytes)
        break;

      if (fromProgmem) {
        frameBuffer[destOffset + col] =
            pgm_read_byte(&imageData[srcOffset + col]);
      } else {
        frameBuffer[destOffset + col] = imageData[srcOffset + col];
      }
    }
  }

  if (Serial)
    Serial.printf("[%lu]   Image drawn to frame buffer\n", millis());
}

// Draws only black pixels from the image, leaves white pixels clear (unchanged
// in framebuffer)
void EInkDisplay::drawImageTransparent(const uint8_t *imageData,
                                       const uint16_t x, const uint16_t y,
                                       const uint16_t w, const uint16_t h,
                                       const bool fromProgmem) const {
  if (!frameBuffer) {
    Serial.printf("[%lu]   ERROR: Frame buffer not allocated!\n", millis());
    return;
  }

  // Calculate bytes per line for the image
  const uint16_t imageWidthBytes = w / 8;

  // Copy only black pixels to frame buffer
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= displayHeight)
      break;

    const uint16_t destOffset = destY * displayWidthBytes + (x / 8);
    const uint16_t srcOffset = row * imageWidthBytes;

    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((x / 8 + col) >= displayWidthBytes)
        break;

      uint8_t srcByte = fromProgmem ? pgm_read_byte(&imageData[srcOffset + col])
                                    : imageData[srcOffset + col];
      frameBuffer[destOffset + col] &= srcByte;
    }
  }

  if (Serial)
    Serial.printf("[%lu]   Transparent image drawn to frame buffer\n",
                  millis());
}

void EInkDisplay::writeRamBuffer(uint8_t ramBuffer, const uint8_t *data,
                                 uint32_t size) {
  const char *bufferName = (ramBuffer == CMD_WRITE_RAM_BW) ? "BW" : "RED";
  const unsigned long startTime = millis();
  if (Serial)
    Serial.printf("[%lu]   Writing frame buffer to %s RAM (%lu bytes)...\n",
                  startTime, bufferName, size);

  sendCommand(ramBuffer);
  sendData(data, size);

  const unsigned long duration = millis() - startTime;
  if (Serial)
    Serial.printf("[%lu]   %s RAM write complete (%lu ms)\n", millis(),
                  bufferName, duration);
}

void EInkDisplay::writeRamBufferInverted(uint8_t ramBuffer, const uint8_t *data, uint32_t size) {
  const char *bufferName = (ramBuffer == CMD_WRITE_RAM_BW) ? "BW" : "RED";
  const unsigned long startTime = millis();
  if (Serial)
    Serial.printf("[%lu]   Writing inverted buffer to %s RAM (%lu bytes)...\n",
                  startTime, bufferName, size);

  SPI.beginTransaction(spiSettings);
  digitalWrite(_cs, LOW);
  digitalWrite(_dc, LOW);
  SPI.transfer(ramBuffer);
  digitalWrite(_dc, HIGH);
  constexpr uint16_t kChunk = 256;
  uint8_t buf[kChunk];
  for (uint32_t off = 0; off < size; off += kChunk) {
    const uint16_t len = (size - off < kChunk) ? static_cast<uint16_t>(size - off) : kChunk;
    for (uint16_t j = 0; j < len; j++) {
      buf[j] = ~data[off + j];
    }
    SPI.writeBytes(buf, len);
  }
  digitalWrite(_cs, HIGH);
  SPI.endTransaction();

  const unsigned long duration = millis() - startTime;
  if (Serial)
    Serial.printf("[%lu]   %s RAM inverted write complete (%lu ms)\n", millis(),
                  bufferName, duration);
}

void EInkDisplay::setFramebuffer(const uint8_t *bwBuffer) const {
  memcpy(frameBuffer, bwBuffer, bufferSize);
}

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
void EInkDisplay::swapBuffers() {
  uint8_t *temp = frameBuffer;
  frameBuffer = frameBufferActive;
  frameBufferActive = temp;
}
#endif

void EInkDisplay::grayscaleRevert() {
  if (!inGrayscaleMode) {
    return;
  }

  inGrayscaleMode = false;

  if (_x3Mode) {
    // X3: scrub the panel back to clean white using the OEM scrub LUT bank.
    // After differential grayscale (AA), DTM1 holds the AA LSB plane and
    // DTM2 holds the AA MSB plane — neither is a valid "previous BW frame"
    // for a normal differential refresh against. Reusing `_full` here drove
    // some pixels with the wrong waveform (BB state had no drive) and let
    // ghost text accumulate page-to-page.
    //
    // Instead we write all-white to both RAM planes, then apply the scrub
    // bank. Scrub's WW/BW pair are byte-identical (and so are its WB/BB
    // pair), meaning the controller picks the drive waveform from DTM2
    // alone and ignores DTM1 state — the same trick X4's
    // lut_grayscale_revert uses with state-coded patterns. With both
    // planes white, every pixel gets the "drive to white" waveform and the
    // panel ends in a clean known state. _x3RedRamSynced is set true
    // because DTM1 now matches DTM2 (both all-white) so the next BW page
    // turn can fast-diff cleanly.
    fillPlaneX3(CMD_X3_DTM1, 0xFF);
    sendCommand(CMD_X3_DATA_STOP);
    fillPlaneX3(CMD_X3_DTM2, 0xFF);
    sendCommand(CMD_X3_DATA_STOP);
    // CDI 0xA9 (absolute mode) — _half bank was extracted from OEM's
    // scrub/half loader (FUN_420a0e7c) which sets CDI 0xA9 before loading
    // these exact bytes. Using 0x29 (differential) here caused the controller
    // to misinterpret pixel state codes and drove unbalanced charge per pixel.
    loadLutBankX3WithCdi(0xA9, 0x07, lut_x3_vcom_half, lut_x3_ww_half,
                         lut_x3_bw_half, lut_x3_wb_half, lut_x3_bb_half);
    triggerRefreshX3(/*turnOffScreen=*/false, "(revert)");
    _x3RedRamSynced = true;
    return;
  }

  // X4: load the revert LUT and fast refresh
  setCustomLUT(true, lut_grayscale_revert);
  refreshDisplay(FAST_REFRESH);
  setCustomLUT(false);
}

void EInkDisplay::copyGrayscaleLsbBuffers(const uint8_t *lsbBuffer) {
  if (!lsbBuffer) {
    _x3GrayState.lsbValid = false;
    return;
  }

  if (_x3Mode) {
    // X3 grayscale: write LSB plane raw to "old" RAM (DTM1).
    // Y-flip in-place, bulk send, Y-flip back. The const_cast is safe because
    // the buffer is fully restored before returning.
    auto *buf = const_cast<uint8_t *>(lsbBuffer);
    uint8_t rowTmp[128];
    for (uint16_t top = 0, bot = displayHeight - 1; top < bot; top++, bot--) {
      uint8_t *rowA = buf + static_cast<uint32_t>(top) * displayWidthBytes;
      uint8_t *rowB = buf + static_cast<uint32_t>(bot) * displayWidthBytes;
      memcpy(rowTmp, rowA, displayWidthBytes);
      memcpy(rowA, rowB, displayWidthBytes);
      memcpy(rowB, rowTmp, displayWidthBytes);
    }
    sendCommand(CMD_X3_DTM1);
    sendData(buf, static_cast<uint16_t>(bufferSize));
    sendCommand(CMD_X3_DATA_STOP); // no refresh follows; commit DTM1
    for (uint16_t top = 0, bot = displayHeight - 1; top < bot; top++, bot--) {
      uint8_t *rowA = buf + static_cast<uint32_t>(top) * displayWidthBytes;
      uint8_t *rowB = buf + static_cast<uint32_t>(bot) * displayWidthBytes;
      memcpy(rowTmp, rowA, displayWidthBytes);
      memcpy(rowA, rowB, displayWidthBytes);
      memcpy(rowB, rowTmp, displayWidthBytes);
    }
    _x3GrayState.lsbValid = true;
    return;
  }
  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_BW, lsbBuffer, bufferSize);
}

void EInkDisplay::copyGrayscaleMsbBuffers(const uint8_t *msbBuffer) {
  if (!msbBuffer) {
    return;
  }

  if (_x3Mode) {
    if (!_x3GrayState.lsbValid) {
      return;
    }

    // X3 grayscale: write MSB plane raw to "new" RAM (DTM2).
    auto *buf = const_cast<uint8_t *>(msbBuffer);
    uint8_t rowTmp[128];
    for (uint16_t top = 0, bot = displayHeight - 1; top < bot; top++, bot--) {
      uint8_t *rowA = buf + static_cast<uint32_t>(top) * displayWidthBytes;
      uint8_t *rowB = buf + static_cast<uint32_t>(bot) * displayWidthBytes;
      memcpy(rowTmp, rowA, displayWidthBytes);
      memcpy(rowA, rowB, displayWidthBytes);
      memcpy(rowB, rowTmp, displayWidthBytes);
    }
    sendCommand(CMD_X3_DTM2);
    sendData(buf, static_cast<uint16_t>(bufferSize));
    sendCommand(CMD_X3_DATA_STOP); // no refresh follows; commit DTM2
    for (uint16_t top = 0, bot = displayHeight - 1; top < bot; top++, bot--) {
      uint8_t *rowA = buf + static_cast<uint32_t>(top) * displayWidthBytes;
      uint8_t *rowB = buf + static_cast<uint32_t>(bot) * displayWidthBytes;
      memcpy(rowTmp, rowA, displayWidthBytes);
      memcpy(rowA, rowB, displayWidthBytes);
      memcpy(rowB, rowTmp, displayWidthBytes);
    }
    return;
  }
  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_RED, msbBuffer, bufferSize);
}

void EInkDisplay::copyGrayscaleBuffers(const uint8_t *lsbBuffer,
                                       const uint8_t *msbBuffer) {
  if (_x3Mode) {
    copyGrayscaleLsbBuffers(lsbBuffer);
    copyGrayscaleMsbBuffers(msbBuffer);
    return;
  }
  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_BW, lsbBuffer, bufferSize);
  writeRamBuffer(CMD_WRITE_RAM_RED, msbBuffer, bufferSize);
}

void EInkDisplay::writeGrayscalePlaneStrip(GrayPlane plane, const uint8_t *rows,
                                           uint16_t yStart, uint16_t numRows) {
  if (!rows || numRows == 0)
    return;

  if (_x3Mode) {
    // X3 (UC81xx) has no SSD1677-style RAM windowing, but PTL partial-window is
    // the equivalent: window to this band, then write its rows to the DTM plane
    // (LSB -> 0x10, MSB -> 0x13).
    //
    // PTL Y is in the controller's gate space, NOT logical rows: the UC81xx
    // knows nothing about logical orientation — the full-frame path gets the
    // image upright purely by Y-flipping the whole buffer in software before a
    // gate-order write (sendPlaneX3: "scans gates upward (UD=1), first byte ->
    // bottom-left"). So logical row y lives at gate (H-1-y), and a logical band
    // [yStart..yEndLogical] occupies gates [H-1-yEndLogical .. H-1-yStart]. We
    // window that flipped gate range and emit the band's rows bottom-first
    // (highest logical row first), so the first byte lands at gate
    // (H-1-yEndLogical) holding logical yEndLogical, matching the whole-plane
    // write exactly. The old code windowed the logical range [yStart..yEnd],
    // which only coincided with gate space at the vertical center and
    // mirror-scattered every other band — banding both text AA and images
    // (issue #2190). The post-condition pass's full-screen PTL ([0..H-1]) hid
    // this because that window is its own gate-space mirror.
    //
    // Each band's data is its own CS-low burst so the SPI bus is free for
    // SD-card font reads between bands.
    const uint8_t ramCmd = (plane == GRAY_PLANE_LSB) ? CMD_X3_DTM1 : CMD_X3_DTM2;
    const uint16_t xEnd = displayWidth - 1;
    const uint16_t yEndLogical = yStart + numRows - 1;
    const uint16_t gateYStart = (displayHeight - 1) - yEndLogical;
    const uint16_t gateYEnd = (displayHeight - 1) - yStart;
    const uint8_t win[9] = {0,
                            0,
                            static_cast<uint8_t>(xEnd >> 8),
                            static_cast<uint8_t>(xEnd & 0xFF),
                            static_cast<uint8_t>(gateYStart >> 8),
                            static_cast<uint8_t>(gateYStart & 0xFF),
                            static_cast<uint8_t>(gateYEnd >> 8),
                            static_cast<uint8_t>(gateYEnd & 0xFF),
                            0x01};
    sendCommand(CMD_X3_PARTIAL_IN);
    sendCommandDataX3(CMD_X3_PARTIAL_WINDOW, win, 9);
    sendCommand(ramCmd);
    SPI.beginTransaction(spiSettings);
    digitalWrite(_dc, HIGH);
    digitalWrite(_cs, LOW);
    for (int r = static_cast<int>(numRows) - 1; r >= 0; r--)
      SPI.writeBytes(rows + static_cast<uint32_t>(r) * displayWidthBytes, displayWidthBytes);
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
    sendCommand(CMD_X3_PARTIAL_OUT);
    // X3 displayGrayBuffer gates on lsbValid; the tiled path bypasses
    // copyGrayscaleLsbBuffers, so mark it when the LSB plane lands.
    if (plane == GRAY_PLANE_LSB)
      _x3GrayState.lsbValid = true;
    return;
  }

  // X4 (SSD1677): window the RAM to just this band and write it. setRamArea
  // already maps logical y to the panel's reversed gates, and a band written
  // here lands at the same RAM rows the full-frame write would use for those
  // rows, so bands compose in any order with no reordering.
  const uint8_t ramCmd =
      (plane == GRAY_PLANE_LSB) ? CMD_WRITE_RAM_BW : CMD_WRITE_RAM_RED;
  setRamArea(0, yStart, displayWidth, numRows);
  sendCommand(ramCmd);
  sendData(rows, static_cast<uint16_t>(static_cast<uint32_t>(numRows) *
                                       displayWidthBytes));
}

#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
/**
 * In single buffer mode, this should be called with the previously written BW
 * buffer to reconstruct the RED buffer for proper differential fast refreshes
 * following a grayscale display.
 */
void EInkDisplay::cleanupGrayscaleBuffers(const uint8_t *bwBuffer) {
  if (_x3Mode) {
    if (!bwBuffer) {
      return;
    }

    // Rebase both X3 planes from restored BW buffer. Y-flip once, send to
    // both RAMs (same data), flip back.
    auto *buf = const_cast<uint8_t *>(bwBuffer);
    uint8_t rowTmp[128];
    for (uint16_t top = 0, bot = displayHeight - 1; top < bot; top++, bot--) {
      uint8_t *rowA = buf + static_cast<uint32_t>(top) * displayWidthBytes;
      uint8_t *rowB = buf + static_cast<uint32_t>(bot) * displayWidthBytes;
      memcpy(rowTmp, rowA, displayWidthBytes);
      memcpy(rowA, rowB, displayWidthBytes);
      memcpy(rowB, rowTmp, displayWidthBytes);
    }
    sendCommand(CMD_X3_DTM2);
    sendData(buf, static_cast<uint16_t>(bufferSize));
    sendCommand(CMD_X3_DATA_STOP); // commit DTM2 — no refresh follows
    sendCommand(CMD_X3_DTM1);
    sendData(buf, static_cast<uint16_t>(bufferSize));
    sendCommand(CMD_X3_DATA_STOP); // commit DTM1 — no refresh follows
    for (uint16_t top = 0, bot = displayHeight - 1; top < bot; top++, bot--) {
      uint8_t *rowA = buf + static_cast<uint32_t>(top) * displayWidthBytes;
      uint8_t *rowB = buf + static_cast<uint32_t>(bot) * displayWidthBytes;
      memcpy(rowTmp, rowA, displayWidthBytes);
      memcpy(rowA, rowB, displayWidthBytes);
      memcpy(rowB, rowTmp, displayWidthBytes);
    }

    _x3RedRamSynced = true;
    _x3ForceFullSyncNext = false;
    _x3ForcedConditionPassesNext = 0;
    inGrayscaleMode = false;
    return;
  }

  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_RED, bwBuffer, bufferSize);
  inGrayscaleMode = false;
}
#endif

void EInkDisplay::displayBuffer(RefreshMode mode, const bool turnOffScreen) {
  if (!isScreenOn && !turnOffScreen) {
    // Waking the panel from off: force HALF refresh so the wake transition
    // gets a stronger waveform than a fast differential, matching the X4
    // policy. Applies to both X4 and X3.
    mode = HALF_REFRESH;
  }

  // If currently in grayscale mode, revert first to black/white
  if (inGrayscaleMode) {
    grayscaleRevert();
  }

  if (_x3Mode) {
    // X3 update policy mirrors X4's three-tier refresh hierarchy:
    //
    //   FAST_REFRESH -> `_fast` differential (~4-frame phase). Cheap, used
    //     for most page turns. DTM1 holds the prior frame; turbo LUTs apply
    //     transition waveforms based on (DTM2, DTM1) state pairs.
    //   HALF_REFRESH -> `_half` differential (~6-frame phase, state-
    //     collapsed). Stronger than turbo because WW=BW and WB=BB make the
    //     drive depend only on the target frame (DTM2), ignoring any stale
    //     residue in DTM1. Used for the reader's periodic ghosting-cleanup
    //     cadence (`displayWithRefreshCycle`, every N pages). Faster than
    //     a full sync because it's still single-phase 1-bit-per-pixel.
    //   FULL_REFRESH -> `_full` OEM quality bank from a white baseline.
    //     Strongest drive, used at boot, after wake, and when the caller
    //     explicitly requests it. Also forced when DTM1 is unsynced (after AA).
    //
    // DTM1 ("old" RAM) on the controller stores the previous frame for
    // differential updates. POWER_ON (0x04) re-powers the charge pump when
    // needed.
    const bool fastMode = (mode == FAST_REFRESH);
    const bool halfMode = (mode == HALF_REFRESH);
    const bool forcedFullSync = _x3ForceFullSyncNext;
    const bool doFullSync = (!fastMode && !halfMode) || !_x3RedRamSynced ||
                            _x3InitialFullSyncsRemaining > 0 || forcedFullSync;
    const bool doHalfSync = halfMode && !doFullSync;

    if (Serial) {
      const char *tag = doFullSync ? "FULL" : doHalfSync ? "HALF" : "FAST";
      Serial.printf("[%lu]   X3_OEM_%s\n", millis(), tag);
    }
    _x3GrayState.lastBaseWasPartial = !doFullSync;

    if (doFullSync) {
      loadLutBankX3WithCdi(0x29, 0x07, lut_x3_vcom_full, lut_x3_ww_full,
                           lut_x3_bw_full, lut_x3_wb_full, lut_x3_bb_full);
      // Plane semantics for `_full` in differential mode: DTM1 holds the
      // "old" frame, DTM2 holds the "new" frame, and the controller diffs
      // them per pixel to pick the transition LUT (WW/BW/WB/BB).
      //
      // OEM writes old → DTM1 and new → DTM2 from a stored previous frame.
      // We don't keep a software previous-frame buffer (would cost ~60 KB
      // on a memory-constrained C3), so we use an all-white baseline in
      // DTM1 instead. Differential interpretation becomes "drive every
      // pixel from white to its current target" — black-target pixels get
      // the strong WB transition drive (cleans ghost residue), white-target
      // pixels get a light WW drive (no work needed). That's the classic
      // ghost-buster full refresh.
      //
      // The post-refresh DTM1 sync at the end of this function updates
      // DTM1 to the current frame so subsequent fast diffs work normally.
      fillPlaneX3(CMD_X3_DTM1, 0xFF);
      sendCommand(CMD_X3_DATA_STOP);
      sendPlaneX3(CMD_X3_DTM2, frameBuffer, false);
    } else if (doHalfSync) {
      // Half: _half (scrub) LUTs in absolute mode. WW=BW and WB=BB in this
      // bank, so the controller picks waveform per-pixel from the target
      // state code in DTM2/DTM1 — drive every pixel to its target
      // regardless of accumulated residue. OEM uses CDI 0xA9 with this
      // bank (FUN_420a0e7c); using 0x29 here caused unbalanced drive that
      // accumulated DC bias per pixel under repeated use.
      loadLutBankX3WithCdi(0xA9, 0x07, lut_x3_vcom_half, lut_x3_ww_half,
                           lut_x3_bw_half, lut_x3_wb_half, lut_x3_bb_half);
      sendPlaneX3(CMD_X3_DTM2, frameBuffer, false);
    } else {
      // Fast differential: turbo LUTs, DTM1 retains previous frame.
      loadLutBankX3WithCdi(0x29, 0x07, lut_x3_vcom_fast, lut_x3_ww_fast,
                           lut_x3_bw_fast, lut_x3_wb_fast, lut_x3_bb_fast);
      sendPlaneX3(CMD_X3_DTM2, frameBuffer, false);
    }

    // Note: this branch re-issues POWER_ON when doFullSync is true even if
    // the screen is already on (re-powers the charge pump for the
    // higher-current full refresh). The triggerRefreshX3 helper only
    // power-ons when !isScreenOn, so we inline the sequence here rather
    // than use the helper.
    if (!isScreenOn || doFullSync) {
      sendCommand(CMD_X3_POWER_ON);
      waitForRefresh(" X3_PON");
      isScreenOn = true;
    }
    if (Serial)
      Serial.printf("[%lu]   X3_OEM_TRIGGER=DRF\n", millis());
    sendCommand(CMD_X3_DISPLAY_REFRESH);
    waitForRefresh(" X3_DRF");
    if (turnOffScreen) {
      sendCommand(CMD_X3_POWER_OFF);
      waitForRefresh(" X3_POF");
      isScreenOn = false;
    }

    if (!fastMode)
      delay(200);

    uint8_t postConditionPasses = 0;
    if (doFullSync) {
      if (forcedFullSync)
        postConditionPasses = _x3ForcedConditionPassesNext;
      else if (_x3InitialFullSyncsRemaining == 1)
        postConditionPasses = 1;
    }

    if (postConditionPasses > 0) {
      const uint16_t xStart = 0;
      const uint16_t xEnd = static_cast<uint16_t>(displayWidth - 1);
      const uint16_t yStart = 0;
      const uint16_t yEnd = static_cast<uint16_t>(displayHeight - 1);
      const uint8_t w[9] = {static_cast<uint8_t>(xStart >> 8),
                            static_cast<uint8_t>(xStart & 0xFF),
                            static_cast<uint8_t>(xEnd >> 8),
                            static_cast<uint8_t>(xEnd & 0xFF),
                            static_cast<uint8_t>(yStart >> 8),
                            static_cast<uint8_t>(yStart & 0xFF),
                            static_cast<uint8_t>(yEnd >> 8),
                            static_cast<uint8_t>(yEnd & 0xFF),
                            0x01};

      // CDI 0xA9 (absolute) — _normal bank was extracted from OEM's
      // normal loader (FUN_420a12a0) which sets CDI 0xA9 before loading.
      loadLutBankX3WithCdi(0xA9, 0x07, lut_x3_vcom_normal,
                           lut_x3_ww_normal, lut_x3_bw_normal,
                           lut_x3_wb_normal, lut_x3_bb_normal);

      for (uint8_t i = 0; i < postConditionPasses; i++) {
        if (Serial)
          Serial.printf("[%lu]   X3_OEM_COND %u/%u\n", millis(),
                        static_cast<unsigned>(i + 1),
                        static_cast<unsigned>(postConditionPasses));
        sendCommand(CMD_X3_PARTIAL_IN);
        sendCommandDataX3(CMD_X3_PARTIAL_WINDOW, w, 9);
        sendPlaneX3(CMD_X3_DTM2, frameBuffer, false);
        sendCommand(CMD_X3_PARTIAL_OUT);
        triggerRefreshX3(/*turnOffScreen=*/false, "(cond)");
      }
    }

    // Sync DTM1 ("old" RAM) with non-inverted current frame for next fast diff.
    sendPlaneX3(CMD_X3_DTM1, frameBuffer, false);
    sendCommand(CMD_X3_DATA_STOP); // commit DTM1 — no refresh follows
    _x3RedRamSynced = true;

    // The first differential after a full garbles on X3: the controller's post-full state corrupts
    // the next fast/half diff (not a DTM1-content issue; promoting that op to a half didn't help, a
    // full did). Spend that slot here with a no-op fast of the just-displayed frame. DTM1 and DTM2
    // both hold it, so nothing visibly changes, but it leaves the controller in the post-fast state
    // so the caller's next diff (menu open, first page turn, first turn after the periodic full) is
    // clean instead of the garbling first-after-full.
    if (doFullSync) {
      loadLutBankX3WithCdi(0x29, 0x07, lut_x3_vcom_fast, lut_x3_ww_fast, lut_x3_bw_fast, lut_x3_wb_fast,
                           lut_x3_bb_fast);
      sendPlaneX3(CMD_X3_DTM2, frameBuffer, false);
      triggerRefreshX3(turnOffScreen, "(post-full settle)");
      sendPlaneX3(CMD_X3_DTM1, frameBuffer, false);
      sendCommand(CMD_X3_DATA_STOP);
    }

    if (doFullSync && _x3InitialFullSyncsRemaining > 0) {
      _x3InitialFullSyncsRemaining--;
    }
    _x3ForceFullSyncNext = false;
    _x3ForcedConditionPassesNext = 0;
    return;
  }

  // Set up full screen RAM area
  setRamArea(0, 0, displayWidth, displayHeight);

  if (mode != FAST_REFRESH) {
    // For full refresh, write to both buffers before refresh
    writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer, bufferSize);
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBuffer, bufferSize);
  } else {
    // For fast refresh, write to BW buffer only
    writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer, bufferSize);
    // In single buffer mode, the RED RAM should already contain the previous
    // frame In dual buffer mode, we write back frameBufferActive which is the
    // last frame
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBufferActive, bufferSize);
#endif
  }

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  swapBuffers();
#endif

  // Refresh the display
  refreshDisplay(mode, turnOffScreen);

#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  // In single buffer mode always sync RED RAM after refresh to prepare for next
  // fast refresh This ensures RED contains the currently displayed frame for
  // differential comparison
  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_RED, frameBuffer, bufferSize);
#endif
}

void EInkDisplay::displayBufferDriveAll(bool turnOffScreen) {
  if (turnOffScreen) {
    turnOffScreen = isScreenOn;
  }
  
  grayscaleRevert();

  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer, bufferSize);
  writeRamBufferInverted(CMD_WRITE_RAM_RED, frameBuffer, bufferSize);

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  swapBuffers();
#endif

  refreshDisplay(FAST_REFRESH, turnOffScreen);

  setRamArea(0, 0, displayWidth, displayHeight);
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  writeRamBuffer(CMD_WRITE_RAM_RED, frameBuffer, bufferSize);
#else
  writeRamBuffer(CMD_WRITE_RAM_RED, frameBufferActive, bufferSize);
#endif
}

// EXPERIMENTAL: Windowed update support
// Displays only a rectangular region of the frame buffer, preserving the rest
// of the screen. Requirements: x and w must be byte-aligned (multiples of 8
// pixels)
void EInkDisplay::displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                const bool turnOffScreen, bool partialRedSync) {
  if (Serial)
    Serial.printf("[%lu]   Displaying window at (%d,%d) size (%dx%d)\n",
                  millis(), x, y, w, h);

  // Validate bounds
  if (x + w > displayWidth || y + h > displayHeight) {
    if (Serial)
      Serial.printf("[%lu]   ERROR: Window bounds exceed display dimensions!\n",
                    millis());
    return;
  }

  // Validate byte alignment
  if (x % 8 != 0 || w % 8 != 0) {
    if (Serial)
      Serial.printf("[%lu]   ERROR: Window x and width must be byte-aligned "
                    "(multiples of 8)!\n",
                    millis());
    return;
  }

  if (!frameBuffer) {
    if (Serial)
      Serial.printf("[%lu]   ERROR: Frame buffer not allocated!\n", millis());
    return;
  }

  if (_x3Mode) {
    // X3 uses a different command set for windowed RAM addressing (0x91/0x90/
    // 0x92) than X4 (setRamArea + CMD_WRITE_RAM_*). Rather than maintain a
    // second X3-specific partial-update implementation, route X3 through the
    // shared displayBuffer pipeline. Visual result is equivalent; only
    // difference is the unchanged region of the screen also refreshes.
    // displayBuffer already handles inGrayscaleMode revert and the wake-from-
    // off HALF refresh policy.
    displayBuffer(FAST_REFRESH, turnOffScreen);
    return;
  }

  // displayWindow is not supported while the rest of the screen has grayscale
  // content, revert it
  if (inGrayscaleMode) {
    grayscaleRevert();
  }

  // Calculate window buffer size
  const uint16_t windowWidthBytes = w / 8;
  const uint32_t windowBufferSize = windowWidthBytes * h;

  if (Serial)
    Serial.printf("[%lu]   Window buffer size: %lu bytes (%d x %d pixels)\n",
                  millis(), windowBufferSize, w, h);

  // Allocate temporary buffer on stack
  std::vector<uint8_t> windowBuffer(windowBufferSize);

  // Extract window region from frame buffer
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t srcY = y + row;
    const uint16_t srcOffset = srcY * displayWidthBytes + (x / 8);
    const uint16_t dstOffset = row * windowWidthBytes;
    memcpy(&windowBuffer[dstOffset], &frameBuffer[srcOffset], windowWidthBytes);
  }

  // Configure RAM area for window
  setRamArea(x, y, w, h);

  // Write to BW RAM (current frame)
  writeRamBuffer(CMD_WRITE_RAM_BW, windowBuffer.data(), windowBufferSize);

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  // Dual buffer: Extract window from frameBufferActive (previous frame)
  std::vector<uint8_t> previousWindowBuffer(windowBufferSize);
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t srcY = y + row;
    const uint16_t srcOffset = srcY * displayWidthBytes + (x / 8);
    const uint16_t dstOffset = row * windowWidthBytes;
    memcpy(&previousWindowBuffer[dstOffset], &frameBufferActive[srcOffset],
           windowWidthBytes);
  }
  writeRamBuffer(CMD_WRITE_RAM_RED, previousWindowBuffer.data(),
                 windowBufferSize);
#endif

  // Perform fast refresh
  refreshDisplay(FAST_REFRESH, turnOffScreen);

  if (partialRedSync) {
    setRamArea(x, y, w, h);
    writeRamBuffer(CMD_WRITE_RAM_RED, windowBuffer.data(), windowBufferSize);
  } else {
    setRamArea(0, 0, displayWidth, displayHeight);
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBuffer, bufferSize);
#else
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBufferActive, bufferSize);
#endif
  }

  if (Serial)
    Serial.printf("[%lu]   Window display complete\n", millis());
}

void EInkDisplay::displayGrayBuffer(const bool turnOffScreen,
                                    const unsigned char *lut,
                                    const bool factoryMode) {
  if (_x3Mode) {
    // X3 uses a different command set from X4 — command bytes 0x20-0x22 are
    // LUT registers on X3 but CTRL/activation commands on X4. The X4 path
    // (setCustomLUT + refreshDisplay) cannot be used on X3.
    drawGrayscale = false;
    if (!_x3GrayState.lsbValid) {
      return;
    }

    // Match X4 semantics: differential grayscale leaves the gray bank loaded
    // in the LUT registers, so a subsequent BW page turn must run
    // grayscaleRevert first to drive pixels back to clean BW. Factory
    // absolute mode handles its own cleanup, so no revert is needed there.
    inGrayscaleMode = !factoryMode;

    if (factoryMode) {
      // Factory absolute mode - use image/factory LUTs.
      // Note: X3 has no separate fast factory LUTs. Fast mode falls back to
      // quality (lut_x3_*_full) with a warning.
      if (Serial) {
        const char *modeTag = (lut == lut_factory_fast)
                                  ? "factory_fast (fallback to quality)"
                                  : "factory_quality";
        Serial.printf("[%lu]   X3_GRAY_MODE=%s\n", millis(), modeTag);
      }
      // CDI 0x29 (differential) — _full bank's OEM CDI is 0x29 per
      // FUN_420a1218 / FUN_420a14a0. Factory-mode grayscale loaders in the
      // OEM firmware use this same bank with the same CDI.
      loadLutBankX3WithCdi(0x29, 0x07, lut_x3_vcom_full, lut_x3_ww_full,
                           lut_x3_bw_full, lut_x3_wb_full, lut_x3_bb_full);
    } else {
      // Differential grayscale mode — community 4-level LUT (see definition
      // above). CDI 0x29,0x07 (OEM full-refresh CDI) drives the border cleanly
      // during the 4-level cell refresh.
      if (Serial)
        Serial.printf("[%lu]   X3_GRAY_MODE=community_grayscale\n", millis());
      loadLutBankX3WithCdi(0x29, 0x07, lut_x3_vcom_gc, lut_x3_ww_gc,
                           lut_x3_bw_gc, lut_x3_wb_gc, lut_x3_bb_gc);
    }

    triggerRefreshX3(turnOffScreen, "(gray)");

    _x3RedRamSynced = false;
    _x3ForceFullSyncNext = false;
    _x3ForcedConditionPassesNext = 0;

    _x3GrayState.lsbValid = false;
    return;
  }
  drawGrayscale = false;
  // Differential mode keeps this fallback set for callers that do not re-sync
  // controller RAM with cleanupGrayscaleBuffers(). Reader AA does perform that
  // cleanup after restoring its BW frame, which clears this flag before the
  // next BW page turn.
  inGrayscaleMode = !factoryMode;

  const unsigned char *selectedLut = lut;
  if (selectedLut == nullptr) {
    selectedLut = factoryMode ? lut_factory_quality : lut_grayscale;
  }
  setCustomLUT(true, selectedLut);

  if (factoryMode) {
    // Factory absolute mode: explicit full power cycle sequence.
    // CRITICAL: reset CTRL1 to normal — a prior HALF_REFRESH leaves CTRL1=0x40
    // (BYPASS_RED) which would ignore RED RAM and break 4-level grayscale.
    sendCommand(CMD_DISPLAY_UPDATE_CTRL1);
    sendData(CTRL1_NORMAL); // 0x00
    // 0xC7 = CLOCK_ON(0x80) + ANALOG_ON(0x40) + DISPLAY_START(0x04) +
    //        ANALOG_OFF(0x02) + CLOCK_OFF(0x01) — full self-contained power
    //        cycle.
    sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
    sendData(0xC7);
    sendCommand(CMD_MASTER_ACTIVATION);
    waitWhileBusy("factory_gray");
    isScreenOn = false; // 0xC7 always powers down after update
  } else {
    refreshDisplay(FAST_REFRESH, turnOffScreen);
  }

  setCustomLUT(false);
}

void EInkDisplay::refreshDisplay(const RefreshMode mode,
                                 const bool turnOffScreen) {
  if (_x3Mode) {
    displayBuffer(mode, turnOffScreen);
    return;
  }

  // Configure Display Update Control 1
  sendCommand(CMD_DISPLAY_UPDATE_CTRL1);
  sendData((mode == FAST_REFRESH)
               ? CTRL1_NORMAL
               : CTRL1_BYPASS_RED); // Configure buffer comparison mode

  // best guess at display mode bits:
  // bit | hex | name                    | effect
  // ----+-----+--------------------------+-------------------------------------------
  // 7   | 80  | CLOCK_ON                | Start internal oscillator
  // 6   | 40  | ANALOG_ON               | Enable analog power rails (VGH/VGL
  // drivers) 5   | 20  | TEMP_LOAD               | Load temperature (internal
  // or I2C) 4   | 10  | LUT_LOAD                | Load waveform LUT 3   | 08  |
  // MODE_SELECT             | Mode 1/2 2   | 04  | DISPLAY_START           |
  // Run display 1   | 02  | ANALOG_OFF_PHASE        | Shutdown step 1
  // (undocumented) 0   | 01  | CLOCK_OFF               | Disable internal
  // oscillator

  // Select appropriate display mode based on refresh type
  uint8_t displayMode = 0x00;

  // Enable counter and analog if not already on
  if (!isScreenOn) {
    isScreenOn = true;
    displayMode |= 0xC0; // Set CLOCK_ON and ANALOG_ON bits
  }

  // Turn off screen if requested
  if (turnOffScreen) {
    isScreenOn = false;
    displayMode |= 0x03; // Set ANALOG_OFF_PHASE and CLOCK_OFF bits
  }

  if (mode == FULL_REFRESH) {
    displayMode |= 0x34;
  } else if (mode == HALF_REFRESH) {
    // Write high temp to the register for a faster refresh
    sendCommand(CMD_WRITE_TEMP);
    sendData(0x5A);
    displayMode |= 0xD4;
  } else { // FAST_REFRESH
    displayMode |= customLutActive ? 0x0C : 0x1C;
  }

  // Power on and refresh display
  const char *refreshType = (mode == FULL_REFRESH)   ? "full"
                            : (mode == HALF_REFRESH) ? "half"
                                                     : "fast";
  if (Serial)
    Serial.printf("[%lu]   Powering on display 0x%02X (%s refresh)...\n",
                  millis(), displayMode, refreshType);
  sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
  sendData(displayMode);

  sendCommand(CMD_MASTER_ACTIVATION);

  // Wait for display to finish updating
  if (Serial)
    Serial.printf("[%lu]   Waiting for display refresh...\n", millis());
  waitWhileBusy(refreshType);
}

void EInkDisplay::setCustomLUT(const bool enabled,
                               const unsigned char *lutData) {
  if (enabled) {
    if (Serial)
      Serial.printf("[%lu]   Loading custom LUT...\n", millis());

    // Load custom LUT (first 105 bytes: VS + TP/RP + frame rate)
    sendCommand(CMD_WRITE_LUT);
    for (uint16_t i = 0; i < 105; i++) {
      sendData(pgm_read_byte(&lutData[i]));
    }

    // Set voltage values from bytes 105-109
    sendCommand(CMD_GATE_VOLTAGE); // VGH
    sendData(pgm_read_byte(&lutData[105]));

    sendCommand(CMD_SOURCE_VOLTAGE);        // VSH1, VSH2, VSL
    sendData(pgm_read_byte(&lutData[106])); // VSH1
    sendData(pgm_read_byte(&lutData[107])); // VSH2
    sendData(pgm_read_byte(&lutData[108])); // VSL

    sendCommand(CMD_WRITE_VCOM); // VCOM
    sendData(pgm_read_byte(&lutData[109]));

    customLutActive = true;
    if (Serial)
      Serial.printf("[%lu]   Custom LUT loaded\n", millis());
  } else {
    customLutActive = false;
    if (Serial)
      Serial.printf("[%lu]   Custom LUT disabled\n", millis());
  }
}

void EInkDisplay::deepSleep() {
  if (Serial)
    Serial.printf("[%lu]   Preparing display for deep sleep...\n", millis());

  // First, power down the display properly
  // This shuts down the analog power rails and clock
  if (isScreenOn) {
    sendCommand(CMD_DISPLAY_UPDATE_CTRL1);
    sendData(CTRL1_BYPASS_RED); // Normal mode

    sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
    sendData(0x03); // Set ANALOG_OFF_PHASE (bit 1) and CLOCK_OFF (bit 0)

    sendCommand(CMD_MASTER_ACTIVATION);

    // Wait for the power-down sequence to complete
    waitWhileBusy(" display power-down");

    isScreenOn = false;
  }

  // Now enter deep sleep mode
  if (Serial)
    Serial.printf("[%lu]   Entering deep sleep mode...\n", millis());
  sendCommand(CMD_DEEP_SLEEP);
  sendData(0x01); // Enter deep sleep
}

void EInkDisplay::saveFrameBufferAsPBM(const char *filename) {
#ifndef ARDUINO
  const uint8_t *buffer = getFrameBuffer();

  std::ofstream file(filename, std::ios::binary);
  if (!file) {
    if (Serial)
      Serial.printf("Failed to open %s for writing\n", filename);
    return;
  }

  // Rotate the image 90 degrees counterclockwise when saving
  // Original buffer: 800x480 (landscape)
  // Output image: 480x800 (portrait)
  const int DISPLAY_WIDTH_LOCAL = DISPLAY_WIDTH;   // 800
  const int DISPLAY_HEIGHT_LOCAL = DISPLAY_HEIGHT; // 480
  const int DISPLAY_WIDTH_BYTES_LOCAL = DISPLAY_WIDTH_LOCAL / 8;

  file << "P4\n"; // Binary PBM
  file << DISPLAY_HEIGHT_LOCAL << " " << DISPLAY_WIDTH_LOCAL << "\n";

  // Create rotated buffer
  std::vector<uint8_t> rotatedBuffer(
      (DISPLAY_HEIGHT_LOCAL / 8) * DISPLAY_WIDTH_LOCAL, 0);

  for (int outY = 0; outY < DISPLAY_WIDTH_LOCAL; outY++) {
    for (int outX = 0; outX < DISPLAY_HEIGHT_LOCAL; outX++) {
      int inX = outY;
      int inY = DISPLAY_HEIGHT_LOCAL - 1 - outX;

      int inByteIndex = inY * DISPLAY_WIDTH_BYTES_LOCAL + (inX / 8);
      int inBitPosition = 7 - (inX % 8);
      bool isWhite = (buffer[inByteIndex] >> inBitPosition) & 1;

      int outByteIndex = outY * (DISPLAY_HEIGHT_LOCAL / 8) + (outX / 8);
      int outBitPosition = 7 - (outX % 8);
      if (!isWhite) { // Invert: e-ink white=1 -> PBM black=1
        rotatedBuffer[outByteIndex] |= (1 << outBitPosition);
      }
    }
  }

  file.write(reinterpret_cast<const char *>(rotatedBuffer.data()),
             rotatedBuffer.size());
  file.close();
  if (Serial)
    Serial.printf("Saved framebuffer to %s\n", filename);
#else
  (void)filename;
  if (Serial)
    Serial.println("saveFrameBufferAsPBM is not supported on Arduino builds.");
#endif
}
