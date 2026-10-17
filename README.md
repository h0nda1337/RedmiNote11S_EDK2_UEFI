# Redmi Note 11S: EDK2 / MT6781 bring-up

Trabajo inicial para **Redmi Note 11S 4G, variante `miel`, modelo 2201117SL**.
Estado: identificación y análisis de firmware. **Todavía no hay una imagen UEFI
arrancable para este dispositivo.** Los dispositivos que enumera la base más abajo
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

El paquete inicial está en `Platform/Xiaomi/mielPkg`. Consulta
[`Docs/miel-bringup.md`](Docs/miel-bringup.md) para ver las evidencias, los límites
de los datos estáticos y el siguiente hito. No se ha definido un DSC completo ni
una dirección de carga UEFI: faltan las reservas reales de RAM que añade LK al
DTB durante el arranque.

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
