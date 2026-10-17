/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef MIEL_DEBUG_LOG_H
#define MIEL_DEBUG_LOG_H
#include <Uefi.h>
/* After validating the visible VRAM, use state inside the loaded SEC image.
 * This path never reads or writes the candidate heap or shared log page. */
BOOLEAN EFIAPI MielDebugStartEarly (VOID);
/* Call only after the native framebuffer and the entire pool were validated.
 * Initializes one reserved RAM page; no persistent storage is accessed. */
BOOLEAN EFIAPI MielDebugStart (VOID);
#endif
