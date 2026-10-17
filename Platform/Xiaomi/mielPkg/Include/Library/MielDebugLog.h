/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef MIEL_DEBUG_LOG_H
#define MIEL_DEBUG_LOG_H
#include <Uefi.h>
/* Call only after the native framebuffer and the entire pool were validated.
 * Initializes one reserved RAM page; no persistent storage is accessed. */
BOOLEAN EFIAPI MielDebugStart (VOID);
#endif
