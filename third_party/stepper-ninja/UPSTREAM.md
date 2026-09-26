# Unveränderter Stepper-Ninja-Protokollbestand

Quelle: https://github.com/atrex66/stepper-ninja

Commit: `eb7e5dfa2e76477e606a47038b07cca5e8a4b424`.
Übernommen aus dem lokalen `../../stepper-ninja/` am 26.09.2026 gemäß
`docs/stepper-ninja-files.md`. Die neun in `SHA256SUMS` aufgeführten Dateien
sind bytegleich mit dem Original, einschließlich Leerraum und Zeilenenden.
`UPSTREAM.md` und `SHA256SUMS` sind unsere Herkunftsmetadaten.

Lizenz: beigefügte vollständige `LICENSE.txt`, MIT, Copyright (c) 2025 Zsolt Viola.
Es wurden keine Pico-SDK-, WIZnet-, LinuxCNC- oder BCM2835-Quellen importiert.

`firmware/modules/transmission.c` wird als C11 übersetzt. Das eigene
`src/protocol/OriginalProtocol.hpp` stellt C-Linkage für C++ her; die Originaldateien
bleiben dabei unverändert. Fremdheader sind als SYSTEM-Includes markiert, damit
insbesondere die alten `char*`-Stringliterale in `kbmatrix.h` nicht den eigenen
C++20-Code zu Warnungsunterdrückungen oder Veränderungen am Original zwingen.

Profil: Board 0, 4 Stepgens, 3 Encoder, PWM ausgeschaltet, aber `pwm_count=1`,
kein Analogblock, UDP, kein Software-Schrittring. Änderungen an diesem Profil
sind nicht Teil von Phase 1. C++-Assertions prüfen Größen, Offsets und Profil.

Prüfung: `ctest --test-dir build -R vendor-integrity --output-on-failure`.
Der Test prüft die festgelegten SHA-256-Werte und, sofern der Originalcheckout
vorhanden ist, zusätzlich direkte Bytegleichheit mit ihm.
