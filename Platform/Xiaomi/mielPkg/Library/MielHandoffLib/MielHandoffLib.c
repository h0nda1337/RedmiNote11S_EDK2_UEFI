/** @file
  Original platform handoff interpretation using the project's own FDT reader.
  SPDX-License-Identifier: BSD-2-Clause
**/
#include <Library/MielHandoffLib.h>
#include <Library/MielFdtLib.h>

typedef struct {
  CONST MIEL_FDT_VIEW *View;
  MIEL_BOOT_HANDOFF *Info;
  CONST CHAR8 *ChosenPath;
  MIEL_HANDOFF_TRACE Trace;
  VOID *TraceContext;
} WALK_CONTEXT;

STATIC EFI_STATUS Unsupported (WALK_CONTEXT *Context, CONST CHAR8 *Reason,
                              CONST CHAR8 *Path, CONST CHAR8 *Property,
                              CONST VOID *Value, UINT32 Bytes)
{
  if (Context->Trace != NULL)
    Context->Trace (Reason, Path, Property, Value, Bytes, Context->TraceContext);
  return EFI_UNSUPPORTED;
}

STATIC UINT32 Be32 (CONST UINT8 *P)
{
  return (UINT32)P[0] << 24 | (UINT32)P[1] << 16 | (UINT32)P[2] << 8 | P[3];
}

STATIC UINT32 Le32 (CONST UINT8 *P)
{
  return (UINT32)P[3] << 24 | (UINT32)P[2] << 16 | (UINT32)P[1] << 8 | P[0];
}

STATIC UINT64 Number (CONST UINT8 *P, UINT32 Cells)
{
  return Cells == 2 ? (UINT64)Be32 (P) << 32 | Be32 (P + 4) : Be32 (P);
}

STATIC BOOLEAN Same (CONST CHAR8 *A, CONST CHAR8 *B)
{
  UINTN I;
  for (I = 0; I < 1024; I++) {
    if (A[I] != B[I]) return FALSE;
    if (A[I] == 0) return TRUE;
  }
  return FALSE;
}

STATIC BOOLEAN StringListHas (CONST VOID *Data, UINT32 Bytes, CONST CHAR8 *Wanted)
{
  CONST UINT8 *P = Data;
  UINT32 Start = 0, Index;
  BOOLEAN Found = FALSE;
  if (Bytes == 0 || P[Bytes - 1] != 0) return FALSE;
  for (Index = 0; Index < Bytes; Index++) {
    if (P[Index] == 0) {
      if (Same ((CONST CHAR8 *)P + Start, Wanted)) Found = TRUE;
      Start = Index + 1;
    }
  }
  return Found;
}

STATIC BOOLEAN RangeValid (UINT64 Base, UINT64 Length)
{
  return (BOOLEAN)(Length != 0 && Base <= MAX_UINT64 - Length);
}

STATIC EFI_STATUS AppendRange (MIEL_BOOT_HANDOFF *Info, UINT64 Base, UINT64 Length, BOOLEAN Ram, UINT32 Flags)
{
  MIEL_MEMORY_RANGE *Ranges = Ram ? Info->Ram : Info->Reserved;
  UINTN *Count = Ram ? &Info->RamCount : &Info->ReservedCount;
  UINTN Limit = Ram ? MIEL_MAX_RAM_BANKS : MIEL_MAX_BOOT_RESERVATIONS, Index;
  if (!RangeValid (Base, Length)) return EFI_COMPROMISED_DATA;
  if (*Count == Limit) return EFI_BUFFER_TOO_SMALL;
  if (Ram) {
    for (Index = 0; Index < *Count; Index++)
      if (Base < Ranges[Index].Base + Ranges[Index].Length && Ranges[Index].Base < Base + Length)
        return EFI_COMPROMISED_DATA;
  }
  Ranges[*Count].Base = Base; Ranges[*Count].Length = Length;
  if (!Ram) Info->ReservedFlags[*Count] = Flags;
  (*Count)++;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS CellCount (WALK_CONTEXT *Context, CONST CHAR8 *Parent, CONST CHAR8 *Name,
                            UINT32 Default, UINT32 *Count)
{
  CONST VOID *Value;
  UINT32 Bytes;
  EFI_STATUS Status = MielFdtProperty (Context->View, Parent, Name, &Value, &Bytes);
  if (Status == EFI_NOT_FOUND) { *Count = Default; return EFI_SUCCESS; }
  if (Status != EFI_SUCCESS) return Status;
  if (Bytes != 4 || Be32 (Value) < 1 || Be32 (Value) > 2)
    return Unsupported (Context, "CELL_COUNT", Parent, Name, Value, Bytes);
  *Count = Be32 (Value);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS ResourceEnabled (WALK_CONTEXT *Context, CONST CHAR8 *Path,
                                   BOOLEAN Reservation, BOOLEAN *Enabled)
{
  CONST VOID *Value;
  UINT32 Bytes;
  EFI_STATUS Status = MielFdtProperty (Context->View, Path, "status", &Value, &Bytes);
  *Enabled = TRUE;
  if (Status == EFI_NOT_FOUND) return EFI_SUCCESS;
  if (Status != EFI_SUCCESS) return Status;
  if ((Bytes == 5 && Same (Value, "okay")) || (Bytes == 3 && Same (Value, "ok"))) return EFI_SUCCESS;
  // LK's observed SSPM reservation status has four bytes without a NUL.
  // Accept only that exact literal on reservation nodes. This preserves the
  // reservation; it cannot declare an additional RAM bank or disable a range.
  CONST UINT8 *Data = Value;
  if (Reservation && Bytes == 4 && Data[0] == 'o' && Data[1] == 'k' &&
      Data[2] == 'a' && Data[3] == 'y') return EFI_SUCCESS;
  if (Bytes == 9 && Same (Value, "disabled")) { *Enabled = FALSE; return EFI_SUCCESS; }
  return Unsupported (Context, "RESOURCE_STATUS", Path, "status", Value, Bytes);
}

STATIC EFI_STATUS RegisterRanges (WALK_CONTEXT *Context, CONST CHAR8 *Path, BOOLEAN Ram)
{
  CHAR8 Parent[1024];
  UINTN Length = 0, LastSlash = 0;
  CONST UINT8 *Reg;
  CONST VOID *Value;
  UINT32 Bytes, AddressCells, SizeCells, Width, Position, Flags = 0;
  EFI_STATUS Status;
  while (Length < sizeof (Parent) - 1 && Path[Length]) {
    if (Path[Length] == '/') LastSlash = Length;
    Parent[Length] = Path[Length]; Length++;
  }
  Parent[LastSlash == 0 ? 1 : LastSlash] = 0;
  if (!Ram) {
    Status = MielFdtProperty (Context->View, Parent, "ranges", &Value, &Bytes);
    if (Status == EFI_SUCCESS && Bytes != 0)
      return Unsupported (Context, "RESERVE_TRANSLATION", Parent, "ranges", Value, Bytes);
    if (Status != EFI_SUCCESS && Status != EFI_NOT_FOUND) return Status;
    Status = MielFdtProperty (Context->View, Path, "no-map", &Value, &Bytes);
    if (Status == EFI_SUCCESS) {
      if (Bytes != 0) return EFI_COMPROMISED_DATA;
      Flags = MIEL_RESERVATION_NO_MAP;
    } else if (Status != EFI_NOT_FOUND) return Status;
    Status = MielFdtProperty (Context->View, Path, "reusable", &Value, &Bytes);
    if (Status == EFI_SUCCESS) {
      if (Bytes != 0 || Flags != 0) return EFI_COMPROMISED_DATA;
    } else if (Status != EFI_NOT_FOUND) return Status;
  }
  Status = CellCount (Context, Parent, "#address-cells", 2, &AddressCells);
  if (Status != EFI_SUCCESS) return Status;
  Status = CellCount (Context, Parent, "#size-cells", 1, &SizeCells);
  if (Status != EFI_SUCCESS) return Status;
  Status = MielFdtProperty (Context->View, Path, "reg", &Value, &Bytes);
  if (Status == EFI_NOT_FOUND && !Ram) {
    Status = MielFdtProperty (Context->View, Path, "size", &Value, &Bytes);
    if (Status == EFI_NOT_FOUND) {
      UINT32 Properties;
      Status = MielFdtPropertyCount (Context->View, Path, &Properties);
      if (Status != EFI_SUCCESS) return Status;
      if (Properties == 0) Context->Info->EmptyReservationNodeCount++;
      else Context->Info->UnresolvedReservationCount++;
      return EFI_SUCCESS;
    }
    if (Status != EFI_SUCCESS) return Status;
    if (Bytes != SizeCells * 4 || Number (Value, SizeCells) == 0) return EFI_COMPROMISED_DATA;
    Context->Info->DynamicReservationCount++;
    return EFI_SUCCESS;
  }
  if (Status != EFI_SUCCESS) return Status;
  Width = (AddressCells + SizeCells) * 4;
  if (Bytes == 0 || Bytes % Width != 0) return EFI_COMPROMISED_DATA;
  Reg = Value;
  for (Position = 0; Position < Bytes; Position += Width) {
    Status = AppendRange (Context->Info, Number (Reg + Position, AddressCells),
                          Number (Reg + Position + AddressCells * 4, SizeCells), Ram, Flags);
    if (Status != EFI_SUCCESS) return Status;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI VisitNode (CONST CHAR8 *Path, VOID *UserContext)
{
  WALK_CONTEXT *Context = UserContext;
  CONST VOID *Value;
  UINT32 Bytes;
  UINTN Index;
  BOOLEAN Reserved = TRUE, Enabled;
  if (Same (Path, "/chosen") || Same (Path, "/chosen@0")) {
    if (Context->ChosenPath != NULL) return EFI_COMPROMISED_DATA;
    Context->ChosenPath = Same (Path, "/chosen") ? "/chosen" : "/chosen@0";
  }
  EFI_STATUS Status = MielFdtProperty (Context->View, Path, "device_type", &Value, &Bytes);
  if (Status == EFI_SUCCESS && Bytes == 7 && Same (Value, "memory")) {
    if (Path[1] == 0)
      return Unsupported (Context, "ROOT_MEMORY_TYPE", Path, "device_type", Value, Bytes);
    for (Index = 1; Path[Index]; Index++)
      if (Path[Index] == '/')
        return Unsupported (Context, "NESTED_MEMORY", Path, "device_type", Value, Bytes);
    Status = ResourceEnabled (Context, Path, FALSE, &Enabled);
    if (Status != EFI_SUCCESS || !Enabled) return Status;
    return RegisterRanges (Context, Path, TRUE);
  }
  if (Status != EFI_SUCCESS && Status != EFI_NOT_FOUND) return Status;
  for (Index = 0; Index < sizeof ("/reserved-memory/") - 1; Index++) {
    if (Path[Index] != "/reserved-memory/"[Index]) { Reserved = FALSE; break; }
    if (Path[Index] == 0) { Reserved = FALSE; break; }
  }
  if (!Reserved || Path[Index] == 0) return EFI_SUCCESS;
  for (; Path[Index]; Index++) if (Path[Index] == '/') return EFI_SUCCESS;
  Status = ResourceEnabled (Context, Path, TRUE, &Enabled);
  if (Status != EFI_SUCCESS || !Enabled) return Status;
  return RegisterRanges (Context, Path, FALSE);
}

STATIC EFI_STATUS ReadChosen (CONST MIEL_FDT_VIEW *View, CONST CHAR8 *Path, MIEL_BOOT_HANDOFF *Info)
{
  CONST VOID *Value, *EndValue;
  UINT32 Bytes, EndBytes, High = 0, Low = 0, Size = 0, Found = 0, Index;
  CONST CHAR8 *Keys[] = {"atag,videolfb-fb_base_h", "atag,videolfb-fb_base_l", "atag,videolfb-vramSize"};
  EFI_STATUS Status;
  if (Path == NULL) return EFI_SUCCESS;
  Status = MielFdtProperty (View, Path, "atag,videolfb", &Value, &Bytes);
  if (Status == EFI_SUCCESS) {
    CONST UINT8 *Tag = Value;
    if (Bytes < 20) return EFI_COMPROMISED_DATA;
    Info->NativeFramebuffer.Base = (UINT64)Le32 (Tag + 4) << 32 | Le32 (Tag);
    Info->NativeFramebuffer.Length = Le32 (Tag + 16);
    Info->NativePanelFound = (BOOLEAN)(Le32 (Tag + 8) != 0);
    if (!RangeValid (Info->NativeFramebuffer.Base, Info->NativeFramebuffer.Length)) return EFI_COMPROMISED_DATA;
    Info->NativeFramebufferPresent = TRUE;
  } else if (Status != EFI_NOT_FOUND) return Status;
  for (Index = 0; Index < 3; Index++) {
    Status = MielFdtProperty (View, Path, Keys[Index], &Value, &Bytes);
    if (Status == EFI_NOT_FOUND) continue;
    if (Status != EFI_SUCCESS) return Status;
    if (Bytes != 4) return EFI_COMPROMISED_DATA;
    Found++;
    if (Index == 0) High = Be32 (Value);
    else if (Index == 1) Low = Be32 (Value);
    else Size = Be32 (Value);
  }
  if (Found != 0) {
    if (Found != 3 || !RangeValid ((UINT64)High << 32 | Low, Size)) return EFI_COMPROMISED_DATA;
    Info->SplitFramebufferPresent = TRUE;
    Info->SplitFramebuffer.Base = (UINT64)High << 32 | Low; Info->SplitFramebuffer.Length = Size;
    Info->FramebufferDeclarationsDisagree = (BOOLEAN)(Info->NativeFramebufferPresent &&
      (Info->NativeFramebuffer.Base != Info->SplitFramebuffer.Base || Info->NativeFramebuffer.Length != Size));
  }
  Status = MielFdtProperty (View, Path, "linux,initrd-start", &Value, &Bytes);
  EFI_STATUS EndStatus = MielFdtProperty (View, Path, "linux,initrd-end", &EndValue, &EndBytes);
  if (Status == EFI_NOT_FOUND && EndStatus == EFI_NOT_FOUND) return EFI_SUCCESS;
  if (Status != EFI_SUCCESS || EndStatus != EFI_SUCCESS ||
      (Bytes != 4 && Bytes != 8) || Bytes != EndBytes) return EFI_COMPROMISED_DATA;
  UINT64 Start = Number (Value, Bytes / 4), End = Number (EndValue, EndBytes / 4);
  if (End <= Start || !MielLoadRegionIsUsable (TRUE, Start, End - Start,
      Info->Ram, Info->RamCount, NULL, 0)) return EFI_COMPROMISED_DATA;
  return AppendRange (Info, Start, End - Start, FALSE, MIEL_RESERVATION_BOOT_DATA);
}

EFI_STATUS EFIAPI MielInspectHandoffWithTrace (
  IN CONST VOID *Fdt, IN UINTN Bytes, IN UINT64 PhysicalFdtBase,
  OUT MIEL_BOOT_HANDOFF *Handoff, IN MIEL_HANDOFF_TRACE Trace OPTIONAL,
  IN VOID *TraceContext OPTIONAL
  )
{
  MIEL_FDT_VIEW View;
  MIEL_BOOT_HANDOFF Candidate;
  WALK_CONTEXT Context = {&View, &Candidate, NULL, Trace, TraceContext};
  CONST VOID *Value;
  UINT32 Size;
  UINTN Index;
  EFI_STATUS Status;
  if (Handoff == NULL) return EFI_INVALID_PARAMETER;
  if (!MielFdtOpen (Fdt, Bytes, &View)) return EFI_COMPROMISED_DATA;
  if ((UINTN)Handoff > MAX_UINTN - sizeof (*Handoff) ||
      ((UINTN)Handoff < (UINTN)Fdt + View.TotalBytes &&
       (UINTN)Fdt < (UINTN)Handoff + sizeof (*Handoff))) return EFI_INVALID_PARAMETER;
  volatile UINT8 *Clear = (volatile UINT8 *)&Candidate;
  for (Index = 0; Index < sizeof (Candidate); Index++) Clear[Index] = 0;
  Status = MielFdtProperty (&View, "/", "compatible", &Value, &Size);
  if (Status != EFI_SUCCESS) return Status;
  if (!StringListHas (Value, Size, "mediatek,MT6781"))
    return Unsupported (&Context, "PLATFORM_COMPAT", "/", "compatible", Value, Size);
  for (Index = 0; Index < View.ReserveCount; Index++) {
    MIEL_MEMORY_RANGE Range;
    if (!MielFdtReservation (&View, (UINT32)Index, &Range)) return EFI_COMPROMISED_DATA;
    Status = AppendRange (&Candidate, Range.Base, Range.Length, FALSE,
                          MIEL_RESERVATION_NO_MAP | MIEL_RESERVATION_MEMRESERVE);
    if (Status != EFI_SUCCESS) return Status;
  }
  Status = MielFdtVisitNodes (&View, VisitNode, &Context);
  if (Status != EFI_SUCCESS) return Status;
  if (Candidate.RamCount == 0) return EFI_NOT_FOUND;
  Status = ReadChosen (&View, Context.ChosenPath, &Candidate);
  if (Status != EFI_SUCCESS) return Status;
  Status = MielFdtProperty (&View, "/psci", "compatible", &Value, &Size);
  if (Status != EFI_SUCCESS) return Status;
  if (!StringListHas (Value, Size, "arm,psci-1.0") && !StringListHas (Value, Size, "arm,psci-0.2"))
    return Unsupported (&Context, "PSCI_COMPAT", "/psci", "compatible", Value, Size);
  Status = MielFdtProperty (&View, "/psci", "method", &Value, &Size);
  if (Status != EFI_SUCCESS) return Status;
  if (Size != 4 || !Same (Value, "smc"))
    return Unsupported (&Context, "PSCI_METHOD", "/psci", "method", Value, Size);
  Candidate.PsciSmcDeclared = TRUE;
  if (PhysicalFdtBase != 0) {
    if ((PhysicalFdtBase & 7) != 0 || !MielLoadRegionIsUsable (TRUE, PhysicalFdtBase, View.TotalBytes,
        Candidate.Ram, Candidate.RamCount, NULL, 0)) return EFI_COMPROMISED_DATA;
    Status = AppendRange (&Candidate, PhysicalFdtBase, View.TotalBytes, FALSE, MIEL_RESERVATION_BOOT_DATA);
    if (Status != EFI_SUCCESS) return Status;
    Candidate.FdtPhysicalAddressRecorded = TRUE;
  }
  if (Candidate.NativeFramebufferPresent && MielLoadRegionIsUsable (TRUE,
      Candidate.NativeFramebuffer.Base, Candidate.NativeFramebuffer.Length,
      Candidate.Ram, Candidate.RamCount, NULL, 0)) {
    for (Index = 0; Index < Candidate.ReservedCount; Index++) {
      MIEL_MEMORY_RANGE R = Candidate.Reserved[Index], F = Candidate.NativeFramebuffer;
      if (R.Base <= F.Base && F.Base + F.Length <= R.Base + R.Length)
        Candidate.NativeFramebufferCoveredByReservation = TRUE;
    }
  }
  // Explicit byte writes avoid a freestanding compiler-emitted libc memcpy.
  volatile UINT8 *Destination = (volatile UINT8 *)Handoff;
  CONST UINT8 *Source = (CONST UINT8 *)&Candidate;
  for (Index = 0; Index < sizeof (Candidate); Index++) Destination[Index] = Source[Index];
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI MielInspectHandoff (
  IN CONST VOID *Fdt, IN UINTN Bytes, IN UINT64 PhysicalFdtBase,
  OUT MIEL_BOOT_HANDOFF *Handoff
  )
{
  return MielInspectHandoffWithTrace (Fdt, Bytes, PhysicalFdtBase, Handoff, NULL, NULL);
}
