/* SPDX-License-Identifier: BSD-2-Clause */
/* Real logger with anonymous host mappings. Never operates on a phone. */
#define _GNU_SOURCE
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MielDebugLog.h>
#include <MielNativeProfile.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

VOID EFIAPI MemoryFence (VOID) { __sync_synchronize (); }
VOID *EFIAPI ZeroMem (VOID *Buffer, UINTN Bytes) { return memset (Buffer, 0, Bytes); }
RETURN_STATUS EFIAPI MielDebugConstructor (VOID);

int main (void)
{
  const size_t frame_bytes = (MIEL_NATIVE_FB_STRIDE * MIEL_NATIVE_FB_HEIGHT + 4095) & ~4095UL;
  void *frame = mmap ((void *)MIEL_NATIVE_FB_BASE, frame_bytes, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  void *shared = mmap ((void *)MIEL_NATIVE_DEBUG_BASE, 4096, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  assert (frame == (void *)MIEL_NATIVE_FB_BASE && shared == (void *)MIEL_NATIVE_DEBUG_BASE);
  // Any access to the unvalidated shared page here must fail in the OS.
  assert (MielDebugStartEarly ());
  assert (MielDebugConstructor () == RETURN_SUCCESS);
  size_t lit = 0;
  UINT32 *pixels = frame;
  for (size_t i = 0; i < frame_bytes / 4; i++) if (pixels[i]) lit++;
  assert (lit > 0);
  assert (mprotect (shared, 4096, PROT_READ | PROT_WRITE) == 0);
  memset (shared, 0xcc, 4096);
  assert (MielDebugStart ());
  assert (MielDebugConstructor () == RETURN_SUCCESS);
  assert (*(UINT32 *)shared != 0xccccccccU);
  assert (munmap (frame, frame_bytes) == 0 && munmap (shared, 4096) == 0);
  puts ("Early SEC display logger works with the entire shared log page PROT_NONE; validated-page handover passed.");
  return 0;
}
