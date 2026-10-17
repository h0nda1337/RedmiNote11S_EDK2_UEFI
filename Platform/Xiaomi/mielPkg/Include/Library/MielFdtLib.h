/** @file
  Original bounded, allocation-free FDT reader for platform handoff inspection.
  SPDX-License-Identifier: BSD-2-Clause
**/
#ifndef MIEL_FDT_LIB_H
#define MIEL_FDT_LIB_H

#include <Uefi.h>
#include <Library/MielPlatformLib.h>

typedef struct {
  CONST UINT8 *Data;
  UINT32 TotalBytes;
  UINT32 StructureOffset;
  UINT32 StructureBytes;
  UINT32 StringsOffset;
  UINT32 StringsBytes;
  UINT32 ReserveOffset;
  UINT32 ReserveCount;
} MIEL_FDT_VIEW;

typedef EFI_STATUS (EFIAPI *MIEL_FDT_NODE_VISITOR)(
  IN CONST CHAR8 *Path, IN OUT VOID *Context
  );

/**
  Validate an FDT compatible with v17, at most 2 MiB, with bounded depth/names.
  No property value is interpreted and no physical memory is accessed beyond the
  caller-supplied buffer. Data must remain immutable while View is in use.
**/
BOOLEAN EFIAPI MielFdtOpen (IN CONST VOID *Data, IN UINTN Bytes, OUT MIEL_FDT_VIEW *View);

/**
  Read a property by absolute path, rejecting duplicate selected properties or
  ambiguous duplicate selected nodes. Revalidates the token stream. Value points
  into the immutable input; no allocation is performed. Optional absence returns
  EFI_NOT_FOUND, while malformed/ambiguous input returns EFI_COMPROMISED_DATA.
**/
EFI_STATUS EFIAPI MielFdtProperty (
  IN CONST MIEL_FDT_VIEW *View, IN CONST CHAR8 *Path, IN CONST CHAR8 *Name,
  OUT CONST VOID **Value, OUT UINT32 *Bytes
  );

BOOLEAN EFIAPI MielFdtReservation (
  IN CONST MIEL_FDT_VIEW *View, IN UINT32 Index, OUT MIEL_MEMORY_RANGE *Range
  );

/**
  Validate the whole immutable tree, then visit its nodes in preorder. Path is
  valid only during the callback. A non-success callback status stops traversal
  and is returned to the caller; no later node callback runs after that error.
**/
EFI_STATUS EFIAPI MielFdtVisitNodes (
  IN CONST MIEL_FDT_VIEW *View, IN MIEL_FDT_NODE_VISITOR Visitor,
  IN OUT VOID *Context
  );

/** Count a selected node's own properties; child properties are excluded. */
EFI_STATUS EFIAPI MielFdtPropertyCount (
  IN CONST MIEL_FDT_VIEW *View, IN CONST CHAR8 *Path, OUT UINT32 *Count
  );

#endif
