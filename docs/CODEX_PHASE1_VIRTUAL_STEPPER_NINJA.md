# CNC-Simulator – Phase 1

Wir bauen einen eigenständigen CNC-Maschinensimulator für LinuxCNC in C++.

Das Projekt befindet sich unter:

`~/dev/linuxcnc-sim`

Das originale Stepper-Ninja-Repository befindet sich unter:

`./stepper-ninja/`

Bereits vorhandene und verbindliche Analysedokumente:

- `docs/stepper-ninja-protocol.md`
- `docs/stepper-ninja-files.md`

Lies diese beiden Dokumente vollständig, bevor du Änderungen am Projekt vornimmst.

Das Stepper-Ninja-Repository selbst darf nicht verändert werden.

---

# 1. Ziel dieses Meilensteins

Implementiere einen minimalen virtuellen Stepper-Ninja in C++.

LinuxCNC soll den **originalen Stepper-Ninja-HAL-Treiber** verwenden und glauben, dass echte Stepper-Ninja-Hardware über Ethernet angeschlossen ist.

Architektur:

```text
LinuxCNC
   │
   ▼
originaler stepgen-ninja HAL-Treiber
   │
   │ UDP / originales Stepper-Ninja-Protokoll
   ▼
veth-lcnc
   │
   │ virtuelles Ethernet
   ▼
veth-sim
   │
   ▼
Network Namespace
   │
   ▼
cnc-sim
   │
   ├── UDP Receiver
   ├── originales Stepper-Ninja-Protokoll
   ├── Checksum
   ├── packet_id
   ├── Step-Burst Decoder
   └── virtuelle Achspositionen
```

Noch NICHT implementieren:

- GUI
- Qt
- OpenGL
- Werkstück
- Fräser
- Materialabtrag
- G-Code-Interpreter
- eigene Bahnplanung
- EtherCAT
- Kollisionssimulation

LinuxCNC bleibt allein für G-Code, Kinematik und Bahnplanung verantwortlich.

---

# 2. Wichtigste Architekturregel

Das originale Stepper-Ninja-Wire-Protokoll darf NICHT verändert werden.

Keine neuen Felder.

Keine eigenen Paketheader.

Keine zusätzlichen Positionswerte.

Keine Übertragung von XYZ als float/double.

Der Simulator soll sich gegenüber LinuxCNC wie echte Stepper-Ninja-Hardware verhalten.

Die empfangenen `stepgen_command[]` werden dekodiert und zu virtuellen Motorpositionen integriert.

---

# 3. Originalcode übernehmen

Orientiere dich exakt an:

`docs/stepper-ninja-files.md`

Lege beispielsweise an:

```text
third_party/
└── stepper-ninja/
    ├── LICENSE.txt
    └── ...
```

Übernimm nur die dort als notwendig bzw. empfohlen identifizierten Originaldateien.

Die übernommenen Stepper-Ninja-Dateien müssen unverändert bleiben.

Keine Formatierung.

Keine Modernisierung.

Keine Umbenennung von Variablen.

Keine C++-Portierung des Originalcodes.

Keine Änderungen zur Vereinfachung.

Die Herkunft und der analysierte Stepper-Ninja-Commit müssen dokumentiert werden.

Original-Commit:

`eb7e5dfa2e76477e606a47038b07cca5e8a4b424`

Der originale C-Code soll als C kompiliert werden.

Unser eigener Simulatorcode wird C++20.

Falls C-Linkage benötigt wird, löse dies außerhalb der unveränderten Originaldateien mit einem kleinen Adapter/Wrapper.

---

# 4. Buildsystem

Verwende CMake.

Projektsprachen:

```cmake
project(cnc_sim LANGUAGES C CXX)
```

C++:

```text
C++20
```

C:

für den unveränderten Stepper-Ninja-Protokollcode.

Zielprogramm:

```text
cnc-sim
```

Der Build soll ungefähr funktionieren mit:

```bash
cmake -S . -B build
cmake --build build
```

Keine unnötigen externen Bibliotheken einführen.

Für Phase 1 reichen POSIX/Linux-Sockets und die C/C++-Standardbibliothek.

---

# 5. Virtuelles Ethernet

Erstelle ein Setupskript, z.B.:

```text
scripts/setup-veth.sh
```

Ziel:

LinuxCNC und Simulator sollen über ein virtuelles Ethernet-Paar kommunizieren.

Verwende:

```text
veth-lcnc
veth-sim
```

und einen eigenen Network Namespace für den Simulator, beispielsweise:

```text
cnc-sim-ns
```

Adressierung:

```text
LinuxCNC:
veth-lcnc
192.168.50.1/24

Simulator:
veth-sim
192.168.50.2/24
```

Stepper-Ninja UDP-Port:

```text
8888
```

Der Simulator muss innerhalb seines Namespace auf UDP-Port 8888 lauschen können.

Der originale LinuxCNC-Stepper-Ninja-HAL-Treiber soll als Ziel verwenden können:

```text
192.168.50.2:8888
```

Das ist wichtig, weil der originale HAL-Treiber selbst seinen konfigurierten UDP-Port lokal bindet. Deshalb verwenden wir getrennte Network Namespaces und verändern den HAL-Treiber nicht.

Das Setupskript soll:

- vorhandene gleichnamige Interfaces/Namespaces sinnvoll behandeln
- veth-Paar erstellen
- Simulatorseite in Namespace verschieben
- IP-Adressen konfigurieren
- Interfaces aktivieren
- Loopback im Namespace aktivieren
- verständliche Statusmeldungen ausgeben
- bei Fehlern abbrechen

Erstelle zusätzlich:

```text
scripts/teardown-veth.sh
```

damit das Setup vollständig entfernt werden kann.

Das Setup darf keine permanente Netzwerkkonfiguration erzeugen.

---

# 6. Startskript

Erstelle beispielsweise:

```text
scripts/run-simulator.sh
```

Es soll `cnc-sim` im Namespace starten, sinngemäß:

```bash
sudo ip netns exec cnc-sim-ns ./build/cnc-sim ...
```

Parameter dürfen sinnvoll übergeben werden.

---

# 7. UDP Receiver

Implementiere einen kleinen Linux-UDP-Receiver.

Er muss das originale Stepper-Ninja-Protokoll empfangen.

Standard:

```text
IP im Namespace: 192.168.50.2
UDP-Port: 8888
```

Das originale Standardprofil hat:

```text
4 Stepgens
3 Encoder

PC → Pico: 37 Byte
Pico → PC: 61 Byte
```

Diese Werte nicht unabhängig neu erfinden, sondern aus den übernommenen Originaldefinitionen ableiten und zusätzlich mit `static_assert` oder entsprechenden Tests gegen die dokumentierten Werte absichern.

Beispielsweise sinngemäß:

```cpp
static_assert(sizeof(transmission_pc_pico_t) == 37);
static_assert(sizeof(transmission_pico_pc_t) == 61);
```

---

# 8. Empfangsprüfung

Bei einem empfangenen Datagramm:

1. Länge prüfen.
2. Originale Stepper-Ninja-Checksum prüfen.
3. `packet_id` auswerten.
4. Stepgen-Befehle dekodieren.
5. virtuelle Achsposition aktualisieren.
6. gültige Stepper-Ninja-Antwort erzeugen.
7. Antwort an den tatsächlichen Absender des UDP-Datagramms schicken.

Ungültige Pakete dürfen die virtuelle Position nicht verändern.

Für Phase 1 ist keine perfekte Nachbildung aller Bugs und Sonderfälle der echten Firmware notwendig.

Ziel ist protokollkompatibles normales Verhalten.

Keine absichtliche Reproduktion dokumentierter Fehler im Originalcode, wenn sie für die Kommunikation nicht erforderlich sind.

---

# 9. Step-Burst Decoder

Die verbindliche Semantik steht in:

`docs/stepper-ninja-protocol.md`

Für jedes:

```text
stepgen_command[i]
```

gilt:

```text
W == 0:
    keine neuen Schritte

W != 0:

direction = bit 31

N = (W & 0x3ff) + 1

delta_steps =
    direction == 1
        ? +N
        : -N
```

Die Timingbits dürfen nicht versehentlich als Schrittzahl interpretiert werden.

Implementiere den Decoder als klar getrennte, testbare Funktion.

Beispielsweise:

```cpp
struct StepBurst {
    bool active;
    bool direction;
    uint32_t steps;
    uint32_t timing;
};
```

Die genaue interne API darfst du sinnvoll gestalten.

---

# 10. Maschinenzustand

Verwende intern mindestens:

```cpp
std::array<int64_t, 4> position_steps;
```

Keine Fließkommawerte als primäre Positionsrepräsentation.

Die Schrittposition ist die autoritative virtuelle Motorposition.

Zusätzlich brauchen wir konfigurierbare Skalierungen:

```text
steps_per_unit[4]
```

Für XYZ bedeutet die Einheit normalerweise mm.

Für eine spätere Rotationsachse kann sie Grad sein.

Für Phase 1 dürfen sinnvolle Kommandozeilenparameter oder eine einfache Konfigurationsdatei verwendet werden.

Beispielwerte dürfen vorhanden sein, müssen aber eindeutig als Konfiguration gekennzeichnet sein.

Der Simulator darf nicht behaupten, dass Stepper-Ninja selbst `steps/mm` überträgt.

---

# 11. Startreferenz

Das Wire-Protokoll überträgt keine absolute Startposition.

Für Phase 1 gilt deshalb:

```text
position_steps[0..3] = 0
```

beim Start des Simulators.

Diese Position ist die virtuelle Referenz des Simulators.

Dokumentiere diese Einschränkung.

Später können wir Homing/Referenzierung genauer simulieren.

---

# 12. UDP-Antwort

Der virtuelle Stepper-Ninja muss gültige:

```text
transmission_pico_pc_t
```

Antwortpakete erzeugen.

Mindestens:

- korrekte Paketgröße
- sinnvolle `packet_id`
- gültige originale Checksum
- Inputs zunächst 0
- Encoderwerte zunächst sinnvoll definiert
- Jitter darf zunächst 0 oder sinnvoll gemessen sein
- step_ring_fill zunächst 0
- step_ring_status zunächst 0

Orientiere dich für die Paket-ID-Semantik exakt an der vorhandenen Protokolldokumentation und am Originalcode.

Nicht raten.

Das Ziel ist, dass der originale LinuxCNC-HAL-Treiber die Antwort akzeptiert und sein Watchdog nicht auslöst.

---

# 13. Logging

Der Simulator soll im Terminal gut lesbare Diagnoseausgaben erzeugen.

Nicht zwingend jedes Paket ungefiltert ausgeben, weil bei 1 kHz sonst sehr viel Text entsteht.

Sinnvolle Optionen:

```text
--verbose
--stats
```

oder vergleichbar.

Standardausgabe beispielsweise periodisch:

```text
Stepper-Ninja virtual device
Listening: 192.168.50.2:8888

Connected peer: 192.168.50.1:8888

Packets RX: 1000
Packets invalid: 0
Packet-ID gaps: 0

Position:
X  4000 steps   10.0000 mm
Y     0 steps    0.0000 mm
Z     0 steps    0.0000 mm
A     0 steps    0.0000 unit
```

Die Ausgabe muss nicht exakt so aussehen.

Wichtig sind gute Diagnosemöglichkeiten.

---

# 14. Paketverlust

Für Version 1 akzeptieren wir UDP-Paketverlust als reale Eigenschaft der simulierten Verbindung.

Keine Retransmission implementieren.

Keine Erweiterung des Wire-Protokolls.

Erkenne jedoch nach Möglichkeit Sprünge in `packet_id` und zähle sie statistisch.

Beispielsweise:

```text
received_packets
invalid_packets
checksum_errors
packet_id_gaps
```

Ein verlorenes Paket kann nicht rekonstruiert werden.

Das ist für Phase 1 akzeptiert.

---

# 15. Tests

Implementiere automatisierte Tests mindestens für:

## Paketgrößen

```text
PC→Pico = 37
Pico→PC = 61
```

## Checksum

Teste die originale Stepper-Ninja-Checksum gegen bekannte bzw. aus dem Originalcode erzeugte Testvektoren.

## Decoder

Teste:

```text
W = 0
```

→ kein Burst.

Teste positive Richtung.

Teste negative Richtung.

Teste unterschiedliche Schrittzahlen.

Grenzfälle:

```text
N = 1
N = 1023
```

und, soweit das Wireformat dies zulässt:

```text
N = 1024
```

## Positionsintegration

Beispiel:

```text
+10
+20
-5
```

muss ergeben:

```text
25 steps
```

## Paket-ID

Teste normalen Wrap:

```text
254
255
0
1
```

und Gap-Erkennung.

---

# 16. Integrationstest ohne LinuxCNC

Erstelle nach Möglichkeit einen kleinen Testsender oder Integrationstest, der ein gültiges 37-Byte-Stepper-Ninja-Paket an den Simulator sendet und die 61-Byte-Antwort prüft.

Dieser Testsender ist nur Entwicklungswerkzeug.

Er darf kein alternatives Protokoll definieren.

---

# 17. LinuxCNC-Testanleitung

Erstelle:

```text
docs/phase1-test.md
```

Darin Schritt für Schritt:

1. Projekt bauen.
2. veth/Namespace einrichten.
3. Netzwerk prüfen.
4. Simulator starten.
5. LinuxCNC-Stepper-Ninja-HAL auf `192.168.50.2:8888` konfigurieren.
6. LinuxCNC starten.
7. Verbindung prüfen.
8. X-Achse bewegen.
9. Schrittzähler des Simulators prüfen.
10. berechnete Maschinenposition mit LinuxCNC vergleichen.
11. Netzwerk wieder abbauen.

Wichtig:

Keine bestehende LinuxCNC-Konfiguration ungefragt überschreiben.

Wenn eine Beispiel-HAL-Datei hilfreich ist, lege eine separate Datei unter `examples/` an.

---

# 18. Erwartete Projektstruktur

Du darfst Details verbessern, aber ungefähr:

```text
linuxcnc-sim/
├── CMakeLists.txt
├── README.md
│
├── docs/
│   ├── stepper-ninja-protocol.md
│   ├── stepper-ninja-files.md
│   └── phase1-test.md
│
├── third_party/
│   └── stepper-ninja/
│       └── ...
│
├── src/
│   ├── main.cpp
│   ├── network/
│   │   ├── UdpServer.cpp
│   │   └── UdpServer.hpp
│   ├── protocol/
│   │   ├── StepperNinjaProtocol.cpp
│   │   └── StepperNinjaProtocol.hpp
│   └── machine/
│       ├── MachineState.cpp
│       └── MachineState.hpp
│
├── tests/
│   └── ...
│
├── scripts/
│   ├── setup-veth.sh
│   ├── teardown-veth.sh
│   └── run-simulator.sh
│
└── stepper-ninja/
    └── original repository, DO NOT MODIFY
```

Vermeide unnötige Abstraktion.

Wir wollen einen kleinen, verständlichen technischen Kern.

---

# 19. Definition of Done

Phase 1 ist erfolgreich, wenn:

```text
LinuxCNC
   ↓
original stepgen-ninja HAL
   ↓
UDP
   ↓
virtuelles Ethernet
   ↓
cnc-sim
```

funktioniert und der Simulator:

- gültige 37-Byte-Pakete empfängt
- originale Checksum prüft
- Step-Bursts korrekt dekodiert
- 4 virtuelle Schrittpositionen führt
- gültige 61-Byte-Antworten erzeugt
- den LinuxCNC-Watchdog zufriedenstellt
- Paket-ID-Lücken erkennt
- Positionswerte in Steps und konfigurierten Einheiten anzeigen kann

Der wichtigste manuelle Test:

LinuxCNC fährt eine bekannte Distanz auf X.

Beispiel:

```text
X = +10.000 mm
```

Bei passend eingestelltem:

```text
400 steps/mm
```

muss der Simulator nach der Bewegung:

```text
X = +4000 steps
X = +10.000 mm
```

anzeigen.

Die tatsächliche Skalierung muss natürlich mit der LinuxCNC-/Stepper-Ninja-Konfiguration übereinstimmen.

---

# 20. Arbeitsweise

Arbeite schrittweise.

Vor Änderungen:

1. Lies beide vorhandenen Dokumentationen vollständig.
2. Prüfe die aktuelle Projektstruktur.
3. Prüfe die relevanten Originaldateien.
4. Erstelle dann die Implementierung.

Führe Build und Tests selbst aus.

Ändere `stepper-ninja/` nicht.

Wenn du einen Widerspruch zwischen Dokumentation und Originalcode findest:

- nicht raten
- Originalcode erneut prüfen
- Widerspruch dokumentieren
- keine stillschweigende Protokolländerung vornehmen

Am Ende:

1. Liste alle neu angelegten/geänderten Dateien.
2. Gib Build-Ergebnis an.
3. Gib Testergebnis an.
4. Beschreibe kurz die Architektur.
5. Nenne noch offene Punkte.
6. Zeige die exakten Befehle für unseren ersten manuellen LinuxCNC-Test.