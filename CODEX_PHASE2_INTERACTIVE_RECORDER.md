# CNC-Simulator – Phase 2: Interaktive Konsole und Motion Recorder

## Ausgangslage

Phase 1 des Projekts ist abgeschlossen und praktisch mit LinuxCNC getestet.

Der Simulator empfängt über das originale Stepper-Ninja-UDP-Protokoll die tatsächlichen Step-Bursts des originalen LinuxCNC-Stepper-Ninja-HAL-Treibers, integriert daraus vier ganzzahlige Achspositionen und sendet gültige Stepper-Ninja-Antworten zurück.

Der reale Integrationstest hat unter anderem bestätigt:

- stabiles virtuelles Ethernet über `veth` + Network Namespace
- originales 37-Byte-PC→Pico- und 61-Byte-Pico→PC-Format
- korrekte Checksum-Prüfung
- RX und TX dauerhaft stabil
- keine Paket-ID-Lücken im bisherigen Test
- X/Y-Bewegungen in beide Richtungen korrekt
- simultane XY-Bewegungen korrekt
- G2/G3-Kreisbewegung aus den tatsächlich übertragenen Step-Bursts korrekt rekonstruiert
- Beispiel: bei 400 steps/mm entsprechen 8000 Schritte exakt 20,0000 mm

Das bestehende Phase-1-Verhalten und das originale Stepper-Ninja-Wire-Protokoll dürfen durch Phase 2 nicht beschädigt oder verändert werden.

---

## 1. Ziel von Phase 2

Phase 2 erweitert `cnc-sim` um zwei Funktionen:

1. eine feste, interaktive Terminaloberfläche, die nicht permanent neue Statuszeilen erzeugt
2. einen Motion Recorder, der gezielt die tatsächlich ausgeführten Schrittänderungen der virtuellen Maschine aufzeichnet

Noch NICHT Bestandteil dieser Phase:

- GUI
- Qt
- OpenGL
- 2D-/3D-Bahnvisualisierung
- Werkstück
- Fräsergeometrie
- Materialabtrag
- Kollisionssimulation
- eigener G-Code-Interpreter
- Änderungen am Stepper-Ninja-Wire-Protokoll

Was nach Phase 2 folgt, wird später separat entschieden.

---

## 2. Grundprinzip

Die bestehende Architektur bleibt erhalten:

```text
LinuxCNC
   │
   ▼
originaler Stepper-Ninja HAL-Treiber
   │
   ▼
UDP / originales Stepper-Ninja-Protokoll
   │
   ▼
cnc-sim
   │
   ├── Step-Burst Decoder
   ├── MachineState
   ├── feste Terminalanzeige
   └── MotionRecorder
```

Die autoritative Maschinenposition bleibt die aus den empfangenen Step-Bursts integrierte ganzzahlige Schrittposition.

Keine LinuxCNC-Positionswerte dürfen als Ersatz für diese Simulation eingeführt werden.

---

## 3. Feste Terminaloberfläche

Die aktuelle `--stats`-Ausgabe erzeugt fortlaufend neue Terminalzeilen. Das soll für den normalen interaktiven Betrieb ersetzt werden.

Die Anzeige soll an einer festen Position im Terminal aktualisiert werden, beispielsweise:

```text
CNC Simulator — Virtual Stepper-Ninja
────────────────────────────────────────────────────────

Connection
  State:             CONNECTED
  RX packets:        605563
  TX packets:        605563
  Invalid:           0
  Packet-ID gaps:    0

Machine Position
  X:       8000 steps       20.0000 mm
  Y:       8000 steps       20.0000 mm
  Z:          0 steps        0.0000 mm
  A:          0 steps        0.0000 unit

Recorder
  State:             STOPPED
  Samples:           0
  Changed samples:   0

Command > _
```

Das Layout darf sinnvoll angepasst werden.

Wichtig:

- Das Terminal darf im normalen interaktiven Modus nicht permanent nach unten scrollen.
- Positions- und Statistikwerte sollen an ihrer bestehenden Bildschirmposition aktualisiert werden.
- Netzwerk-/Simulationsverarbeitung darf durch die Terminaldarstellung nicht blockiert werden.
- Die UDP-Verarbeitung läuft weiterhin mit der bestehenden Geschwindigkeit bzw. dem LinuxCNC-Servozyklus.
- Die Anzeige selbst muss nicht mit 1 kHz aktualisiert werden. Etwa 5–10 Aktualisierungen pro Sekunde sind ausreichend.

Für Phase 2 soll nach Möglichkeit keine zusätzliche schwere Terminalbibliothek eingeführt werden. ANSI-Escape-Sequenzen und POSIX-Terminalfunktionen sind ausreichend, sofern damit eine robuste Bedienung möglich ist.

Falls eine kleine zusätzliche Abhängigkeit technisch eindeutig sinnvoller wäre, nicht ungefragt einführen, sondern zuerst begründen.

---

## 4. Interaktive Kommandozeile

Unterhalb der festen Statusanzeige soll eine Kommandozeile vorhanden sein.

Mindestens folgende Befehle implementieren:

```text
record begin
record stop
record clear
record save <datei>
status
help
quit
```

Zusätzlich sollen folgende natürliche Aliase akzeptiert werden:

```text
begin record
stop record
clear record
save record <datei>
```

Die kanonische interne Befehlsform darf frei gewählt werden.

### Verhalten

`record begin`
- startet eine neue bzw. fortgesetzte Aufzeichnung gemäß unten definierter Semantik
- schreibt sofort den aktuellen Maschinenzustand als Startsample
- danach werden nur Samples bei tatsächlichen Schrittänderungen erzeugt

`record stop`
- beendet die aktive Aufzeichnung
- vorhandene Samples bleiben im Speicher

`record clear`
- löscht die aktuelle Aufzeichnung aus dem Speicher
- wenn gerade aufgenommen wird, soll ein klares und dokumentiertes Verhalten verwendet werden; bevorzugt Aufnahme stoppen und dann löschen

`record save <datei>`
- speichert die aktuelle Aufzeichnung
- primäres Format dieser Phase ist CSV
- vorhandene Daten dürfen nach dem Speichern im Speicher bleiben

`status`
- zeigt bzw. aktualisiert den aktuellen Status

`help`
- zeigt die verfügbaren Befehle mit kurzer Erklärung

`quit`
- beendet den Simulator sauber
- Socket und Terminalzustand müssen ordentlich aufgeräumt werden

Ungültige Befehle sollen eine kurze verständliche Fehlermeldung erzeugen, ohne den Simulator zu beenden.

---

## 5. Terminal und Eingabe dürfen UDP nicht blockieren

Das ist eine zentrale Anforderung.

Der Simulator muss weiterhin Stepper-Ninja-Pakete zuverlässig verarbeiten, auch wenn:

- der Benutzer gerade einen Befehl tippt
- lange Zeit kein Befehl eingegeben wird
- die Anzeige aktualisiert wird
- eine Aufnahme aktiv ist

Die Implementierung darf beispielsweise Polling, `select`/`poll`, einen separaten Eingabethread oder eine andere saubere Lösung verwenden.

Vermeide unnötige Parallelität. Falls Threads verwendet werden, müssen gemeinsame Zustände sauber synchronisiert werden.

Die bestehende UDP-Verarbeitung darf nicht durch blockierendes `std::getline()` im Hauptverarbeitungspfad angehalten werden.

---

## 6. Motion Recorder – Grundprinzip

Der Recorder soll die tatsächlich aus Stepper-Ninja-Step-Bursts entstandene Bewegung speichern.

Er soll NICHT jedes empfangene UDP-Paket speichern.

Nach dem Startsample wird nur dann ein neuer Datensatz erzeugt, wenn sich durch ein gültiges empfangenes Paket mindestens eine virtuelle Achsposition geändert hat.

Beispiel:

```text
Paket 100: keine Schritte       -> kein Sample
Paket 101: X +1                 -> Sample
Paket 102: keine Schritte       -> kein Sample
Paket 103: X +1, Y +1           -> Sample
Paket 104: keine Schritte       -> kein Sample
Paket 105: Y -1                 -> Sample
```

Damit repräsentiert die Aufnahme die tatsächlichen diskreten Zustandsänderungen der virtuellen Maschine und nicht den Netzwerk-Leerlauf.

---

## 7. Startsample

Beim Befehl:

```text
record begin
```

muss sofort genau ein Startsample mit der aktuellen Position geschrieben werden, auch wenn in diesem Moment keine Achse bewegt wird.

Beispiel:

```text
aktuelle Position:
X = 8000
Y = 8000
Z = 2000
A = 0
```

Dann beginnt die Aufnahme mit genau diesem Zustand.

Das Startsample ist notwendig, damit eine später geladene Aufzeichnung einen eindeutig definierten Ausgangspunkt besitzt.

Das Startsample soll als solches erkennbar sein, beispielsweise durch `changed_axes = 0` und `sequence = 0`.

Danach werden ausschließlich Samples bei Schrittänderungen gespeichert.

---

## 8. Autoritative Daten: Schritte

Die primären Aufzeichnungsdaten sind ganzzahlige Schrittpositionen.

Verwende mindestens:

```cpp
struct MotionSample {
    uint64_t sequence;
    uint64_t time_us;
    int64_t x_steps;
    int64_t y_steps;
    int64_t z_steps;
    int64_t a_steps;
    uint8_t changed_axes;
};
```

Die genaue Struktur darf technisch sinnvoll angepasst werden.

Wichtig:

- X/Y/Z/A bleiben `int64_t`
- keine Fließkommapositionen als autoritative Daten
- keine Rundung der Schrittpositionen
- `sequence` beginnt pro Aufnahme bei 0
- `time_us` ist relativ zum Beginn der Aufnahme

`changed_axes` soll als Bitmaske verwendet werden:

```text
bit 0 = X
bit 1 = Y
bit 2 = Z
bit 3 = A
```

Für das Startsample:

```text
changed_axes = 0
```

---

## 9. Zeitinformation

Jedes MotionSample soll eine relative Zeitinformation besitzen.

Bevorzugt:

```text
time_us
```

als monotone Mikrosekunden seit `record begin`.

Verwende eine monotone Clock, nicht die Wanduhrzeit.

Die Zeitinformation dient später dazu, die tatsächlich gefahrene Bewegung zeitlich rekonstruieren zu können.

Sie darf nicht aus der Anzahl der Samples abgeleitet werden, weil Samples ohne Positionsänderung absichtlich nicht gespeichert werden.

---

## 10. Wann genau ein Sample entsteht

Nach erfolgreicher Prüfung eines gültigen Stepper-Ninja-Pakets:

1. Step-Bursts dekodieren
2. MachineState aktualisieren
3. feststellen, welche Achsen ihre Schrittposition geändert haben
4. wenn Recorder aktiv UND mindestens eine Achse geändert wurde:
   - genau ein Sample für den neuen vollständigen Maschinenzustand erzeugen
   - `changed_axes` entsprechend setzen

Wenn mehrere Achsen im selben UDP-Paket geändert werden, entsteht nur ein gemeinsames Sample.

Beispiel:

```text
vorher: X=100 Y=200 Z=300 A=0
Paket:  X +3, Y -2
nachher: X=103 Y=198 Z=300 A=0
```

Ergebnis: genau ein Sample mit X=103, Y=198, Z=300, A=0 und X/Y in `changed_axes` gesetzt.

---

## 11. Speicherverhalten

Für Phase 2 darf die aktive Aufzeichnung im RAM gehalten werden.

Verwende eine geeignete dynamische Struktur, beispielsweise `std::vector<MotionSample>`.

Keine künstlich kleine feste Samplegrenze einführen.

Falls Speicherreservierung sinnvoll ist, darf moderat vorreserviert werden.

Bei Allokations-/Speicherfehlern darf der Prozess nicht stillschweigend falsche Aufzeichnungen erzeugen. Fehler sauber melden.

---

## 12. CSV-Speicherung

`record save <datei>` soll mindestens CSV unterstützen.

Beispiel:

```csv
sequence,time_us,x_steps,y_steps,z_steps,a_steps,changed_axes
0,0,8000,8000,0,0,0
1,12000,8001,8000,0,0,1
2,13000,8002,8001,0,0,3
3,14000,8003,8002,0,0,3
```

Die Schrittpositionen müssen exakt gespeichert werden.

Zusätzlich soll die Datei genügend Metadaten enthalten oder es soll eine eindeutig dokumentierte Begleitlösung geben, damit später bekannt ist, mit welcher Skalierung die Aufnahme erzeugt wurde.

Bevorzugt ist ein kleiner Kommentar-/Metadatenkopf vor den CSV-Daten, beispielsweise:

```text
# cnc-sim motion record
# steps_per_unit=400,400,400,400
# axis_units=mm,mm,mm,unit
```

Danach die normale CSV-Kopfzeile.

Falls Kommentarzeilen verwendet werden, dokumentiere dies.

Noch kein eigenes Binärformat implementieren.

---

## 13. Dateiname

Wenn bei `record save` ein relativer Dateiname angegeben wird, soll klar dokumentiert sein, relativ zu welchem Verzeichnis gespeichert wird.

Keine Datei ungefragt überschreiben.

Wenn die Zieldatei bereits existiert:

- Fehlermeldung ausgeben
- nicht überschreiben

Eine spätere explizite `--force`-Funktion ist nicht Bestandteil dieser Phase.

---

## 14. Bestehende Diagnosemöglichkeiten erhalten

Die bisherige Diagnosefähigkeit darf nicht verloren gehen.

Insbesondere weiterhin sichtbar bzw. abrufbar:

- RX packets
- accepted packets
- invalid packets
- TX packets
- send errors
- length errors
- checksum errors
- timing errors
- position overflows
- packet-ID gaps
- X/Y/Z/A in Steps
- X/Y/Z/A in konfigurierten Einheiten

Falls `--stats` bisher bewusst fortlaufende Ausgabe erzeugt, darf diese Option als Debug-/Legacy-Modus erhalten bleiben.

Der neue interaktive feste Bildschirm soll jedoch der bevorzugte normale Betriebsmodus sein.

---

## 15. Trennung der Verantwortlichkeiten

Vermeide, Recorderlogik direkt in die UDP-Klasse zu vermischen.

Bevorzugte Verantwortlichkeiten:

```text
UdpServer
  -> empfängt/sendet Datagramme

StepperNinjaProtocol
  -> prüft/dekodiert/erzeugt Protokolldaten

MachineState
  -> hält autoritative virtuelle Schrittpositionen

MotionRecorder
  -> Start/Stop/Clear/Samples/Save

TerminalUI bzw. Console
  -> Anzeige und Befehlsverarbeitung
```

Die exakten Klassennamen dürfen an die bestehende Phase-1-Struktur angepasst werden.

Keine unnötige Framework-Architektur einführen.

---

## 16. Thread-Sicherheit / Zustandskonsistenz

Falls Netzwerkverarbeitung und Terminaleingabe in unterschiedlichen Threads laufen:

- keine Data Races
- keine teilweise geschriebenen MotionSamples
- `record stop` darf nicht mitten in einer inkonsistenten Zustandsänderung landen
- `record clear` und `record save` müssen gegen gleichzeitiges Schreiben geschützt sein
- Terminalanzeige muss einen konsistenten Snapshot erhalten

Bevorzuge eine einfache, nachvollziehbare Synchronisationsstrategie.

Performance ist wichtig, aber Korrektheit und Verständlichkeit sind wichtiger als Mikrooptimierung.

---

## 17. Tests

Erweitere die automatisierten Tests mindestens um folgende Fälle.

### Recorder Start

MachineState steht beispielsweise bei:

```text
X=100 Y=200 Z=300 A=400
```

`record begin`

muss genau ein Startsample erzeugen.

### Keine Bewegung

100 gültige Pakete ohne Step-Burst dürfen nach dem Startsample keine weiteren Samples erzeugen.

### Einzelachse

X ändert sich -> genau ein neues Sample, X-Bit gesetzt.

### Mehrere Achsen

X und Y ändern sich im selben Paket -> genau ein Sample, X- und Y-Bit gesetzt.

### Stop

Nach `record stop` dürfen weitere Maschinenbewegungen keine Samples hinzufügen.

### Clear

`record clear` löscht alle Samples.

### Zeit

`time_us` muss monoton nicht fallend sein.

### CSV

Gespeicherte CSV-Datei enthält:

- Metadaten
- korrekte Kopfzeile
- exakte Schrittwerte
- korrekte `changed_axes`

### Bestehende Tests

Alle Phase-1-Tests müssen weiterhin bestehen.

---

## 18. Integrationstest

Erweitere nach Möglichkeit den bestehenden Integrationstest so, dass folgende Sequenz automatisiert geprüft wird:

1. Simulator starten
2. Recorder starten
3. gültige Pakete ohne Bewegung senden
4. X-Schritte senden
5. simultane X/Y-Schritte senden
6. weitere Pakete ohne Bewegung senden
7. Recorder stoppen
8. weitere Bewegung senden
9. Aufnahme speichern
10. CSV prüfen

Erwartung:

- Startsample vorhanden
- keine Samples für Stillstand
- genau ein Sample pro Paket mit mindestens einer tatsächlichen Schrittänderung
- keine Samples nach Stop
- gespeicherte Endposition der Aufnahme entspricht der Position beim Stop

---

## 19. Manueller LinuxCNC-Test

Erweitere `docs/phase2-test.md` mit einer konkreten Testanleitung.

Beispielablauf:

Simulator starten.

In der Simulator-Konsole:

```text
record begin
```

In LinuxCNC eine definierte Bewegung bzw. einen Kreis fahren.

Danach Simulator:

```text
record stop
record save circle.csv
```

Prüfen:

- Datei existiert
- Startsample vorhanden
- keine dauernden Stillstandssamples
- letzte gespeicherte Position entspricht der LinuxCNC-Endposition zum Zeitpunkt von `record stop`
- Simulator läuft nach Stop/Save normal weiter

Danach zusätzlich eine zweite Aufnahme durchführen, ohne den Simulator neu zu starten.

---

## 20. Robustheit der Terminaloberfläche

Teste insbesondere:

- `Ctrl+C`
- `quit`
- leere Eingabe
- unbekannter Befehl
- `record stop` obwohl keine Aufnahme läuft
- `record begin` obwohl bereits aufgenommen wird
- `record save` ohne Samples
- `record save` auf existierende Datei
- sehr langer/ungültiger Dateiname

Nach Programmende soll das Terminal in einem normalen benutzbaren Zustand zurückbleiben.

---

## 21. Dokumentation

Aktualisiere README und erstelle:

```text
docs/phase2-test.md
```

Dokumentiere mindestens:

- neue interaktive Bedienung
- alle Befehle
- Recordersemantik
- Startsample
- nur Änderungen werden gespeichert
- Bedeutung von `changed_axes`
- CSV-Format
- Zeitbasis
- steps-per-unit-Metadaten
- Speicher-/Dateiverhalten
- manuellen LinuxCNC-Test

---

## 22. Definition of Done

Phase 2 ist abgeschlossen, wenn:

- Phase 1 weiterhin vollständig funktioniert
- der normale Terminalbetrieb nicht mehr permanent scrollt
- Live-Position und Statistiken an festen Stellen aktualisiert werden
- eine interaktive Kommandozeile vorhanden ist
- `record begin` funktioniert
- `record stop` funktioniert
- `record clear` funktioniert
- `record save <datei>` funktioniert
- nur tatsächliche Schrittänderungen nach dem Startsample aufgezeichnet werden
- Startposition immer eindeutig gespeichert wird
- Samples exakte `int64_t`-Schrittpositionen enthalten
- relative monotone Zeit gespeichert wird
- mehrere Achsänderungen eines Pakets zu genau einem Sample führen
- CSV korrekt gespeichert wird
- bestehende Phase-1-Tests weiterhin bestehen
- neue Recorder-/Konsolentests bestehen
- der echte LinuxCNC-Stepper-Ninja-Datenstrom weiterhin ohne Watchdog-/Protokollprobleme läuft

---

## 23. Arbeitsweise für Codex

Vor Änderungen:

1. Lies diese Datei vollständig.
2. Lies die bestehende Phase-1-Dokumentation.
3. Prüfe die aktuelle Implementierung und Tests.
4. Ändere das originale Repository `stepper-ninja/` nicht.
5. Verändere das originale Stepper-Ninja-Wire-Protokoll nicht.

Arbeite anschließend selbstständig bis zu einem gebauten und automatisiert getesteten Stand.

Führe mindestens aus:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Behebe Build- und Testfehler selbstständig.

Keine sudo-Systemänderungen ohne ausdrückliche Aufforderung.

Bei grundlegenden Architekturproblemen oder einem Widerspruch zur bestehenden Phase-1-Implementierung nicht stillschweigend umplanen, sondern Problem dokumentieren.

Am Ende einen kompakten Bericht liefern mit:

1. geänderten/neuen Dateien
2. Architektur der Terminaloberfläche
3. Architektur des Recorders
4. Build-Ergebnis
5. Test-Ergebnis
6. CSV-Beispiel
7. offenen Punkten
8. exakten Befehlen für den ersten manuellen Phase-2-Test mit LinuxCNC
