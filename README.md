# Virtueller Stepper-Ninja – Phase 5

Ein Linux-C++20-Programm empfängt die originalen Stepper-Ninja-UDP-Pakete,
dekodiert Step-Bursts, integriert vier virtuelle Motorpositionen in `int64_t`
und sendet gültige Antworten an den originalen LinuxCNC-HAL-Treiber.
LinuxCNC übernimmt weiterhin G-Code, Kinematik und Bahnplanung.
Phase 2 ergänzt eine feste Terminaloberfläche und einen Recorder für tatsächliche
Schrittänderungen. Phase 1 wurde vom Benutzer mit echtem LinuxCNC erfolgreich
getestet: 1-ms-Servozeit, 400 Schritte/mm, Geraden und G2/G3-Kreise, über 600.000
Pakete ohne gemeldete Fehler oder ID-Lücken.

Der erste reale Phase-5-Lauf entfernte geometrisch korrekt **296560 Voxel**,
zeigte aber einen großen Performance-Rückstand. Die Optimierung fasst exakt
kollineare Motion zusammen, invalidiert chunkweise und mesht unveränderliche
Snapshots parallel. Der UDP-Pfad bleibt unabhängig. Die reale Abnahme muss mit
demselben LinuxCNC-Test wiederholt werden: **NOT YET PASSED**.

`--material-workers 1` behält einen autoritativen Material-Thread.
`--mesh-workers 0` (Standard) wählt bis zu vier Mesh-Threads und lässt rechnerisch
zwei logische CPUs frei; explizit sind 1..32 möglich. Andere Material-Threadzahlen
werden derzeit abgewiesen. Messwerte, Snapshot-Semantik und Grenzen stehen in
[Phase-5-Design](docs/phase5-design.md) und [Testprotokoll](docs/phase5-test.md).

## Projektstand

| Phase | Status |
|---|---|
| Phase 1: virtuelle Stepper-Ninja-UDP-Hardware und Motion-Anbindung | abgeschlossen und real mit LinuxCNC getestet |
| Phase 2: Terminaloberfläche und Motion Recorder | abgeschlossen und real mit LinuxCNC getestet |
| Phase 3: VirtualIO, Endschalter, Homing und Probe/G38.2 | abgeschlossen und real mit LinuxCNC getestet |
| Phase 4A: Sparse-Voxel-Modell und OpenGL-Liveansicht | automatisiert getestet; OpenGL-/G53-Pfad im Rahmen der realen 4A.1-Abnahme bestätigt |
| Phase 4A.1: konfigurierbare Werkstückgeometrie und Platzierung | automatisiert getestet und am 2026-09-27 real/interaktiv mit LinuxCNC 2.9.10 erfolgreich abgenommen |
| Phase 5: kontinuierlicher Voxel-Materialabtrag | implementiert und automatisiert getestet; reale LinuxCNC-Abnahme: **NOT YET PASSED** |

Die reale Phase-4A.1-Abnahme wurde vom Benutzer bestätigt: Runtime-Geometrie,
Origins, Platzierung und G53-Werkzeugposition stimmten mit LinuxCNC 2.9.10
überein. Bei aktiver Phase-3-VirtualIO wurden **RX = accepted = TX = 841238**
und ausschließlich null Fehlerzähler beobachtet. Das ist ein funktionaler
Sitzungsnachweis, keine harte Echtzeit- oder formale Zuverlässigkeitsgarantie.
Das [reale Abnahmeprotokoll](docs/phase4a1-test.md) enthält Startbefehle,
Bounds und den G53-End-to-End-Test.

Auch die frühere reale Phase-3-Abnahme wurde vom Benutzer bestätigt. Dabei liefen Simulator und
LinuxCNC stabil mit **RX = accepted = TX**; Invalid-Pakete, Send Errors, Length
Errors, Checksum Errors, Timing Errors, Position Overflows und Packet-ID Gaps
blieben bei **0**. `input 22 on` kam am Original-HAL als GP22=TRUE und
GP22-not=FALSE an. Virtuelle Endschalter und LinuxCNC-Homing funktionierten.

Der reale Probe-Test mit `G91` und `G38.2 Z-12 F50` bestätigte Probe ON,
Wire28=1 und `motion.probe-input=TRUE`. Bei 400 Schritten/mm wurden gemessen:

| Größe | Z |
|---|---:|
| Geometrische Probe-Ebene | -2000 Schritte = -5.000000 mm |
| Von LinuxCNC gespeicherte Probe-Position (`#5063`) | -5.003050 mm |
| Endgültige Simulatorposition | -2005 Schritte = -5.012500 mm |

`#5061` und `#5062` ergaben jeweils 0.000000. Der Pin `motion.probed-position`
existiert in dieser Konfiguration nicht; geprüft wurde mit
`(DEBUG, Probe X=#5061 Y=#5062 Z=#5063)`. Diese Werte sind Messergebnisse des
realen Softwaretests und keine Aussage zur mechanischen Genauigkeit einer
realen Maschine: Der Simulator besitzt keine reale Mechanik, kein Spiel und
keine Motor-/Antriebsdynamik. Das vollständige Protokoll steht in
[docs/phase3-test.md](docs/phase3-test.md#reale-phase-3-abnahme-mit-linuxcnc).

```text
LinuxCNC / originaler stepgen-ninja HAL (Host, UDP :8888)
    │
veth-lcnc 192.168.50.1/24
    │ virtuelles Ethernet
veth-sim 192.168.50.2/24
    │ Network Namespace cnc-sim-ns
cnc-sim 192.168.50.2:8888
```

Die getrennten Netzwerk-Namespaces erlauben Port 8888 auf beiden Seiten,
obwohl der unveränderte HAL-Treiber an `0.0.0.0:8888` bindet. Kein NAT,
Routingdienst oder permanenter Netzwerkeintrag ist erforderlich.

## Bauen und testen

Voraussetzungen: Linux, CMake >=3.20, C-/C++20-Compiler, Make oder Ninja,
GLM (`libglm-dev`). Der optionale Renderer wird standardmäßig mitgebaut und
benötigt `libgl1-mesa-dev` und `libglfw3-dev`; `mesa-utils` dient der Diagnose.
Tests benötigen zusätzlich Python 3 und Bash. Für veth: iproute2. Die
normalen Protokoll-/UDP-Tests benötigen weder Root noch LinuxCNC.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Der Namespace-Test nutzt, falls verfügbar, unprivilegierte User-, Mount- und
Netzwerk-Namespaces mit einem privaten temporären `/run`. Er verändert das
Hostnetz nicht. Ist diese Kernel-Funktion gesperrt, meldet CTest diesen Test
als übersprungen; die übrigen Tests laufen trotzdem. Der Release-Build kann
mit `-DCMAKE_BUILD_TYPE=Release` konfiguriert werden. Ohne Tests ist Python
nicht erforderlich: `-DBUILD_TESTING=OFF`.

## Phase 4A: optionale 3D-Ansicht

```bash
./build/cnc-sim --render --bind 127.0.0.1 --steps-per-unit 400,400,400,400
```

Das Beispiel öffnet die lokale Ansicht ohne LinuxCNC-Verbindung. Für LinuxCNC
im vorhandenen Netzwerk-Namespace siehe den [real abgenommenen Grafik-Start
mit Phase-3-VirtualIO und G53-Test](docs/phase4a1-test.md).
Die bisherige Nutzung ohne `--render` bleibt erhalten.

Dargestellt werden ein 50×50×10-mm-Rohteil (Oberseite Z=0), ein 6-mm-Flachfräser,
Tisch und XYZ-Achsen in Rot/Grün/Blau. Die Werkzeugspitze folgt ausschließlich den
bestehenden int64-Step-Positionen. Linke Maustaste: Orbit; mittlere: Pan;
Mausrad: Zoom; Home: Fit Scene. Der Fenstertitel zeigt XYZ in mm.

Kompatible Startoptionen: `--voxel-size 0.1`, `--stock-size 50,50,10`,
`--stock-origin 0,0,-10`, `--stock-rotation 0,0,0` (Grad, Rz·Ry·Rx).
Sparse-Volumen und CPU-Mesher sind OpenGL-unabhängig; keine Voxelarrays für das
unbearbeitete Rohteil. Phase 5 ergänzt den unten beschriebenen kontinuierlichen Materialabtrag.

## Phase 5: kontinuierlicher Materialabtrag

```text
tool flat-end 6 20
material on
material show
material off
material reset
```

Material startet **OFF**. Erst nach Homing und Positionierung bewusst einschalten.
Der Flachfräser verwendet die tatsächliche G53-Werkzeugspitze als Bottom-Centre
und schneidet von dort 20 mm entlang +Z. `tool show` zeigt seine Konfiguration.
ON erfasst auch die aktuelle stehende Position; OFF erhält vorhandene Schnitte.
Reset erzeugt das Rohteil aus der aktuellen Werkstückkonfiguration neu und behält
Werkzeug und Ein/Aus-Zustand. `workpiece reset` stellt dagegen die ursprüngliche
Werkstückkonfiguration wieder her.

Eine verlustfreie geordnete FIFO übergibt integrierte XYZ-Bewegungen und
Steuerereignisse an einen separaten Material-Worker. Dieser prüft analytische
kontinuierliche Zylinder-Sweeps gegen Voxelzentren, entfernt Material dauerhaft
und übergibt geänderte Chunks samt Nachbarn als Snapshot an den Mesh-Pool.
OpenGL erhält unveränderliche,
konsistente Mesh-Stände; Kamera und Werkzeugposition bleiben unabhängig bedienbar.
UDP wartet weder auf Materialberechnung noch Meshing oder OpenGL.

Befehlsantworten bestätigen die Einreihung; `material show` zeigt den verarbeiteten
Zustand, Queue-Rückstau, Schnitt-/Meshzähler, Volumen und Laufzeiten. Bei Überlast
wächst die Queue im RAM, ohne Bewegungen zu verwerfen. Speicher-/Workerfehler
beenden die Simulation ausdrücklich als unvollständig. Dies ist keine harte
Echtzeitgarantie des Betriebssystems oder Speicherallokators.

Die Voxelzentrenregel ist binär (255 -> 0), auflösungsabhängig und nicht CAD-exakt.
Keine Kollisionen, Schnittkräfte, A-Achsen-Rotation oder automatische G54/G55-
Verarbeitung. Mathematik und Grenzen: [Phase-5-Design](docs/phase5-design.md).
Build-/Testergebnisse und **exakte Befehle für die noch ausstehende reale Abnahme**:
[Phase-5-Tests](docs/phase5-test.md).

## Werkstück zur Laufzeit konfigurieren (Phase 4A.1)

```text
workpiece size 100 60 20
workpiece position 50 30 0
workpiece origin 10 center max
workpiece voxel 0.10
workpiece show
workpiece reset
```

`size` sind die physischen Rohteilabmessungen in mm. `position` ist die
**G53-Maschinenposition des ausgewählten Bezugspunkts**. `origin` ist dessen
**Offset vom Rohteilminimum**, pro Achse als mm-Zahl oder `min`, `center`, `max`.
Gemischte Eingaben sind zulässig. Es gilt `machine_min = position - origin`
und `machine_max = machine_min + size`. Das Beispiel ergibt Min (40,0,-20)
und Max (140,60,0) mm. LinuxCNC verwaltet G54/G55; der Simulator wendet keinen
zusätzlichen Work Offset an.

Die Symbole werden einmalig in Zahlen aufgelöst. Größenänderungen behalten den
numerischen Origin; liegt er danach außerhalb `[0,size]`, wird die Änderung
vollständig abgewiesen. Auch NaN/Inf, nichtpositive Größen/Auflösungen,
Indexüberläufe und Jobs über 65536 Chunks werden abgewiesen.

Jede gültige Änderung erstellt frisches Rohmaterial, erscheint ohne Neustart im
OpenGL-Fenster und aktualisiert Home/Fit. Das Werkzeug bleibt an seiner tatsächlichen
Maschinenposition. UDP läuft unabhängig vom Rebuild weiter. Alle Befehle
funktionieren auch headless. `show` zeigt die vollständige Transformation.
`reset` stellt die zentralen Defaults wieder her: Größe 50×50×10 mm,
Position (0,0,0), Origin (0,0,10), Voxel 0,10 mm; Oberseite Z=0.

Die alte CLI-Option `--stock-origin` bleibt eine Minimum-Translation. Eine mit
`--stock-rotation` gestartete Szene bleibt bis zum ersten gültigen schreibenden
`workpiece`-Befehl rotiert; die Konsole meldet den Wechsel zum achsparallelen
Modell. Nicht teilbare Abmessungen bleiben wie in Phase 4A nach außen gerastert;
`show` zeigt die ungerundeten physischen Grenzen. Details und Grenzen:
[Phase-4A.1-Architektur](docs/phase4a1-design.md).

```bash
# Grafiktests bewusst separat aktivieren (Desktop erforderlich):
cmake -S . -B build -DCNC_SIM_RENDER_TESTS=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure

# Komplett ohne OpenGL-/GLFW-Abhängigkeit bauen und headless testen:
cmake -S . -B build-headless -DCNC_SIM_RENDER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-headless -j4
env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir build-headless --output-on-failure
```

Historischer Phase-4A.1-Stand: **26/26 Tests mit Grafiktests**, **24/24 im Headless-Release-Build**,
keine Fehler/Skips; auch OpenGL 3.3 Core auf Mesa/radeonsi erfolgreich geprüft.
Details: [Architektur und Entscheidungen](docs/phase4a-design.md),
[automatisierte Testergebnisse](docs/phase4a-test.md),
[reale Phase-4A.1-Abnahme](docs/phase4a1-test.md).

## Start für LinuxCNC

```bash
sudo ./scripts/setup-veth.sh
sudo ./scripts/run-simulator.sh --steps-per-unit 400,400,400,400
```

LinuxCNC verwendet `ip_address="192.168.50.2:8888"`. Ein eigenständiges
XYZ-Beispiel liegt unter `examples/phase1/`. Die vollständige Anleitung
einschließlich Bau/Installation des unveränderten HAL-Moduls, Referenzierung,
10-mm-Test und Teardown steht in [docs/phase1-test.md](docs/phase1-test.md).

Nach Beenden von LinuxCNC und Simulator:

```bash
sudo ./scripts/teardown-veth.sh
```

Die Skripte sind vom aktuellen Arbeitsverzeichnis unabhängig. Sie überschreiben
keine bestehenden LinuxCNC-Konfigurationen. Ohne Root brechen sie mit einer
verständlichen Meldung ab; sie fordern selbst kein Passwort an. Setup kann
für eigene bestehende Ressourcen wiederholt werden. Fremde gleichnamige
Ressourcen werden nicht entfernt. Teardown verlangt, dass alle Prozesse den
Namespace verlassen haben. Der Startwrapper verwendet standardmäßig
`build/cnc-sim`; für andere Buildverzeichnisse kann `CNC_SIM_BINARY` auf einen
absoluten Programmpfad gesetzt werden.


## Phase 3: virtuelle Inputs, Endschalter und Probe

Phase 1, 2 und 3 sind mit echtem LinuxCNC erfolgreich getestet.
Phase 3 ergänzt die Rückrichtung über die **bestehenden** vier 32-Bit-Inputwörter.
Ohne Sensor-Konfiguration bleiben alle automatischen Inputs aus und die bisherigen
Optionen, Recorderbefehle und die Netzwerkarchitektur erhalten.

```bash
sudo ./scripts/setup-veth.sh
sudo ./scripts/run-simulator.sh --steps-per-unit 400,400,400,400 --io-config examples/phase3/virtual-io.conf
# Separates Terminal, normaler Benutzer:
linuxcnc examples/phase3/phase3.ini
```

Wichtiger Originalbefund: 128 Wire-Bits, aber nur **22/26/27/28** werden als
`stepgen-ninja.0.input.gp22/gp26/gp27/gp28` exportiert, jeweils mit inversen
`-not`-Pins. Input 0 ist kein Alias für GP22. Deshalb teilen sich X-Min/Max,
Y-Min/Max und Z-Min/Max jeweils einen Eingang (22/26/27); Probe verwendet 28.
Die sieben Sensoren bleiben getrennt modelliert und frei zuordenbar. HAL verbindet
je Achse den gemeinsamen Eingang mit Home, negativem und positivem Limit.

```text
input show
input 22 on
input 22 off
input clear
limits show
limits set X min -4000 40 22
limits off X min
probe plane Z -2000 28
probe show
probe off
help io
```

Alle Konfigurationspositionen sind exakte Schritte ab Simulatorstart. Die
Beispielschalter liegen bei X/Y=-10 und +80 mm, Z=-30 und +10 mm, mit 40 Schritten
(0,1 mm bei Scale 400) Hysterese. Min schaltet bei p≤Trigger, löst erst bei
p≥Trigger+Hysterese; Max entsprechend umgekehrt. Manuelle Bits werden mit den
automatischen Quellen geodert; `off` entfernt nur den manuellen Override.
`input show` zeigt sämtliche manuellen, automatischen und effektiven Wörter.

Die Probe prüft einen Punkt gegen den Halbraum Z≤-5 mm und berechnet einen
analytischen Eintrittskontakt zwischen vorheriger und aktueller Pose. Sie ist
im Startprofil **aus** und wird erst nach der Z-Referenzfahrt aktiviert, da diese
die spätere Oberfläche durchquert. LinuxCNC führt Homing, Limitreaktionen und
G38.2 selbst aus. Es gibt keine Simulatorpositionskorrektur oder G-Code-Auswertung.

`Simulation` wertet Sensoren nach gültiger Schrittintegration und vor dem
Response-Aufbau aus: Inputs und Position gehören zum selben Paket; die
Originalprüfsumme wird danach berechnet. Die Sensoren werden im bestehenden
UDP-Thread synchron ausgewertet; Eingabe, Darstellung und Datei-I/O bleiben
getrennt. Inputänderungen ohne Bewegung erzeugen keine MotionSamples.

Die feste Terminalanzeige zeigt zusätzlich sechs Schalter, Probe, relevante
Wire-Bits und manuelle Overrides. Die **vollständige Anleitung mit echten
HAL-Pins, Homingparametern, G38.2 und Testbefunden** steht in
[docs/phase3-test.md](docs/phase3-test.md). Die reale Phase-3-Abnahme ist
erfolgreich abgeschlossen und ergänzt die automatisierten Tests.

## Interaktive Bedienung und Recorder

Ohne Diagnoseoptionen erscheint auf einem Terminal eine feste ANSI-Anzeige mit
5 Hz: Verbindungsstatus, alle Fehler-/Paketzähler, vier Positionen in Schritten
und Einheiten, Recorderzustand, Samplezahlen und `Command >`. Mindestens 80×24
Zeichen verwenden. Es entstehen keine fortlaufenden Statuszeilen. Backspace,
Ctrl+U (Eingabe löschen), Ctrl+C und `quit` werden unterstützt; Terminalattribute
und normaler Bildschirm werden beim Beenden wiederhergestellt.

| Befehl | Bedeutung |
|---|---|
| `record begin` / `begin record` | Neue Aufnahme mit genau einem Startsample; ersetzt eine gestoppte Aufnahme. Während aktiver Aufnahme wirkungsloser Hinweis. |
| `record stop` / `stop record` | Aufnahme stoppen, Daten behalten. Wiederholtes Stop ist harmlos. |
| `record clear` / `clear record` | Stoppen und sämtliche aktuellen Samples freigeben. |
| `record save <datei>` / `save record <datei>` | Konsistenten CSV-Snapshot exportieren; laufende Aufnahme bleibt aktiv. |
| `status` | Aktuellen vollständigen Status abrufen. |
| `help` | Befehle, Kurzbeschreibung und Aliase anzeigen. |
| `quit` | Sauber beenden; bereits gestarteten Export abschließen. |

`begin` übernimmt die aktuelle vollständige Maschinenposition mit `sequence=0`,
`time_us=0`, `changed_axes=0`. Danach entsteht genau ein Sample pro gültigem
Paket mit tatsächlicher Positionsänderung, auch bei simultaner Bewegung mehrerer
Achsen. Stillstand und verworfene Pakete erzeugen keine Samples. Integer-Schritte
bleiben autoritativ; Mikrosekunden stammen aus `steady_clock` relativ zu `begin`.
Bits 0/1/2/3 in `changed_axes` stehen für X/Y/Z/A. Die Folge beginnt pro Aufnahme
neu; eine Aufnahme setzt niemals die Maschinenposition zurück.

Dateinamen sind relativ zum **Arbeitsverzeichnis beim Programmstart**, auch über
`run-simulator.sh`. Nach `cd ~/dev/linuxcnc-sim` liegt `circle.csv` dort. Der ganze
Rest hinter `save` ist der Dateiname; Leerzeichen sind erlaubt, Shell-Anführungszeichen
werden nicht ausgewertet. Elternverzeichnisse müssen existieren. Bestehende
Dateien und Symlinks werden atomar abgewiesen, niemals überschrieben. Ein leerer
Recorder wird nicht gespeichert. Es läuft höchstens ein Export gleichzeitig;
Erfolg oder Fehler erscheint in der Meldungszeile. Bei Start mit sudo gehören
exportierte Dateien root (normalerweise für den Benutzer lesbar).

CSV beginnt mit drei `#`-Metadatenzeilen; CSV-Leser müssen Kommentarzeilen
überspringen. Skalierung und Einheiten beschreiben die lokale Konfiguration:

```csv
# cnc-sim motion record
# steps_per_unit=400,400,400,400
# axis_units=mm,mm,mm,unit
sequence,time_us,x_steps,y_steps,z_steps,a_steps,changed_axes
0,0,8000,8000,0,0,0
1,12000,8001,8000,0,0,1
2,13000,8002,8001,0,0,3
```

Die Aufnahme wächst im RAM in Blöcken von 1024 Samples, ohne feste Samplegrenze.
Bei Allokationsfehlern stoppt der Recorder sichtbar mit ERROR, behält den gültigen
Präfix und markiert einen Export als `# incomplete=...`; UDP läuft weiter.
Ein fehlgeschlagener Neustart lässt die vorige Aufnahme erhalten. Speichern
löscht keine Samples; Clear/Begin verändern einen bereits gestarteten Export
nicht. Ein Prozessende ohne Export verwirft die RAM-Aufnahme.

Ein eigener Thread besitzt UDP, Protokoll, MachineState und Recorder. Befehle
werden zwischen vollständigen Paketen bearbeitet. Die Konsole pollt Eingabe
separat und erhält konsistente kurze Statuskopien über eine Mutex-Mailbox.
Ausgabe, Eingabewarten und CSV-Dateioperationen halten diesen Mutex niemals.
Ein zeitweise vorhandener Exportthread schreibt unveränderliche Blöcke; beim
Snapshot werden maximal 1024 Samples kopiert, unabhängig von der Gesamtlänge.
Auch blockierte Terminalausgabe hält deshalb UDP nicht an. Dies ist keine
Garantie harter Echtzeit unter beliebiger Systemlast.

Ohne TTY gibt es keine ANSI-Steuerzeichen und keine periodische Ausgabe im
Standardmodus. Befehle funktionieren auch über stdin-Pipes; EOF allein beendet
den UDP-Dienst nicht. `--no-stats` aktiviert diesen Modus explizit. Die manuelle
Abnahme mit LinuxCNC steht in [docs/phase2-test.md](docs/phase2-test.md).

## Optionen und Protokollsemantik

`./build/cnc-sim --help` beschreibt die Optionen:

* `--bind`, `--port`: Standard `192.168.50.2:8888`; Loopback/Port 0 sind für Tests
  möglich, ohne die Produktionsarchitektur zu verändern.
* `--steps-per-unit X,Y,Z,A`: lokale Skalierung, standardmäßig jeweils 1000
  entsprechend der Originalkonfiguration. Für das Beispiel explizit 400 wählen.
* `--units mm,mm,mm,unit`: reine Anzeigebezeichnungen, keine automatische Umrechnung.
* `--stats` / `--stats-ms N`: expliziter Legacy-Modus mit fortlaufender Statistik
  und Positionen; `--no-stats` zeigt Befehlsantworten und die Abschlussstatistik.
* `--verbose`: zeigt im Textmodus die zuletzt veröffentlichten Paketzähler mit 5 Hz.
  Einzelpaket-Logs wurden durch zusammengefasste Diagnose ersetzt, damit keine
  Ausgabewarteschlange im UDP-Pfad wächst. SIGINT/SIGTERM beendet sauber.

Autoritative Positionen sind ganzzahlige Schritte und starten bei null. Eine
Position in mm entsteht nur aus der lokal konfigurierten Skalierung. Das
Protokoll überträgt weder Skalierung noch absolute Startposition, und das
Programm interpretiert die vier Motorachsen nur zur Anzeige als X/Y/Z/A.

Wire-Kompatibilität: unveränderter C-Protokollkern, Board-0-Profil, 4 Stepgens,
3 Encoder, 37 Byte Anfrage, 61 Byte Antwort, Originalprüfsumme, ID-Echo modulo
256. Inputs enthalten die konfigurierten virtuellen Zustände; Encoderzähler/-geschwindigkeiten,
Indexflags und Ringstatus bleiben null; Encoderzeitstempel laufen in Mikrosekunden seit Simulatorstart weiter.
Jitter ist der Abstand gültiger Pakete in Mikrosekunden. `enc_control`,
Ausgangsbits und PWM-Felder bewirken in Phase 1 noch keine Hardwarefunktion.

Ein gültiger Burst wird bei Empfang vollständig integriert. Die Timingbits
werden getrennt dekodiert, aber elektrische Pulsflanken und PIO-FIFOs werden
noch nicht zeitlich emuliert. Das entspricht dem Umfang dieser Phase; es ist
keine neue Bahnplanung. Der Original-Software-Schrittring ist im verwendeten
Profil abgeschaltet.

Längenfehler, Prüfsummenfehler, ungültiger PIO-Index oder Positionsüberlauf
bewirken keine Positionsänderung und keine Antwort. Danach wird mit dem nächsten
gültigen Paket normal fortgefahren; bekannte Firmwarefehler werden nicht
absichtlich nachgebildet. ID-Abweichungen werden gezählt und synchronisiert,
Duplikate/umgeordnete Pakete entsprechend dem Original erneut integriert.
Es gibt keine Retransmission und keine Schätzung verlorener Schritte.

Das Programm simuliert eine Maschine mit einem Sender. Es antwortet jedem
gültigen Datagramm an dessen tatsächliche Quelladresse/-port; mehrere Sender
würden denselben Motorzustand und ID-Zähler bedienen. Die Statistik
`Packet-ID gaps` zählt Abweichungsereignisse, keine eindeutig rekonstruierbare
Anzahl verlorener Pakete. Nach längeren Pausen bleiben virtuelle Positionen
und Sequenzzustand erhalten; eine neue Referenz entsteht durch Prozessneustart.

## Quellen und Grenzen

* [Phase-3-Abnahme, Original-HAL-Mapping und Sensorik](docs/phase3-test.md)
* [Phase-2-Abnahme und Architektur](docs/phase2-test.md)
* [Phase-1-Aufgabe](docs/CODEX_PHASE1_VIRTUAL_STEPPER_NINJA.md)
* [Wire-Protokollanalyse](docs/stepper-ninja-protocol.md)
* [Übernahmeliste und Lizenzbefund](docs/stepper-ninja-files.md)
* [Herkunft des unveränderten C-Codes](third_party/stepper-ninja/UPSTREAM.md)
* [Testanleitung und Implementierungsbefunde](docs/phase1-test.md)

Die eingebundenen Originalquellen in `stepper-ninja/` bleiben unverändert.
Das Produktionsprogramm baut den Protokollkern aus `third_party/` und unseren
Code; der Test `original-hal-inputs` benötigt zusätzlich den Original-HAL-Code
in `stepper-ninja/`. Beide Bestände sind als normale Dateien im Repository
enthalten, einschließlich der benötigten relativen Symlinks, ohne Submodule.
Herkunft, bewusst ausgeschlossene Upstream-Artefakte, Verzeichnisstruktur und
lokale/generierte Dateien beschreibt [docs/repository.md](docs/repository.md).
Paketgrößen, alle Offsets, Profil und Little Endian werden beim Kompilieren abgesichert. Das Programm unterstützt in dieser
Phase Linux auf Little-Endian-Systemen. Die optionale OpenGL-Ansicht erweitert
den Simulator um Visualisierung. Phase 5 ergänzt persistenten Voxel-Materialabtrag. Qt, EtherCAT und ein eigener
G-Code-Interpreter sind nicht enthalten.

Die Antwort bestätigt den Empfang, keine mechanisch gemessene Bewegung. Der
originale HAL-Treiber erzeugt `motor-pos-fb` selbst aus seinem Sollwert. Ein
erfolgreicher UDP-Test beweist deshalb noch nicht den vollständigen Lauf mit
LinuxCNC oder harte Echtzeitfähigkeit eines normalen Userspace-Prozesses.
