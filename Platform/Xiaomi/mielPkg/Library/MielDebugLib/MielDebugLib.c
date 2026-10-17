/** @file
  Original display-only laboratory DebugLib and hand-drawn 5x7 glyphs.
  Shared cursor state lives in a reserved RAM page, never in a partition.
  SPDX-License-Identifier: BSD-2-Clause
**/
#include <Uefi.h>
#include <MielNativeProfile.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/MielDebugLog.h>
#include <Library/MielFrameBufferLib.h>

#define LOG_SIGNATURE 0x31474c4dU
#define FIRST_ROW 320U
#define CELL_WIDTH 12U
#define CELL_HEIGHT 16U
#define COLUMNS (MIEL_NATIVE_FB_WIDTH / CELL_WIDTH)
#define ROWS ((MIEL_NATIVE_FB_HEIGHT - FIRST_ROW) / CELL_HEIGHT)
#define FRAME_BYTES (MIEL_NATIVE_FB_STRIDE * MIEL_NATIVE_FB_HEIGHT)
#define LOG_LEVELS (DEBUG_INIT | DEBUG_WARN | DEBUG_LOAD | DEBUG_INFO | DEBUG_DISPATCH | DEBUG_ERROR)

typedef struct {
  UINT32 Signature;
  UINT32 Revision;
  UINT32 Busy;
  UINT32 Column;
  UINT32 Row;
  MIEL_FB_CONTEXT Frame;
} LOG_STATE;
STATIC_ASSERT (sizeof (LOG_STATE) <= 4096, "Log state exceeds its reserved page");
STATIC BOOLEAN mReady;
STATIC BOOLEAN mUseEarlyState;
STATIC LOG_STATE mEarlyState;

typedef struct { CHAR8 Character; UINT8 Rows[7]; } GLYPH;
STATIC CONST GLYPH mGlyphs[] = {
  {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
  {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
  {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
  {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
  {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,2,18,12}},
  {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
  {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,25,21,19,19,17}},
  {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
  {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
  {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
  {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
  {'W',{17,17,17,21,21,27,17}}, {'X',{17,17,10,4,10,17,17}},
  {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
  {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
  {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
  {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
  {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
  {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
  {':',{0,4,4,0,4,4,0}}, {'.',{0,0,0,0,0,4,4}},
  {',',{0,0,0,0,4,4,8}}, {'-',{0,0,0,31,0,0,0}},
  {'_',{0,0,0,0,0,0,31}}, {'=',{0,0,31,0,31,0,0}},
  {'/',{1,2,2,4,8,8,16}}, {'\\',{16,8,8,4,2,2,1}},
  {'[',{14,8,8,8,8,8,14}}, {']',{14,2,2,2,2,2,14}},
  {'(',{2,4,8,8,8,4,2}}, {')',{8,4,2,2,2,4,8}},
  {'+',{0,4,4,31,4,4,0}}, {'*',{0,17,10,31,10,17,0}},
  {'?',{14,17,1,2,4,0,4}}, {'!',{4,4,4,4,4,0,4}},
  {'<',{2,4,8,16,8,4,2}}, {'>',{8,4,2,1,2,4,8}},
  {'%',{25,26,2,4,8,11,19}}, {'#',{10,10,31,10,31,10,10}},
  {' ',{0,0,0,0,0,0,0}}
};

STATIC LOG_STATE *State (VOID)
{
  LOG_STATE *S = mUseEarlyState ? &mEarlyState : (LOG_STATE *)(UINTN)MIEL_NATIVE_DEBUG_BASE;
  if (S->Signature != LOG_SIGNATURE || S->Revision != 1 || S->Column >= COLUMNS ||
      S->Row >= ROWS || S->Frame.Pixels != (volatile UINT32 *)(UINTN)MIEL_NATIVE_FB_BASE ||
      S->Frame.MappedBytes != FRAME_BYTES || S->Frame.Layout.Width != MIEL_NATIVE_FB_WIDTH ||
      S->Frame.Layout.Height != MIEL_NATIVE_FB_HEIGHT ||
      S->Frame.Layout.StrideBytes != MIEL_NATIVE_FB_STRIDE ||
      S->Frame.Layout.RedShift != MIEL_NATIVE_FB_RED_SHIFT ||
      S->Frame.Layout.BlueShift != MIEL_NATIVE_FB_BLUE_SHIFT ||
      !S->Frame.Layout.OpaqueAlpha) return NULL;
  return S;
}

STATIC VOID Fill (LOG_STATE *S, EFI_GRAPHICS_OUTPUT_BLT_PIXEL Color,
                  UINTN X, UINTN Y, UINTN Width, UINTN Height)
{
  MielFbBlt (&S->Frame, &Color, sizeof (Color), EfiBltVideoFill,
             0, 0, X, Y, Width, Height, 0);
}

STATIC VOID NewLine (LOG_STATE *S)
{
  S->Column = 0;
  if (++S->Row == ROWS) {
    Fill (S, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){0,0,0,0}, 0, FIRST_ROW,
          COLUMNS * CELL_WIDTH, ROWS * CELL_HEIGHT);
    S->Row = 0;
  }
}

STATIC VOID Character (LOG_STATE *S, CHAR8 C, EFI_GRAPHICS_OUTPUT_BLT_PIXEL Color)
{
  if (C == '\r') { S->Column = 0; return; }
  if (C == '\n') { NewLine (S); return; }
  if (C == '\t') {
    for (UINTN I = 0; I < 4; I++) Character (S, ' ', Color);
    return;
  }
  if (C >= 'a' && C <= 'z') C = (CHAR8)(C - 'a' + 'A');
  CONST GLYPH *Glyph = NULL;
  for (UINTN I = 0; I < ARRAY_SIZE (mGlyphs); I++)
    if (mGlyphs[I].Character == C) { Glyph = &mGlyphs[I]; break; }
  if (Glyph == NULL) {
    for (UINTN I = 0; I < ARRAY_SIZE (mGlyphs); I++)
      if (mGlyphs[I].Character == '?') { Glyph = &mGlyphs[I]; break; }
  }
  if (Glyph == NULL) return;
  UINTN X = S->Column * CELL_WIDTH, Y = FIRST_ROW + S->Row * CELL_HEIGHT;
  Fill (S, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){0,0,0,0}, X, Y, CELL_WIDTH, CELL_HEIGHT);
  for (UINTN Row = 0; Row < 7; Row++)
    for (UINTN Bit = 0; Bit < 5; Bit++)
      if (Glyph->Rows[Row] & (1U << (4 - Bit))) Fill (S, Color, X + Bit * 2, Y + Row * 2, 2, 2);
  if (++S->Column == COLUMNS) NewLine (S);
}

STATIC VOID Write (LOG_STATE *S, CONST CHAR8 *Buffer, UINTN Bytes, UINTN Level)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Color = (Level & DEBUG_ERROR) ?
    (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){0,0,255,0} : (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){255,255,255,0};
  for (UINTN I = 0; I < Bytes && I < 255; I++) Character (S, Buffer[I], Color);
  MemoryFence ();
}

BOOLEAN EFIAPI MielDebugStart (VOID)
{
  mUseEarlyState = FALSE;
  LOG_STATE *S = (LOG_STATE *)(UINTN)MIEL_NATIVE_DEBUG_BASE;
  MIEL_FB_LAYOUT Layout = {MIEL_NATIVE_FB_WIDTH, MIEL_NATIVE_FB_HEIGHT, MIEL_NATIVE_FB_STRIDE,
    MIEL_NATIVE_FB_RED_SHIFT, MIEL_NATIVE_FB_BLUE_SHIFT, TRUE};
  ZeroMem (S, 4096);
  if (!MielFbInitialize (&S->Frame, (VOID *)(UINTN)MIEL_NATIVE_FB_BASE, FRAME_BYTES, &Layout)) return FALSE;
  S->Revision = 1;
  Fill (S, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){0,0,0,0}, 0, FIRST_ROW,
        COLUMNS * CELL_WIDTH, ROWS * CELL_HEIGHT);
  MemoryFence (); S->Signature = LOG_SIGNATURE; MemoryFence ();
  mReady = TRUE;
  Write (S, "MIEL ORIGINAL SEC/DXE DISPLAY LOG\n", 34, DEBUG_INFO);
  return TRUE;
}

BOOLEAN EFIAPI MielDebugStartEarly (VOID)
{
  LOG_STATE *S = &mEarlyState;
  MIEL_FB_LAYOUT Layout = {MIEL_NATIVE_FB_WIDTH, MIEL_NATIVE_FB_HEIGHT, MIEL_NATIVE_FB_STRIDE,
    MIEL_NATIVE_FB_RED_SHIFT, MIEL_NATIVE_FB_BLUE_SHIFT, TRUE};
  ZeroMem (S, sizeof (*S));
  if (!MielFbInitialize (&S->Frame, (VOID *)(UINTN)MIEL_NATIVE_FB_BASE, FRAME_BYTES, &Layout)) return FALSE;
  S->Revision = 1;
  Fill (S, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){0,0,0,0}, 0, FIRST_ROW,
        COLUMNS * CELL_WIDTH, ROWS * CELL_HEIGHT);
  MemoryFence (); S->Signature = LOG_SIGNATURE; MemoryFence ();
  mUseEarlyState = TRUE;
  mReady = TRUE;
  CONST CHAR8 Banner[] = "MIEL ORIGINAL SEC EARLY DISPLAY LOG\n";
  Write (S, Banner, sizeof (Banner) - 1, DEBUG_INFO);
  return TRUE;
}

RETURN_STATUS EFIAPI MielDebugConstructor (VOID)
{
  // SEC explicitly starts the log after validation. The core binds that same
  // reserved page from its constructor; early SEC debug calls stay disabled.
  mReady = (BOOLEAN)(State () != NULL);
  return RETURN_SUCCESS;
}

STATIC LOG_STATE *ReadyState (VOID)
{
#ifdef MIEL_LOG_EARLY_CORE
  // This variant is linked only into the core, after SEC validated and reserved
  // the page. Its early memory assertions precede library constructors.
  if (!mReady) mReady = (BOOLEAN)(State () != NULL);
#endif
  return mReady ? State () : NULL;
}

VOID EFIAPI DebugVPrint (UINTN ErrorLevel, CONST CHAR8 *Format, VA_LIST Marker)
{
  CHAR8 Buffer[256];
  LOG_STATE *S = ReadyState ();
  if (S == NULL || S->Busy || Format == NULL || !(ErrorLevel & LOG_LEVELS)) return;
  S->Busy = 1; MemoryFence ();
  UINTN Bytes = AsciiVSPrint (Buffer, sizeof (Buffer), Format, Marker);
  Write (S, Buffer, Bytes, ErrorLevel);
  S->Busy = 0; MemoryFence ();
}

VOID EFIAPI DebugPrint (UINTN ErrorLevel, CONST CHAR8 *Format, ...)
{
  VA_LIST Marker;
  VA_START (Marker, Format); DebugVPrint (ErrorLevel, Format, Marker); VA_END (Marker);
}

VOID EFIAPI DebugBPrint (UINTN ErrorLevel, CONST CHAR8 *Format, BASE_LIST Marker)
{
  CHAR8 Buffer[256];
  LOG_STATE *S = ReadyState ();
  if (S == NULL || S->Busy || Format == NULL || !(ErrorLevel & LOG_LEVELS)) return;
  S->Busy = 1; MemoryFence ();
  UINTN Bytes = AsciiBSPrint (Buffer, sizeof (Buffer), Format, Marker);
  Write (S, Buffer, Bytes, ErrorLevel);
  S->Busy = 0; MemoryFence ();
}

VOID EFIAPI DebugAssert (CONST CHAR8 *FileName, UINTN LineNumber, CONST CHAR8 *Description)
{
  STATIC BOOLEAN Asserting;
  if (Asserting) return;
  Asserting = TRUE;
  // Do not put the host's full build path on the physical screen.
  CONST CHAR8 *Base = FileName == NULL ? "UNKNOWN" : FileName;
  if (FileName != NULL)
    for (UINTN I = 0; I < 1024 && FileName[I]; I++)
      if (FileName[I] == '/' || FileName[I] == '\\') Base = FileName + I + 1;
  DebugPrint (DEBUG_ERROR, "\nASSERT %a:%u %a\n", Base, (UINT32)LineNumber,
              Description == NULL ? "UNKNOWN" : Description);
  for (;;) CpuDeadLoop ();
}

VOID *EFIAPI DebugClearMemory (VOID *Buffer, UINTN Length)
{
  if (Buffer == NULL || Length > MAX_UINTN - (UINTN)Buffer) {
    DebugAssert ("MielDebugLib.c", 0, "INVALID CLEAR RANGE");
    return NULL;
  }
  return SetMem (Buffer, Length, 0xaf);
}
BOOLEAN EFIAPI DebugAssertEnabled (VOID) { return TRUE; }
BOOLEAN EFIAPI DebugPrintEnabled (VOID) { return TRUE; }
BOOLEAN EFIAPI DebugCodeEnabled (VOID) { return TRUE; }
BOOLEAN EFIAPI DebugClearMemoryEnabled (VOID) { return FALSE; }
BOOLEAN EFIAPI DebugPrintLevelEnabled (UINTN Level) { return (BOOLEAN)((Level & LOG_LEVELS) != 0); }
