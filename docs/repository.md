# Repositorybestand bis Phase 4A.1

## Struktur und reproduzierbarer Build

```text
.gitignore / .gitattributes     Artefaktausschlüsse und unveränderte Vendor-Bytes
CMakeLists.txt                  Simulator und automatisierte Tests
README.md                       Einstieg, Bedienung und aktueller Abnahmestatus
CODEX_PHASE2_*.md                ursprünglicher Phase-2-Auftrag
CODEX_PHASE3_*.md                ursprünglicher Phase-3-Auftrag
codex_phase4a_linuxcnc_sim.md    verbindlicher Phase-4A-Auftrag
codex_phase4a1_*.md              verbindlicher Phase-4A.1-Auftrag
docs/                           Protokollanalyse, Herkunft und Phasenabnahmen
examples/phase1/                 LinuxCNC-Beispiel für Motion
examples/phase3/                 LinuxCNC-Beispiel mit VirtualIO, Homing und Probe
scripts/                        veth-/Namespace-Einrichtung, Start und Teardown
src/                            C++20-Simulator: Protokoll, UDP, Maschine,
                                Simulation, Recorder, Konsole, virtuelle I/O,
                                Sparse-Volumen, CPU-Meshing, Werkzeug und Renderer
tests/                          C/C++-, Python- und Shell-Tests
third_party/stepper-ninja/       neun unveränderte Dateien für den Protokollkern,
                                SHA256SUMS und UPSTREAM.md
stepper-ninja/                   unveränderte Originalquellen und Referenzdokumente
```

Die ursprünglichen CODEX-Auftragsdokumente bleiben als historische Spezifikationen
erhalten. Den aktuellen Status liefern README und die Phasenabnahmen.

Der Produktionsbuild verwendet `src/` und `third_party/stepper-ninja/`.
Für die vollständige Suite mit 24 Headless-Tests und optional zwei Grafiktests ist außerdem
`stepper-ninja/hal-driver/modules/breakoutboard_hal_0.c` erforderlich:
`original-hal-inputs` kompiliert und prüft den Original-HAL-Code direkt.
Die Anleitung zum separat gebauten LinuxCNC-HAL-Modul benötigt ebenfalls den
Originalbestand mit seinen relativen Symlinks. Ein frischer Projektcheckout
enthält diese Dateien direkt; ein Submodule-Download ist nicht nötig.

## Originalbestand

Quelle des lokalen Originalcheckouts: `https://github.com/atrex66/stepper-ninja`,
Commit `eb7e5dfa2e76477e606a47038b07cca5e8a4b424`. Der vorhandene Checkout wurde
vor der Aufnahme geprüft und hatte keine Änderungen. Seine Quellen,
Dokumentation, Lizenzen und relativen Symlinks werden als normale Dateien in das
Projekt aufgenommen. Die bestehenden Original-Git-Metadaten bleiben lokal unter
`stepper-ninja/.git/`; sie gehören weder zum Projektcommit noch zu einem Submodul.

Drei upstream bereits enthaltene Dateien werden bewusst nicht aufgenommen:

- `stepper-ninja/utility/benchmark_udp`: vorgebautes Hilfsprogramm.
- `stepper-ninja/utility/counter/step-dir-counter.uf2`: fertiges Firmwareabbild.
- `stepper-ninja/firmware/ioLibrary_Driver.zip`: WIZnet-Hardwarebibliotheksarchiv.

Auch der nicht ausgecheckte Upstream-Gitlink `firmware/ioLibrary_Driver`
(WIZnet-Submodul) wird nicht übernommen; im Projekt gibt es keine Gitlinks.

Keine dieser Dateien wird vom Simulator, seinen Tests oder dem dokumentierten
UDP-HAL-Build benötigt. Sie bleiben im lokalen Originalcheckout erhalten.
Dieser Projektbestand ist damit kein vollständiges Archiv zum Bauen beliebiger
Pico-Firmware. Alle übrigen vom Originalcheckout versionierten Dateien bleiben
enthalten, auch bewusst aufbewahrte Referenzdateien, die dessen eigene
`.gitignore` für unversionierte Dateien ausschließen würde.

Die neun Produktions-Vendordateien bleiben unverändert und werden durch den
Test `vendor-integrity` anhand der festgelegten SHA-256-Werte sowie gegen den
Originalbestand geprüft. Vorhandene Lizenzhinweise bleiben erhalten;
Einzelheiten stehen in [stepper-ninja-files.md](stepper-ninja-files.md).

Der Originalbestand enthält bereits Leerraumfehler. `.gitattributes` deaktiviert
nur für `stepper-ninja/**` und `third_party/stepper-ninja/**` Git-Zeilenenden-
Normalisierung und Leerraumwarnungen (`-text -whitespace`), damit die Originalbytes
erhalten bleiben. Eigene Quellen behalten die normale Leerraumprüfung. In
`examples/phase3/phase3.ini` wurde lediglich eine zusätzliche Leerzeile am Dateiende
entfernt; Konfigurationswerte, Simulatorlogik und Tests bleiben unverändert.

## Lokale und generierte Dateien

`.gitignore` schließt Buildverzeichnisse, CMake-/Compilerprodukte, sämtliche
Recorder-CSV-Dateien, Python-/Testcaches, temporäre Dateien, Editor-/IDE-Daten
und lokale LinuxCNC-Parameter, Logs und Positionsspeicher aus. Die vorhandenen
`phase2-line.csv` und `phase2-circle.csv` sind lokale Aufzeichnungen und werden
nicht committed. Dasselbe gilt für `.var`, `.var.bak` und `.var.new` in den
Beispielverzeichnissen; LinuxCNC erzeugt den Parameterspeicher zur Laufzeit.
Benötigte `.ini`, `.hal`, `.tbl` und `virtual-io.conf` bleiben versioniert.

Die Initialisierung betrifft ausschließlich das lokale Projekt-Repository mit
Branch `main`; dafür wird kein Remote eingerichtet und nichts gepusht. Der
bereits vorhandene Originalcheckout behält seine eigene lokale Git-Konfiguration.

## Werkstückkonfiguration (Phase 4A.1)

`src/simulation/Workpiece.*` definiert numerische Konfiguration, Bounds,
Validierung und unveränderliche Snapshots. `Simulation.*` besitzt und
veröffentlicht sie unabhängig von der UDP-Mailbox. Die Befehle liegen in
`src/console/WorkpieceCommands.*`, die Runtime-Anbindung in `src/main.cpp`.
`Renderer::set_workpiece` invalidiert GPU-Chunks und benutzt den bestehenden
CPU-Mesher erneut. `tests/workpiece_tests.cpp` und `workpiece_cli.py` ergänzen
Geometrie-, Transaktions-, Snapshot-, Parser- und Terminaltests; die bestehenden
Grafiktests prüfen auch Rebuilds und UDP/Recorder während des Remeshings.
Semantik und Kompatibilitätsentscheidungen: [phase4a1-design.md](phase4a1-design.md).
