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

## Plan al inicio de la sesión

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

## Avance posterior en la misma sesión

Se obtuvieron dos registros independientes de Preloader/LK mediante Fastboot USB,
con resumen público sin identificadores en `lk-observations.json`. LK declara
DRAM `0x40000000+0x80000000` y `0xc0000000+0x100000000`. Asigna VRAM en
`0x7c010000+0x1df0000`; la capa visible usa `0x7ca06000`, 1080×2400, pitch 4320,
`eBGRA8888`. El bloque inicialmente libre `0x40000000+0xc080000` contiene las
regiones candidatas del FD (8 MiB desde `0x40080000`) y del heap (64 MiB desde
`0x43000000`). Son eventos del asignador; no se presentan como un mapa final.

La plataforma `MielNative.dsc/.fdf` ya compila EDK2 nativo, con entrada SEC propia
que conserva x0, interpretación FDT, política de memoria, GOP, Runtime y BDS
propios, y Shell del core EDK2. Se reutilizó código de
`h0nda1337/OppoReno5Lite_EDK2_UEFI` a commit
`13db93f82358caba3eb5214bb7985b0161810946`, con adaptación explícita del hardware;
las direcciones del OPPO no se trasladaron como perfil del Redmi. Los archivos,
hashes originales y autoría están en `own-code-provenance.json`.

El core se fijó en `b03a21a63e3bd001f52c527e5a57feddb53a690b`. La inspección del FD
comprueba las ramas cabecera→FV→SEC, formatos TE/PE AArch64, módulos y DEPEX del
productor PCD. Hay pruebas de entrada QEMU EL1/EL2, bibliotecas con ASan/UBSan
y 40 pruebas Python. Estos resultados son de host y simulación.

## Pruebas físicas y recuperación

Primero se probó `fastboot boot` con el boot stock sin modificar. El teléfono
regresó a Android declarando `lk_crash`; esta ruta RAM no se usa como garantía
de recuperación. El análisis estático de LK apunta a su inicialización del estado
de cabeceras, pero no demuestra el punto exacto de fallo en ejecución.

Se preparó un diagnóstico Linux con los drivers/kernel stock, DTB sin modificar
y un PID 1 original, estático, sin libc. Solo usa sistemas de archivos virtuales
y dispositivos de caracteres. No contiene TWRP, Magisk ni otro userspace externo.
Las imágenes de laboratorio conservan propiedades AVB stock y añaden un footer
con algoritmo NONE; no están firmadas por Xiaomi y no se modifica vbmeta.

La primera prueba temporal de `boot_a` entró en bucle. Tras recuperar Fastboot
con botones, se restauró el stock y Android completó el arranque. DropBox del
sistema conservó `Failed to execute /init (error -14)` y `No working init found`.
El ELF tenía un segmento RW sin bytes de archivo y BSS sin alinear. El binario
original también falló como proceso ordinario en el teléfono (salida 139), mientras
la corrección con datos inicializados salió 2 por su guardia de PID 1. QEMU user
mode por sí solo no detectaba ese problema.

La relación con la carga y el borrado de la página parcial de BSS es una
inferencia apoyada por el formato observado y la implementación de
[Linux 4.19, binfmt_elf.c](https://github.com/torvalds/linux/blob/v4.19/fs/binfmt_elf.c).
El empaquetador y el runner ahora rechazan ese diseño y ejecutan el ELF exacto
como proceso ordinario antes de una nueva escritura de boot.

Se corrigió también `O_DIRECTORY` para AArch64 (`0x4000`). Las constantes,
ioctls y tamaños se contrastan al compilar con los headers instalados, sin enlazar
código de bibliotecas. Una sonda original de solo lectura ejecutada por ADB
enumeró `musb-hdrc`. Las siguientes pruebas no entregaron el FDT por CDC-ACM;
en una de ellas fue necesario reconectar el USB para que Windows viera Fastboot.
Cada sesión terminó con stock restaurado y `sys.boot_completed=1`.

La primera sonda nativa se empaquetó en boot v2 con FD gzip y ramdisk vacía.
Regresó automáticamente a Fastboot, se restauró stock y se verificó Android.
El usuario solo observó logo/Fastboot, sin las franjas esperadas. Esto no confirma
ejecución SEC ni Shell. La sonda corta se está separando de la política del heap
para identificar el primer punto de parada con una señal de pantalla verificable.

Los ensayos han escrito temporalmente **solo boot_a**, con copia stock comprobada
antes de cada escritura. No se han cambiado slots, vbmeta, preloader/LK, bloqueo
ni datos personales. El retorno solicitado por software no sustituye a la vía
manual de Fastboot: mantener Volumen abajo, forzar apagado con Encendido y soltar
solo Encendido al apagarse/vibrar.

El siguiente hito sigue siendo una señal nativa inequívoca y, después, Shell
UEFI en pantalla con la memoria del arranque validada. El FDT crudo, imágenes
y registros completos permanecen privados en `Local/` y fuera de Git.

## Fuentes primarias

- Base: https://github.com/linux-mediatek/edk2-mtk/tree/fdb0ef928ac70ff0f2725b4cb302db4d25a55c47
- Fuente Xiaomi MT6781: https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/32022887f842ec64e317fc22745fedf35e982837/arch/arm64/boot/dts/mediatek/mt6781.dts
- Placa fleur: https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/32022887f842ec64e317fc22745fedf35e982837/arch/arm64/boot/dts/mediatek/fleur.dts
- Cabeceras Android boot: https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/main/include/bootimg/bootimg.h
- Tabla Android DT/DTBO: https://source.android.com/docs/core/architecture/dto/partitions

La rama pública `fleur-s-oss` es una referencia de hardware; no se ha demostrado
que sea la fuente exacta del kernel stock de diciembre de 2024.
