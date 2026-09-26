# CNC-Simulator – Phase 3: Virtual I/O, Endschalter, Homing und Probe

## 1. Ziel

Phase 3 erweitert den bestehenden LinuxCNC-CNC-Simulator um die Rückrichtung **Simulator → LinuxCNC**.

Phase 1 hat die Bewegungskette validiert:

```text
LinuxCNC
  ↓
original Stepper-Ninja HAL driver
  ↓
original Stepper-Ninja UDP wire protocol
  ↓
C++ cnc-sim als virtuelle Stepper-Ninja-Hardware
  ↓
MachineState mit integrierten int64-Schrittpositionen
```

Phase 2 ergänzt eine interaktive Terminaloberfläche und einen Motion Recorder. Phase 2 ist auf dem Zielrechner gebaut und mit 11/11 automatisierten Tests sowie anschließend mit echtem LinuxCNC praktisch validiert worden. Gerade und Kreis wurden korrekt rekonstruiert; die bestehende UDP-/veth-/Namespace-Architektur funktioniert.

Phase 3 soll nun virtuelle digitale Eingänge, Endschalter und eine Probe implementieren. LinuxCNC soll diese Signale über den **unveränderten originalen Stepper-Ninja-Rückkanal** empfangen.

Zielarchitektur:

```text
                    Step-Bursts
LinuxCNC ─────────────────────────► cnc-sim
                                      │
                                      ▼
                                 MachineState
                                      │
                         ┌────────────┴────────────┐
                         ▼                         ▼
                    Axis switches                Probe
                         │                         │
                         └────────────┬────────────┘
                                      ▼
                                  VirtualIO
                                      │
                               inputs[0..3]
                                      │
LinuxCNC ◄────────────────────────────┘
          Stepper-Ninja UDP response
```

LinuxCNC soll keinen speziellen Simulatorpfad benötigen. Der Simulator soll sich weiterhin wie Stepper-Ninja-Hardware verhalten.

---

## 2. Nicht verhandelbare Randbedingungen

1. Das originale Stepper-Ninja-Wire-Protokoll darf nicht verändert oder erweitert werden.
2. Der originale Stepper-Ninja-Checkout und importierte Original-C-Code bleiben unverändert.
3. Die funktionierende Phase-1- und Phase-2-Architektur darf nicht neu geschrieben werden.
4. Die veth-/Network-Namespace-Lösung bleibt erhalten.
5. Der bestehende `MachineState` mit exakten `int64_t`-Schrittpositionen bleibt die maßgebliche Maschinenposition.
6. Der bestehende Motion Recorder muss unverändert weiter funktionieren.
7. Keine blockierenden Terminal-, Datei- oder sonstigen langsamen Operationen im UDP-Pfad.
8. LinuxCNC entscheidet über Homing, Limits und Probe-Verhalten. Der Simulator darf LinuxCNC-Positionen nicht heimlich korrigieren oder Home-Positionen selbst setzen.
9. Digitale Eingangssignale werden ausschließlich über die bestehenden Stepper-Ninja-Responsefelder `inputs[0..3]` übertragen.
10. Vor der endgültigen HAL-Verdrahtung muss anhand des originalen Stepper-Ninja-HAL-Treibers exakt festgestellt werden, welche `inputs[]`-Bits auf welche exportierten HAL-Pins abgebildet werden. Keine Pin-Namen oder Bitzuordnungen raten.

---

## 3. Bestehendes Protokoll nutzen

Das Standard-Stepper-Ninja-Responsepaket besitzt vier 32-Bit-Eingangsfelder:

```text
uint32_t inputs[0]
uint32_t inputs[1]
uint32_t inputs[2]
uint32_t inputs[3]
```

Damit stehen insgesamt 128 digitale Bits im bestehenden Wire-Format zur Verfügung.

Phase 3 darf hierfür keine neuen UDP-Felder, Pakettypen, Header oder Simulator-Sondernachrichten einführen.

Die virtuellen Eingänge müssen vor der Erstellung von Checksumme und Response vollständig feststehen.

---

## 4. Architektur: VirtualIO

Eine eigenständige I/O-Schicht einführen, z. B.:

```text
src/io/VirtualIO.hpp
src/io/VirtualIO.cpp
```

Konzeptionelle Verantwortung:

```cpp
class VirtualIO {
public:
    void set_input(std::size_t index, bool state);
    bool input(std::size_t index) const;

    std::array<std::uint32_t, 4> packed_inputs() const;

private:
    // geeignete threadsichere oder synchronisierte Darstellung
};
```

Die konkrete Implementierung darf vom Beispiel abweichen, wenn sie besser zur bestehenden Architektur passt.

`VirtualIO` soll die logischen Eingangszustände verwalten. Das Stepper-Ninja-Protokoll soll nur die daraus erzeugten `inputs[0..3]` in die Response übernehmen.

Keine Endschalter- oder Probe-Geometrie direkt in die UDP-Klasse schreiben.

---

## 5. Verarbeitungskette und Reihenfolge

Die bestehende Phase-2-Verarbeitung muss so erweitert werden, dass virtuelle Inputs für die Antwort desselben Maschinenzustands berücksichtigt werden.

Logisch erforderlich:

```text
UDP request empfangen
       ↓
Länge / Checksumme / Timing validieren
       ↓
stepgen_command[] dekodieren
       ↓
MachineState aktualisieren
       ↓
virtuelle Sensoren aus aktuellem MachineState aktualisieren
       ↓
VirtualIO aktualisieren
       ↓
Stepper-Ninja Response mit inputs[] erzeugen
       ↓
Response-Checksumme erzeugen
       ↓
UDP response senden
       ↓
MotionRecorder / Diagnose
```

Der genaue Refactoring-Umfang soll klein bleiben. Die bisher validierte Protokollverarbeitung nicht unnötig neu implementieren.

Falls `StepperNinjaProtocol::process()` aktuell MachineState-Update und Response-Erstellung untrennbar kombiniert, eine kleine saubere Schnittstelle schaffen, über die die aktuellen digitalen Inputs vor dem finalen Response-Build eingespeist werden können.

---

## 6. Phase 3A – manuelle digitale Eingänge

Vor automatischen Endschaltern oder Probe zuerst den kompletten Rückkanal isoliert validieren.

Terminalbefehle mindestens:

```text
input show
input <n> on
input <n> off
```

Optional sinnvolle Aliase sind erlaubt.

Beispiele:

```text
input 0 on
input 0 off
input 6 on
input 6 off
```

Ungültige Indizes, Werte oder Syntax müssen mit einer verständlichen Fehlermeldung abgewiesen werden und dürfen den Simulator nicht beenden.

Das Terminal soll die Eingangszustände sichtbar machen. Mindestens die für Phase 3 konfigurierten Endschalter- und Probe-Eingänge sollen direkt erkennbar sein.

Zieltest:

```text
Simulator command
      ↓
VirtualIO bit
      ↓
Stepper-Ninja response.inputs[]
      ↓
original Stepper-Ninja HAL
      ↓
LinuxCNC HAL pin
```

Dieser Test muss erfolgreich sein, bevor automatische Sensorlogik als validiert gilt.

---

## 7. Input-Mapping

Eine zentrale konfigurierbare Zuordnung vorsehen. Als gewünschtes logisches Modell beispielsweise:

```text
X-Min
X-Max
Y-Min
Y-Max
Z-Min
Z-Max
Probe
```

Die konkreten Inputnummern dürfen **erst nach Untersuchung des originalen Stepper-Ninja-HAL-Treibers** festgelegt werden.

Keine Annahme treffen, dass ein bestimmtes Wire-Bit automatisch `motion.probe-input`, `joint.N.home-sw-in`, `joint.N.neg-lim-sw-in` oder `joint.N.pos-lim-sw-in` entspricht. Diese Verbindung geschieht über HAL und muss in der Phase-3-Testkonfiguration explizit und nachvollziehbar hergestellt werden.

Die ermittelte Zuordnung in der Phase-3-Dokumentation festhalten.

---

## 8. Phase 3B – virtuelle Achsschalter

Virtuelle physische Schaltpunkte für X, Y und Z implementieren.

Mindestens unterstützen:

```text
X-Min
X-Max
Y-Min
Y-Max
Z-Min
Z-Max
```

A ist zunächst optional, sofern die bestehende Phase-1-Testmaschine dafür keine sinnvolle Homing-/Limit-Konfiguration besitzt.

Die Schaltpunkte müssen konfigurierbar sein und intern bevorzugt in exakten Steps dargestellt werden. Benutzereingaben dürfen in Maschinen-/Achseinheiten erfolgen und anhand der vorhandenen `steps_per_unit` sauber konvertiert werden.

Wichtig:

- Ein Endschalter meldet nur seinen Zustand.
- Er setzt den MachineState nicht zurück.
- Er setzt keine LinuxCNC-Koordinate.
- Er teleportiert die virtuelle Maschine nicht.
- LinuxCNC führt Stop, Backoff, erneute Anfahrt und Setzen der Home-Position selbst aus.

---

## 9. Hysterese

Virtuelle Achsschalter sollen eine kleine konfigurierbare Hysterese unterstützen, damit ein Schalter an der Grenze nicht flattert.

Konzept für einen Min-Schalter:

```text
EIN bei:  position <= trigger
AUS bei:  position >= trigger + hysteresis
```

Für einen Max-Schalter entsprechend umgekehrt.

Keine Zufallswerte in Phase 3. Das Verhalten muss deterministisch und reproduzierbar sein.

---

## 10. Home- und Limit-Verhalten

Die Simulatorlogik darf Home und Hard Limit nicht konzeptionell vermischen.

Das physische Schaltermodell liefert nur digitale Zustände. Ob ein Signal in LinuxCNC als:

```text
home switch
negative limit
positive limit
oder Kombination daraus
```

verwendet wird, wird in der LinuxCNC-HAL-/INI-Testkonfiguration festgelegt.

Damit sollen auch realistische gemeinsame Home-/Limit-Schalter möglich bleiben.

---

## 11. Phase 3C – reale LinuxCNC-Homing-Abnahme

Eine eigene Phase-3-LinuxCNC-Testkonfiguration erstellen bzw. die vorhandene Beispielkonfiguration kontrolliert erweitern.

Ziel ist eine echte LinuxCNC-Referenzfahrt über die simulierten Schalter:

```text
LinuxCNC startet Homing
       ↓
Achse fährt Richtung Home-Schalter
       ↓
cnc-sim integriert reale Stepper-Ninja-Bursts
       ↓
virtueller Schaltpunkt wird erreicht
       ↓
Input wird aktiv
       ↓
LinuxCNC erkennt den Schalter
       ↓
LinuxCNC stoppt / fährt frei / nähert sich gemäß eigener Homing-Konfiguration erneut
       ↓
Input löst sich und schaltet erneut
       ↓
LinuxCNC beendet Homing
```

Die Testdokumentation muss beschreiben, welche LinuxCNC-Homingparameter verwendet werden und welches Verhalten erwartet wird.

---

## 12. Phase 3D – virtuelle Probe

Eine Probe als eigenen virtuellen Sensor implementieren.

Für Phase 3 ist **keine allgemeine 3D-Material- oder Mesh-Simulation** erforderlich.

Die erste Probe darf gegen eine einfache konfigurierbare Geometrie arbeiten, vorzugsweise:

```text
- Ebene, z. B. Z = 0
oder
- axis-aligned Box als einfaches Werkstück
```

Die Probe soll einen eigenen konfigurierbaren Input ansteuern.

Die Probe-Geometrie und der Schneidwerkzeugzustand sind konzeptionell getrennt zu halten. Probe-Kontakt entfernt niemals Material.

---

## 13. Probe-Kontakt und Sweep

Die Schnittstelle der Probe so entwerfen, dass Kontakt zwischen vorheriger und aktueller Pose berücksichtigt werden kann.

Nicht langfristig auf reine Endpoint-Prüfung festlegen:

```text
previous pose ───────────────► current pose
                     X
                  contact
```

Für die einfache Phase-3-Ebene kann eine analytische Segment-/Ebenenprüfung verwendet werden.

Ziel ist, dass die spätere 3D-Simulation dieselbe Grundidee für Swept Tool, Kollision und Materialabtrag wiederverwenden kann.

Die Stepper-Ninja-/LinuxCNC-Eingangsmeldung bleibt dennoch an den normalen Kommunikations-/Servozyklus gebunden. Dokumentiere deshalb bei Bedarf den Unterschied zwischen berechnetem geometrischem Erstkontakt und dem von LinuxCNC im nächsten Zyklus beobachteten digitalen Eingang.

---

## 14. LinuxCNC-Probe-Test

Die Phase-3-Testkonfiguration muss den gewählten virtuellen Probe-Eingang nachvollziehbar auf LinuxCNCs Probe-Eingang verdrahten.

Danach einen echten G-Code-Probezyklus testen, z. B. sinngemäß:

```gcode
G38.2 Z-20 F50
```

Die konkreten Koordinaten müssen zur Testkonfiguration und virtuellen Oberfläche passen.

Erwartetes Verhalten:

```text
LinuxCNC bewegt Z
       ↓
Stepper-Ninja-Bursts erreichen Simulator
       ↓
virtuelle Probe erreicht Oberfläche
       ↓
Probe-Input wird aktiv
       ↓
Response an LinuxCNC
       ↓
LinuxCNC beendet G38.2 aufgrund echten HAL-Probe-Signals
```

Keine Simulator-Sonderbehandlung für `G38.2`; der Simulator kennt keinen G-Code und soll ihn auch weiterhin nicht kennen.

---

## 15. Terminal UI

Die bestehende feste Phase-2-Terminaloberfläche erhalten.

Eine Sektion für virtuelle Inputs/Sensoren ergänzen, beispielsweise:

```text
Virtual Inputs
  X-Min:             OFF
  X-Max:             OFF
  Y-Min:             OFF
  Y-Max:             OFF
  Z-Min:             OFF
  Z-Max:             OFF
  Probe:             OFF
```

Die Anzeige soll weiterhin ungefähr 5 Hz aktualisiert werden. Der UDP-Pfad läuft unabhängig davon weiter.

Die bestehende Kommandozeile und Recorderanzeige bleiben funktionsfähig.

Sinnvolle zusätzliche Diagnosebefehle:

```text
input show
limits show
probe show
```

Weitere kleine Befehle sind erlaubt, wenn sie für Test und Diagnose klar nützlich sind. Kein unnötiges Konsolen-Framework bauen.

---

## 16. Manuelle Overrides und automatische Quellen

Das Design muss klar definieren, wie ein manuell gesetztes Input-Bit mit einem automatisch durch Endschalter oder Probe erzeugten Zustand zusammenspielt.

Bevorzugtes Modell:

```text
physical/automatic source OR manual test override
```

oder eine ähnlich eindeutige, dokumentierte Semantik.

Wichtig ist vor allem, dass Tests reproduzierbar sind und ein manueller Testzustand nicht unbemerkt dauerhaft einen automatischen Sensor übersteuert.

Falls sinnvoll, getrennte Befehle für manuelle Testbits und automatische Sensoren verwenden.

---

## 17. Threading und Synchronisation

Phase 2 besitzt bereits einen separaten UDP-Thread und getrennte Terminal-/Exportpfade.

Phase 3 muss dieses Modell erhalten.

Anforderungen:

- Keine blockierenden Operationen im UDP-Thread.
- VirtualIO-Snapshot für Response muss konsistent sein.
- Terminalbefehle dürfen keine Data Races erzeugen.
- Sensorberechnung soll deterministisch sein.
- Locks im 1-ms-Pfad kurz halten.
- Kein Datei-I/O im UDP-Pfad.

Nicht ohne Not weitere Threads hinzufügen. Sensorberechnung ist klein genug, um im Maschinen-/UDP-Verarbeitungspfad synchron ausgeführt zu werden.

---

## 18. Motion Recorder

Der Phase-2-Motion-Recorder muss unverändert semantisch weiterarbeiten:

- ein Startsample,
- danach nur Samples bei tatsächlicher Schrittpositionsänderung,
- volle `int64_t`-Positionen,
- monotone Mikrosekunden,
- `changed_axes`-Maske,
- keine Idle-Paketflut.

Virtuelle Inputänderungen allein erzeugen in Phase 3 **keine MotionSamples**, solange sich keine Achsposition geändert hat.

Falls später ein Event-Recorder gewünscht wird, ist das eine eigene Erweiterung und nicht Bestandteil dieser Phase.

---

## 19. Tests

Alle bisherigen Phase-1- und Phase-2-Tests müssen weiter bestehen.

Neue automatisierte Tests mindestens für:

1. `VirtualIO` kann jedes relevante Bit setzen und löschen.
2. Packen von Input 0, 31, 32, 63, 64, 95, 96 und 127 in die korrekten `inputs[0..3]`-Wörter.
3. Response-Checksumme bleibt mit gesetzten Inputs korrekt.
4. Manueller `input n on/off`-Befehl funktioniert.
5. Ungültiger Inputindex wird sauber abgewiesen.
6. Min-Schalter schaltet am Trigger ein.
7. Min-Schalter löst erst nach Hysterese wieder aus.
8. Max-Schalter entsprechend.
9. Mehrere Schalter können gleichzeitig aktiv sein.
10. MachineState wird durch Schalteraktivierung nicht verändert.
11. Probe gegen einfache Ebene erkennt Kontakt.
12. Probe löst vor Kontakt nicht aus.
13. Probe löst nach Rückzug wieder aus bzw. setzt sich gemäß definierter Hysterese/Geometrie zurück.
14. Sweep erkennt einen Kontakt zwischen zwei Zuständen, sofern implementiert.
15. Manueller Input erreicht das Responsepaket im UDP-Integrationstest.
16. Automatischer Endschalter erreicht das Responsepaket.
17. Recordersemantik bleibt unverändert.
18. Terminal bleibt bei aktiven Inputs stabil und nicht scrollend.
19. Bestehende Netzwerk-/Namespace-/Vendor-Integrity-/Shelltests bleiben grün.

Falls die bestehende Testarchitektur eine leicht andere Aufteilung sinnvoll macht, ist das erlaubt; die genannten Verhaltensweisen müssen abgedeckt sein.

---

## 20. Manuelle Abnahme mit echtem LinuxCNC

### Test A – Rückkanal ohne Physik

1. veth/Namespace wie bisher starten.
2. cnc-sim starten.
3. LinuxCNC mit Phase-3-Testkonfiguration starten.
4. Einen freien virtuellen Input per Simulatorbefehl `on` setzen.
5. In LinuxCNC/HAL beobachten, dass der korrekte Pin aktiv wird.
6. Input wieder `off` setzen und Rücknahme prüfen.

Erst danach Endschalter/Homing testen.

### Test B – Endschalter

Eine Achse kontrolliert auf einen virtuellen Schaltpunkt zufahren.

Prüfen:

- Input wird exakt am vorgesehenen Bereich aktiv.
- Input löst beim Zurückfahren wieder.
- Keine Änderung/Teleportation des MachineState durch den Sensor selbst.
- keine UDP-/Checksum-/Timing-/Packet-ID-Fehler.

### Test C – Homing

LinuxCNC `Home` bzw. `Home All` ausführen.

Prüfen:

- Suchfahrt,
- Schalterkontakt,
- Freifahrt,
- gegebenenfalls langsame zweite Anfahrt,
- erfolgreiche LinuxCNC-Referenzierung,
- korrekte Sensoranzeige im Simulator.

### Test D – Probe

1. Probe-Geometrie/virtuelle Oberfläche konfigurieren.
2. Probe-Input zunächst inaktiv prüfen.
3. passenden `G38.2`-Zyklus ausführen.
4. LinuxCNC muss aufgrund des simulierten Stepper-Ninja-Eingangs stoppen.
5. Probe-Ergebnis und Simulatorposition vergleichen.
6. Rückzug ausführen und Rücknahme des Probe-Signals prüfen.

---

## 21. Dokumentation

Neu bzw. aktualisiert dokumentieren:

```text
docs/phase3-test.md
README.md
```

`docs/phase3-test.md` soll mindestens enthalten:

- Architektur der virtuellen Inputs,
- tatsächliches Wire-Bit → Stepper-Ninja-HAL-Pin Mapping,
- LinuxCNC-HAL-Netze für Home/Limit/Probe,
- konfigurierte virtuellen Schaltpunkte,
- Hysterese,
- Probe-Geometrie,
- manuelle Inputbefehle,
- Homing-Test,
- G38.2-Test,
- erwartete Anzeigen und Fehlerzähler,
- bekannte Einschränkungen.

---

## 22. Nicht Bestandteil von Phase 3

Explizit nicht implementieren:

- OpenGL oder andere 3D-Renderer,
- GUI/Qt,
- allgemeine Mesh-Szene,
- STL-Import,
- Voxelmaterial,
- Heightfield-Materialabtrag,
- Fräser-Materialabtrag,
- Schraubstock-/Spannmittelkollision,
- allgemeine Maschinenkollision,
- komplexe Werkzeughaltergeometrie,
- 4-/5-Achs-Kinematik,
- eigener G-Code-Parser,
- Änderungen am Stepper-Ninja-Wire-Protokoll,
- EtherCAT,
- Änderungen am originalen Stepper-Ninja-Checkout.

Diese Themen gehören in spätere Phasen.

---

## 23. Vorbereitung für Phase 4

Ohne Phase 4 bereits zu implementieren, die Schnittstellen so halten, dass später folgende Struktur möglich ist:

```text
MachineState
    │
    ├── MotionRecorder
    │
    └── Simulation Engine
          │
          ├── VirtualIO / sensors
          ├── Probe sweep
          ├── Tool sweep
          ├── Material model
          └── Collision engine
```

Probe-Kontakt soll möglichst nicht fest an eine `Z=0`-Sonderlogik gekoppelt werden. Eine kleine abstrakte Kontakt-/Geometrieschnittstelle ist sinnvoll, solange daraus kein unnötiges Framework entsteht.

---

## 24. Definition of Done

Phase 3 gilt erst als abgeschlossen, wenn:

- das Projekt sauber konfiguriert und gebaut wird,
- alle alten Tests weiterhin bestehen,
- alle neuen VirtualIO-/Limit-/Probe-Tests bestehen,
- der originale Stepper-Ninja-Code unverändert ist,
- das Wire-Protokoll unverändert ist,
- manuelle virtuelle Inputs real über den originalen HAL-Treiber in LinuxCNC sichtbar sind,
- mindestens eine echte LinuxCNC-Referenzfahrt über einen simulierten Schalter erfolgreich ist,
- ein echter LinuxCNC-`G38.2`-Probezyklus über den simulierten Probe-Eingang erfolgreich ist,
- der Phase-2-Recorder weiterhin korrekt arbeitet,
- RX/TX-Kommunikation ohne neue Invalid-/Checksum-/Timing-/Packet-ID-Fehler läuft,
- die Testanleitung die exakte HAL-/Input-Zuordnung dokumentiert.

---

# Codex-Arbeitsauftrag

Arbeite im bestehenden Projekt:

```text
~/dev/linuxcnc-sim
```

Lies zuerst diese Datei vollständig und untersuche danach den aktuellen Phase-2-Code sowie den originalen Stepper-Ninja-HAL-Treiber.

## Aufgabe

Implementiere **Phase 3: Virtual I/O, Endschalter, Homing und Probe** entsprechend dieser Spezifikation.

Wichtige Prioritäten:

1. Bestehende Phase-1-/Phase-2-Funktionalität erhalten.
2. Originalen Stepper-Ninja-Checkout und importierten Original-C-Code nicht verändern.
3. Wire-Protokoll nicht verändern.
4. Zuerst tatsächliches `inputs[]` → HAL-Pin-Mapping aus dem Originalcode ermitteln und dokumentieren.
5. Eine saubere `VirtualIO`-Schicht implementieren.
6. Manuellen Input-Rückkanal implementieren und testen.
7. Virtuelle Min-/Max-Achsschalter mit Hysterese implementieren.
8. LinuxCNC-Testkonfiguration für echte Homing-Sequenz vorbereiten.
9. Virtuelle Probe gegen einfache Geometrie implementieren; Schnittstelle für Sweep vorbereiten.
10. LinuxCNC-HAL-Verbindung für `motion.probe-input` korrekt dokumentieren und testen.
11. Terminalanzeige und Befehle erweitern, ohne den 1-ms-UDP-Pfad zu blockieren.
12. Automatisierte Tests ergänzen.
13. `docs/phase3-test.md` und README aktualisieren.

Keine 3D-Grafik, Materialsimulation oder allgemeine Kollisionsengine implementieren.

## Build und Tests

Nach den Änderungen mindestens ausführen:

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Keine unnötigen `sudo`-Systemänderungen durchführen.

## Abschlussbericht

Am Ende exakt berichten:

- welche Dateien neu oder geändert wurden,
- wie `VirtualIO` aufgebaut ist,
- welches Wire-Inputbit auf welchen originalen Stepper-Ninja-HAL-Pin abgebildet wird,
- welche HAL-Netze für Home/Limit/Probe verwendet werden,
- wie Endschalter und Hysterese modelliert sind,
- wie Probe und Kontaktprüfung funktionieren,
- wie manuelle Inputbefehle funktionieren,
- ob und wie die Verarbeitungskette für Response-Inputs refaktoriert wurde,
- welche Synchronisation/Threading-Regeln gelten,
- welche Tests hinzugekommen sind,
- vollständiges Build-/CTest-Ergebnis,
- genaue Schritte für die manuelle LinuxCNC-Abnahme von Input, Homing und `G38.2`,
- bekannte Einschränkungen.

Wenn die Spezifikation an einer Stelle mit dem tatsächlichen Phase-2-Code oder dem originalen Stepper-Ninja-Treiber kollidiert, **nicht stillschweigend etwas anderes implementieren**. Die Abweichung zuerst im Abschlussbericht klar benennen und die kleinstmögliche technisch saubere Lösung wählen.
