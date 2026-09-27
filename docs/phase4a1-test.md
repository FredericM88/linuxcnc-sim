# Phase 4A.1: reale LinuxCNC-Abnahme

**Ergebnis: am 2026-09-27 real/interaktiv erfolgreich abgenommen.**
Dieses Protokoll dokumentiert die vom Benutzer durchgeführte und bestätigte
Sitzung mit LinuxCNC 2.9.10. Implementierungsstand:
`7d91d9ea060fe0d0c7d3cea0e640f37d876fefb0`
(`Add configurable workpiece placement`). Die Dokumentation ergänzt die
bereits erfolgreichen automatisierten Prüfungen; sie beschreibt keinen neuen
Testlauf durch den Dokumentationsautor.

Die [automatisierte Abnahme](phase4a-test.md#phase-4a1-runtime-konfiguration-und-regressionen)
umfasst Graphics Release 26/26, Headless Release 24/24 und den zusätzlichen
OpenGL-3.3-Test. Stepper-Ninja-/Vendor-Dateien blieben unverändert; das
Headless-Binary besitzt keine OpenGL-/GLFW-Abhängigkeit. Koordinatenmodell,
Transaktionen und Grenzen stehen in [phase4a1-design.md](phase4a1-design.md).

## 1. Start und Testumgebung

Simulator mit Renderer, 400 Steps/mm und aktiver Phase-3-VirtualIO:

```bash
sudo ip netns exec cnc-sim-ns \
  runuser -u "$USER" -- env \
  DISPLAY="$DISPLAY" \
  XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}" \
  XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
  ./build/cnc-sim \
    --render \
    --steps-per-unit 400,400,400,400 \
    --io-config examples/phase3/virtual-io.conf
```

LinuxCNC:

```bash
linuxcnc examples/phase3/phase3.ini
```

Geprüft wurde die vollständige reale Softwarekette:

```text
LinuxCNC 2.9.10
  -> originaler Stepper-Ninja-HAL-Treiber
  -> originales Stepper-Ninja-UDP-Wire-Protokoll
  -> virtuelles Ethernet / Network Namespace
  -> cnc-sim
  -> integrierte Maschinen-Step-Positionen
  -> OpenGL-Werkzeugdarstellung im gemeinsamen Maschinenraum

Werkstückkonfiguration -> Werkstücktransformation -> OpenGL-Rohteildarstellung
```

Werkzeug und Rohteil teilen den G53-Maschinenraum. Der Werkstücktransform wird
nicht auf die Maschinen-Step-Positionen des Werkzeugs angewendet. VirtualIO aus
Phase 3 blieb während der Sitzung aktiv.

## 2. Default-Rohteil

`workpiece show` direkt nach dem Start bestätigte:

| Größe | X | Y | Z |
|---|---:|---:|---:|
| Size (mm) | 50 | 50 | 10 |
| Position (G53, mm) | 0 | 0 | 0 |
| Origin-Offset vom Rohteilminimum (mm) | 0 | 0 | 10 |
| Machine bounds (mm) | 0 .. 50 | 0 .. 50 | -10 .. 0 |

Voxelgröße: **0,1 mm**. Chunkgröße: **32 × 32 × 32**.
Der Default entsprach `workpiece origin min min max`; die Oberseite lag bei Z=0.

## 3. Größenänderung zur Laufzeit

```text
workpiece size 100 60 20
```

Bei unveränderter Position (0,0,0) blieb der bestehende numerische Origin
bewusst (0,0,10). Die Grenzen wurden X 0..100, Y 0..60, Z -10..10 mm.
Das bestätigt: Symbolische Werte wie `max` sind Eingabehilfen und werden
numerisch gespeichert; eine Größenänderung löst sie nicht erneut auf.

## 4. Symbolischer Origin

Bei Größe (100,60,20) und Position (0,0,0):

| Befehl | Numerischer Origin (mm) | X-Bounds (mm) | Y-Bounds (mm) | Z-Bounds (mm) |
|---|---|---|---|---|
| `workpiece origin min min max` | (0,0,20) | 0 .. 100 | 0 .. 60 | -20 .. 0 |
| `workpiece origin center center max` | (50,30,20) | -50 .. 50 | -30 .. 30 | -20 .. 0 |

Nach dem zweiten Befehl lag der Werkzeugpunkt bei G53 (0,0,0) im Renderer
sichtbar in der Mitte der Werkstückoberseite.

## 5. Position unabhängig vom Origin und Werkzeug

```text
workpiece position 50 30 0
```

Der Origin blieb (50,30,20), die Größe (100,60,20). Daraus folgten
X 0..100, Y 0..60, Z -20..0 mm. Das Werkstück verschob sich im Renderer;
die Werkzeugposition blieb unverändert bei G53 (0,0,0).
Workpiece Position und Tool Machine Position wurden unabhängig behandelt.

## 6. Frei numerischer Origin

```text
workpiece origin 10 15 20
```

Bei Größe (100,60,20) und Position (50,30,0) ergaben sich
X 40..140, Y 15..75, Z -20..0 mm. Der Renderer aktualisierte die Geometrie
unmittelbar. Die beobachteten Grenzen entsprachen exakt:

```text
machine_min = position - origin_offset
machine_max = machine_min + size
```

Dabei bezeichnet `position` die G53-Position des ausgewählten Bezugspunkts,
`origin_offset` seinen Offset vom Rohteilminimum und `size` die physischen
Rohteilabmessungen, jeweils in mm.

## 7. Negative G53-Z-Position

```text
workpiece position 50 30 -50
```

Bei unveränderter Größe (100,60,20) und Origin (10,15,20) wurden die Grenzen
X 40..140, Y 15..75, Z -70..-50 mm. Das Rohteil lag vollständig unter G53-Z=0.
Der Renderer zeigte Werkzeug und Werkstück weiterhin korrekt getrennt.

## 8. Rohteil für den Phase-3-Verfahrraum

Für den abschließenden LinuxCNC-Test wurde folgende kleinere Konfiguration
berichtet:

```text
workpiece size 30 20 10
workpiece position 15 10 -10
workpiece origin center center max
```

| Größe | X | Y | Z |
|---|---:|---:|---:|
| Size (mm) | 30 | 20 | 10 |
| Position (G53, mm) | 15 | 10 | -10 |
| Origin-Offset (mm) | 15 | 10 | 10 |
| Machine bounds (mm) | 0 .. 30 | 0 .. 20 | -20 .. -10 |

Der Bezugspunkt lag exakt in der Mitte der Werkstückoberseite.

**Hinweis zur Wiederholung:** Die berichteten Testfälle sind keine lückenlose
Befehlschronik. Direkt nach Abschnitt 7 würde `workpiece size 30 20 10` wegen
des noch gespeicherten Z-Origin 20 mm transaktional abgewiesen. Für eine direkte
Wiederholung zuerst beispielsweise `workpiece origin min min min` eingeben,
danach die drei obigen Befehle. Dieser vorbereitende Befehl ist eine ergänzte
Reproduktionsanweisung; der tatsächliche Zwischenschritt der Sitzung ist nicht
überliefert. Die berichtete und bestätigte Endkonfiguration steht in der Tabelle.

## 9. LinuxCNC-Referenzfahrt

LinuxCNC wurde mit der bestehenden Phase-3-Konfiguration erfolgreich referenziert.
Danach zeigten alle drei Ebenen:

| Anzeige | X | Y | Z |
|---|---:|---:|---:|
| LinuxCNC (mm) | 0.000 | 0.000 | 0.000 |
| Simulator (Steps) | 0 | 0 | 0 |
| Simulator (mm) | 0.0000 | 0.0000 | 0.0000 |
| OpenGL (mm) | 0.000 | 0.000 | 0.000 |

Eine zuvor sichtbare Differenz vor der Referenzfahrt wurde damit für diese
Sitzung als LinuxCNC-Referenzzustand identifiziert, nicht als Simulator-,
Skalierungs- oder Übertragungsfehler. Das ist eine Beobachtung dieser Sitzung;
die Simulator-Schritthistorie wird durch LinuxCNC-Homing nicht zurückgesetzt.

## 10. End-to-End-Test mit G53

Nach der Referenzfahrt wurde über LinuxCNC-MDI ausgeführt:

```gcode
G21 G90
G53 G1 X15 Y10 Z-5 F300
```

Nach Abschluss der Bewegung:

| Anzeige | X | Y | Z |
|---|---:|---:|---:|
| LinuxCNC (mm) | 15.000 | 10.000 | -5.000 |
| Simulator (Steps) | 6000 | 4000 | -2000 |
| Simulator (mm) | 15.0000 | 10.0000 | -5.0000 |
| OpenGL (mm) | 15.000 | 10.000 | -5.000 |

Bei 400 Steps/mm stimmen alle Werte überein. Das Werkzeug stand visuell exakt
über der Rohteilmitte und **5 mm über der Oberseite bei G53 Z=-10**.
Damit ist der Pfad LinuxCNC → Original-HAL → Original-UDP → Simulator → Renderer
mit tatsächlicher Maschinenbewegung bestätigt. LinuxCNC bleibt für G54/G55
verantwortlich; der Simulator wendet keine zusätzlichen Work Offsets an.

## 11. UDP- und Protokollzustand

Beim abschließenden End-to-End-Test waren mehr als 841000 UDP-Zyklen verarbeitet.
Beobachteter Stand:

```text
RX = accepted = TX = 841238

Invalid:             0
Send errors:         0
Length errors:       0
Checksum errors:     0
Timing errors:       0
Position overflows:  0
Packet-ID gaps:      0
```

Dies ist ein erfolgreicher funktionaler Langzeittest der beobachteten Sitzung,
keine harte Echtzeit- oder formale Zuverlässigkeitsgarantie.

## 12. Abnahmeumfang und offene Phasen

Real/interaktiv bestätigt wurden:

- Runtime-Änderungen von Workpiece Size und Workpiece Position.
- Symbolische Origins `min`/`center`/`max`, frei numerischer Origin und korrekte
  Bounds nach `position - origin_offset` und anschließender Addition von `size`.
- Numerische Speicherung symbolischer Origins über Größenänderungen hinweg.
- Werkstück-Revision-Updates, Runtime-Rebuild/Remesh und unmittelbare
  OpenGL-Aktualisierung; konkrete Revisionsnummern wurden nicht überliefert.
- Trennung von Tool Position und Workpiece Position sowie korrekte
  G53-Maschinenraumdarstellung.
- Kompatibilität mit aktiver Phase-3-VirtualIO und LinuxCNC-Homing.
- Vollständiger LinuxCNC-/HAL-/UDP-/Simulator-/Renderer-Pfad und fehlerfreie
  Kommunikation während der beobachteten Sitzung.

Gemischte symbolisch/numerische Origins, Änderungen der Voxelauflösung, Reset
und ungültige transaktionale Eingaben sind durch die vorhandenen automatisierten
Tests abgedeckt; für diese Sitzung liegen dazu keine separaten realen
Abnahmeschritte vor. Die VirtualIO-Kompatibilität bedeutet nicht, dass sämtliche
früheren Phase-3-Probe-/I/O-Einzeltests erneut durchgeführt wurden.

Nicht Bestandteil dieser Abnahme und weiterhin außerhalb von Phase 4A.1:

- Materialabtrag und Swept Tool Cutting.
- Werkzeug-/Werkstück-Kollision.
- A-Achsen-Rotation des Werkstücks.
- G54/G55-Synchronisation im Simulator.
- Echte Maschinenphysik.

Materialabtrag gehört weiterhin zu einer späteren Phase. Die reale Abnahme
ändert weder diese Phasengrenze noch das bestehende Koordinatenmodell.
