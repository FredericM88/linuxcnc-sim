# Codex-Auftrag: LinuxCNC CNC Simulator -- Phase 4A

## Auftrag

Implementiere **Phase 4A** des bestehenden Projekts `linuxcnc-sim`.

Phase 1--3 sind bereits implementiert, getestet und als funktionierende
Git-Baseline vorhanden. Phase 4A ergänzt den Simulator um ein
Sparse-Voxel-/Chunk-Modell, eine getrennte Surface-Meshing-Schicht,
einen modernen OpenGL-Renderer, eine interaktive 3D-Kamera, ein
einfaches Werkzeugmodell und die Live-Darstellung der vom bestehenden
Stepper-Ninja-Pfad empfangenen Maschinenposition.

**Phase 4A enthält ausdrücklich noch keinen Materialabtrag.** Zuerst
müssen Koordinatensysteme, Chunking, Meshing, Rendering und
Live-Kopplung an den bestehenden Machine State korrekt funktionieren.

## 1. Bestehende Baseline

``` text
LinuxCNC
  ↓
original Stepper-Ninja HAL driver
  ↓
original Stepper-Ninja UDP wire protocol
  ↓
virtual Ethernet / network namespace
  ↓
cnc-sim
  ↓
protocol decoder
  ↓
integrated int64 step positions
  ↓
Simulation / recorder / VirtualIO
```

Abgeschlossen und real getestet:

-   Phase 1: Hardwareemulation und Positionsintegration
-   Phase 2: Terminal UI und Motion Recorder
-   Phase 3: VirtualIO, Endschalter, Homing und Probe
-   16/16 bestehende Tests bestanden
-   Git-Baseline vorhanden

Das originale Stepper-Ninja-Wire-Protocol darf nicht verändert werden.
Vendor-/Originalcode nicht unnötig verändern.

## 2. Harte Randbedingungen

Alle bisherigen Funktionen und Tests müssen erhalten bleiben: UDP,
Protokoll, Checksummen, Positionsintegration, Recorder, Terminal UI,
VirtualIO, Endschalter, Homing, Probe und bestehende Beispiele.

Der UDP-/Simulationspfad läuft ungefähr mit 1 ms / 1000 Hz. OpenGL,
GLFW, GPU-Uploads, Meshing, Shader-Kompilierung, Dateioperationen und
Fensterereignisse dürfen diesen Pfad nicht blockieren.

OpenGL-Aufrufe ausschließlich im Thread, der den OpenGL-Context besitzt.

## 3. Zielarchitektur

``` text
LinuxCNC / Stepper-Ninja
          │
          ▼
     Machine State
          │
          ▼
       Tool Pose
          │
          ├──────────────────────────┐
          │                          ▼
          │                   OpenGL Renderer
          │
SparseVoxelVolume
          │
          ▼
     Mesh Extractor
          │
          ▼
      Chunk Mesh
          │
          ▼
   OpenGL Renderer
```

Simulation besitzt die Daten. Renderer visualisiert
Snapshots/Renderdaten. OpenGL besitzt niemals den authoritative
simulation state.

## 4. Zielsystem und Dependencies

Debian 13/Trixie, LinuxCNC, RT-Kernel, C++20, CMake.

Installiert:

``` text
libgl1-mesa-dev
libglfw3-dev
libglm-dev
mesa-utils
pkg-config
```

Entwicklungsrechner: AMD Radeon Graphics / radeonsi Phoenix, Mesa
25.0.7, Hardwarebeschleunigung aktiv, OpenGL 4.6 Core, GLSL 4.60.

Trotzdem **OpenGL 3.3 Core als Mindestanforderung** anstreben.

Keine unnötigen Frameworks wie Qt, SDL, Vulkan, CUDA, OpenCL, VTK, CGAL,
OpenVDB oder Game Engine einführen. GLFW und GLM sind erlaubt. Falls ein
OpenGL-Loader nötig ist, kleine nachvollziehbare Lösung wählen und
dokumentieren.

## 5. Sparse-Voxel-Modell

Initiale Chunkgröße:

``` text
32 × 32 × 32 Voxel
```

Zentrale Konfiguration, z. B.:

``` cpp
struct VolumeConfig {
    double voxel_size_mm = 0.10;
    uint32_t chunk_size = 32;
};
```

Voxelgröße muss konfigurierbar sein. Default zunächst 0,10 mm.

Voxel nicht als `bool` modellieren:

``` cpp
using VoxelValue = uint8_t;
```

Semantik Phase 4A:

``` text
0   = leer
255 = Material
1..254 = für spätere Oberflächen-/Scalar-Field-Verfahren reserviert
```

Öffentliche API nicht dauerhaft auf binäre Belegung festlegen.

## 6. Sparse/Lazy Chunks

Mindestens logisch:

``` cpp
enum class ChunkState : uint8_t {
    Empty,
    Solid,
    Mixed
};
```

Solid und Empty benötigen kein vollständiges Voxelarray. Mixed speichert
Werte. Ein 32³-uint8-Chunk benötigt 32768 Byte.

Das unbearbeitete Rohteil nicht vollständig voxelweise allokieren.
Konzept:

``` text
innerhalb initiales Werkstück + kein Mixed-Chunk → Solid
außerhalb Werkstück                             → Empty
Mixed-Chunk vorhanden                           → Voxelwerte
```

`std::unordered_map<ChunkCoord, VoxelChunk, ChunkCoordHash>` ist
möglich, darf aber sinnvoll verbessert werden.

## 7. Koordinatensysteme

Sauber trennen:

``` text
Machine Space
    ↓
Workpiece Transform
    ↓
Workpiece Local Space
    ↓
Chunk Coordinates
    ↓
Voxel Coordinates
```

Voxel `(0,0,0)` darf nicht implizit Maschinenkoordinate `(0,0,0)` sein.

Werkstück erhält Translation/Orientierung, z. B. auf GLM-Basis.
Architektur soll eine spätere A-Achse ermöglichen, ohne Millionen Voxel
physisch zu drehen. Später soll das Werkzeug/die Abfrage in den lokalen
Werkstückraum transformiert werden können.

Phase 4A braucht noch keinen 4-Achs-Materialabtrag.

## 8. SparseVoxelVolume

Klare Core-Komponente, konzeptionell:

``` cpp
class SparseVoxelVolume {
public:
    VoxelValue sample(/* voxel coordinate */) const;
private:
    VolumeConfig config_;
    // bounds, transform, sparse chunks
};
```

Anforderungen:

-   keine OpenGL-/GLFW-Abhängigkeit
-   unabhängig unit-testbar
-   sichere negative Koordinaten
-   mathematisch korrekte Floor-Division
-   korrekte Chunkgrenzen
-   klare Umrechnung mm ↔ voxel ↔ chunk

Negative Koordinaten explizit testen. C++-Integerdivision gegen Null
nicht versehentlich als Floor-Division verwenden.

## 9. Initiales Werkstück

Mindestens rechteckiges Rohteil, erster Demonstrator z. B.:

``` text
50 × 50 × 10 mm
```

Maße und Position nicht im Renderer hart codieren. Rohteil über
SparseVoxelVolume repräsentieren.

## 10. Meshing-Abstraktion

Simulation und Meshing strikt trennen, etwa:

``` cpp
class IMeshExtractor {
public:
    virtual ~IMeshExtractor() = default;
    virtual ChunkMesh build(
        const SparseVoxelVolume& volume,
        ChunkCoord chunk
    ) = 0;
};
```

CPU-Daten etwa:

``` cpp
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
};

struct ChunkMesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
```

Simulation darf nicht von `GLuint` oder anderen OpenGL-Typen abhängen.

## 11. Phase-4A-Mesher

Zunächst einfacher, eindeutig überprüfbarer Block-/Surface-Mesher. Für
Solid→Empty-Übergänge sichtbare Flächen erzeugen.

Nicht Würfel einzeln rendern. Keine OpenGL-Aufrufe pro Voxel. Mesher
erzeugt zusammenhängende CPU-Meshdaten. Greedy Meshing optional;
Korrektheit wichtiger als Optimierung.

## 12. Chunk-Grenzen

Nachbarwerte über Chunkgrenzen korrekt berücksichtigen. Keine
künstlichen Innenflächen oder sichtbaren Nähte.

Mesher vorzugsweise über `volume.sample(...)` und bei Bedarf
1-Voxel-Halo arbeiten lassen. Unit-Test über Chunkgrenze erstellen.

## 13. Dirty-/Versionsmodell

Architektur auf spätere Materialänderungen vorbereiten. Wiederholtes
Dirty-Markieren desselben Chunks darf später nicht zwingend identische
Meshjobs stapeln.

Dirty-Set oder Versionsmodell vorsehen, z. B. simulation_version /
mesh_version / gpu_version. Phase 4A braucht noch keinen komplexen
Worker-Pool.

## 14. OpenGL-Renderer

Moderner Renderer:

-   OpenGL 3.3 Core oder kompatibel
-   GLFW
-   Depth Test
-   VAO/VBO/EBO
-   Vertex- und Fragment-Shader
-   kein `glBegin/glEnd`
-   keine Fixed-Function-Matrixpipeline

GPU-Typen bleiben ausschließlich im Renderer, z. B.:

``` cpp
struct GpuChunk {
    GLuint vao;
    GLuint vbo;
    GLuint ebo;
    uint32_t index_count;
    uint64_t version;
};
```

## 15. Threading

Alle OpenGL-Aufrufe im Context-/Render-Thread.

UDP-Thread darf niemals `glBufferData`, `glDrawElements`,
`glfwPollEvents` etc. aufrufen.

Datenübergabe über Snapshot-/Queue-/State-Schnittstelle. Locks im
1-ms-Pfad extrem kurz halten. Keine Meshgenerierung unter einem Lock,
den der UDP-Thread benötigt.

## 16. Kamera

Technische 3D-Kamera mit mindestens:

``` text
linke Maustaste      Orbit
mittlere Maustaste   Pan
Mausrad              Zoom
Home                  Fit Scene
```

Perspektivische Darstellung. Abweichungen dokumentieren, falls sinnvoll.

## 17. Grundszene

Mindestens darstellen:

-   Werkstück
-   Werkzeug
-   einfache Tisch-/Grundfläche
-   X/Y/Z-Achsen
-   sinnvolle Beleuchtung/Oberflächenschattierung
-   Depth Test

Keine GUI oder Texturen nötig.

## 18. Werkzeug

Erster Test:

``` text
Flat End Mill
Durchmesser 6 mm
```

Werkzeug nicht voxelisieren. Geometrisch/analytisch modellieren.
Architektur für spätere `FlatEndMill`, `BallEndMill`, `VBit`,
`ProbeSphere` offen halten.

Eine spätere Schnittstelle darf z. B. `bounds()` und `signedDistance()`
bereitstellen; Phase 4A benötigt noch keine Material-SDF-Operation.

## 19. Live Machine State

Authoritative Position bleibt die bestehende integrierte
`int64`-Step-Position.

Bei 400 steps/mm:

``` text
4000 steps = 10.000 mm
```

Renderer interpretiert keine Stepper-Ninja-Pakete selbst und integriert
keine zweite Position.

Pfad:

``` text
Stepper-Ninja decoder
        ↓
integrated authoritative step positions
        ↓
MachineState / Snapshot
        ↓
unit conversion
        ↓
ToolPose
        ↓
Renderer
```

## 20. Achsenrichtung

Keine stillschweigende Invertierung:

``` text
LinuxCNC +X → Werkzeug +X
LinuxCNC +Y → Werkzeug +Y
LinuxCNC +Z → Werkzeug +Z
```

Weltachsen sichtbar machen.

## 21. Manueller Live-Abnahmetest

LinuxCNC:

``` gcode
G21 G90
G53 G1 X10 Y10 Z2 F300
```

Bei 400 steps/mm erwartet:

``` text
X = 4000 steps = 10.000 mm
Y = 4000 steps = 10.000 mm
Z =  800 steps =  2.000 mm
```

Numerische Simulatorwerte und sichtbare Werkzeugposition müssen aus
derselben authoritative state source stammen.

## 22. Taktraten

Entkoppeln:

``` text
LinuxCNC / UDP   ~1000 Hz
Renderer         ~60 Hz / vsync-orientiert
```

Renderer darf Zwischenstände überspringen und neuesten konsistenten
Snapshot anzeigen. Motion Recorder bleibt unabhängig.

## 23. Materialabtrag NICHT implementieren

Phase 4A endet vor Tool Sweep → Voxel Removal → Dirty Chunks → Remesh.

Nicht erforderlich:

-   Materialabtrag
-   Werkzeug-/Halterkollision
-   adaptive Refinement
-   Marching Cubes
-   Dual Contouring
-   SDF-Reinitialisierung
-   GPU Compute
-   4-Achs-Materialabtrag

Schnittstellen dürfen vorbereitet werden, aber keine halbfertige Phase
4B einbauen.

## 24. Tests

Neue automatisierte Tests mindestens für:

**Volume:** Solid/Empty, Werkstückgrenzen, Voxelgröße, mm→Voxel,
Voxel→Chunk, lokale Koordinaten, negative Koordinaten, Chunkgrenzen,
Sparse/Lazy-Verhalten.

**Mesher:** Empty erzeugt keine Geometrie, korrekte Außenflächen, keine
Innenflächen zwischen Solid-Nachbarn, Chunkgrenzen ohne künstliche Naht.

**Machine State:** 4000 Steps / 400 steps/mm = 10 mm, negative
Positionen, mehrere Achsen, Snapshot-Konsistenz.

**Regression:** alle bestehenden Phase-1--3-Tests weiterhin erfolgreich.

## 25. Headless Tests

Core-Tests dürfen keinen laufenden X11/OpenGL-Desktop voraussetzen.
SparseVoxelVolume, Koordinaten, CPU-Mesher und
Machine-State-Konvertierung müssen headless testbar bleiben.

Renderer-Tests separat behandeln.

## 26. CMake

Sauber erweitern. Core und Renderer nach Möglichkeit trennen,
konzeptionell:

``` text
cnc-sim-core
cnc-sim-render
cnc-sim
tests
```

An vorhandenen Aufbau anpassen. OpenGL/GLFW nicht unnötig in reine
Core-Tests ziehen.

## 27. Betrieb ohne Grafik

Bestehende Phase-1--3-Nutzung soll weiterhin ohne OpenGL-Fenster möglich
sein.

Sinnvolle Option z. B.:

``` text
--render
```

oder entsprechende Konfiguration. Verhalten dokumentieren.

## 28. Fehlerbehandlung

Klare Meldungen bei GLFW-/Context-/Shader-/Fensterfehlern. Keine stillen
Fehler. Simulations-Core sauber beenden oder, falls sinnvoll, ohne
Renderer weiterlaufen lassen.

## 29. Dokumentation

Mindestens erstellen/aktualisieren:

``` text
docs/phase4a-design.md
docs/phase4a-test.md
README.md
```

Dokumentieren: Architektur, Koordinatensysteme, Chunkmodell,
Voxel-Semantik, Threading, Renderer, Kamera, Dependencies, Build, Start,
manuelle Tests, Einschränkungen und klare Grenze zu Phase 4B.

## 30. Git-Disziplin

Vor Änderungen:

``` bash
git status
```

Vor Abschluss mindestens:

``` bash
git status
git diff --check
ctest --test-dir build --output-on-failure
```

bzw. vollständige passende Testsequenz.

Keine Build-Artefakte committen. **Nicht automatisch pushen**, sofern
nicht ausdrücklich angefordert.

Sinnvolle Commit-Message:

``` text
Add Phase 4A sparse voxel and OpenGL visualization
```

## 31. Bestehenden Code zuerst untersuchen

Vor Implementierung:

1.  Source Tree lesen
2.  bestehende Simulation/Position-State-Strukturen verstehen
3.  Threading verstehen
4.  CMake prüfen
5.  Tests lesen
6.  Phase-1--3-Dokumentation lesen

Keine parallele zweite Simulationsebene erfinden, wenn bestehende
Strukturen sinnvoll erweitert werden können. Stil/Namenskonventionen
beibehalten.

## 32. Prioritäten

``` text
1. Phase-1–3-Regression vermeiden
2. korrekte Maschinen-/Werkstückkoordinaten
3. Echtzeitpfad nicht blockieren
4. saubere Simulation/Renderer-Trennung
5. testbare Sparse-Voxel-Struktur
6. korrekte Chunkgrenzen
7. stabile OpenGL-Darstellung
8. Performance
9. optische Schönheit
```

## 33. Definition of Done

Phase 4A ist abgeschlossen, wenn:

-   alle bisherigen Tests bestehen
-   neue Volume-/Mesher-/Koordinatentests bestehen
-   SparseVoxelVolume implementiert ist
-   32³-Chunking funktioniert
-   Voxelgröße konfigurierbar ist
-   Solid/Empty/Mixed-Konzept vorhanden ist
-   Werkstück- und Maschinenkoordinaten getrennt sind
-   rechteckiges Rohteil dargestellt wird
-   OpenGL-3.3-Core-kompatibler Renderer läuft
-   Orbit/Pan/Zoom funktionieren
-   XYZ-Achsen und Tisch/Grundfläche sichtbar sind
-   6-mm-Werkzeug sichtbar ist
-   Werkzeugposition aus dem bestehenden authoritative step state stammt
-   LinuxCNC-Bewegungen live im 3D-Fenster erscheinen
-   Renderer den 1-ms-UDP-Pfad nicht blockiert
-   Core-Tests headless laufen
-   Dokumentation vollständig ist
-   kein Materialabtrag als unfertige Zusatzfunktion implementiert wurde

## 34. Abschlussbericht

Nach Implementierung einen technischen Abschlussbericht liefern mit:

-   neuen/geänderten Dateien
-   Architekturentscheidungen
-   Dependencies
-   Build-Befehl
-   Testresultaten
-   Startbefehl für Simulator + Renderer
-   manuellem LinuxCNC-Abnahmetest
-   bekannten Einschränkungen
-   Git-Status
-   Commit-Hash, falls committed

Wenn bestehende Projektstrukturen einer hier beispielhaft gezeigten
C++-Signatur widersprechen, darf die konkrete Signatur angepasst werden.
Architekturziele und Randbedingungen haben Vorrang vor
Beispielsignaturen.

## Ausblick Phase 4B -- nicht implementieren

Phase 4A soll folgende spätere Pipeline ermöglichen:

``` text
ToolPose[n-1] + ToolPose[n]
          ↓
   Swept Tool Volume
          ↓
Broad Phase / affected chunks
          ↓
    Material Removal
          ↓
      Dirty Chunks
          ↓
Asynchronous Remeshing
          ↓
      OpenGL Upload
```

Geplanter erster Phase-4B-Test: 6-mm-Flachfräser, 50×50×10-mm-Rohteil:

``` gcode
G21 G90
G0 X5 Y25 Z2
G1 Z-2 F100
G1 X45 F300
G0 Z2
```

Phase 4A schafft dafür die Grundlage, implementiert diesen
Materialabtrag aber noch nicht.
