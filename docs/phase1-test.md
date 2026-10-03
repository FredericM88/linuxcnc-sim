# Phase 1: Bau, Tests und erster Lauf mit LinuxCNC

**Aktueller Status: Phase 1 ist abgeschlossen und real mit LinuxCNC getestet.**
Der Benutzer bestätigte 1-ms-Servozeit, 400 Schritte/mm, Geraden und G2/G3-Kreise
sowie über 600.000 Pakete ohne gemeldete Fehler oder ID-Lücken. Auch Phase 2
und Phase 3 sind inzwischen abgeschlossen und real mit LinuxCNC getestet;
siehe [Projektstand](../README.md#current-features) und [Phase-3-Abnahme](phase3-test.md).
Die historischen Implementierungsbefunde und Testzahlen unten beschreiben den
jeweiligen damaligen Stand, keine noch ausstehende Abnahme.

## 1. Voraussetzungen und Bau

Im Projektverzeichnis arbeiten:

```bash
# Run from the linuxcnc-sim checkout root.
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Benötigt werden CMake >=3.20, ein C-/C++20-Compiler, Make oder Ninja, Bash,
Python 3 für Tests und iproute2 für Namespaces. Fehlt CMake auf Debian, kann
der Benutzer es mit `sudo apt-get install cmake` installieren. Diese
Systeminstallation wurde bei der Umsetzung nicht ausgeführt.

Die ursprüngliche Phase-1-Validierung nutzte temporär bereitgestellte CMake-Pakete.
Für neue Builds gelten die [aktuellen Voraussetzungen](../README.md#requirements).
Tests für Decoder/Prüfsumme verwenden keine abgeschalteten `assert`-Makros und
bleiben auch bei Release-Builds wirksam.

## 2. Originalen HAL-Treiber bereitstellen

Benötigt wird der unveränderte `stepgen-ninja`-HAL-Treiber aus Commit
`eb7e5dfa2e76477e606a47038b07cca5e8a4b424`, gebaut mit dem dokumentierten
Standardprofil. Ein installiertes gleichnamiges Modul mit anderem Profil ist
nicht durch den Namen oder die Paketlänge allein als kompatibel erkennbar.

Der vollständige Originalcheckout wird seit der Release-Bereinigung nicht mehr
mitgeliefert. Der [aktuelle Quick Start](../README.md#1-provide-the-original-linuxcnc-hal-driver)
beschreibt den separaten, auf diesen Commit festgelegten Checkout sowie Bau und
Installation des unveränderten Moduls. Die historische Abnahme nutzte denselben
Originaltreiber. Die Installation ersetzt ein gegebenenfalls bereits vorhandenes
gleichnamiges LinuxCNC-Modul.

## 3. Virtuelles Ethernet einrichten und prüfen

```bash
# Run from the linuxcnc-sim checkout root.
sudo ./scripts/setup-veth.sh
ip -4 address show dev veth-lcnc
sudo ip -n cnc-sim-ns -4 address show dev veth-sim
sudo ip -n cnc-sim-ns link show dev lo
ip -4 route get 192.168.50.2
ping -c 3 192.168.50.2
sudo ip netns exec cnc-sim-ns ping -c 3 192.168.50.1
```

Erwartet: `veth-lcnc` im Host mit `192.168.50.1/24`, `veth-sim` in
`cnc-sim-ns` mit `192.168.50.2/24`, beide aktiv, Loopback im Namespace aktiv.
Die Route zur Simulator-IP muss über `veth-lcnc` führen.

Die Skripte erzeugen nur temporäre Kernel-Ressourcen und den Besitzmarker
`/run/cnc-sim-veth.state`. Sie verändern weder Netzwerkmanagerdateien noch
IP-Forwarding, NAT oder Firewallregeln. Ein vorhandener Eintrag für das
angeforderte Netz bzw. fremde gleichnamige Ressourcen führen beim erstmaligen
Setup zu einer Meldung und Abbruch. Das Skript löscht keine fremden Ressourcen.
Ein unvollständiges eigenes Setup kann mit Teardown und erneutem Setup bereinigt
werden. Ein Fehler während einer neuen Einrichtung löst deren Rollback aus.

## 4. Simulator zuerst starten

Terminal A:

```bash
# Run from the linuxcnc-sim checkout root.
sudo ./scripts/run-simulator.sh --steps-per-unit 400,400,400,400 --units mm,mm,mm,unit --stats
```

Erwartet: `Listening: 192.168.50.2:8888`, zunächst alle Schrittpositionen null.
Der Startwrapper übernimmt CLI-Argumente unverändert und läuft im Namespace.
Das Beispiel nutzt 400 Schritte/mm für XYZ. A ist unbenutzt; die vierte
Skalierung ist nur eine lokale Beispielkonfiguration.

Die IP und der Port sind eigene Laufzeitoptionen des Netzwerkendpunkts, keine
Änderung der übernommenen `config.h` oder des Wire-Protokolls. Die dortige
ursprüngliche Geräte-IP `192.168.0.177` bleibt im unveränderten Import erhalten.

## 5. Separate LinuxCNC-Beispielmaschine starten

Terminal B, als normaler Benutzer:

```bash
# Run from the linuxcnc-sim checkout root.
linuxcnc examples/phase1/phase1.ini
```

Das Beispiel ist eine separate virtuelle XYZ-Maschine mit 1-ms-Servozeit,
400 Schritten/mm, 2500-ns-Pulsbreite und dem originalen HAL-Modul:

```text
loadrt stepgen-ninja ip_address="192.168.50.2:8888"
```

Es werden keine vorhandenen LinuxCNC-Konfigurationen überschrieben. `axis`
ist die vorhandene LinuxCNC-Bedienoberfläche; der damalige Phase-1-Simulator besaß
keine GUI; die aktuelle Version bietet eine optionale OpenGL-Ansicht. Die Beispielmaschine lädt keine physischen Maschinen-/GPIO-Treiber.
Encoder- und PWM-Pins werden für diesen Test nicht verbunden.

Die Aufrufreihenfolge im Servo-Thread ist Motion-Handler, Motion-Controller,
Watchdog, Send, Receive. Der Simulator muss bereits laufen, wenn der HAL-Treiber
startet: Dessen Watchdog stoppt nach mehr als zehn Zählerschritten ohne gültige
Antwort sowohl Senden als auch Empfangen. Nach diesem Fehler LinuxCNC neu
starten; spätes Starten des Simulators allein heilt ihn nicht.
Beleg: [Original-HAL:367–388, 484–490, 605–607](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L367).

## 6. Verbindung und Referenz prüfen

In Terminal A muss `Connected peer: 192.168.50.1:8888` erscheinen. RX, accepted
und TX steigen bei 1-ms-Servozeit ungefähr um 1000/s. Ungültige Pakete,
Sendefehler und ID-Abweichungen sollten bei einem frischen störungsfreien Lauf
null bleiben. Eine Start-ID ungleich null zählt einmal als Synchronisierungsabweichung.

Zusätzlich in einem normalen Terminal:

```bash
halcmd getp stepgen-ninja.0.connected
halcmd getp stepgen-ninja.0.stepgen.ring-fill
halcmd getp stepgen-ninja.0.stepgen.ring-active
halcmd getp stepgen-ninja.0.stepgen.0.step-scale
halcmd getp stepgen-ninja.0.jitter
```

Erwartet: connected TRUE nach Empfang; Ringfüllstand 0 und Ring aktiv FALSE;
Scale 400. Der Jitterpin ist beim Original `1000 − Empfangsabstand_in_µs`,
nicht der rohe Paketwert. Keine Eingaben an den als HAL_IN deklarierten
`connected`-Pin vornehmen; der Originaltreiber beschreibt ihn selbst.

In LinuxCNC Not-Aus zurücksetzen (F1), Maschine einschalten (F2) und „Home All“
ausführen. Das Beispiel verwendet ausschließlich sofortige virtuelle
Referenzierung ohne Suchfahrt und ohne Index. Erst bei stillstehenden Achsen
und null Motorpositionen den Vergleich beginnen.

Der Simulator startet bei null Schritten. LinuxCNC sendet keine absolute
Startposition; der Treiber übernimmt seinen ersten Sollwert als interne Basis.
Wenn LinuxCNC zuvor schon bewegt wurde, nicht nur den Simulator neu starten
und anschließend absolute Positionen vergleichen. Für eine neue gemeinsame
Referenz beide Programme neu starten und die Beispielreferenzierung wiederholen.

## 7. Bekannte Distanz fahren

In LinuxCNC MDI ausführen (zunächst Maschine stillstehend bei X=0):

```text
G21 G90 G53 G1 X10 F60
```

Dies fährt X im Maschinenkoordinatensystem auf +10 mm. Nach Ende der Bewegung
muss Terminal A zeigen:

```text
X  4000 steps  10.0000 mm
Y  0 steps     0.0000 mm
Z  0 steps     0.0000 mm
```

Gegenprüfen:

```bash
halcmd getp joint.0.motor-pos-cmd
halcmd getp stepgen-ninja.0.stepgen.0.command
halcmd getp stepgen-ninja.0.stepgen.0.feedback
halcmd getp stepgen-ninja.0.stepgen.0.debug-steps
```

Sollwert/Command/Feedback sollten am Endpunkt 10, der Step-Debugzähler bei
diesem frischen Lauf 4000 sein. Die Original-Feedbackvariable ist nur die
lokale Sollwertkopie, kein vom Simulator gemessener Stand. Deshalb immer auch
die tatsächliche Schrittstatistik im Simulatorterminal vergleichen.

Rückfahrt:

```text
G21 G90 G53 G1 X0 F60
```

Danach erwartet: X=0 Schritte/0 mm. Kleine Zwischenabweichungen zum unquantisierten
LinuxCNC-Sollwert sind wegen Schrittquantisierung und Float-Zwischenwerten des
Originaltreibers möglich. Der Endpunkttest von 0 nach 10 bei Scale 400 ist
bewusst eindeutig. Für Positionsvergleiche andere Skalierungen auf beiden
Seiten gemeinsam ändern; sie werden nicht im UDP-Paket übertragen.

## 8. Beenden und Netzwerk entfernen

Zuerst LinuxCNC schließen, dann in Terminal A Ctrl+C. Der Simulator gibt eine
Abschlussstatistik aus. Danach:

```bash
# Run from the linuxcnc-sim checkout root.
sudo ./scripts/teardown-veth.sh
ip link show dev veth-lcnc
sudo ip netns list
```

`veth-lcnc` soll nicht mehr existieren, `cnc-sim-ns` nicht mehr aufgelistet sein.
Teardown beendet keine fremden oder laufenden Namespace-Prozesse. Bei einer
Meldung über verbleibende PIDs zuerst diese Simulator-/Diagnoseprozesse sauber
beenden und Teardown erneut ausführen. Es gibt keine permanente Netzwerkkonfiguration.

## 9. Automatisierte Prüfungen und Befunde

Die Tests decken ab:

| Prüfung | Inhalt |
|---|---|
| `protocol-unit` | Originalgrößen/-Offsets, bekannte Prüfsummen 0x90/0xf0/0x99/0x03, unsigned Byteindex/Overflow, alle N=1..1024 mit beiden Richtungen und unterschiedlichen Timingbits, Nullwort, +10/+20/−5=25, vier Achsen, Skalierung, atomarer Überlaufschutz, ID-Wrap und Lücken, ungültige Pakete, Zeitstempelwrap |
| `udp-integration` | Tatsächlichen Prozess starten, 37-Byte-Pakete senden, jede 61-Byte-Antwort nach Länge/ID/Originaltabelle/Feldern/Absender prüfen; ungültige Datagramme einschließlich Überlänge dürfen keine Antwort/Bewegung erzeugen; verschiedene Quellports, 1000 Pakete nominell bei 1 kHz, Abschlussposition, CLI-Fehler und SIGTERM |
| `network-namespace` | Setup/Start/Teardown in rootlosen isolierten Namespaces, echte veth-Adressen, UDP 8888 auf beiden Seiten, 1000 Bursts → 4000 Schritte/10 mm, wiederholtes Setup/Teardown, Schutz laufender Prozesse und fremder Ressourcen |
| `vendor-integrity` | Neun importierte Dateien gegen feste SHA-256-Werte und vorhandene Originaldateien vergleichen |
| `shell-syntax*` | Bash-Syntax aller vier Betriebs-/Hilfsskripte; Namespace-Testskript wird zusätzlich tatsächlich ausgeführt |

Die Namespace-Prüfung ist reproduzierbar ohne sudo:

```bash
bash tests/network_namespace.sh
```

Ist das Anlegen unprivilegierter Namespaces im Kernel deaktiviert, endet dieser
Test mit Skip-Code 77 statt Systemrechte anzufordern. Kein Test führt `sudo`
aus. Die Netzwerkantworten werden vor Diagnoseausgaben gesendet. Ein normales
Linux-Userspace-Programm und Terminalausgaben garantieren trotzdem keine harte
Echtzeitgrenze; der reale LinuxCNC-Watchdog ist Teil des anschließenden manuellen
Tests, nicht allein durch nominale 1-kHz-Testsendungen bewiesen.

Technische Entscheidungen innerhalb des vorgegebenen Umfangs:

* Die Aufgabenbeschreibung lag tatsächlich unter `docs/`, nicht im Projektstamm.
* C++ benutzt die originalen Pakettypen über `extern "C"`; ursprünglicher
  C-Code wird separat als C11 gebaut. Fremdheaderwarnungen bleiben auf
  SYSTEM-Includes begrenzt; generic macros `low`, `high`, `version` werden
  nach dem Include im eigenen Wrapper entfernt.
* Schrittbursts werden sofort ganzzahlig integriert; das Timing wird dekodiert,
  aber noch nicht als elektrische Pulssimulation ausgeführt. PIO-Indizes >=299
  werden abgewiesen. Im Original erfolgt ein fehlerhafter Tabellenzugriff vor
  der Prüfung ([main.c:394–402](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L394)); dieser
  Bug wird gemäß Phase-1-Auftrag nicht reproduziert.
* Strikte Längenprüfung nutzt einen Empfangspuffer mit einem zusätzlichen Byte.
  Auch überlange Datagramme mit gültigem 37-Byte-Präfix werden verworfen.
  Prüfsummenfehler erzeugen keinen bleibenden Fehlerzustand und ändern weder
  Position noch Sequenz/Timingreferenz.
* IDs starten erwartungsseitig bei 0, werden bei Abweichung synchronisiert und
  in der Antwort unverändert gespiegelt; dann wird ID+1 modulo 256 erwartet.
  Duplikate/Umordnungen werden wie im normalen Originalpfad erneut angewendet.
  Lückenstatistik ist keine beweisbare Verlustanzahl. Quellen:
  [main.c:797–805, 907–908, 1022](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L797).
* Antworten gehen immer an den tatsächlichen Absender, wie die Phase-1-Aufgabe
  ausdrücklich verlangt. Die ursprüngliche Firmware fixiert ihren Antwortpeer
  beim ersten Senden; diese Einschränkung wird nicht übernommen.
* Virtuelle Encoder bleiben unabhängige stillstehende Sensoren (Zähler und
  Delta null), Zeitstempel laufen weiter. Sie spiegeln nicht erfundene
  absolute Stepgenpositionen in zusätzliche oder zweckentfremdete Paketfelder.
* Positionsstand und ID-Erwartung bleiben bei Netzpausen erhalten. Es gibt
  noch keine vollständige Firmware-Timeout-/Ausgangs-/Index-/PIO-Emulation.
  Diese Teilfunktionen sind für den beschriebenen ersten Schrittzähler-Test
  nicht erforderlich. Alle Änderungen betreffen nur das Projekt außerhalb
  `stepper-ninja/`; das Wire-Protokoll wurde nicht erweitert.

## 10. Noch manuell nachzuweisen

Der tatsächliche LinuxCNC-Lauf mit dem Original-HAL-Modul, dessen Watchdog und
der hier bereitgestellten separaten Beispielmaschine muss auf dem Host mit
eingerichtetem Namespace ausgeführt werden. Bei der Umsetzung wurden keine
sudo-Systeminstallation und kein privilegiertes Host-Netzwerksetup durchgeführt.
Das Beispiel wurde als Konfiguration vorbereitet; eine laufende LinuxCNC-
Bewegung wird nicht allein aus dem erfolgreichen Modulbuild behauptet.

Für diese Phase sind GUI im Simulator, Materialabtrag, Homing-Sensoren,
Kollisionen, Encoderbewegungen und elektrische PIO-Zeitdetails bewusst nicht
implementiert. Die erste Abnahme ist ausschließlich Pakettransport,
Burstdekodierung, Schrittintegration und gültige Antwort.

## 11. Historische Implementierungsprüfung und Dateiliste (vor der realen Abnahme)

Stand 26.09.2026: Konfiguration und Build mit CMake 3.31.6 und GCC/G++ 14.2.0
erfolgreich. `ctest --test-dir build --output-on-failure`: **8/8 bestanden**,
einschließlich Namespace-Test, kein Test übersprungen. Alle `scripts/*.sh` und
`tests/network_namespace.sh` zusätzlich mit `bash -n` geprüft. Der unveränderte
HAL-Treiber wurde in diesem damaligen Implementierungslauf separat erfolgreich
gebaut, aber noch nicht installiert oder mit einer laufenden LinuxCNC-Maschine
getestet. Die spätere reale Abnahme ist im aktuellen Status oben festgehalten.
Der Originalcheckout wurde zusätzlich vollständig gegen vor der Implementierung erfasste Dateihashes
geprüft und hat einen leeren Git-Status. Nach den Tests existiert kein
`veth-lcnc` oder `cnc-sim-ns` im Hostnetz.

Neu angelegte Projektdateien (Buildprodukte ausgenommen):

```text
.gitignore
CMakeLists.txt
README.md
docs/phase1-test.md
src/main.cpp
src/machine/MachineState.hpp
src/machine/MachineState.cpp
src/network/UdpServer.hpp
src/network/UdpServer.cpp
src/protocol/OriginalProtocol.hpp
src/protocol/StepperNinjaProtocol.hpp
src/protocol/StepperNinjaProtocol.cpp
scripts/network-common.sh
scripts/setup-veth.sh
scripts/teardown-veth.sh
scripts/run-simulator.sh
tests/protocol_tests.cpp
tests/udp_integration.py
tests/network_namespace.sh
tests/vendor_integrity.py
examples/phase1/phase1.ini
examples/phase1/phase1.hal
examples/phase1/phase1.tbl
third_party/stepper-ninja/UPSTREAM.md
third_party/stepper-ninja/SHA256SUMS
third_party/stepper-ninja/LICENSE.txt
third_party/stepper-ninja/firmware/inc/config.h
third_party/stepper-ninja/firmware/inc/internals.h
third_party/stepper-ninja/firmware/inc/footer.h
third_party/stepper-ninja/firmware/inc/kbmatrix.h
third_party/stepper-ninja/firmware/modules/transmission.c
third_party/stepper-ninja/firmware/modules/inc/transmission.h
third_party/stepper-ninja/firmware/modules/inc/jump_table.h
third_party/stepper-ninja/firmware/modules/inc/pio_settings.h
```

Die drei vorhandenen Aufgaben-/Analysedokumente und `stepper-ninja/` wurden
nicht geändert. Die neun importierten Originaldateien sind unverändert;
`UPSTREAM.md` und `SHA256SUMS` sind eigene Metadaten.
