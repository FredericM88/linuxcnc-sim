# Phase 3: Virtual I/O, gemeinsame Home-/Limitschalter und Probe

## Ausgangsbasis und wichtiger Befund

**Status: Phase 1, Phase 2 und Phase 3 sind abgeschlossen und real mit LinuxCNC
getestet.** Die vom Benutzer bestätigten Phase-3-Messergebnisse stehen im
Abschnitt [Reale Phase-3-Abnahme mit LinuxCNC](#reale-phase-3-abnahme-mit-linuxcnc).
Die nachstehenden manuellen Testschritte dienen der Wiederholung der Abnahme.

Diese Phase erweitert den bestehenden Rückkanal. Originalcheckout und die neun
importierten Originaldateien bleiben unverändert. Request/Response bleiben
37/61 Byte, mit derselben Prüfsumme, ID- und Encodersemantik. UDP, MachineState,
MotionRecorder, CSV und veth-/Namespace-Skripte werden weiterverwendet.

**128 Wire-Bits sind nicht 128 exportierte HAL-Eingänge.** Das aktuelle originale
Board-0-Profil exportiert nur vier normale digitale Eingänge und vier inverse
Gegenstücke. Deshalb können die sechs Schalter und die Probe nicht als sieben
unabhängige HAL-Eingänge angeschlossen werden. Die Beispielkonfiguration nutzt
einen gemeinsamen Min-/Max-/Home-Eingang je Achse plus einen Probe-Eingang.
Die Min- und Max-Sensoren selbst bleiben getrennt konfigurierbar und sichtbar;
ihre Signale werden bei gleicher Inputnummer logisch ODER-verknüpft. Die
Schalterseite lässt sich aus dem gemeinsamen HAL-Pin allein nicht unterscheiden.
Dies entspricht einer normalen gemeinsamen Schalterverdrahtung; es ist keine
Erweiterung des Treibers und keine neue Wire-Codierung.

## Verbindliches Original-Mapping

Quellen im unveränderten Checkout:

- `firmware/modules/inc/transmission.h:35–48`: gepackte Response einschließlich
  vier `uint32_t inputs`, anschließend Jitter, Ringstatus, ID und Checksumme.
- `firmware/inc/config.h:20,44–45`: Board 0, `in_pins = {PIN_29, PIN_31, PIN_32, PIN_34}`,
  Pull-ups für diese vier physischen Pins.
- `firmware/inc/internals.h:128–131`: Pin-Aliase ergeben GPIO **22,26,27,28**.
- `hal-driver/stepgen-ninja.c:80–85`: `input_pins[]` und dessen Anzahl aus der Konfiguration.
- `hal-driver/modules/breakoutboard_hal_0.c:15–38`: Export der normalen und der
  `-not`-Pins als `HAL_OUT`.
- Dieselbe Datei, Zeilen 55–65: GPIO-Nummer bestimmt das Wire-Bit, nicht der
  Index 0..3 in der `input_pins`-Liste; `input_not = !input`.
- `hal-driver/stepgen-ninja.c:480–592`: Längen-/Checksumprüfung und anschließender
  Aufruf des Board-Empfangscodes.
- `firmware/src/main.c:544–560,873–876`: Pull-ups sind Hardwarekonfiguration;
  `gpio_get_all64()` liefert die GPIO-Bits unverändert in die ersten zwei Wörter.
  Es gibt hier keine durch Pull-ups implizierte logische Invertierung.

| Wire-Index | Feld/Bit | Normaler Original-HAL-Pin | Invertierter Pin | Beispielquelle |
|---:|---|---|---|---|
| 22 | `inputs[0]`, Bit 22 | `stepgen-ninja.0.input.gp22` | `stepgen-ninja.0.input.gp22-not` | X-Min ODER X-Max |
| 26 | `inputs[0]`, Bit 26 | `stepgen-ninja.0.input.gp26` | `stepgen-ninja.0.input.gp26-not` | Y-Min ODER Y-Max |
| 27 | `inputs[0]`, Bit 27 | `stepgen-ninja.0.input.gp27` | `stepgen-ninja.0.input.gp27-not` | Z-Min ODER Z-Max |
| 28 | `inputs[0]`, Bit 28 | `stepgen-ninja.0.input.gp28` | `stepgen-ninja.0.input.gp28-not` | Probe |
| alle übrigen | entsprechende Wire-Bits | **kein Input-HAL-Pin in diesem Profil** | keiner | manuell/automatisch auf dem Wire testbar |

`input 0 on` setzt also wirklich Wire-Bit 0, nicht den ersten exportierten
GPIO-Pin. `input 127 on` setzt `inputs[3]` Bit 31; der aktuelle HAL-Treiber
ignoriert es. Die Konsole weist bei solchen manuellen Inputs darauf hin.
Andere Boardprofile werden nicht stillschweigend aktiviert; ihre Paketlayouts
und Pinexporte sind teilweise anders.

Offsets in der unveränderten 61-Byte-Response: `inputs[0..3]` beginnen an Byte
37/41/45/49, Little Endian; die Prüfsumme steht an Byte 60. Allgemein ist
Input n das Bit `n % 32` im Wort `n / 32`. Ein Wert 1 ergibt am normalen HAL-Pin
TRUE, am `-not`-Pin FALSE. Alle Sensoren dieser Beispielmaschine sind aktiv-high
und werden mit den normalen Pins verdrahtet.

## Komponenten und Paketablauf

Neue C++20-Komponenten:

- `src/io/VirtualIO.*`: 128 manuelle und 128 automatische Zustandsbits;
  effektiver Zustand = manuell ODER automatisch.
- `src/io/Sensors.*`: sechs unabhängige Min-/Max-Schalter, Punktprobe,
  abstrakte `ContactGeometry` und analytische `PlaneGeometry`.
- `src/io/VirtualSensors.*`: zentrale Konfiguration/Zuordnung und Zusammenführung
  aller automatischen Quellen. Alle Quellen derselben Inputnummer werden geodert.
- `src/console/IOCommands.*`: Syntax, Konfigurationsdatei und lesbare Diagnosen;
  Dateilesen und Formatierung bleiben außerhalb des UDP-Threads.

`StepperNinjaProtocol::accept()` enthält die bisherige Validierung und atomare
Schrittintegration. Nur nach erfolgreichem Accept wertet `Simulation` Sensoren
mit vorherigem und aktuellem MachineState aus. `make_response()` übernimmt dann
die vier fertigen Inputwörter und berechnet zuletzt die Originalprüfsumme.
Der bisherige `process()` bleibt als kompatible Kombination mit Null-Inputs
für bisherige Aufrufer/Tests erhalten. Die Produktionssimulation nutzt:

```text
receive -> accept/validate/decode/integrate -> sensors.update(previous,current)
        -> make_response(inputs, checksum) -> UDP send -> MotionRecorder
```

Damit enthalten Position und Sensoren denselben verarbeiteten Bewegungszustand.
Ungültige Pakete erzeugen weder Bewegung, Sensor-Update, Recorder-Sample noch
Antwort. Es gibt keine Sensorlogik im UDP-Socket und keine LinuxCNC-/UI-Logik
in Protokoll oder MachineState. MachineState wird niemals durch Homing, Limits
oder Probe korrigiert, geklemmt oder zurückgesetzt.

Der vorhandene UDP-Thread besitzt VirtualIO und Sensoren exklusiv. Befehle werden
an Paketgrenzen über die bestehende Mailbox übernommen; konsistente IOStatus-
Kopien gehen mit der bisherigen Statusveröffentlichung an die UI. Keine neuen
Threads für Sensorik, kein Dateilesen/Schreiben oder Terminalwarten im Paketpfad.
Sensorberechnung pro Paket ist klein und ohne dynamische Allokation.

## Befehle und Konfiguration

Ohne `--io-config` sind sämtliche Sensoren und manuellen Bits aus. Dies erhält
das bisherige Phase-1/2-Verhalten. Startkonfigurationen werden vor dem Start des
UDP-Arbeitsthreads gelesen. Die neue Option ergänzt alle bestehenden CLI-Optionen:

```bash
./build/cnc-sim --steps-per-unit 400,400,400,400 --io-config examples/phase3/virtual-io.conf
```

Die Textdatei enthält je Zeile einen I/O-Befehl; Leerzeilen und `#`-Kommentare
sind erlaubt. Ein fehlerhafter Befehl verhindert den Start und nennt Datei und
Zeile. Relative Konfigurationspfade beziehen sich auf das Start-Arbeitsverzeichnis.

| Befehl | Wirkung |
|---|---|
| `input show` | Alle vier effektiven, manuellen und automatischen Wörter vollständig in Hex anzeigen. |
| `input <0..127> on` | Manuellen Testzustand einschalten. |
| `input <0..127> off` | Manuellen Testzustand entfernen; eine aktive automatische Quelle bleibt wirksam. |
| `input clear` | Alle manuellen Overrides löschen, automatische Quellen behalten. |
| `limits show` | Zustand, Trigger, Hysterese und Inputnummer aller sechs Schalter. |
| `limits set X min <steps> <hysteresis_steps> <input>` | Schalter konfigurieren und aktivieren; X/Y/Z und min/max möglich. |
| `limits off X min` | Genau diesen Sensor deaktivieren. Andere Quellen desselben Bits bleiben erhalten. |
| `probe show` | Aktivität, Ebene, Input und letzten berechneten Eintrittskontakt anzeigen. |
| `probe plane Z <surface_steps> <input>` | Punktprobe für den Halbraum Achskoordinate ≤ Ebene aktivieren; X/Y/Z möglich. |
| `probe off` | Automatische Probe deaktivieren und Kontaktdiagnose löschen. |
| `help io` | Kurze Syntaxübersicht für Sensoren. |

Die sieben logischen Sensorzuordnungen sind zentral in `virtual-io.conf`
konfiguriert und auch zur Laufzeit änderbar. Beim Umkonfigurieren wird der
betroffene Sensorzustand neu aus der aktuellen Position ausgewertet. Alte
Inputbits bleiben dabei nicht versehentlich gesetzt. Ein physisch aktiver
Sensor wird durch `input n off` nicht überstimmt. Die UI zeigt die Zahl manueller
Overrides dauerhaft an; `input show` trennt manuelle und automatische Wörter.

Alle Sensorkoordinaten sind ausdrücklich **int64-Schritte ab Simulatorstart**;
es gibt keine versteckte mm-Konvertierung. Bei 400 Schritten/mm sind 40 Schritte
0,1 mm. Andere Skalierungen benötigen eine bewusst passende Sensor- und
LinuxCNC-Konfiguration. Min/Max beziehen sich auf aufsteigende Schrittkoordinaten;
bei negativen Skalen müssen Richtung und Zuordnung entsprechend gewählt werden.

Die feste Anzeige bleibt bei 5 Hz und passt einschließlich I/O-Diagnosen und
Eingabe in 80×24 Zeichen. Sie zeigt sechs getrennte physische Schalterzustände,
Probe, effektive Wire-Bits 22/26/27/28 und die Anzahl manueller Overrides. `--`
bedeutet deaktivierter Sensor. Recorder, Eingabe, CSV und die bisherigen
Diagnoseoptionen bleiben verfügbar. Inputänderungen allein erzeugen keine
MotionSamples.

## Endschaltermodell und Beispielwerte

| Sensor | Trigger Schritte | Einheit bei 400 steps/mm | Hysterese | Input |
|---|---:|---:|---:|---:|
| X-Min | -4000 | -10 mm | 40 Schritte = 0,1 mm | 22 |
| X-Max | 32000 | 80 mm | 40 Schritte | 22 |
| Y-Min | -4000 | -10 mm | 40 Schritte | 26 |
| Y-Max | 32000 | 80 mm | 40 Schritte | 26 |
| Z-Min | -12000 | -30 mm | 40 Schritte | 27 |
| Z-Max | 4000 | 10 mm | 40 Schritte | 27 |
| Probe, nach Homing aktivieren | -2000 | Z=-5 mm | keine | 28 |

Min: EIN bei `p <= trigger`, AUS erst bei `p >= trigger + hysteresis`.
Max: EIN bei `p >= trigger`, AUS erst bei `p <= trigger - hysteresis`.
Im Zwischenband bleibt der vorherige Zustand erhalten. Bei Hysterese 0 hat am
Trigger die EIN-Bedingung Vorrang: wiederholte Idle-Pakete lassen ihn nicht
flattern. Negative Hysterese, Werte außerhalb int64 und überlaufende
Freigabepositionen werden vor Änderung abgewiesen.

## Eigene LinuxCNC-Konfiguration

`examples/phase3/phase3.ini`, `.hal`, `.tbl` sind eine separate Beispielmaschine;
`examples/phase1/` bleibt unverändert. Original-HAL, Servozeit 1 ms, 400 Schritte/mm,
Positionsmodus und Reihenfolge der HAL-Funktionen bleiben erhalten.

Exakte neue HAL-Netze:

```hal
net x-switch stepgen-ninja.0.input.gp22 => joint.0.home-sw-in joint.0.neg-lim-sw-in joint.0.pos-lim-sw-in
net y-switch stepgen-ninja.0.input.gp26 => joint.1.home-sw-in joint.1.neg-lim-sw-in joint.1.pos-lim-sw-in
net z-switch stepgen-ninja.0.input.gp27 => joint.2.home-sw-in joint.2.neg-lim-sw-in joint.2.pos-lim-sw-in
net probe-contact stepgen-ninja.0.input.gp28 => motion.probe-input
```

Ein gemeinsamer Achsschalter macht beide Limitpins TRUE. LinuxCNC entscheidet
über Stop und Freifahrt. `HOME_IGNORE_LIMITS=YES` ignoriert ausschließlich die
Limits der gerade referenzierenden Achse während ihrer Referenzfahrt.
Die dokumentierte LinuxCNC-Kombination „both limit switches and the home switch
for one axis“ ist hier maßgeblich, nicht sieben erfundene GPIO-Pins.

Homingparameter pro Joint:

- `HOME_SEARCH_VEL=-2` mm/s und `HOME_LATCH_VEL=-0.2` mm/s: negative Suchfahrt,
  Bremsen, Freifahren in positiver Richtung, erneute langsame negative Anfahrt.
  Wenn der Schalter bereits anfangs aktiv ist, fährt LinuxCNC zuerst frei.
- `HOME_OFFSET=-10` für X/Y und `-30` für Z: physischer Trigger in mm.
- `HOME=0`, `HOME_FINAL_VEL=5`: nach Latch zur freien Nullposition fahren.
- `HOME_USE_INDEX=NO`, `HOME_IGNORE_LIMITS=YES`, `HOME_SEQUENCE=0,1,2`:
  nacheinander X/Y/Z ohne Encoderindex referenzieren.
- Softlimits X/Y=-9..79 mm und Z=-29..9 mm liegen innerhalb der physischen
  Schalter; im referenzierten Normalbetrieb verhindert LinuxCNC das Anfahren
  der Hardlimits. Zum gezielten Schaltertest vor der Referenzierung joggen.

Die Bedeutung dieser Parameter und gemeinsamer Schalter wurde zusätzlich in
der installierten offiziellen Dokumentation geprüft:
`/usr/share/doc/linuxcnc/LinuxCNC_Documentation.pdf`, Abschnitte 3.1 (gemeinsame
Schalter), 4.5.6.1–4.5.6.8 (Homing) und INI-Parameter
`NO_PROBE_HOME_ERROR`/`NO_PROBE_JOG_ERROR`. Diese Fehlerprüfungen werden nicht
abgeschaltet. **Die Probe ist deshalb im Startprofil aus**: Die Z-Homingfahrt
bis -30 mm würde sonst schon die spätere Probe-Ebene bei -5 mm betreten.

## Probe und Sweep

Die Probe ist ein geometrischer Punkt an der aus Steps rekonstruierten XYZ-Pose.
Die Ebene definiert einen geschlossenen Halbraum, im Beispiel Z ≤ -2000 Schritte,
unbegrenzt in X/Y. Es gibt keine lateralen Werkstückgrenzen, Werkzeugradien oder
Materialentfernung. „Außerhalb“ bedeutet bei dieser Geometrie Z > -2000.
Nach Rückzug über die Ebene wird das Signal unmittelbar wieder inaktiv.

`ContactGeometry::evaluate(previous,current)` ist die kleine austauschbare
Geometrieschnittstelle. `PlaneGeometry` klassifiziert den Endpunkt exakt per
Integer-Vergleich und bestimmt beim Eintritt den analytischen Schnittpunkt des
Segments mit der Ebene. Die Diagnose speichert den letzten Eintritt einschließlich
Segmentanteil und Kontaktkoordinaten. Differenzen werden vor der Subtraktion
konvertiert, um int64-Überlauf zu vermeiden; nur die geometrische Interpolation
verwendet `long double`. Autoritative Schritte bleiben int64.

Ein Burst, der die Ebene überschreitet, aktiviert den Input bereits in seiner
Antwort. Der diagnostische Erstkontakt liegt ggf. zwischen den diskreten
Endpunkten; er wird nicht als erfundene Encoderposition übertragen. LinuxCNC
beobachtet das digitale Signal bei seiner normalen Empfangs-/Servoabfolge und
führt selbst den Probe-Stopp aus. Nachlauf durch Abtastung und Bremsen ist normal.
Der Simulator klemmt die Achse nicht auf die Kontaktposition und interpretiert
keinen G-Code. Die einfache Halbebene kann später über die Geometrieschnittstelle
ersetzt werden; allgemeine Szene/Kollision wird hier noch nicht implementiert.

## Manueller Test A–D: Start und Input-Rückkanal

Simulator und LinuxCNC für eine gemeinsame Ausgangsreferenz neu starten.
Den bereits funktionierenden Originaltreiber weiterverwenden, nicht neu
konfigurieren oder durch ein anderes Boardprofil ersetzen.

Terminal A:

```bash
cd ~/dev/linuxcnc-sim
cmake -S . -B build
cmake --build build -j"$(nproc)"
sudo ./scripts/setup-veth.sh
sudo ./scripts/run-simulator.sh --steps-per-unit 400,400,400,400 --units mm,mm,mm,unit --io-config examples/phase3/virtual-io.conf
```

Terminal B, als normaler Benutzer:

```bash
cd ~/dev/linuxcnc-sim
linuxcnc examples/phase3/phase3.ini
```

Zunächst Maschine noch nicht einschalten. Simulator muss CONNECTED zeigen;
RX/TX steigen etwa mit 1000/s, alle Fehler- und Gapzähler sollen 0 bleiben.
Im separaten Terminal C den tatsächlichen Pinexport des installierten Moduls
prüfen:

```bash
halcmd show pin 'stepgen-ninja.0.input.*'
halcmd getp stepgen-ninja.0.input.gp22
halcmd getp stepgen-ninja.0.input.gp22-not
```

Simulator:

```text
input show
input 22 on
```

Terminal C:

```bash
halcmd getp stepgen-ninja.0.input.gp22
halcmd getp stepgen-ninja.0.input.gp22-not
halcmd getp joint.0.home-sw-in
halcmd getp joint.0.neg-lim-sw-in
halcmd getp joint.0.pos-lim-sw-in
```

Erwartet TRUE/FALSE/TRUE/TRUE/TRUE. Simulator:

```text
input 22 off
input show
```

Dieselben HAL-Abfragen müssen FALSE/TRUE/FALSE/FALSE/FALSE ergeben. Erst wenn
beide Richtungen stimmen, den manuellen Rückkanal als erfolgreich markieren.
Optional 26/27/28 ebenso prüfen, bevor die Maschine eingeschaltet wird. Beispiel
für Probe-Pinprüfung: `input 28 on`, `halcmd getp motion.probe-input`, danach
`input 28 off`. Zum Abschluss `input clear`; Manual overrides muss 0 sein.

## Manueller Test E: physischer Endschalter

In LinuxCNC F1 (Not-Aus zurücksetzen), F2 (Maschine ein), noch nicht referenzieren.
Die Schrittposition startet bei 0, Probe ist aus. X vorsichtig mit kleiner
Joggeschwindigkeit negativ fahren (etwa 1 mm/s). Bei X ≤ -4000 Schritten muss
X-Min ON und Wire22=1 werden. LinuxCNC muss den Hardlimit erkennen und stoppen.
Die endgültige Bremsposition kann bereits unter -4000 liegen; dies ist keine
veränderte Schaltschwelle.

Prüfen:

```bash
halcmd getp stepgen-ninja.0.input.gp22
halcmd getp joint.0.neg-lim-sw-in
halcmd getp joint.0.pos-lim-sw-in
```

Erwartet alle TRUE wegen der gemeinsamen Verdrahtung. Für die Freifahrt die
LinuxCNC-Option „Override Limits“ benutzen, Maschine ggf. wieder einschalten und
X positiv vom Schalter weg joggen. Bei X ≥ -3960 Schritten muss X-Min OFF und
Wire22=0 sein; innerhalb des Hysteresebands bleibt er ON. Override danach
zurücknehmen. Keine `setp`-Zwangswerte auf HAL_OUT-Pins schreiben und keine
Simulatorpositionskorrektur verwenden. Alternativ kann LinuxCNC beim anschließenden
Homing auch von einem bereits aktiven Home-Schalter aus selbst freifahren.

## Manueller Test F: echte Referenzfahrt

Im Simulator zuerst sicherstellen:

```text
input clear
probe off
limits show
record begin
```

In LinuxCNC „Home All“ starten. Erwartete Reihenfolge X, Y, Z: Suchfahrt,
Schalter EIN, Bremsen, positive Freifahrt bis Schalter AUS, langsamere negative
Anfahrt bis EIN, anschließend Endfahrt zur Home-Position 0. Es handelt sich
nicht um die sofortige Referenzierung aus Phase 1. HOME_IGNORE_LIMITS gilt nur
während dieser von LinuxCNC ausgeführten Sequenz.

Terminal C danach:

```bash
halcmd getp joint.0.homed
halcmd getp joint.1.homed
halcmd getp joint.2.homed
halcmd getp stepgen-ninja.0.input.gp22
halcmd getp stepgen-ninja.0.input.gp26
halcmd getp stepgen-ninja.0.input.gp27
```

Erwartet drei TRUE für homed und drei FALSE für die freigefahrenen Schalter.
LinuxCNC zeigt Maschinenkoordinaten 0/0/0. Simulatorpositionen liegen nahe
0/0/0 Schritten; kleine Unterschiede sind durch die digitale Abtastung der
Latchposition und Schrittquantisierung möglich. LinuxCNC setzt seinen eigenen
Referenzoffset; die physische Step-Historie wird im Simulator nicht verändert.
Für Vergleiche zwischen beiden Koordinatensystemen diesen Unterschied beachten.

Simulator:

```text
record stop
record save phase3-home.csv
```

Die Aufnahme muss echte negative/positive Fahrbewegungen enthalten, keine
Homing-Teleportation und keine Sampleflut im anschließenden Stillstand. Die
Beispielmaschine stellt bei Schrittänderungen nur lokale Sollwertkopien als
HAL-Motorfeedback bereit; das ist das unveränderte Verhalten des Originaltreibers.

## Manueller Test G: G38.2 gegen die Probe-Ebene

Nach erfolgreichem Homing, Probe noch aus, LinuxCNC-MDI:

```gcode
G21 G90 G53 G1 Z2 F120
```

Stillstand abwarten. Im Simulator die Probe ausdrücklich aktivieren:

```text
probe plane Z -2000 28
probe show
record begin
```

Anfangs Probe OFF. Terminal C:

```bash
halcmd getp stepgen-ninja.0.input.gp28
halcmd getp motion.probe-input
```

Beide FALSE. LinuxCNC-MDI, inkrementelle Fahrt um 12 mm nach unten:

```gcode
G91 G38.2 Z-12 F50
G90
```

Jeden Befehl vollständig ausführen lassen. Der Zielpunkt läge ungefähr bei Z=-10,
doch G38.2 muss zuvor durch den Probe-Input bei der Oberfläche um Z=-5 mm
beendet werden. Diese Koordinaten bleiben innerhalb der Softlimits. Simulator
meldet Probe ON und Wire28=1, beide HAL-Pins TRUE. `probe show` nennt als letzten
geometrischen Eintritt Z=-2000 Schritte. Die Brems-/Endposition darf wegen
Servoabtastung und Nachlauf etwas darunter liegen; sie wird nicht auf -2000
zurückgesetzt. Das von LinuxCNC erfasste Probeergebnis ist die beobachtete
Triggerposition, nicht notwendigerweise der analytische Segmentkontakt.

Terminal C:

```bash
halcmd getp stepgen-ninja.0.input.gp28
halcmd getp motion.probe-input
```

Die gespeicherte Probe-Position in LinuxCNC-MDI ausgeben und in AXIS ablesen:

```gcode
(DEBUG, Probe X=#5061 Y=#5062 Z=#5063)
```

`#5061`, `#5062` und `#5063` sind die hier geprüften G-Code-Parameter für X/Y/Z.
Der HAL-Pin `motion.probed-position` existiert in dieser Konfiguration **nicht**.
`motion.probe-input` zeigt den Kontaktzustand, nicht die gespeicherte Position.

LinuxCNC-MDI zum Rückzug:

```gcode
G90 G53 G1 Z2 F120
```

Nach Überschreiten der Ebene müssen beide Eingangspins wieder FALSE sein.
Simulator:

```text
probe show
record stop
record save phase3-probe.csv
```

Bei „Probe bereits aktiv“ vor Fahrtbeginn zuerst die tatsächliche Position,
Manual overrides und Oberflächenkonfiguration prüfen. Bei „Probe nicht erreicht“
Pinexport, Bit28/HAL-Netz und Geometrie prüfen. Keine speziellen G38.2-Nachrichten
an den Simulator senden. Vor erneutem Homing die Probe mit `probe off` deaktivieren.

## Beenden und Netzwerk

Zuerst LinuxCNC schließen, dann Simulator `quit` oder Ctrl+C. Terminalattribute
müssen wieder normal sein. Fehler-/Checksum-/Timing-/Overflow-/Gapzähler und
Sendefehler sollen weiterhin 0 sein. Anschließend:

```bash
cd ~/dev/linuxcnc-sim
sudo ./scripts/teardown-veth.sh
```

Die bisherigen Namespace-/veth-Skripte sind unverändert. Es wurden während der
Implementierung keine systemweiten sudo-Änderungen ausgeführt.

## Automatisierte Tests

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Neue Tests:

- `io-unit`: alle 128 Bits inklusive Wortgrenzen, OR-Quellen, Min/Max, Hysterese,
  Nullhysterese, Overflow-Abweisung, gemeinsame Eingänge, Punktprobe, Segmentkontakt,
  Rückzug, int64-Extrema, Befehlssyntax, Prüfsumme, gleiches Paket für Position und
  Inputs, ungültige Pakete und unveränderte Recordersemantik.
- `original-hal-inputs`: kompiliert den originalen C-Board-0-Exporter/-Empfänger
  direkt mit kleinen HAL-Teststubs; prüft echte Pin-Namen, HAL_OUT, normale und
  invertierte Werte für jedes der 128 Bits sowie gleichzeitige Inputs.
  Erfordert den Originalcheckout; ohne ihn wird dieser zusätzliche Quelltest
  nicht registriert. Das Produktionsprogramm baut weiterhin nur mit `third_party`.
- `io_integration`: echter Simulatorprozess mit Config, manuellen Befehlen,
  37-Byte-Requests und unabhängig geprüften 61-Byte-Antworten; alle Wortgrenzen,
  manuelles ON/OFF, kombinierte/umkonfigurierte Sensoren, Hysterese, Probe,
  keine Reaktion auf ungültige Pakete und exakte Recorder-CSV.
- `io_terminal`: echtes PTY mit 80×24, aktive Inputs/Sensoren, Show-/Help-Befehle,
  Recorder-/Kommandoanzeige ohne Scrollen und Wiederherstellung des Terminals.
- `phase3-configuration`: Konsistenz der separaten INI/HAL und des Sensorprofils,
  echte Pin-Netze, passende Home-Offsets, Softlimits, Servozeit/Skalierung und
  deaktivierte Probe vor der Homingfahrt.

Alle elf bestehenden Phase-1/2-Tests bleiben unverändert und müssen zusätzlich
bestehen, einschließlich rootlosem veth-/Namespace-Test, Originaldatei-Integrität,
Shellsyntax, Recorder/CSV und blockierter Terminalausgabe.

Historischer Abschlusslauf der Phase-3-Implementierung: CMake-Konfiguration und
paralleler Build erfolgreich,
keine Compilerwarnungen. CTest: **16/16 bestanden, 0 Fehler, 0 übersprungen**,
Gesamtdauer 16,10 Sekunden. Vollständiges Laufprotokoll:
`build/Testing/Temporary/LastTest.log`. Alle vier Betriebsskripte und das
Namespace-Testskript wurden zusätzlich mit `bash -n` geprüft. Der tatsächliche
Start mit `examples/phase3/virtual-io.conf`, `limits show`, `probe show` und `quit`
wurde ohne Root auf Loopback ausgeführt: sechs konfigurierte, inaktive Schalter,
deaktivierte Probe und sauberer Abschluss. Alle Dateien des Originalcheckouts
stimmen mit den vor Phase 3 erfassten SHA-256-Werten überein; sein Git-Status ist
leer. MachineState-, UDP- und Recorder-Quellen stimmen ebenfalls mit ihren
Ausgangshashes überein.

Bei der Phase-3-Implementierung geänderte Dateien:

```text
.gitignore
CMakeLists.txt
README.md
src/main.cpp
src/protocol/StepperNinjaProtocol.hpp
src/protocol/StepperNinjaProtocol.cpp
src/simulation/Simulation.hpp
src/simulation/Simulation.cpp
```

Bei der Phase-3-Implementierung neue Dateien:

```text
src/io/VirtualIO.hpp
src/io/VirtualIO.cpp
src/io/Sensors.hpp
src/io/Sensors.cpp
src/io/VirtualSensors.hpp
src/io/VirtualSensors.cpp
src/console/IOCommands.hpp
src/console/IOCommands.cpp
examples/phase3/phase3.ini
examples/phase3/phase3.hal
examples/phase3/phase3.tbl
examples/phase3/virtual-io.conf
tests/io_tests.cpp
tests/original_hal_inputs.c
tests/io_integration.py
tests/io_terminal.py
tests/phase3_configuration.py
docs/phase3-test.md
```

## Erneute Prüfung vor dem ersten Git-Commit

Am 26.09.2026 erneut ausgeführt:

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Konfiguration und Build erfolgreich, **16/16 Tests bestanden, 0 fehlgeschlagen,
0 übersprungen**, Laufzeit 15,91 Sekunden. Das lokale aktuelle Testprotokoll
liegt unter `build/Testing/Temporary/LastTest.log` und wird nicht committed.
Die Simulatorlogik wurde für Dokumentation und Initialcommit nicht geändert.

## Reale Phase-3-Abnahme mit LinuxCNC

**Erfolgreich abgeschlossen, vom Benutzer am 26.09.2026 berichtet.** Diese
Ergebnisse stammen aus dem realen gemeinsamen Lauf von Simulator und LinuxCNC
mit der Phase-3-Konfiguration und dem originalen Stepper-Ninja-HAL-Treiber.
Sie ergänzen die automatisierten Tests; bei dieser Dokumentationsarbeit wurde
keine erneute interaktive LinuxCNC-Abnahme durchgeführt.

### Stabilität und manueller VirtualIO-Rückkanal

Simulator und LinuxCNC liefen stabil zusammen. Während der Tests galt
**RX = accepted = TX**. Alle folgenden Zähler waren **0**:

- Invalid-Pakete
- Send Errors
- Length Errors
- Checksum Errors
- Timing Errors
- Position Overflows
- Packet-ID Gaps

Im Simulator wurde `input 22 on` ausgeführt. LinuxCNC bestätigte:

```text
halcmd getp stepgen-ninja.0.input.gp22
-> TRUE
halcmd getp stepgen-ninja.0.input.gp22-not
-> FALSE
```

Damit ist der vollständige Rückkanal real bestätigt:

```text
VirtualIO -> Stepper-Ninja Response -> UDP -> original Stepper-Ninja HAL -> LinuxCNC HAL
```

### Virtuelle Endschalter und Homing

Die virtuellen Endschalter funktionierten mit LinuxCNC. Die LinuxCNC-
Referenzfahrt gegen diese Schalter wurde erfolgreich durchgeführt. LinuxCNC
reagierte damit tatsächlich auf die über das originale Stepper-Ninja-Protokoll
übertragenen virtuellen Endschaltersignale.

### Virtuelle Probe und gespeicherte Probe-Position

Im Simulator wurde die Probe-Ebene auf **Z = -2000 Schritte** gesetzt,
bei **400 Schritten/mm** also **Z = -5.000000 mm**, mit **Wire-Input 28**:

```text
probe plane Z -2000 28
```

LinuxCNC führte aus:

```gcode
G91
G38.2 Z-12 F50
```

Nach Kontakt meldete der Simulator `Probe: ON` und `Wire 28=1`.
Die Simulator-Endposition war **Z = -2005 Schritte = -5.0125 mm**.
LinuxCNC bestätigte:

```text
halcmd getp motion.probe-input
-> TRUE
```

Der tatsächlich verwendete HAL-Signalweg war:

```text
stepgen-ninja.0.input.gp28 -> probe-contact -> motion.probe-input
```

Die gespeicherte Probe-Position wurde anschließend über LinuxCNC-MDI geprüft:

```gcode
(DEBUG, Probe X=#5061 Y=#5062 Z=#5063)
```

AXIS meldete:

```text
Probe X=0.000000
Probe Y=0.000000
Probe Z=-5.003050
```

| Messgröße | Z in mm |
|---|---:|
| Geometrische Probe-Ebene (-2000 Schritte) | -5.000000 |
| Von LinuxCNC gespeicherte Probe-Position (`#5063`) | -5.003050 |
| Endgültige Simulatorposition (-2005 Schritte) | -5.012500 |

Diese drei Werte dokumentieren das **Messergebnis dieses realen Tests**.
Die Differenzen sind keine Messung mechanischer Genauigkeit einer realen
Maschine. Der Simulator besitzt keine reale Mechanik, kein Spiel und keine
Motor-/Antriebsdynamik. Geometrischer Kontakt, von LinuxCNC gespeicherte
Triggerposition und endgültige Schrittposition sind unterschiedliche Größen;
eine mechanische Genauigkeit oder Wiederholgenauigkeit lässt sich daraus
nicht ableiten.

### Tatsächlicher Probe-Pinexport

Der Pin `motion.probed-position` existiert in dieser LinuxCNC-Konfiguration
**nicht**. `halcmd show pin | grep -i probe` ergab die Verbindungen:

```text
motion.probe-input <== probe-contact
stepgen-ninja.0.input.gp28 ==> probe-contact
```

Die Positionsprüfung erfolgte deshalb korrekt über **#5061, #5062 und #5063**,
während der HAL-Eingang den Kontaktzustand bestätigte.

## Verbleibende Modellgrenzen

Grenzen: vier exportierte digitale Eingänge im Originalprofil, gemeinsame
Min/Max-Signale je Achse, XYZ-Sensorik in Simulator-Stepkoordinaten, unendlich
breite Punktprobe-Halbebene, keine elektrische Pulssimulation und keine harte
Echtzeitgarantie. A wird weiterhin als Stepgen integriert/aufgezeichnet, hat aber
kein eigenes Schaltermodell. GUI/Qt/OpenGL, Materialabtrag, Mesh-/Voxel-Szene,
allgemeine Maschinenkollision und eigener G-Code-Interpreter sind nicht enthalten.
