# Redmi Note 11S: EDK2 / MT6781 bring-up

Trabajo inicial para **Redmi Note 11S 4G, variante `miel`, modelo 2201117SL**.
Estado: **SEC nativo y framebuffer observados en el teléfono; Shell aún sin confirmar**.
Las pruebas temporales han terminado restaurando el boot stock y verificando Android.
Los dispositivos que enumera la base más abajo
son los soportados por upstream; esa lista no implica soporte para `miel`.

Este repositorio parte de `linux-mediatek/edk2-mtk`, rama `next`, commit
`fdb0ef928ac70ff0f2725b4cb302db4d25a55c47`. Se conserva su historial y licencia.
El código de otros equipos sigue siendo una referencia, no una configuración de
memoria válida para este teléfono.

## Trabajo realizado

- Identificación del teléfono por USB: MT6781, Android 13, slot `_a`, bootloader
  desbloqueado. Todas las consultas ADB utilizan `-d`.
- Herramienta de diagnóstico de solo lectura con verificación de variante.
- Extracción offline del DTB y DTBO del firmware local
  `OS1.0.9.0.TKEMIXM`, con registro de hashes SHA-256.
- Aplicación del overlay 0 sobre el DTB 0 mediante `fdtoverlay`.
- Primeros registros y PCD del MT6781 documentados a partir de ese DTB.
- Dos capturas de LK coherentes: bancos de DRAM, asignaciones y framebuffer activo
  `0x7ca06000`, 1080×2400, pitch 4320, BGRA8888. No sustituyen al FDT completo.
- Plataforma nativa con SEC propio, lector FDT, plan de memoria, GOP, BDS y Shell
  de EDK2. El código de plataforma procede del repositorio OPPO del mismo autor,
  adaptado a MT6781; procedencia en `Docs/own-code-provenance.json`.
- Entrada ARM64 comprobada en QEMU en EL1/EL2; bibliotecas verificadas con
  ASan/UBSan y 40 pruebas Python de formatos, transporte y selección USB.
- Diagnóstico Linux original sin libc. Su primer ELF produjo `-EFAULT` al cargar
  PID 1; la corrección ya carga como proceso ordinario en el kernel del teléfono.
  El canal CDC-ACM aún no ha entregado el FDT. No se afirma que funcione como PID 1.

## Empezar

El directorio de trabajo local es `G:\Codex\RedmiNote11S_EDK2_UEFI`.
Las descargas, dependencias y compilaciones deben permanecer en **G: o F:**.
Las capturas y binarios de fabricante se guardan en `Local/`, excluido de Git.

```powershell
Set-Location G:\Codex\RedmiNote11S_EDK2_UEFI
python Tools/Bringup/collect_usb.py
python Tools/Bringup/inspect_stock_boot.py --boot F:\ruta\images\boot.img --dtbo F:\ruta\images\dtbo.img
python -m unittest discover -s Tools/Bringup/tests -v
```

La segunda orden requiere sustituir `F:\ruta\images` por el directorio real del
firmware. No escribe en el teléfono.

La compilación nativa usa el EDK2 fijado en
`b03a21a63e3bd001f52c527e5a57feddb53a690b`, GCC AArch64 y Debian/WSL:

```powershell
wsl -d Debian -- bash /mnt/g/Codex/RedmiNote11S_EDK2_UEFI/Tools/Build/build_native.sh
wsl -d Debian -- env MIEL_PROBE=entry bash /mnt/g/Codex/RedmiNote11S_EDK2_UEFI/Tools/Build/build_native.sh
wsl -d Debian -- bash /mnt/g/Codex/RedmiNote11S_EDK2_UEFI/Tools/Tests/run_native_host.sh
wsl -d Debian -- bash /mnt/g/Codex/RedmiNote11S_EDK2_UEFI/Tools/Tests/test_sec_entry.sh
```

Los binarios y logs quedan en `Local/Build/`. Las simulaciones prueban contratos
de entrada y bibliotecas; no representan un arranque del Redmi ni una Shell física.

El paquete está en `Platform/Xiaomi/mielPkg`. Consulta
[`Docs/miel-bringup.md`](Docs/miel-bringup.md) para ver las evidencias, los límites
de los datos y el siguiente hito. El empaquetador `Tools/Bringup/native_boot.py`
preserva la cabecera v2 y DTB stock, usa gzip como transporte de LK y una ramdisk
vacía. El runner de laboratorio escribe temporalmente solo `boot_a` y restaura
el stock preparado antes de volver a Android. No se distribuyen imágenes stock
ni se recomienda instalar el prototipo.

La sonda de tres buffers produjo las franjas **azul y blanca**, observadas por el
usuario. Eso confirma ejecución SEC y salida visible en `0x7ca06000`; no hay foto
adjunta. El teléfono volvió a Fastboot y se verificó Android tras restaurar stock.
GOP y Shell siguen pendientes. El modo completo valida las reservas antes de
habilitar heap y DXE. Evidencias por etapa en `Docs/physical-tests.json`.

**Fotografía del arranque nativo:** [consola SEC en EL1 y `HANDOFF: UNSUPPORTED`](Docs/evidence/2026-10-17-sec-handoff-unsupported.md).
El fallo actual está en la interpretación del handoff, antes del plan de memoria.
La sesión terminó restaurando stock y verificando Android.

---

## Base original: EDK2 UEFI firmware for MediaTek Platforms

## Description

This repository aims to provide an usable EDK2 UEFI environment MediaTek SoCs.

It can be used to boot Mainline Linux without devicetree hacks that requires MediaTek LK.

## Supported Devices
- Redmi Note 13 Pro 4G (emerald)
- FPlus T1100
- Mashtab T1 (Ayya T1)
- Nokia 3.1 Plus

## Acknowledgements
- Renegade Project [edk2-msm](https://github.com/edk2-porting/edk2-msm)
- [EFIDroid](https://github.com/efidroid)
- [Lumia950XLPkg](https://github.com/WOA-Project/Lumia950XLPkg)
- BigfootACA and his [SimpleInit](https://github.com/BigfootACA/simple-init)

## License
All code is licensed under BSD 2-Clause.
