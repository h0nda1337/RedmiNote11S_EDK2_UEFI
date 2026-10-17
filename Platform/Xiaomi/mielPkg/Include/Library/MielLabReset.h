/* SPDX-License-Identifier: BSD-2-Clause */
/* Original laboratory reset intent, using the MT6781 stock DT binding.
 * /toprgu@10007000/reboot-mode: offset 0x24, mask 0xf, bootloader value 3.
 * This is a reset-mode register, not a persistent partition write.
 */
#ifndef MIEL_LAB_RESET_H
#define MIEL_LAB_RESET_H
#include <MielNativeProfile.h>
#include <Library/BaseLib.h>

STATIC inline BOOLEAN MielSelectBootloaderReset (VOID)
{
  volatile UINT32 *Mode = (volatile UINT32 *)(UINTN)
    (MIEL_NATIVE_WDT_BASE + MIEL_NATIVE_REBOOT_MODE_OFFSET);
  *Mode = (*Mode & ~MIEL_NATIVE_REBOOT_MODE_MASK) | MIEL_NATIVE_REBOOT_BOOTLOADER;
  MemoryFence ();
  return (BOOLEAN)((*Mode & MIEL_NATIVE_REBOOT_MODE_MASK) == MIEL_NATIVE_REBOOT_BOOTLOADER);
}
#endif
