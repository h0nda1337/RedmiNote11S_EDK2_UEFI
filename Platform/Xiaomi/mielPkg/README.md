# mielPkg

Primer paquete de plataforma para el Redmi Note 11S 4G `miel` / MT6781.
El perfil registra lo observado en este equipo y firmware; el slot puede cambiar
después de una OTA. No es una configuración universal de la familia `fleur`.

`MielNative.dsc` y `MielNative.fdf` componen el FD ARM64 de laboratorio, con
SEC propio y servicios genéricos del core EDK2. Compila, pero todavía no hay
prueba visual de SEC, GOP ni Shell ejecutándose en este Redmi.

El perfil usa el firmware stock y dos registros LK del equipo. Las direcciones
propuestas se distinguen de la validación física en `device-profile.json`.
La entrada comprueba el FDT de x0, PSCI y watchdog. La sonda corta no utiliza
heap: valida el tag de VRAM y marca tres superficies completas, cada una dentro
de sus límites. Verde, azul o magenta identifica el buffer que se está mostrando;
una franja blanca acompaña al color. La sesión solicita Fastboot tras diez segundos.

El modo completo requiere además interpretar las reservas y aprobar FD/heap
antes de habilitar MMU, HOBs y DXE. No se publican las capturas ni las imágenes.
Los valores de `emeraldPkg` y del OPPO no son el mapa de memoria del Redmi.

Consulta `Docs/miel-bringup.md`.
