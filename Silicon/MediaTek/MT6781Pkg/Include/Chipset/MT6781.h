/** @file
  MT6781 register bases observed in OS1.0.9.0.TKEMIXM DTB + overlay 0.
  See Docs/miel-bringup.md for provenance and validation limits.
  SPDX-License-Identifier: BSD-2-Clause
**/
#ifndef MT6781_REGISTERS_H_
#define MT6781_REGISTERS_H_

#define MT6781_GIC_DISTRIBUTOR_BASE    0x0C000000U
#define MT6781_GIC_DISTRIBUTOR_SIZE    0x00040000U
#define MT6781_GIC_REDISTRIBUTORS_BASE 0x0C040000U
#define MT6781_GIC_REDISTRIBUTORS_SIZE 0x00200000U
#define MT6781_UART0_BASE              0x11002000U
#define MT6781_UART1_BASE              0x11003000U
#define MT6781_TOPRGU_BASE             0x10007000U
#define MT6781_ARCH_TIMER_HZ           13000000U

// ARM architectural interrupt IDs: DT PPI index + 16.
#define MT6781_ARCH_TIMER_SECURE_IRQ   29U
#define MT6781_ARCH_TIMER_PHYSICAL_IRQ 30U
#define MT6781_ARCH_TIMER_VIRTUAL_IRQ  27U
#define MT6781_ARCH_TIMER_HYP_IRQ      26U

#endif
