# Redmi Note 11S miel: registro de bring-up

Sesión: 17 de octubre de 2026. Repositorio local:
`G:\Codex\RedmiNote11S_EDK2_UEFI`. Descargas y dependencias en G: o F:.

## Equipo observado por USB

| Dato | Resultado |
| --- | --- |
| Modelo | 2201117SL |
| Producto Android / SKU | miel / miel |
| Plataforma | mt6781 |
| ABI | arm64-v8a, armeabi-v7a, armeabi |
| Android / kernel | 13 / 4.19.191-g6e5440f6a542 |
| Fingerprint Android | Redmi/miel_global/miel:13/TP1A.220624.014/V816.0.9.0.TKEMIXM:user/release-keys |
| Fingerprint boot | Redmi/fleur/fleur:12/SP1A.210812.016/V816.0.9.0.TKEMIXM:user/release-keys |
| Bloqueo | ro.boot.flash.locked=0; vbmeta.device_state=unlocked |
| Verified boot | orange |
| Slot observado | _a |
| Índices DTB / DTBO | 0 / 0 |
| MemTotal del kernel | 5860028 kB; no equivale al mapa físico de DRAM |
| Permisos ADB | uid=2000(shell), sin su disponible |

Hay otro teléfono conectado por ADB inalámbrico. El recolector usa `adb -d`
en cada orden, comprueba plataforma y variante antes de las sondas y verifica que
el dispositivo USB no haya cambiado al terminar. No almacena el número de serie.

La lectura de `/sys/firmware/fdt`, `/proc/iomem`, `/proc/cmdline` y `boot_a`
está bloqueada en esta sesión. Se pueden enumerar los nombres de las reservas
`mblock-*`, pero no sus direcciones. La captura completa queda en
`Local/Device/usb-report.json`, fuera de Git.

## Firmware analizado offline

Se encontró una ROM previamente extraída en F:, carpeta
`fleur_global_images_OS1.0.9.0.TKEMIXM_20241206.0000.00_13.0_global_3030fec09a`.
Coincide la versión declarada con la del teléfono. Esto **no demuestra** que las
particiones instaladas sean idénticas: todavía no se han podido leer.

| Archivo | Tamaño | SHA-256 local |
| --- | --- | --- |
| boot.img | 67108864 | f82b804775d3d205b774ace88e3a36c65f9500f5df8a312a370322fc2999cf5d |
| dtbo.img | 74464 | 3c4ec181ae56a833785bab10bb70b059ed615468be5f6bee189c03d72fd210f7 |

Estos hashes identifican los archivos locales analizados; no son una firma ni una
verificación contra un manifiesto oficial independiente.

La cabecera de boot es **v2**, tamaño 1660 bytes y página de 2048 bytes.
Kernel: 12438840 bytes; ramdisk: 20080502 bytes. La sección DTB empieza en
`0x01f04000`, mide 138548 bytes y contiene una tabla Android DT con una entrada,
no un FDT crudo. El DTB interior mide 138484 bytes. El DTBO tiene una entrada
de 70701 bytes. Ambos índices son 0, como declaran las propiedades del teléfono.

La cabecera declara kernel en `0x40080000`, ramdisk en `0x47c80000` y
DTB/tags en `0x4bc80000`. Son valores del archivo stock, no direcciones UEFI
aprobadas ni una confirmación de las direcciones usadas por LK.

En la carpeta de firmware no se encontró `vendor_boot.img`; el teléfono sí expone
particiones `vendor_boot_a/b`. Debe investigarse antes de decidir el empaquetado.

## Registros estáticos extraídos

Aplicación de overlay y decompilación verificadas con `fdtoverlay` y `dtc` en
Debian/WSL. Los archivos resultantes permanecen en `Local/Stock/`.

| Nodo en DTB + overlay 0 | Valor |
| --- | --- |
| /interrupt-controller, arm,gic-v3 | GICD 0x0c000000, tamaño 0x40000 |
| /interrupt-controller | GICR 0x0c040000, tamaño 0x200000 |
| /serial@11002000 | UART0 0x11002000, tamaño 0x1000 |
| /serial@11003000 | UART1 0x11003000, tamaño 0x1000 |
| /toprgu@10007000 | Watchdog 0x10007000, tamaño 0x1000 |
| /timer | 13000000 Hz; PPI 13,14,11,10 => IRQ 29,30,27,26 |
| /psci | arm,psci-1.0, método smc |

Son datos de configuración del firmware, todavía no una prueba de funcionamiento
de esos periféricos desde EDK2. No se ha copiado el comportamiento del timer ni
la inicialización GPIO/PMIC de MT6789 al MT6781.

El `/memory/reg` estático solo declara `0x40000000 + 0x3e605000`, muy por debajo
del MemTotal observado en Android. `/chosen` conserva un nombre de panel de
referencia `nt35695_fhd_dsi_cmd_truly_rt5081_drv`. Estos datos muestran que falta
la actualización de LK: no sirven para fijar el framebuffer ni toda la RAM.

## Siguiente hito

1. Obtener el DTB que LK entrega realmente al kernel, incluyendo `/memory`,
   `/reserved-memory`, `/chosen/atag,mem` y los campos `atag,videolfb*`. La ruta
   directa está bloqueada por los permisos actuales. Una captura desde un entorno
   con root debe recoger también `/proc/iomem` y el tamaño de las particiones.
2. Resolver cómo conservar el puntero al DTB al entrar en EDK2. En la base actual,
   `BootShim.S` conserva x0, pero `PrePi/ModuleEntryPoint.S` lo sobrescribe al
   comienzo. El contrato de entrada real del LK de este teléfono debe comprobarse.
3. Elegir regiones de FD, stack, vectores y heap que no se solapen con LK, BL31,
   TrustZone, GenieZone, SCP, SSPM, módem, ramdisk ni framebuffer. El mapa del
   Redmi Note 13 Pro 4G `emerald` no es válido para este equipo.
4. Añadir el DSC/FDF y las bibliotecas de plataforma con esas evidencias; decidir
   consola de diagnóstico y comprobar timer/interrupciones antes del resto de DXE.
5. Validar el empaquetado con la cabecera v2 observada y preparar una prueba de
   arranque temporal únicamente cuando exista una imagen y una vía de recuperación.

No se ha reiniciado el dispositivo, flasheado, cambiado slots, alterado vbmeta,
modificado el estado de bloqueo ni escrito particiones.

## Fuentes primarias

- Base: https://github.com/linux-mediatek/edk2-mtk/tree/fdb0ef928ac70ff0f2725b4cb302db4d25a55c47
- Fuente Xiaomi MT6781: https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/32022887f842ec64e317fc22745fedf35e982837/arch/arm64/boot/dts/mediatek/mt6781.dts
- Placa fleur: https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/32022887f842ec64e317fc22745fedf35e982837/arch/arm64/boot/dts/mediatek/fleur.dts
- Cabeceras Android boot: https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/main/include/bootimg/bootimg.h
- Tabla Android DT/DTBO: https://source.android.com/docs/core/architecture/dto/partitions

La rama pública `fleur-s-oss` es una referencia de hardware; no se ha demostrado
que sea la fuente exacta del kernel stock de diciembre de 2024.
