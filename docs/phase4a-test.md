# Phase 4A: Build, Tests und LinuxCNC-Abnahme

## Reproduzierbarer Build

Debian 13, CMake >=3.20, C++20-Compiler, Python 3/Bash für Tests, GLM.
Für Grafik zusätzlich libgl1-mesa-dev und libglfw3-dev; mesa-utils dient der
Diagnose. Die vorhandenen Systempakete wurden verwendet, nichts nachinstalliert.

```bash
cmake -S . -B build -DCNC_SIM_RENDER=ON -DCNC_SIM_RENDER_TESTS=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
git diff --check
```

`CNC_SIM_RENDER` ist standardmäßig ON; das Laufzeitfenster öffnet nur `--render`.
`CNC_SIM_RENDER_TESTS` ist standardmäßig OFF. Die beiden mit `renderer`
markierten Tests benötigen einen Desktop; alle übrigen Tests sind headless.

Separater Release-Build ohne OpenGL-/GLFW-Abhängigkeiten:

```bash
cmake -S . -B build-headless -DCNC_SIM_RENDER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-headless -j4
env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir build-headless --output-on-failure
```

Die alte CLI bleibt gültig. `--render` in einem Build ohne Renderer meldet einen
klaren Fehler. Fehlender Displayzugang bei aktiviertem Renderer beendet sauber;
normaler Betrieb ohne `--render` funktioniert auch ohne DISPLAY.

## Automatisierte Ergebnisse am 27.09.2026

Ausgangsbaseline `d2fcb37`: **16/16 bestanden, 0 fehlgeschlagen, 0 übersprungen**.
Erweiterter Build: **22/22 bestanden, 0 fehlgeschlagen, 0 übersprungen**.
Separater Headless-Release-Build: **20/20 bestanden, 0 fehlgeschlagen,
0 übersprungen**, ohne DISPLAY/WAYLAND_DISPLAY. GCC 14.2.0, CMake 3.31.6;
Build ohne Compilerwarnungen. Vollständige lokale Protokolle:
`build/Testing/Temporary/LastTest.log` und
`build-headless/Testing/Temporary/LastTest.log` (nicht versioniert).

| Test | Ergebnis / Nachweis |
|---|---|
| phase4a-volume | PASS: Solid/Empty/Mixed, Grenzen, negative Koordinaten, mm/Voxel/Chunk, int64-Modulo, Transform/Inverse, Lazy-Allokation, ungültige Parameter |
| phase4a-mesher | PASS: leere Chunks, Außenflächen/Flächeninhalt, Winding, keine Innenflächen, alle drei Chunkgrenzen, deduplizierte Dirty-Menge |
| phase4a-snapshot | PASS: 4000/400, negative/multiple Achsen, parallele Snapshot-Abfragen während 1000 echter UDP-Pakete, Terminal-/Snapshot-Gleichheit |
| phase4a-cli | PASS: Szenenargumente, Display-/Renderer-Fehler, Start ohne Display |
| render-smoke | PASS: GL-Context, Shader, Rohteil-/Werkzeugpixel, sichtbare Positionsänderung, Kameramatrizen, keine GL-Fehler |
| render-integration | PASS: Grafik + 1000 originale UDP-Requests nominell 1 kHz + Recorder, 1001 exakte CSV-Samples, CLI und Quit |
| protocol-unit | PASS |
| recorder-unit | PASS |
| io-unit | PASS |
| original-hal-inputs | PASS |
| phase3-configuration | PASS |
| io_integration | PASS |
| io_terminal | PASS |
| recorder-integration | PASS, einschließlich blockierter Terminalausgabe |
| terminal-integration | PASS |
| udp-integration | PASS |
| network-namespace | PASS, echter rootloser veth-/Namespace-Test |
| vendor-integrity | PASS, Originaldateien unverändert |
| shell-syntax | PASS |
| shell-syntax-teardown-veth | PASS |
| shell-syntax-run-simulator | PASS |
| shell-syntax-network-common | PASS |

Der Grafiktest wurde auf AMD Radeon Graphics/radeonsi, Mesa 25.0.7 mit
Hardwarebeschleunigung ausgeführt. Zusätzlich wurde der Smoke-Test ausdrücklich
mit auf OpenGL 3.3 / GLSL 330 begrenztem Mesa ausgeführt:

```bash
MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330 ./build/render-smoke
```

Die UDP-Laufzeiten des Renderintegrationstests werden als Median/p99/Maximum
protokolliert. Sie sind diagnostische Messungen unter aktueller Systemlast,
keine Garantie harter Echtzeit oder des LinuxCNC-Watchdogs.
Im abschließenden Lauf: Median 0,097 ms, p99 0,214 ms, Maximum 0,220 ms.
Die vollständigen Suiten liefen in 21,08 s (Grafikbuild) und 16,10 s
(Headless-Release). `git diff --check` war ohne Befund; `ldd` bestätigt beim
Headless-Binary keine OpenGL-/GLFW-Abhängigkeit. Die exportierte 3.3-Testansicht
wurde zusätzlich visuell kontrolliert; Rohteil, Werkzeug, Tisch und Achsen sind
sichtbar. Alle Vendor-/Originaldateien blieben unverändert.

## Start ohne LinuxCNC

Im angemeldeten Desktop-Terminal:

```bash
cd ~/dev/linuxcnc-sim
./build/cnc-sim --render --bind 127.0.0.1 --port 8888 --steps-per-unit 400,400,400,400
```

Erwartet: blauer 50×50×10-mm-Block, Oberseite Z=0, goldener 6-mm-Flachfräser,
Tisch, rote X-/grüne Y-/blaue Z-Achse. Der Fenstertitel zeigt die Werkzeugspitze.
Ohne Pakete bleibt sie bei 0/0/0. Linke Maustaste drehen, mittlere verschieben,
Mausrad zoomen, Home Szene einpassen. Auch Resize und Minimieren prüfen.
`quit`, Ctrl+C oder Fensterschließen beendet den Simulator.

## Konkrete manuelle LinuxCNC-Abnahme

**Die interaktive LinuxCNC-Abnahme mit Grafik steht noch aus.** Automatisiert geprüft sind GL
und die originale UDP-Anbindung einschließlich Live-Pose und Recorder.
Die bestätigten früheren Phase-1–3-Abnahmen bleiben unverändert gültig.

Für den exakten Positionsvergleich die Phase-1-Konfiguration mit sofortiger
Nullreferenz verwenden. Simulator und LinuxCNC gemeinsam frisch starten; die
Schritthistorie wird nicht bei LinuxCNC-Homing zurückgesetzt. Die echte
Phase-3-Schaltersuchfahrt kann kleine Referenzoffsets erzeugen, siehe Phase 3.

Terminal A, als normaler angemeldeter Desktopbenutzer:

```bash
cd ~/dev/linuxcnc-sim
sudo ./scripts/setup-veth.sh
sudo ip netns exec cnc-sim-ns runuser -u "$USER" -- env \
  DISPLAY="$DISPLAY" XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}" \
  XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
  ./build/cnc-sim --render --steps-per-unit 400,400,400,400
```

Der Namespace benötigt Root zum Betreten; `runuser` startet den Simulator danach
mit dem Desktopbenutzer und dessen Gruppen. Dadurch bleiben GPU-Zugriff,
X11-Authentifizierung und Recorderdateien beim Benutzer. Dies verwendet die
vorhandenen Namespace-Ressourcen und ändert keine X-Server-Zugriffsregeln.
Die Befehle sind für das geprüfte X11-System gedacht; bei einem anderen
Desktop muss dessen DISPLAY/XAUTHORITY zum angemeldeten Benutzer passen.
Der bisherige `scripts/run-simulator.sh` bleibt für den Betrieb ohne Grafik
unverändert verfügbar.

Terminal B:

```bash
cd ~/dev/linuxcnc-sim
linuxcnc examples/phase1/phase1.ini
```

In LinuxCNC F1, F2 und Home All. Simulator CONNECTED, Positionen null und
RX=accepted=TX prüfen. Im Simulatorterminal `record begin`. Dann LinuxCNC-MDI:

```gcode
G21 G90
G53 G1 X10 Y10 Z2 F300
```

Nach Stillstand:

| Achse | Simulator-Steps | Terminal / Fenstertitel / Werkzeugspitze |
|---|---:|---:|
| X | 4000 | 10.000 mm |
| Y | 4000 | 10.000 mm |
| Z | 800 | 2.000 mm |

Die Werkzeugunterseite steht 2 mm über der Rohteiloberseite, 10 mm entlang
+X und +Y. Der Fräserdurchmesser beträgt 6 mm. `status` zeigt die Integerquelle.
Zusätzlich einzeln +X, +Y, +Z joggen: Bewegung jeweils entlang der roten,
grünen bzw. blauen positiven Achse. Zurückfahren und negative Richtung prüfen;
Home darf nur die Kamera verändern, niemals Maschinenpositionen.

Während einer längeren Bewegung Kamera bedienen, Fenster verschieben/vergrößern
und minimieren. RX/accepted/TX müssen weiterlaufen; Invalid, Send Errors,
Checksum/Timing/Overflow und Packet-ID Gaps sollen null bleiben. Im Terminal
`record stop` und `record save phase4a-live.csv`; Endsample muss obige Steps
enthalten, die Grafik darf keine Samples erzeugen oder verlieren.

Das Rohteil bleibt bei jeder Bewegung unverändert, auch wenn der Fräser darin
steht. Dies ist ein explizites Abnahmekriterium für die Grenze zu Phase 4B.
Optional die Phase-3-Konfiguration samt `--io-config examples/phase3/virtual-io.conf`
mit Grafik wiederholen; deren Homing-/Probe-Anleitung und Offsethinweise gelten
weiterhin. Die Probe ist weiterhin die unabhängige Phase-3-Punktebene.

Zuerst LinuxCNC, danach Simulator beenden, dann:

```bash
sudo ./scripts/teardown-veth.sh
```

## Grenzen

Kein Materialabtrag/Kollision, keine dynamische A-Achse, kein G-Code-Interpreter
oder zusätzlicher Positionsintegrator. Statisches Meshing beim Start, maximal
65536 Rohteilchunks im Renderer. Voxelmaße werden nach außen gerastert.
GPU-Positionen verwenden float; für extreme Maschinenkoordinaten ist dies keine
Präzisionsvisualisierung. Snapshot etwa 50 Hz, Anzeige bis etwa 60 Hz;
keine Bewegungsinterpolation. Core-/UDP-Erfolg ersetzt nicht die oben beschriebene
interaktive LinuxCNC- und Kamerabedienungsabnahme.

## Phase 4A.1: Runtime-Konfiguration und Regressionen

Die Aussagen zu statischem Meshing und unveränderlicher Szene oben beschreiben
die Phase-4A-Baseline. Seit 4A.1 gilt die
[Runtime-Architektur](phase4a1-design.md). Ausgangscommit für diese Erweiterung:
`10206f001f3afa00a341ce039996d3c8ee5645b2`.

Beide oben beschriebenen Buildvarianten wurden als Release gebaut und ihre
vollständigen Suiten ausgeführt. Ergebnisse am 27.09.2026:

| Variante / Prüfung | Ergebnis |
|---|---|
| `CNC_SIM_RENDER=ON`, `CNC_SIM_RENDER_TESTS=ON` | 26/26 bestanden, keine Fehler oder Skips |
| `CNC_SIM_RENDER=OFF`, ohne DISPLAY/WAYLAND_DISPLAY | 24/24 bestanden, keine Fehler oder Skips |
| `ldd build-headless/cnc-sim` | Keine OpenGL-/GLFW-Abhängigkeit |
| OpenGL-3.3-/GLSL-330-Override, `render-smoke` | bestanden, einschließlich Runtime-Rebuilds |
| Compiler | Keine Warnungen in beiden Builds |
| Protected-source-Diff gegen Ausgangscommit | Keine Änderung in `stepper-ninja/`, `third_party/`, `src/protocol/`, `src/machine/`, `src/network/`, `src/io/` |
| `vendor-integrity` | Alle neun Produktionsdateien entsprechen SHA-256-Manifest und Originalquellen |

Neue und erweiterte Prüfungen:

- `workpiece-geometry`: alle drei Spezifikationsbeispiele, gemischter Origin,
  kontinuierliche Offsets, Exponenten/Zahlenvorzeichen, numerisch beibehaltener
  Origin bei Größenänderung, zentrale Reset-Defaults.
- `workpiece-transactions`: ungültige Syntax/Arity, NaN/Inf, negative und zu große
  Origins, ungültiges Verkleinern, nichtpositive/extreme Voxelwerte,
  Chunkbudget/Overflow. Snapshot-Identität und Revision bleiben unverändert;
  unabhängige API-Validierung und physische gegenüber gerasterten Bounds.
- `workpiece-runtime`: parallele Leser während 100 Veröffentlichungen,
  zusammengehörige Config/Volumen/Revision, gültige alte Snapshots, frisches
  Rohmaterial, geänderte Voxelzahl und Meshergebnisse, entfernte Chunks,
  Reset, Legacy-Rotation und unveränderte Werkzeugquelle.
- `workpiece-cli`: echter Prozess, Befehle, vollständiges `show`, transaktionale
  Fehler, alte Startoptionen, Hilfe, 80×24-PTY und Terminalwiederherstellung.
- `render-smoke`: derselbe GL-Context zeigt nach Position, Origin, Größe,
  Voxelauflösung und Reset jeweils geänderte Rohteilpixel; Kamera und GL-Fehler
  bleiben geprüft. Das Werkzeug erhält weiterhin ausschließlich MachineSnapshots.
- `render-integration`: 1000 originale UDP-Antworten während mehrerer
  Konsolenänderungen und GL-Remeshings; RX=accepted=TX=1000, keine ID-Lücken,
  exakt 1001 Recorder-Samples mit den ursprünglichen Stepwerten.
- Alle bisherigen Protokoll-, HAL-, Namespace-, Recorder-, Terminal-, I/O-,
  Sensor-, CLI-, Sparse-Volume-, Mesher- und Snapshot-Tests bestehen weiterhin.

Die vollständigen Laufprotokolle bleiben lokal in
`build/Testing/Temporary/LastTest.log` und
`build-headless/Testing/Temporary/LastTest.log`.

Für die manuelle LinuxCNC-Prüfung bleibt der bestehende Startpfad gültig:

```bash
sudo ip netns exec cnc-sim-ns runuser -u "$USER" -- env \
  DISPLAY="$DISPLAY" XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}" \
  XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
  ./build/cnc-sim --render --steps-per-unit 400,400,400,400 \
  --io-config examples/phase3/virtual-io.conf
```

Im laufenden Simulator nacheinander `workpiece size 100 60 20`,
`workpiece origin center center max`, `workpiece position 50 30 0`,
`workpiece voxel 0.2`, `workpiece show` und `workpiece reset` eingeben.
Bounds mit der Formel vergleichen und Home/Fit prüfen. Während LinuxCNC-Motion
weitere Änderungen senden; Werkzeugkoordinaten, Recorder, VirtualIO und Probe
müssen unverändert ihrer bisherigen Quelle folgen. Ungültiges `workpiece size 1 1 1` bei zu großem numerischem Origin muss die letzte gültige Szene erhalten.
Eine interaktive LinuxCNC-Abnahme wurde für 4A.1 noch nicht durchgeführt;
automatisierte GL-, Wire- und Namespace-Tests ersetzen diese Bedienungsprüfung
nicht. Kein Materialabtrag ist implementiert. Große Rebuilds pausieren die
Grafikausgabe, nicht UDP; die bestehenden Raster-/Float-Grenzen gelten weiter.
