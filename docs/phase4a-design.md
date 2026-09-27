# Phase 4A: Sparse-Volumen und Live-Visualisierung

> Historische Beschreibung der Phase-4A-Baseline. Seit Phase 4A.1 sind
> Simulation und Renderer zur Laufzeit konfigurierbar. Die statischen
> Besitz-/Startmeshing-Aussagen unten werden durch
> [phase4a1-design.md](phase4a1-design.md) ersetzt. Sparse- und Protokollmodell
> bleiben erhalten.

Phase 4A ergänzt die funktionierende Phase-1–3-Baseline `d2fcb37` um eine
optionale OpenGL-Ansicht. Die verbindliche Aufgabenbeschreibung liegt in
[`codex_phase4a_linuxcnc_sim.md`](../codex_phase4a_linuxcnc_sim.md).
**Es gibt keinen Materialabtrag, Tool Sweep oder Kollisionsstopp.**

## Datenfluss und Besitz

```text
Originale Stepper-Ninja-Pakete
  -> StepperNinjaProtocol::accept -> MachineState::integrate (int64, alle Achsen)
  -> VirtualSensors -> originale Response -> UDP send -> MotionRecorder
  -> bestehende Status-Mailbox (ca. 20 ms)
       -> Terminal / Recorderbefehle
       -> MachineSnapshot { steps, scales, accepted_packets }
            -> tool_pose (steps / steps_per_unit)
                 -> geometrisches Werkzeug im Renderer

Simulation besitzt const SparseVoxelVolume
  -> IMeshExtractor / SurfaceMesher
       -> ChunkMesh (CPU: Position, Normale, Indizes)
            -> Renderer: ChunkCoord -> VAO/VBO/EBO
```

`MachineState`, Decoder, Socketcode, Recorder, VirtualIO und Originalquellen
sind unverändert. Simulation erhält nur eine optionale unveränderliche Szene
und eine zusätzliche Snapshot-Lesefunktion. Die Grafik kennt keine Pakete und
integriert keine Position. Das Fenster zeigt dieselbe Step-Historie wie Terminal
und Recorder; unterschiedliche Aktualisierungszeitpunkte sind möglich.

## Threads und Lebensdauer

| Thread | Aufgaben |
|---|---|
| UDP/Simulation | Bisherige Paketverarbeitung, Sensoren, Recorder, Befehle und Statusveröffentlichung |
| Hauptthread bei `--render` | GLFW, Context, initiales CPU-Meshing, Upload, Shader, Kamera, Zeichnen, Fensterereignisse |
| Terminal bei `--render` | Bisherige Terminalschleife, Bedienung und Statusformatierung |
| Zeitweiliger CSV-Thread | Unveränderlicher Recorder-Snapshot und Dateiausgabe wie bisher |

Ohne `--render` bleibt die Terminalschleife im Hauptthread. GLFW wird dann nicht
initialisiert und es wird kein Szenenvolumen in Simulation angelegt.
`machine_snapshot()` kopiert unter dem bestehenden Mutex nur vier int64-Werte,
vier Skalierungen und einen Paketzähler. Umrechnung, Meshing, Formatierung und
alle GL-Aufrufe finden nach Freigabe statt. Der UDP-Thread bekommt keine neue
Arbeit pro Paket. Die bestehende Veröffentlichung bleibt bei etwa 50 Hz;
der Renderer läuft mit VSync und zusätzlicher Begrenzung auf etwa 60 Hz.
Zwischenstände dürfen übersprungen werden, Recorder-Samples nicht.

Das statische Anfangsmeshing läuft einmalig im Hauptthread, während UDP bereits
läuft. Dazu ist in 4A kein weiterer Mesh-Worker nötig. Mesh-Extraktion ist trotzdem
eine eigenständige CPU-Komponente ohne GL-/GLFW-Header. Die Oberfläche kann beim
Start kurz leer sein; Fensterereignisse werden zwischen Chunks bearbeitet.
In 4B können dieselben Extraktoren außerhalb des Renderthreads arbeiten, sofern
sie dann unveränderliche Volumen-Snapshots bekommen. Keine großen Meshdaten
gehören in die Maschinenstatus-Mailbox.

`quit`, SIGINT/SIGTERM und normales Fensterschließen beenden das Programm;
Terminal, ggf. CSV-Export und UDP werden geordnet beendet. GL-Ressourcen werden
vor dem Fenster im Context-Thread freigegeben. Fehler bei GLFW, Context, Shadern,
Upload oder Zeichnen führen zu einer Meldung und sauberem Fehlerende statt
stillem Wechsel in einen anderen Modus. Die bestehenden Grenzen blockierender
Terminal-/Dateiausgabe beim Beenden bleiben bestehen; der UDP-Pfad wartet darauf
während des Betriebs nicht.

## Koordinaten und Einheiten

Maschinenraum ist rechtshändig: +X, +Y, +Z zeigen in die jeweiligen positiven
Weltachsen. X/Y/Z werden rot/grün/blau gezeichnet. Es gibt keine zusätzliche
Vorzeicheninversion. Explizit negative Steps/mm behalten die bisherige Semantik.
Die Grafik verlangt XYZ-Anzeigelabels `mm`; eine automatische Zollumrechnung
findet nicht statt. `4000 / 400 = 10 mm`. A wird weiterhin integriert,
aufgezeichnet und in der Pose mitgeführt, aber noch nicht als Rotation benutzt.

`ToolPose::tip_mm` bezeichnet die Mitte der unteren Fräserfläche. Das Werkzeug
ragt in +Z; Default: Flat End Mill, Durchmesser 6 mm, Länge 30 mm. Es ist ein
geometrischer Zylinder, kein Voxelobjekt. `ToolKind` lässt spätere BallEndMill-,
VBit- und ProbeSphere-Typen zu; diese werden in 4A nicht gerendert.
Es gibt keine HAL-Werkzeuglängen- oder G54/G92-Offsetübertragung.

Der Werkstücktransform ist eine Translation und ein normalisiertes Quaternion:
`machine = translation + rotation * local`; die inverse Abfrage nutzt die
konjugierte Rotation. Das Voxelgitter bleibt lokal unverändert. Eine spätere
A-Achse kann dadurch Abfragen transformieren, statt Voxel umzuschichten.

Default-Rohteil: lokal `[0,50) × [0,50) × [0,10)` mm; Translation `(0,0,-10)` mm.
Seine Oberfläche liegt damit bei Maschinen-Z=0. Der Tisch liegt knapp unter der
transformierten Bounding Box. Maße, Lage und Rotation stammen aus SceneConfig,
nicht aus fest codierter Rohteilgeometrie im Renderer. CLI:

```text
--voxel-size 0.1
--stock-size 50,50,10
--stock-origin 0,0,-10
--stock-rotation 0,0,0
```

Rotationen sind Grad um lokale X/Y/Z-Achsen, zusammengesetzt als `Rz * Ry * Rx`.
Der Kamera-Fit umfasst Tisch, Werkstück, Achsen und aktuelle Werkzeugpose.

## Sparse-Chunks und Voxel-Semantik

`VoxelValue = uint8_t`: 0 leer, 255 Material; 1..254 bleiben für spätere
Scalar-Fields reserviert. Die öffentliche Sample-API liefert Werte, kein bool.
Der Blockmesher behandelt vorerst jeden Wert ungleich 0 als Material.

Standardchunk: 32³ Zellen. `VolumeConfig` enthält Voxelgröße (0,10 mm) und
Chunkgröße (API: 1..128). Das Default-Rohteil besitzt 500×500×100 logische Voxel
in 16×16×4 Chunks, aber anfangs **null gespeicherte Chunks und null Voxelbytes**.
Eine fehlende Map-Zelle bedeutet implizit Material innerhalb der Anfangsbox,
sonst leer. Voll belegte Chunks sind `Solid`, vollständig leere `Empty`,
teilweise belegte Randchunks logisch `Mixed`, zunächst ebenfalls ohne Array.

`materialize_chunk` kann eine solche Darstellung ohne Materialänderung explizit
machen: Solid/Empty bleiben ohne Array; Mixed erhält bei 32³ genau 32768 Werte,
X am schnellsten. Dies ist eine Lazy-Speicheroperation für die spätere Erweiterung,
keine Schneid-/Removal-API. Die gerenderte Simulation stellt ihr Volumen als const
bereit. Ein großer unberührter Block bleibt unabhängig von seiner Voxelzahl billig.

Zellen haben halboffene Grenzen. Nicht ganzzahlige Maße werden auf die nächste
Voxelgrenze nach außen aufgerundet. Eine kleine, vier-Epsilon-relative Toleranz
fängt binäres Rundungsrauschen an exakten Vielfachen ab. Dieselbe Behandlung
vor `floor(mm/h)` verhindert z.B. ein falsches Ergebnis für 0,3/0,1; wirklich
negative Werte direkt unter null bleiben negativ. Negative Chunkkoordinaten
verwenden mathematische Floor-Division: -1 -> Chunk -1 / lokal 31,
-32 -> -1 / 0, -33 -> -2 / 31. Ganzzahlige Floor-/Modulo-Operationen unterstützen
auch int64-Extrema. mm-Konvertierung und Chunkursprünge sind auf ±2^40 Voxel
begrenzt, um nachfolgende Arithmetik sicher zu halten.

## Meshing und Renderer

`SurfaceMesher` liest Nachbarn über `volume.sample`, auch außerhalb des eigenen
Chunks. Nur Material->Leer-Grenzen werden Flächen. Pro Ebene werden angrenzende
Flächen zu Rechtecken zusammengefasst. Es gibt keine einzelnen Voxel-Drawcalls,
keine Innenflächen zwischen Solid-Nachbarn und keine Ersatzbox für das Rohteil.
Normalen und Dreieckswinding zeigen nach außen. Gemeinsame Eckpunkte entstehen
über identische Integerkoordinaten und Double->Float-Konvertierung; Chunkränder
verwenden dieselben Positionen. CPU-Meshpositionen liegen im Werkstückraum.

`DirtyChunks` dedupliziert Markierungen in einem Set und übergibt sie mit `take()`.
Der Anfangsaufbau verwendet dieses Set; GPU-Meshes bleiben über ChunkCoord
adressierbar. Ein späterer Schnitt muss betroffene Chunks **und Grenznachbarn**
markieren. Es gibt derzeit keine dynamischen Änderungen, keinen Worker-Pool und
keine komplexen Versions-/Jobregeln. Das ist die bewusst einfache 4A-Entscheidung.

`cnc_sim_render` nutzt OpenGL 3.3 Core, GLSL 330, VAO/VBO/EBO, Depth Test,
Backface Culling und einfache gerichtete Beleuchtung. Polygon Offset hält die
Weltachsen auf der Rohteiloberfläche sichtbar. Die GPU erhält Meshes nur beim
Szenenaufbau. Werkzeugbewegung ändert ausschließlich eine Modellmatrix.
Der Fenstertitel zeigt XYZ in mm und die RGB-Achsenzuordnung.

Auf dem Linux-Zielsystem exportiert libGL die Core-Funktionen direkt.
`GL_GLEXT_PROTOTYPES` und die installierten GL-Header genügen; kein GLAD/GLEW,
kein generierter Loader und keine weiteren Frameworks. Diese Wahl ist bewusst
Linux-spezifisch. GLFW prüft beim Context-Aufbau mindestens 3.3 Core.

Kamera: linke Maustaste Orbit, mittlere Maustaste Pan, Rad Zoom, Home Fit Scene;
Z bleibt oben, Perspektive 45°. Resize und minimierte Fenster werden behandelt.
Zur Begrenzung unabsichtlich riesiger Startjobs akzeptiert der Renderer maximal
65536 logische Rohteilchunks; bei Überschreitung nennt er `--voxel-size` als Abhilfe.
Core-Sparse-Volumen sind nicht auf diese Chunkzahl beschränkt.

## Build und Phasengrenze

Core verwendet die C++-Standardbibliothek, POSIX und header-only GLM.
Nur das optionale Renderziel linkt OpenGL und GLFW. Build-/Startbefehle,
Abnahmeschritte und Ergebnisse stehen in [phase4a-test.md](phase4a-test.md).

Phase 4B bleibt vollständig offen: Tool Sweep, Material Removal, veränderliche
Volumen-Snapshots, Nachbar-Invalidierung, asynchrones Remeshing und GPU-Updates.
Auch allgemeine Kollision, SDF-Auswertung, adaptive Verfeinerung und
4-Achs-Bearbeitung sind nicht Teil dieses Stands. Das Werkzeug kann das Rohteil
sichtbar durchdringen; dieses Verhalten ist in Phase 4A beabsichtigt.
