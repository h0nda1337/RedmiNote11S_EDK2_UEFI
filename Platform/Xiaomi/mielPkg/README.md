# mielPkg

Primer paquete de plataforma para el Redmi Note 11S 4G `miel` / MT6781.
El perfil registra lo observado en este equipo y firmware; el slot puede cambiar
después de una OTA. No es una configuración universal de la familia `fleur`.

`MielNative.dsc` y `MielNative.fdf` componen el FD ARM64 de laboratorio, con
SEC propio y servicios genéricos del core EDK2. El usuario observó las franjas
azul/blanca de la sonda SEC en el Redmi. GOP y Shell siguen pendientes.

El perfil usa el firmware stock y dos registros LK del equipo. Las direcciones
propuestas se distinguen de la validación física en `device-profile.json`.
La entrada comprueba el FDT de x0, PSCI y watchdog. La sonda corta no utiliza
heap: valida el tag de VRAM y marca tres superficies completas, cada una dentro
de sus límites. Verde, azul o magenta identifica el buffer que se está mostrando;
una franja blanca acompaña al color. La sesión solicita Fastboot tras diez segundos.

La consola previa al heap guarda su estado dentro del SEC cargado. Una prueba
de host protege con PROT_NONE toda la página del log compartido y comprueba que
esa consola funciona antes de habilitarla. No escribe el heap candidato.

El modo completo requiere además interpretar las reservas y aprobar FD/heap
antes de habilitar MMU, HOBs y DXE. No se publican las capturas ni las imágenes.
Los valores de `emeraldPkg` y del OPPO no son el mapa de memoria del Redmi.

Consulta `Docs/miel-bringup.md`.
