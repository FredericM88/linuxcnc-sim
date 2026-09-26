# Phase 2: Interaktive Konsole und Motion Recorder

**Aktueller Status: Phase 2 ist abgeschlossen und real mit LinuxCNC getestet.**
Die erfolgreiche reale Abnahme wurde vom Benutzer bestätigt. Auch Phase 1
und Phase 3 sind abgeschlossen und real mit LinuxCNC getestet; siehe
[Projektstand](../README.md#projektstand) und [Phase-3-Abnahme](phase3-test.md).

## Ausgangsbasis und unveränderte Kommunikation

Der Benutzer hat Phase 1 mit dem originalen LinuxCNC-HAL-Treiber, veth/Namespace,
1-ms-Servozeit und 400 Schritten/mm auf X/Y/Z praktisch bestätigt. Geraden,
beide Richtungen, simultanes XY, unterschiedliche Vorschübe und G2/G3-Kreise
funktionierten. Ein Kreis endete in beiden Systemen exakt bei X=Y=20 mm bzw.
8000 Schritten. Über 600.000 Pakete wurden ohne gemeldete Fehler oder ID-Lücken
verarbeitet. Die Aussagen über einen noch ausstehenden Live-Test in
`phase1-test.md` sind der historische Stand vor dieser Benutzerabnahme.

Phase 2 verwendet dieselben Quellen für `UdpServer`, `StepperNinjaProtocol`,
`MachineState` und den Original-C-Kern. Sie wurden nicht verändert. Paketgrößen
37/61 Byte, Checksummen, Burstdekodierung, ID-Echo, Antwortfelder und
Fehlerbehandlung bleiben erhalten. Der HAL-Treiber bleibt unverändert.

```text
LinuxCNC + originaler HAL (Host, :8888)
    -> veth-lcnc 192.168.50.1/24
    -> veth-sim 192.168.50.2/24 im Namespace cnc-sim-ns
    -> cnc-sim 192.168.50.2:8888
```

Keine neuen Netzwerk-/Installationsskripte, Systemdienste oder Abhängigkeiten
außer der C++-Standardbibliothek mit POSIX-Threads. Kein eigenes G-Code-Parsing,
keine GUI, kein Qt/OpenGL und keine Materialsimulation.

## Bedienung

Normaler Start ohne `--stats`/`--no-stats` öffnet auf einem TTY einen festen
alternativen Terminalbildschirm. Alle Zähler, vier Positionen und Recorderwerte
werden alle 200 ms aktualisiert. Eingabe steht unter der Anzeige und bleibt beim
Refresh erhalten. 80×24 Zeichen oder größer verwenden. Bei zu kleinem Terminal
erscheint ein Hinweis; nach Vergrößern kommt die vollständige Anzeige zurück.

`CONNECTED` bedeutet: mindestens ein gültiges Paket innerhalb der letzten
Sekunde. `WAITING` bedeutet noch kein gültiges Paket, `STALE` eine längere Pause.
Dies ist eine lokale Anzeige, kein zusätzliches Protokollfeld und kein Eingriff
in den originalen LinuxCNC-Watchdog. Positionen und ID-Zustand bleiben bei einer
Pause erhalten.

| Befehl | Alias | Wirkung |
|---|---|---|
| `record begin` | `begin record` | Neue Aufnahme mit Startsample der aktuellen Position; ersetzt gestoppte Daten. Bei bereits aktiver Aufnahme keine Änderung. |
| `record stop` | `stop record` | Stoppen; Samples behalten. Auch ohne aktive Aufnahme gültig. |
| `record clear` | `clear record` | Stoppen und Samples löschen. |
| `record save <datei>` | `save record <datei>` | Snapshot exportieren; aktive Aufnahme läuft weiter. |
| `status` | — | Frischen konsistenten Status abrufen. |
| `help` | — | Übersicht mit Kurzbeschreibungen und Aliasformen. |
| `quit` | — | Beenden und Terminal wiederherstellen. |

Backspace löscht, Ctrl+U leert die Eingabe. Pfeiltasten-Sequenzen werden ignoriert;
History und Cursorbearbeitung innerhalb einer Zeile sind nicht implementiert.
Dateinamen können UTF-8 enthalten; die einfache Terminalanzeige ersetzt
Nicht-ASCII-Zeichen zur verlässlichen Spaltenberechnung durch `?`. Gespeichert
wird der originale Dateiname. Die Eingabe ist auf 8192 Bytes begrenzt; längere
oder ungültige Zeilen werden vollständig verworfen, niemals als verkürzter
Dateiname ausgeführt. Ctrl+C/SIGTERM sowie `quit` räumen den Terminalzustand auf.

Im Textmodus (`--no-stats` oder umgeleitetes stdin/stdout) gibt es Befehlsantworten
und eine Abschlussstatistik ohne ANSI-Sequenzen. EOF beendet den UDP-Dienst
nicht. `--stats`/`--stats-ms` sind explizite fortlaufende Diagnoseausgaben.
`--verbose` zeigt im Textmodus zusammengefasste aktuelle Zähler alle 200 ms;
Einzelpaket-Ausgaben werden nicht mehr im Netzwerkthread erzeugt.

## Recorder, Snapshot und Threading

Der UDP-Thread besitzt Protokoll, Maschinenzustand und Recorder exklusiv.
Ein gültiges Paket wird vollständig integriert und beantwortet. Anschließend
vergleicht der aktive Recorder die vollständigen Integer-Positionen mit dem
zuletzt aufgezeichneten Zustand. Mindestens eine Änderung erzeugt genau ein
Sample mit allen vier int64-Werten und gemeinsamer Achsmaske. Ein ungültiges
Paket erreicht den Recorder nicht. Ein gültiges Nullburst-Paket erzeugt kein
Sample. Ein Sample stellt den nach Empfang integrierten Burst dar, keine
zeitlich aufgelöste elektrische Pulsfolge.

`begin` schreibt unabhängig von Bewegung exakt ein Startsample mit Sequenz 0,
Zeit 0 und Maske 0. Jede neue Aufnahme beginnt wieder mit Sequenz 0 und neuer
Zeitbasis; MachineState wird niemals zurückgesetzt. Die Zeit sind Mikrosekunden
von `std::chrono::steady_clock` relativ zum Start der Aufnahme, monoton nicht
fallend, unabhängig von der Anzahl empfangener Pakete oder gespeicherter Samples.
Maske: X=1, Y=2, Z=4, A=8; XY=3, alle Achsen=15.

Die UI pollt stdin unabhängig mit höchstens 20 ms Wartezeit. Ihre Befehle gehen
über eine kurze Mutex-Mailbox zum UDP-Thread und werden an Paketgrenzen
bestätigt. Im Leerlauf wartet dessen UDP-Poll höchstens 10 ms. Statuskopien werden
etwa alle 20 ms veröffentlicht; Formatierung und Ausgabe passieren nach
Freigabe des Mutex. Es gibt kein Warten auf Terminaleingabe/-ausgabe im UDP-Pfad.
`std::getline` wird ausschließlich zum Zerlegen bereits vorhandener Strings im
UI-Thread verwendet, niemals für stdin oder innerhalb der Netzwerkverarbeitung.

Die dynamische Aufnahme besteht aus Blöcken von 1024 Samples (typisch 56 Byte
pro Sample, rund 56 kB/s bei 1000 bewegenden Paketen/s, plus geringe Blockkosten).
Es gibt keine feste Aufnahmelängengrenze. Abgeschlossene Blöcke sind unveränderlich
und werden durch `shared_ptr` geteilt. Ein Snapshot kopiert höchstens den aktuellen
1024-Sample-Block und übernimmt die vorhandene Blockkette. Somit wird keine
vollständige große Aufzeichnung im UDP-Thread kopiert. Der Export läuft in einem
separaten, nur für diese Operation vorhandenen Thread; maximal ein Export zugleich.
Clear/Begin und weiteres Aufzeichnen verändern seinen Snapshot nicht.

Bei einer Allokationsstörung beim Aufzeichnen stoppt nur der Recorder mit
sichtbarem ERROR. Der bisher vollständige Präfix bleibt erhalten und bekommt beim
Export zusätzlich `# incomplete=allocation failure; valid prefix only`.
Fehlschlag bei `begin` erhält die vorherige Aufnahme. Die Tests injizieren
Allokationsfehler gezielt. Betriebssystemseitiges Beenden durch den OOM-Killer
lässt sich damit nicht abfangen. Normale Speicherverwaltung, Mutex-Scheduling
und Linux-Userspace garantieren keine harte Echtzeit; der tatsächliche Watchdog
wird im anschließenden LinuxCNC-Lauf geprüft.

## CSV und Dateiverhalten

```csv
# cnc-sim motion record
# steps_per_unit=400,400,400,400
# axis_units=mm,mm,mm,unit
sequence,time_us,x_steps,y_steps,z_steps,a_steps,changed_axes
0,0,8000,8000,0,0,0
1,12000,8001,8000,0,0,1
2,13000,8002,8001,0,0,3
```

Diese Beispielzeiten sind illustrativ. Reale Zeiten stammen vom Empfang auf dem
Simulator. CSV-Leser überspringen mit `#` beginnende Kommentarzeilen.
Integer-Werte bleiben dezimal exakt, auch außerhalb der exakten
Fließkommadarstellung und an den int64-Grenzen. `steps_per_unit` und `axis_units`
bezeichnen die lokale Konfiguration, die nicht im Stepper-Ninja-Paket steht.

Relative Pfade beziehen sich auf das Arbeitsverzeichnis beim Programmstart.
Der Namespace-Startwrapper ändert dieses Verzeichnis nicht. Nach `cd` in den
Projektstamm speichert `record save circle.csv` dort. Der gesamte Text nach
`save` ist der Pfad einschließlich innerer Leerzeichen; keine Shell-Expansion,
keine Auswertung von Anführungszeichen oder `~`. Absolute Pfade sind möglich.

Die Datei wird mit `O_CREAT|O_EXCL` geöffnet: vorhandene Dateien einschließlich
Symlinks werden atomar abgewiesen. Ein leerer Recorder erzeugt keine Datei.
Fehlende Verzeichnisse, zu lange Namen und Schreibfehler werden gemeldet.
Bei Schreibfehlern wird die neu erstellte Teil-Datei entfernt; die RAM-Daten
bleiben erhalten. Ein erfolgreicher Export bleibt im RAM und stoppt eine aktive
Aufnahme nicht. Der Snapshot endet an der bestätigten Save-Paketgrenze.
`quit`/Ctrl+C beendet nach einem bereits gestarteten Export; das Terminal wird
vor dem Warten auf den Export wiederhergestellt. Ohne Export gehen RAM-Daten
beim Beenden verloren. Bei Start über sudo werden Dateien als root angelegt.

## Automatisierte Prüfung

```bash
cd ~/dev/linuxcnc-sim
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

| Test | Nachweis |
|---|---|
| `protocol-unit` | Unveränderte Phase-1-Checks: Layout, Originalprüfsummen, Burstgrößen/Richtungen, Positionen, Überlauf, IDs, ungültige Pakete. |
| `recorder-unit` | Start bei 100/200/300/400, 100 gültige Idle-Pakete, ungültiges Paket, X/XY/XYZA, Stop/Clear/Neustart, monotone Zeit, exakte int64-Grenzen, CSV, Blockübergänge, unveränderliche Snapshots, Allokationsfehler. |
| `recorder-integration` | Echter Prozess und UDP plus Befehle/Aliase; Idle→X→XY→Stop→Bewegung→CSV, zweite Aufnahme, Export während Bewegung über mehrere Blöcke, bestehende Dateien/Symlinks und fehlerhafte Namen, blockierte stdout-Ausgabe während 1000 Paketen nominal bei 1 kHz. |
| `terminal-integration` | Echtes PTY: feste Anzeige ohne Newlines, leere/unbekannte Eingabe, Hilfe, Stop/Begin-Randfälle, Save-Fehler, Tippen während UDP, Backspace/Ctrl+U/Pfeiltasten, Resize, quit/Ctrl+C/SIGTERM und exakte Wiederherstellung der Terminalattribute. |
| `udp-integration` | Unveränderter Phase-1-Prozesstest mit 1000 Paketen nominal bei 1 kHz, Antwortfeldern, Fehlerfällen und Endposition. |
| `network-namespace` | Unveränderter rootloser veth-/Namespace-Test mit beiden Port-8888-Endpunkten; Setup/Start/Teardown und Schutzprüfungen. |
| `vendor-integrity` | Neun Originaldateien gegen feste SHA-256-Werte und Originalcheckout. |
| `shell-syntax` + drei Varianten | Bash-Syntax der unveränderten vier Betriebsskripte. |

## Erster manueller Test mit echtem LinuxCNC

Der zuvor erfolgreich verwendete Original-HAL-Treiber bleibt installiert.
Eine erneute Installation ist nicht erforderlich. Simulator und LinuxCNC für
eine gemeinsame Nullreferenz neu starten; nicht nur den Simulator bei bereits
bewegter LinuxCNC-Maschine neu starten.

Terminal A:

```bash
cd ~/dev/linuxcnc-sim
sudo ./scripts/setup-veth.sh
sudo ./scripts/run-simulator.sh --steps-per-unit 400,400,400,400 --units mm,mm,mm,unit
```

Keine `--stats`-Option verwenden: Der Standard ist jetzt die feste Oberfläche.

Terminal B, als normaler Benutzer:

```bash
cd ~/dev/linuxcnc-sim
linuxcnc examples/phase1/phase1.ini
```

In LinuxCNC F1, F2, Home All wie in Phase 1. Simulator muss vorher laufen.
Bei Watchdog-Abbruch LinuxCNC neu starten; keine Änderung am HAL-Treiber.
Zunächst CONNECTED, steigende RX/TX und alle Fehler-/Gapzähler 0 prüfen.

LinuxCNC-MDI: auf den bekannten Kreisstart fahren und Stillstand abwarten:

```text
G21 G90 G53 G1 X20 Y20 Z0 F300
```

Simulator zeigt X=Y=8000 Schritte und Z=0. Dann in seiner Kommandozeile:

```text
record begin
```

Einige Sekunden stehen lassen: Samples=1, Changed samples=0, RX/TX steigen
weiter. In LinuxCNC-MDI einen vollständigen Kreis mit Radius 10 mm fahren:

```text
G17 G91 G91.1 G2 X0 Y0 I-10 J0 F300
G90
```

Der Kreis verwendet inkrementelle Endpunkt- und Mittelpunktangaben und kehrt
zur Maschinenposition X20/Y20 zurück. Nach vollständigem Stillstand im Simulator:

```text
record stop
record save circle.csv
```

Erfolgsmeldung abwarten. Ein vorhandenes `circle.csv` wird abgewiesen; in diesem
Fall einen neuen Dateinamen wählen. Endpunkt wieder X=Y=8000, Z=0. Samples müssen
im Stillstand unverändert bleiben. Steigende RX/TX zeigen, dass Stop/Save den
Dienst nicht beendet haben. Eingabe teilweise tippen und warten: Bewegung und
UDP müssen weiterlaufen, Terminal darf dabei nicht scrollen.

Terminal C:

```bash
cd ~/dev/linuxcnc-sim
python3 - <<'PY'
import csv
from pathlib import Path
text = Path('circle.csv').read_text()
rows = list(csv.DictReader(line for line in text.splitlines() if not line.startswith('#')))
assert rows
assert int(rows[0]['sequence']) == 0
assert int(rows[0]['time_us']) == 0
assert int(rows[0]['changed_axes']) == 0
assert all(int(r['changed_axes']) != 0 for r in rows[1:])
assert [int(r['sequence']) for r in rows] == list(range(len(rows)))
assert [int(r['time_us']) for r in rows] == sorted(int(r['time_us']) for r in rows)
for row in (rows[0], rows[-1]):
    assert [int(row[k]) for k in ('x_steps', 'y_steps', 'z_steps', 'a_steps')] == [8000, 8000, 0, 0]
print('Samples:', len(rows), '\nStart:', rows[0], '\nEnde:', rows[-1])
PY
halcmd getp stepgen-ninja.0.connected
halcmd getp stepgen-ninja.0.jitter
```

Zweite Aufnahme ohne Simulatorneustart, im Simulator:

```text
record begin
```

Erneut genau ein Startsample bei 8000/8000/0/0. In LinuxCNC:

```text
G21 G90 G53 G1 X25 Y15 F300
```

Nach Stillstand im Simulator:

```text
record stop
record save second.csv
```

Endsample X=10000/Y=6000 Schritte, Z=A=0; die erste CSV bleibt erhalten.
Optional aktive Aufnahme mit `record clear` löschen und Samples=0/STOPPED
prüfen. Unbekannte Befehle und doppeltes Begin/Stop dürfen UDP nicht stören.

Beenden: zuerst LinuxCNC schließen, danach im Simulator `quit` oder Ctrl+C.
Das Terminal muss normal echoen, und die Abschlussstatistik soll keine neuen
Protokoll-/Sende-/Timingfehler oder ID-Lücken zeigen. Danach:

```bash
cd ~/dev/linuxcnc-sim
sudo ./scripts/teardown-veth.sh
```

## Historische Implementierungsprüfung und heutiger Abnahmestatus

Automatisierte Abschlussprüfung der Phase-2-Implementierung am 26.09.2026:
`cmake -S . -B build` und
`cmake --build build` erfolgreich ohne Compilerwarnungen.
`ctest --test-dir build --output-on-failure`: **11/11 bestanden, 0 Fehler,
0 übersprungen**, Gesamtdauer 13,47 Sekunden. Das vollständige Testprotokoll
liegt nach diesem Lauf unter `build/Testing/Temporary/LastTest.log`.
Zusätzlich wurden alle vier Betriebsskripte und das Namespace-Testskript mit
`bash -n` geprüft. Die sieben bisherigen C++-Dateien für Protokoll, Maschine
und UDP stimmen mit den vor Phase 2 erfassten SHA-256-Werten überein.
Der Originalcheckout hat einen leeren Git-Status; alle neun übernommenen
Originaldateien bestehen den Integritätstest.

Geändert: `CMakeLists.txt`, `README.md`, `src/main.cpp`.
Neu: `src/console/TerminalUI.hpp` und `.cpp`,
`src/recorder/MotionRecorder.hpp` und `.cpp`,
`src/simulation/Simulation.hpp` und `.cpp`,
`tests/recorder_tests.cpp`, `tests/recorder_integration.py`,
`tests/terminal_integration.py` und dieses Dokument.

Die reale Phase-2-Abnahme mit LinuxCNC wurde inzwischen erfolgreich durchgeführt
und vom Benutzer bestätigt. Die oben dokumentierten 11 Tests gehören zum
historischen Phase-2-Stand; die aktuelle Suite umfasst 16 Tests, siehe
[Phase 3](phase3-test.md#automatisierte-tests). Während der damaligen
Implementierungsprüfung wurden keine sudo-Systemänderungen ausgeführt.
Bahnvisualisierung und Materialsimulation sind weiterhin außerhalb des Umfangs.
