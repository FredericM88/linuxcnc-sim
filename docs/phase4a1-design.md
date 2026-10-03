# Phase 4A.1: konfigurierbares Rohteil

Historische Architektur der Phase 4A.1; Materialabtrag wurde später in
[Phase 5](phase5-design.md) ergänzt.
Diese Erweiterung ersetzt die statische Szenenverwaltung aus Phase 4A.
Kein Materialabtrag, Tool Sweep, Kollisionsmodell, G54/G55-Abgleich oder neue
Probe-Semantik. Die Phase-3-Probe bleibt eine unabhängige Ebene in Steps.

Die reale/interaktive Abnahme mit LinuxCNC 2.9.10 am 2026-09-27 war
erfolgreich. Beobachtete Konfigurationen, G53-Positionen und UDP-Zähler sind
in [phase4a1-test.md](phase4a1-test.md) dokumentiert.

## Koordinaten und Konsole

| Parameter | Bedeutung |
|---|---|
| `size X Y Z` | Physische Rohteilabmessungen in mm, endlich und positiv |
| `position X Y Z` | G53-Maschinenposition des ausgewählten Werkstückbezugspunkts in mm |
| `origin X Y Z` | Offset dieses Bezugspunkts vom Rohteilminimum in mm, pro Achse in `[0,size]` |
| `voxel SIZE` | Endliche positive Voxelauflösung in mm; Chunkgröße fest 32³ |

```text
workpiece show
workpiece size 100 60 20
workpiece position 50 30 0
workpiece origin 10 center max
workpiece voxel 0.10
workpiece reset
```

Die drei Origin-Komponenten akzeptieren unabhängig Zahlen, `min` (=0),
`center` (=size/2) und `max` (=size). Die Symbole werden bei Eingabe einmalig
aufgelöst; gespeichert werden ausschließlich Zahlen. Eine spätere Größenänderung
behält diese numerischen Offsets. Wird dadurch ein Offset größer als die neue
Abmessung, wird die gesamte Änderung abgewiesen. Zuerst den Origin ändern,
dann das Rohteil verkleinern. Es gibt kein stilles Clamping.

```text
machine_min = position_machine_mm - origin_offset_mm
machine_max = machine_min + size_mm
```

Für `size 100 60 20` ergeben sich:

| Position | Origin | Machine min | Machine max |
|---|---|---|---|
| `0 0 0` | `min min max` | `0 0 -20` | `100 60 0` |
| `0 0 0` | `center center max` | `-50 -30 -20` | `50 30 0` |
| `50 30 0` | `10 15 20` | `40 15 -20` | `140 75 0` |
| `50 30 0` | `10 center max` | `40 0 -20` | `140 60 0` |

LinuxCNC ist für G54/G55 usw. verantwortlich. Die Werkzeugposition entsteht
weiterhin nur aus integrierten tatsächlichen Steps und Steps/mm. Ein neuer
Origin verschiebt weder Werkzeug, Recorder noch Sensoren.

`show` und erfolgreiche Änderungen zeigen Größe, G53-Position, numerischen
Origin, physische Maschinengrenzen, Voxel- und Chunkgröße sowie die Revision.
Bei langen Antworten gibt das feste Terminal Statuszeilen zugunsten der
Befehlsausgabe frei; `workpiece show` passt vollständig in 80×24 Zeichen.

## Defaults und bestehende Startoptionen

`WorkpieceConfig{}` ist die zentrale Reset-Konfiguration. Sie verwendet die
vorhandenen `SceneConfig`-/`VolumeConfig`-Defaults: Größe 50×50×10 mm,
Position (0,0,0), Origin (0,0,10), Voxel 0,10 mm, Chunks 32³.
Grenzen: X/Y 0..50, Z -10..0 mm. Reset stellt diese Projektdefaults wieder her,
auch wenn der Prozess mit anderen Startoptionen gestartet wurde.

Die Phase-4A-Optionen `--stock-size`, `--stock-origin`, `--stock-rotation` und
`--voxel-size` bleiben kompatibel. **`--stock-origin` ist die alte Translation
des lokalen Minimums**, nicht der neue Konsolen-Origin. Beim unrotierten Start
wird `origin_offset=(0,0,size.z)` und `position=translation+origin_offset`
abgeleitet. Bei vorhandener Legacy-Rotation wird stattdessen das lokale Minimum
als Bezugspunkt gewählt (`origin_offset=0`, `position=translation`), sodass
`position` auch dort die tatsächliche G53-Position des gewählten Punkts ist.
Diese Startkonfiguration gilt jetzt auch ohne `--render`.

Die bestehende statische CLI-Rotation kollidiert mit dem achsparallelen
4A.1-Modell. Minimal-invasive Entscheidung: Die Startszene behält ihre Rotation,
und `show` kennzeichnet ihre rechnerischen Grenzen ausdrücklich als Werte vor
der Legacy-Rotation. Beim nächsten **gültigen schreibenden** `workpiece`-Befehl
wird ein achsparalleles Rohteil nach obiger Formel erzeugt. Dies wird sowohl
vorher bei `show` als auch nach dem Wechsel ausdrücklich angezeigt. Ungültige
Befehle und `show` verändern auch die Rotation nicht. Die untergeordnete
`WorkpieceTransform`-API und ihre Rotationstests bleiben erhalten. Eine
A-Achsen-Rotation oder ein neuer Rotationsbefehl gehört nicht zu 4A.1.

## Besitz, Veröffentlichung und Rebuild

```text
UDP-Worker -> MachineState -> bestehende Maschinenstatus-Mailbox -> Werkzeugpose

Konsole -> Parser -> Simulation::configure_workpiece
                        | validieren und neues implizites Sparse-Volumen bauen
                        v
              shared_ptr<const WorkpieceSnapshot>
              {numerische Config, const Volume, Revision}
                        |
Hauptthread -> Versionsvergleich -> SurfaceMesher -> GPU-Meshes -> OpenGL
```

Die Simulation besitzt auch headless ein implizites Rohteil ohne Voxelarrays.
Der Parser arbeitet auf einer Kopie. `configure_workpiece` validiert unabhängig
vom Parser und erzeugt zuerst den kompletten neuen Zustand. Erst danach tauscht
es den Snapshot unter einem eigenen kurzen Mutex. Fehler einschließlich
Allokationsfehlern vor der Veröffentlichung lassen Config, Volumen und Revision
unverändert. Vorherige Snapshots bleiben für laufende Leser gültig. Die Konsole
ist der einzelne Schreiber; der Renderer liest nur unveränderliche Snapshots.

Der UDP-Worker greift auf diesen Mutex und die Werkstückdaten überhaupt nicht
zu. Er wartet weder auf Validierung, Meshing, OpenGL noch Terminalausgabe.
Die vorhandene Maschinenstatus-Mailbox, Paketverarbeitung, Step-Integration,
VirtualIO und Sensorbefehle bleiben getrennt und unverändert.

Jede gültige Änderung, auch eine reine Verschiebung oder Reset auf identische
Werte, bedeutet frisches Rohmaterial und eine neue Revision. Der Renderer
invalidiert alle bisherigen Chunk-Meshes, einschließlich nun entfallener Chunks,
und baut sie aus dem neuen Volumen mit dem bestehenden CPU-Mesher neu auf.
Tisch, Achsen, Bounds und Kamera-Fit werden aktualisiert. Home verwendet damit
die aktuelle Szene und Werkzeugpose. Schnelle Zwischenrevisionen dürfen wie
Maschinensnapshots übersprungen werden; die jüngste Revision wird übernommen.

CPU-Meshing bleibt wie in Phase 4A im Haupt-/Context-Thread, getrennt vom
Mesher-Modul und vom UDP-Pfad. Dafür ist kein zusätzlicher Worker erforderlich.
Während eines größeren Rebuilds pausiert das Zeichnen; GLFW-Ereignisse werden
zwischen Chunks bearbeitet. Es gibt keine Render- oder Mesharbeit in der
Werkstück-Mailbox oder im Netzwerkthread. GPU-/Contextfehler führen wie bisher
zu einem klaren Fehlerende. Es gibt noch kein inkrementelles Remeshing.

## Grenzen und Rasterung

Der bisherige Renderjob-Grenzwert von **65536 logischen Chunks** gilt jetzt
zentral für Werkstückkonfigurationen, auch headless. Zu feine Auflösungen und
Indexüberläufe werden vor Veröffentlichung abgewiesen. Das generische
`SparseVoxelVolume` behält seine größeren Sparse-Grenzen und variable Chunk-API;
die Simulator-Werkstücke verwenden ausschließlich 32³.

Zusätzlich müssen gerasterte Weltgrenzen und lokale Ausdehnungen innerhalb
±10¹² mm liegen, um ausreichend Spielraum für Float-Matrizen/Kameraarithmetik
zu lassen. Das ist keine Präzisionsgarantie für große Maschinenkoordinaten.
Ungültige Werte werden abgewiesen, niemals still begrenzt.

Die bereits vorhandene Phase-4A-Rasterung bleibt erhalten: nicht durch die
Voxelgröße teilbare Abmessungen werden für die Voxeloberfläche nach außen
aufgerundet. `show` meldet die angeforderten **physischen** Grenzen nach der
Formel, nicht gerundete Maße. Beispielsweise ergibt size 0,11 mm bei voxel
0,10 mm zwei Voxel und damit 0,20 mm gerasterte Ausdehnung. Kamera und Tisch
verwenden die tatsächlich gerasterten Grenzen. Dies ist eine bewusste
Kompatibilitätsentscheidung; das Koordinatenmodell selbst wird nicht gerundet.
