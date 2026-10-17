# mielPkg

Primer paquete de plataforma para el Redmi Note 11S 4G `miel` / MT6781.
El perfil registra lo observado en este equipo y firmware; el slot puede cambiar
después de una OTA. No es una configuración universal de la familia `fleur`.

`device-profile.json` deja sin valor las direcciones que todavía necesitan
evidencia del arranque real. Los registros estáticos están en
`Silicon/MediaTek/MT6781Pkg`.

Antes de añadir `miel.dsc`, `miel.fdf` y `PlatformMemoryMapLib` necesitamos el DTB
modificado por LK, todas sus reservas de memoria y un plan de observación del
primer arranque. Los valores de `emeraldPkg` pertenecen a otro equipo.

Consulta `Docs/miel-bringup.md`.
