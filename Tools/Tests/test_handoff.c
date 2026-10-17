/* SPDX-License-Identifier: BSD-2-Clause */
#include <Library/MielHandoffLib.h>
#include <Library/MielFdtLib.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static UINT8 *load(const char *path, UINTN *size)
{
  FILE *f = fopen(path, "rb");
  assert(f && fseek(f, 0, SEEK_END) == 0);
  long n = ftell(f);
  assert(n >= 40 && n <= 2 * 1024 * 1024 && fseek(f, 0, SEEK_SET) == 0);
  UINT8 *p = malloc((size_t)n);
  assert(p && fread(p, 1, (size_t)n, f) == (size_t)n);
  fclose(f); *size = (UINTN)n; return p;
}

static VOID EFIAPI trace_status(CONST CHAR8 *reason, CONST CHAR8 *path,
                               CONST CHAR8 *property, CONST VOID *value,
                               UINT32 bytes, VOID *context)
{
  unsigned *calls = context;
  assert(strcmp(reason, "RESOURCE_STATUS") == 0);
  assert(strcmp(path, "/memory@40000000") == 0 && strcmp(property, "status") == 0);
  assert(bytes == 5 && memcmp(value, "fail", 5) == 0);
  (*calls)++;
}

static void fixture_checks(UINT8 *data, UINTN size)
{
  MIEL_BOOT_HANDOFF info, before;
  MIEL_FDT_VIEW view;
  CONST VOID *value;
  UINT32 bytes;
  UINT8 *changed = malloc(size);
  assert(changed && MielInspectHandoff(data, size, 0x57300000, &info) == EFI_SUCCESS);
  assert(info.RamCount == 1 && info.Ram[0].Base == 0x40000000 && info.Ram[0].Length == 0x180000000ULL);
  assert(info.ReservedCount == 6 && info.DynamicReservationCount == 1 && info.UnresolvedReservationCount == 1);
  assert(info.EmptyReservationNodeCount == 1);
  assert(info.ReservedFlags[0] == (MIEL_RESERVATION_NO_MAP | MIEL_RESERVATION_MEMRESERVE));
  assert(info.FdtPhysicalAddressRecorded && info.PsciSmcDeclared);
  assert(info.NativeFramebufferPresent && info.NativePanelFound && info.NativeFramebufferCoveredByReservation);
  assert(info.NativeFramebuffer.Base == 0x90000000 && info.NativeFramebuffer.Length == 0x2000000);
  assert(info.SplitFramebufferPresent && info.FramebufferDeclarationsDisagree);
  assert(MielLoadRegionIsUsable(TRUE, 0x43000000, 0x4000000, info.Ram, info.RamCount,
                               info.Reserved, info.ReservedCount));
  assert(!MielLoadRegionIsUsable(TRUE, 0x78800000, 4096, info.Ram, info.RamCount,
                                info.Reserved, info.ReservedCount));
  assert(!MielLoadRegionIsUsable(TRUE, 0x57300000, 4096, info.Ram, info.RamCount,
                                info.Reserved, info.ReservedCount));
  // The LK quirk must retain the entire SSPM reservation and its no-map flag.
  assert(!MielLoadRegionIsUsable(TRUE, 0x57400000, 0x100000, info.Ram, info.RamCount,
                                info.Reserved, info.ReservedCount));
  unsigned sspm_ranges = 0;
  for (UINTN i = 0; i < info.ReservedCount; i++)
    if (info.Reserved[i].Base == 0x57400000 && info.Reserved[i].Length == 0x100000) {
      assert(info.ReservedFlags[i] == MIEL_RESERVATION_NO_MAP);
      sspm_ranges++;
    }
  assert(sspm_ranges == 1);
  before = info;
  unsigned successful_trace_calls = 0;
  assert(MielInspectHandoffWithTrace(data, size, 0x57300000, &info,
                                    trace_status, &successful_trace_calls) == EFI_SUCCESS);
  assert(successful_trace_calls == 0 && memcmp(&info, &before, sizeof info) == 0);
  assert(MielInspectHandoff(data, size, 0x200000000ULL, &info) == EFI_COMPROMISED_DATA);
  assert(memcmp(&info, &before, sizeof info) == 0);
  // Exact, length-bounded compatibility: never accept uppercase, prefixes,
  // embedded NULs or an unknown four-byte status as an enabled reservation.
  static const UINT8 invalid_status[][4] = {{'O','K','A','Y'}, {'o','k',0,0},
                                           {'o','k','a',0}, {'f','a','i','l'}};
  for (UINTN i = 0; i < sizeof invalid_status / sizeof invalid_status[0]; i++) {
    memcpy(changed, data, size);
    assert(MielFdtOpen(changed, size, &view));
    assert(MielFdtProperty(&view, "/reserved-memory/reserve-memory-sspm_share", "status",
                           &value, &bytes) == EFI_SUCCESS && bytes == 4);
    memcpy(changed + ((const UINT8 *)value - changed), invalid_status[i], 4);
    assert(MielInspectHandoff(changed, size, 0, &info) == EFI_UNSUPPORTED);
    assert(memcmp(&info, &before, sizeof info) == 0);
  }
  // Do not apply the reservation quirk to a RAM bank. Shorten the five-byte
  // property in place and use a valid FDT_NOP for the freed aligned word.
  memcpy(changed, data, size);
  assert(MielFdtOpen(changed, size, &view));
  assert(MielFdtProperty(&view, "/memory@40000000", "status", &value, &bytes) == EFI_SUCCESS && bytes == 5);
  UINT8 *ram_status = changed + ((const UINT8 *)value - changed);
  static const UINT8 four_be[4] = {0, 0, 0, 4};
  memcpy(ram_status - 8, four_be, 4);
  memcpy(ram_status + 4, four_be, 4);
  assert(MielFdtOpen(changed, size, &view));
  assert(MielInspectHandoff(changed, size, 0, &info) == EFI_UNSUPPORTED);
  assert(memcmp(&info, &before, sizeof info) == 0);
  assert(MielInspectHandoff(data, size, 0x57300001, &info) == EFI_COMPROMISED_DATA);
  assert(memcmp(&info, &before, sizeof info) == 0);
  for (UINTN i = 0; i < 4; i++) {
    memcpy(changed, data, size);
    assert(MielFdtOpen(changed, size, &view));
    assert(MielFdtProperty(&view, i == 0 || i == 3 ? "/memory@40000000" : i == 1 ? "/psci" : "/chosen",
                           i == 0 ? "reg" : i == 1 ? "method" : i == 2 ? "atag,videolfb" : "status",
                           &value, &bytes) == EFI_SUCCESS);
    UINT8 *p = changed + ((CONST UINT8 *)value - changed);
    if (i == 0) memset(p + 8, 0, 8); // Zero RAM length.
    if (i == 1) memcpy(p, "hvc", 4); // Unsupported for this MT6781 handoff.
    if (i == 2) memset(p, 0xff, 8); // Framebuffer address would overflow.
    if (i == 3) memcpy(p, "fail", 5); // Unavailable memory must not become a usable bank.
    assert(MielInspectHandoff(changed, size, 0, &info) != EFI_SUCCESS);
    assert(memcmp(&info, &before, sizeof info) == 0);
    if (i == 3) {
      unsigned calls = 0;
      assert(MielInspectHandoffWithTrace(changed, size, 0, &info, trace_status, &calls) == EFI_UNSUPPORTED);
      assert(calls == 1 && memcmp(&info, &before, sizeof info) == 0);
      assert(memcmp(p, "fail", 5) == 0);
    }
  }
  UINT32 random = 0x196c7305;
  for (UINTN i = 0; i < 512; i++) {
    memcpy(changed, data, size);
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    changed[random % size] ^= (UINT8)(1U << (i % 8));
    info = before;
    EFI_STATUS status = MielInspectHandoff(changed, size, 0, &info);
    if (status != EFI_SUCCESS) assert(memcmp(&info, &before, sizeof info) == 0);
    else assert(info.RamCount > 0 && info.RamCount <= MIEL_MAX_RAM_BANKS &&
                info.ReservedCount <= MIEL_MAX_BOOT_RESERVATIONS);
  }
  free(changed);
}

int main(int argc, char **argv)
{
  UINTN size;
  assert(argc == 2 || argc == 3);
  UINT8 *fixture = load(argv[1], &size);
  fixture_checks(fixture, size);
  free(fixture);
  if (argc == 3) {
    MIEL_BOOT_HANDOFF info;
    UINT8 *raw = load(argv[2], &size);
    assert(MielInspectHandoff(raw, size, 0, &info) == EFI_SUCCESS);
    assert(info.RamCount > 0 && info.NativePanelFound && info.NativeFramebufferPresent);
    assert(info.NativeFramebufferCoveredByReservation && info.PsciSmcDeclared);
    printf("Private runtime handoff: %zu RAM bank, %zu explicit reservations, %u dynamic, %u empty and %u unresolved.\n",
           (size_t)info.RamCount, (size_t)info.ReservedCount,
           info.DynamicReservationCount, info.EmptyReservationNodeCount, info.UnresolvedReservationCount);
    free(raw);
  }
  puts("Original handoff interpretation, reservation exclusion and 512 corruptions passed with host sanitizers; no hardware operation.");
  return 0;
}
