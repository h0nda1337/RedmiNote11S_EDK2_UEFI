# Instrucciones del port

- Hito inicial cumplido el 17 de octubre de 2026: UEFI Shell fotografiada con
  prompt `Shell>` en Redmi Note 11S miel/MT6781. Evidencia en
  `Docs/evidence/2026-10-17-first-uefi-shell.md`.
- Sesión física cerrada a petición del usuario, con stock restaurado y Android
  arrancado. Retomar pruebas sobre este teléfono solo ante una petición posterior.
- Seleccionar exclusivamente el teléfono USB. ADB con `-d`; Fastboot con el
  número de serie obtenido de ese transporte, sin escribirlo en evidencias públicas.
- Mantener descargas, dependencias, capturas y compilaciones en G: o F:.
- El propietario permite código externo únicamente para EDK2 y drivers.
  El resto debe ser original de este port o proceder de sus repositorios
  `h0nda1337/*`, registrando origen, commit y autoría. No utilizar TWRP, Magisk
  ni otros payloads externos para las pruebas.
- La rama principal independiente es `miel-native`; `main` conserva el registro
  histórico. El árbol activo solo admite `Platform/Xiaomi/mielPkg`, las
  definiciones propias MT6781 y las herramientas propias. No reintroducir
  BootShim, `edk2-platforms` ni paquetes de otras plataformas.
- Mantener la procedencia del código y los créditos de h0nda1337 y Codex en
  `CONTRIBUTORS.md` y `Docs/source-policy.md`; no reatribuir commits ajenos.
- El firmware stock puede analizarse como referencia y servir de recuperación;
  no presentarlo como código propio ni publicarlo en este repositorio.
- Conservar licencias y atribuciones. Mantener binarios y datos privados en Local/.
- Distinguir inspección, pruebas de host/simulación y ejecución física.
- Comprobar batería antes de cada fase sobre el equipo; mínimo operativo 20 %.
- Preparar y validar una imagen y su recuperación antes de probarla. Preferir
  arranque desde RAM y no escribir imágenes sin validar sobre particiones.
- No cambiar el bloqueo, borrar datos ni modificar preloader/LK/vbmeta para avanzar.
- Revisar el índice preparado y ejecutar los controles correspondientes antes de
  publicar, sin force-push.
