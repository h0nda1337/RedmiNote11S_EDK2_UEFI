/** @file
  Original conservative plan for the first UEFI memory pool and protections.
  SPDX-License-Identifier: BSD-2-Clause
**/
#ifndef MIEL_MEMORY_PLAN_LIB_H
#define MIEL_MEMORY_PLAN_LIB_H

#include <Uefi.h>
#include <Library/MielHandoffLib.h>

typedef struct {
  MIEL_MEMORY_RANGE Image;
  MIEL_MEMORY_RANGE Pool;
  MIEL_MEMORY_RANGE Protected[MIEL_MAX_BOOT_RESERVATIONS + 1];
  EFI_MEMORY_TYPE ProtectedType[MIEL_MAX_BOOT_RESERVATIONS + 1];
  UINTN ProtectedCount;
  UINT32 DynamicReservationsDeferredToOs;
  UINT32 EmptyNodesIgnored;
} MIEL_EARLY_MEMORY_PLAN;

/**
  Plan a bounded pool, firmware image and page-rounded fixed exclusions. No HOBs,
  allocations, page-table changes or hardware accesses are made. Only the image
  and chosen pool are candidates for initial UEFI-owned RAM; this is not a map of
  all DRAM. Dynamic DT reservations remain deferred to the OS per DTSpec 3.5.4.
  Nonempty unresolved nodes and overlapping rounded exclusions are rejected.
  Failure leaves Plan unchanged.
**/
EFI_STATUS EFIAPI MielPlanEarlyMemory (
  IN CONST MIEL_BOOT_HANDOFF *Handoff, IN MIEL_MEMORY_RANGE Image,
  IN MIEL_MEMORY_RANGE Pool, OUT MIEL_EARLY_MEMORY_PLAN *Plan
  );

#endif
