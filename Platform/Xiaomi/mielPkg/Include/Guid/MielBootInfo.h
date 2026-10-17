/** @file
  Original SEC-to-DXE handoff, without private device identifiers.
  SPDX-License-Identifier: BSD-2-Clause
**/
#ifndef MIEL_BOOT_INFO_H
#define MIEL_BOOT_INFO_H
#include <Uefi.h>

#define MIEL_BOOT_INFO_SIGNATURE 0x49424c4dU
#define MIEL_BOOT_INFO_REVISION 1
#define MIEL_BOOT_MEMORY_READY 1U
#define MIEL_BOOT_EXPECTED_DISPLAY_PROFILE 2U
#define MIEL_BOOT_WATCHDOG_STOPPED 4U

typedef struct {
  UINT32 Signature;
  UINT32 Revision;
  UINT64 InitialEl;
  UINT64 CounterFrequency;
  UINT64 FramebufferBase;
  UINT64 FramebufferBytes;
  UINT32 Width;
  UINT32 Height;
  UINT32 Stride;
  UINT8 RedShift;
  UINT8 BlueShift;
  BOOLEAN OpaqueAlpha;
  UINT8 Reserved;
  UINT64 InitialSctlr;
  UINT32 PsciVersion;
  UINT32 Flags;
} MIEL_BOOT_INFO;

extern EFI_GUID gMielBootInfoHobGuid;
extern EFI_GUID gMielGopDeviceGuid;
#endif
