# Fuentes y autoría del port independiente

El árbol activo de este proyecto se limita a Redmi Note 11S `miel` / MT6781.
La plataforma nativa y sus herramientas son trabajo de h0nda1337 con asistencia
de Codex, o adaptaciones de código de los repositorios del mismo propietario.
Los créditos de esta colaboración están en [CONTRIBUTORS.md](../CONTRIBUTORS.md).

## Composición permitida

| Ruta | Procedencia |
| --- | --- |
| `Platform/Xiaomi/mielPkg` | Plataforma del port; adaptaciones propias registradas |
| `Silicon/MediaTek/MT6781Pkg` | Definiciones originales basadas en la referencia de hardware |
| `Tools/Bringup`, `Tools/Build`, `Tools/Tests`, `build.sh` | Herramientas del port y adaptaciones de repositorios de h0nda1337 |
| `Common/edk2` | Core EDK2 externo, fijado en `b03a21a63e3bd001f52c527e5a57feddb53a690b` |

La excepción externa autorizada es EDK2 y drivers. Actualmente no se conserva
ningún paquete de otra plataforma fuera del submódulo EDK2. Sus dependencias
internas forman parte del core fijado y mantienen sus propias licencias.
El hardware genérico y los servicios UEFI que utiliza `MielNative.dsc` proceden
del core; no se presentan como implementación propia.

La procedencia de las adaptaciones se conserva en
[own-code-provenance.json](own-code-provenance.json): repositorio, commit,
archivo, hash del original y modificaciones. Los archivos propios usan
BSD-2-Clause; el submódulo conserva sus licencias y atribuciones originales.
Los firmwares de fabricante son referencias privadas y material de recuperación.

## Limpieza del árbol

Se retiraron 329 rutas heredadas: BootShim, los paquetes de otros teléfonos,
los paquetes Silicon ajenos al MT6781, la herramienta antigua de compresión de
ramdisk y el submódulo `edk2-platforms`, que la compilación nativa no utiliza.
El lanzador raíz `build.sh` se sustituyó por un lanzador propio para `mielPkg`.

La imagen que llegó físicamente a Shell ya usaba SEC nativo propio y no
incorporaba BootShim. Esta limpieza elimina fuentes inactivas del repositorio;
se comprueba en host con compilación, inspección del FD y pruebas. No requiere
operaciones sobre el teléfono. El resultado está en
[source-cleanup.json](source-cleanup.json).

## Historial y contribuidores

`miel-native` es la rama principal independiente, creada a partir del árbol
limpio y sin padres del historial de otras plataformas. `main` conserva el
registro histórico para que los commits de las pruebas y las fotografías sigan
siendo consultables. También se conserva un bundle privado de respaldo en G:.
Los commits anteriores mantienen su autoría; no se reatribuyen a h0nda1337 ni
a Codex. Las licencias del código conservado permanecen intactas.

Los commits de la rama independiente se publican con la identidad de h0nda1337.
La asistencia de Codex queda registrada en los créditos. GitHub calcula su
gráfico sobre la rama predeterminada y advierte que sus estadísticas pueden
tardar unas 24 horas en refrescarse después de cambios de historial:
[documentación de GitHub](https://docs.github.com/en/repositories/viewing-activity-and-data-for-your-repository/viewing-a-projects-contributors).
