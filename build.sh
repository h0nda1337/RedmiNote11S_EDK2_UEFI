#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Host-only entry point for this port's single native platform.
set -euo pipefail
if (( $# != 0 )); then
  echo 'Usage: bash build.sh (optional environment: MIEL_PROBE=integration|entry|mmu)' >&2
  exit 2
fi
root="$(cd -- "$(dirname -- "$0")" && pwd)"
exec bash "$root/Tools/Build/build_native.sh"
