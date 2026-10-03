# Stepper-Ninja: Wire-Protokoll und Datenfluss

Stand der Analyse: 26.09.2026. Maßgeblich ist ausschließlich der Upstream-Commit
`eb7e5dfa2e76477e606a47038b07cca5e8a4b424`. Mitgelieferte Quellen liegen unter
`third_party/stepper-ninja/`; andere Quellverweise sind auf diesen Commit
festgelegte Upstream-Permalinks.
Die nachstehenden Quellverweise nennen Dateien und Zeilen dieses Commits. Die
Ergebnisse beschreiben den implementierten Code einschließlich seiner Grenzen;
Kommentare und Beispielkonfigurationen sind nicht immer aktuell.

Diese Analyse entstand vor der Implementierung des Empfängers; während der
Analyse wurde kein Empfänger implementiert und keine Originaldatei kopiert oder
verändert. Die Strukturgrößen, Offsets und Beispielprüfsummen wurden mit einem
temporären C-Analyseprogramm direkt gegen die Originalheader und
`transmission.c` geprüft. Ein LinuxCNC-/Hardware-Lauf war nicht Teil dieser
ursprünglichen Analyse. Inzwischen sind Phase 1–3 implementiert, abgeschlossen
und real mit LinuxCNC getestet; den aktuellen Stand beschreibt die
[README](../README.md#current-features). Die Originalquellen bleiben unverändert.

## 1. Verbindliche Standardkonfiguration

| Eigenschaft | Effektiver Wert | Quelle |
|---|---:|---|
| `breakout_board` | 0, direkte GPIOs | [config.h:20–26](../third_party/stepper-ninja/firmware/inc/config.h#L20) |
| `stepgens` | 4 | [config.h:31](../third_party/stepper-ninja/firmware/inc/config.h#L31) |
| `encoders` | 3 | [config.h:38](../third_party/stepper-ninja/firmware/inc/config.h#L38) |
| `pwm_count` / `use_pwm` | 1 / 0 | [config.h:49–50](../third_party/stepper-ninja/firmware/inc/config.h#L49) |
| `ANALOG_CH` | nicht definiert, in `#if` effektiv 0 | [footer.h:12–200](../third_party/stepper-ninja/firmware/inc/footer.h#L12) |
| `raspberry_pi_spi` | 0, UDP | [config.h:59](../third_party/stepper-ninja/firmware/inc/config.h#L59) |
| `use_timer_interrupt` | 0, kein Software-Schrittring | [config.h:74](../third_party/stepper-ninja/firmware/inc/config.h#L74) |
| `encoder_pio_version` | `ENCODER_PIO_SUBSTEP`, Wert 1 | [config.h:76–78](../third_party/stepper-ninja/firmware/inc/config.h#L76), [internals.h:140–145](../third_party/stepper-ninja/firmware/inc/internals.h#L140) |
| `use_stepcounter` / `debug_mode` | 0 / 0 | [footer.h:202–203](../third_party/stepper-ninja/firmware/inc/footer.h#L202) |
| `pico_clock` | 200.000.000 Hz, 5 ns/Zyklus | [footer.h:217](../third_party/stepper-ninja/firmware/inc/footer.h#L217) |
| `default_pulse_width` | 2500 ns | [config.h:71](../third_party/stepper-ninja/firmware/inc/config.h#L71) |
| `default_step_scale` | 1000 Schritte/Maschineneinheit | [config.h:72](../third_party/stepper-ninja/firmware/inc/config.h#L72) |
| Netzwerk ab Werk | 192.168.0.177:8888, Netzmaske 255.255.255.0, Gateway 192.168.0.1 | [config.h:10–17](../third_party/stepper-ninja/firmware/inc/config.h#L10) |
| Firmware-Verbindungstimeout ab Werk | 1.000.000 µs = 1 s | gleiche Quelle |
| Beispiel-Servozeit | 1.000.000 ns = 1 ms | [stepper-ninja.ini:74–84](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/test_config/stepper-ninja.ini#L74) |

**UDP-Nutzdaten: PC→Pico 37 Byte, Pico→PC 61 Byte.** Auch bei `use_pwm=0`
bleiben beide PWM-Arrays im Paket: Entscheidend ist `pwm_count`, nicht `use_pwm`.
Es gibt weder Konfigurationsaushandlung noch Versionsfeld oder Typkennung.
Sender und Empfänger müssen dasselbe Layout voraussetzen.

Die HAL-Header sind Symlinks auf die Firmware-Dateien; `stepper-ninja.c` ist ein
Alias von `stepgen-ninja.c`. Die gemeinsam verwendete Timingtabelle liegt unter
`firmware/modules/inc/pio_settings.h`. Die gleichnamige Datei im Repository-Wurzelverzeichnis
hat im untersuchten Stand identische 299 Datensätze, aber einen anderen
Generierungszeitstempel. Maßgeblich ist die eingebundene Moduldatei.
Belege: [make_symlinks.sh:31–38](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/make_symlinks.sh#L31),
[stepgen-ninja.c:13–15](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L13),
[Firmware-CMakeLists.txt:50–60, 69–71](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/CMakeLists.txt#L50).

## 2. Byteformat

Die beiden Strukturen sind durch `#pragma pack(push, 1)` und `pop` vollständig
gepackt. Es gibt keine Paddingbytes, keinen zusätzlichen Anwendungsheader und
keinen Stringabschluss. Die letzte Position ist immer die ein Byte lange
Prüfsumme. Jeder UDP-Datagramm-Payload enthält genau eine Struktur.
Quelle: [transmission.h:14–49](../third_party/stepper-ninja/firmware/modules/inc/transmission.h#L14).

Es wird direkt der Strukturspeicher gesendet: **native Byteordnung**, keine
`htonl`/`ntohl`-Konvertierung der Nutzdaten. Für Pico und die üblichen LinuxCNC-
x86-/ARM-Little-Endian-Systeme bedeutet das **Little Endian** für alle 16-/32-Bit-
Felder; negative `int32_t`-Encoderwerte erscheinen im Zweierkomplement.
Ein portabler Empfänger muss dieses konkrete Little-Endian-Format lesen.
Netzwerkadressen und Ports werden separat in Netzwerkbyteordnung behandelt.
Quellen: [stepgen-ninja.c:450–464](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L450),
[main.c:698–722, 760–789](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L698).

### 2.1 PC→Pico, Standardlayout (37 Byte)

| Offset | Größe | Typ/Feld | Bedeutung |
|---:|---:|---|---|
| 0 | 4 | `uint32_t stepgen_command[0]` | Schritt-/Timingwort Kanal 0 |
| 4 | 4 | `stepgen_command[1]` | Kanal 1 |
| 8 | 4 | `stepgen_command[2]` | Kanal 2 |
| 12 | 4 | `stepgen_command[3]` | Kanal 3 |
| 16 | 4 | `uint32_t outputs[0]` | digitale Ausgangsbits 0–31 |
| 20 | 4 | `outputs[1]` | digitale Ausgangsbits 32–63; siehe GPIO-Zuordnung |
| 24 | 4 | `uint32_t pwm_duty[0]` | PWM-Level, standardmäßig 0 |
| 28 | 4 | `uint32_t pwm_frequency[0]` | Sollfrequenz in Hz, standardmäßig 0 |
| 32 | 2 | `uint16_t pio_timing` | Index 0–298 der gemeinsamen Timingtabelle |
| 34 | 1 | `uint8_t enc_control` | Bit i: Indexerkennung für Encoder i anfordern |
| 35 | 1 | `uint8_t packet_id` | Sendezähler modulo 256 |
| 36 | 1 | `uint8_t checksum` | Prüfsumme über Bytes 0–35 |

Quelle: [transmission.h:16–32](../third_party/stepper-ninja/firmware/modules/inc/transmission.h#L16).

### 2.2 Pico→PC, Standardlayout (61 Byte)

| Offset | Größe | Typ/Feld | Bedeutung |
|---:|---:|---|---|
| 0, 4, 8 | je 4 | `int32_t encoder_counter[0..2]` | rohe Encoderzähler |
| 12, 16, 20 | je 4 | `int32_t encoder_velocity[0..2]` | Legacy-Zähldelta; Standard-Substep-Modus sendet 0 |
| 24, 28, 32 | je 4 | `uint32_t encoder_timestamp[0..2]` | jeweiliger Abtastzeitpunkt in µs seit Pico-Start, modulo 2³² |
| 36 | 1 | `uint8_t interrupt_data` | Bit i: Encoder i durch Indexereignis zurückgesetzt |
| 37 | 4 | `uint32_t inputs[0]` | GPIO 0–31 im Standardprofil |
| 41 | 4 | `inputs[1]` | GPIO 32–63 im Standardprofil, nur vorhandene GPIOs sinnvoll |
| 45 | 4 | `inputs[2]` | Standard 0; bei anderen Boards Expanderbits |
| 49 | 4 | `inputs[3]` | Standard 0; bei anderen Boards Expanderbits |
| 53 | 4 | `uint32_t jitter` | Abstand zweier verarbeiteter Anfragen in µs |
| 57 | 1 | `uint8_t step_ring_fill` | Software-Ringfüllstand, Standard 0 |
| 58 | 1 | `uint8_t step_ring_status` | Bit 0 aktiv, Bit 1 Underflow, Bit 2 Overflow; Standard 0 |
| 59 | 1 | `uint8_t packet_id` | ID der verarbeiteten Anfrage |
| 60 | 1 | `uint8_t checksum` | Prüfsumme über Bytes 0–59 |

Quelle: [transmission.h:10–12, 35–48](../third_party/stepper-ninja/firmware/modules/inc/transmission.h#L35).
Insbesondere `inputs` und `jitter` sind nicht 32-Bit-ausgerichtet. Ein C++-Parser
darf keine natürliche Ausrichtung voraussetzen.

### 2.3 Allgemeines Layout und Varianten

Seien `S=stepgens`, `P=pwm_count`, `E=encoders`, `A=ANALOG_CH` (effektiv 0,
falls undefiniert) und `B=1`, wenn `E>0`, sonst 0. Alle Größen sind Bytes:

| PC→Pico-Feld | Offset | Größe |
|---|---:|---:|
| `stepgen_command` | 0 | 4S, entfällt bei S=0 |
| `outputs` | 4S | 8 |
| `pwm_duty` | 4S+8 | 4P |
| `pwm_frequency` | 4S+8+4P | 4P |
| `pio_timing` | 4S+8+8P | 2, auch bei S=0 vorhanden |
| `enc_control` | 4S+10+8P | B |
| `analog_out` | 4S+10+8P+B | 4A, entfällt bei A=0 |
| `packet_id` | 4S+10+8P+B+4A | 1 |
| `checksum` | 4S+11+8P+B+4A | 1 |

**PC→Pico-Länge = 4S + 8P + 4A + B + 12.**

| Pico→PC-Feld | Offset | Größe |
|---|---:|---:|
| `encoder_counter` | 0 | 4E |
| `encoder_velocity` | 4E | 4E |
| `encoder_timestamp` | 8E | 4E |
| `interrupt_data` | 12E | B |
| `inputs` | 12E+B | 16 |
| `jitter` | 12E+B+16 | 4 |
| `step_ring_fill` | 12E+B+20 | 1 |
| `step_ring_status` | 12E+B+21 | 1 |
| `packet_id` | 12E+B+22 | 1 |
| `checksum` | 12E+B+23 | 1 |

**Pico→PC-Länge = 12E + B + 24.** Bei E=0 entfällt der gesamte Encoderblock.
Die PWM-Nullarrays verwenden bei P=0 eine GNU-C-Erweiterung; der unveränderte
Header ist dann kein streng portables ISO-C++-Layoutdeklarat.

Die Boardzweige in `footer.h` ergeben folgende Profile, falls dieses Board in
`config.h` ausgewählt wird; sie sind keine zusätzlich parallel aktiven Formate:

| `breakout_board` | S | P | E | A | PC→Pico | Pico→PC | Transport/Besonderheit |
|---:|---:|---:|---:|---:|---:|---:|---|
| 0, aktueller Stand | 4 | 1 | 3 | 0 | 37 | 61 | UDP |
| 1 | 4 | 0 | 2 | 2 | 37 | 49 | UDP; andere Feldpositionen trotz gleicher Anfragelänge! |
| 2 | 0 | 0 | 0 | 0 | 12 | 24 | UDP, IO-Board |
| 3 | 0 | 0 | 4 | 4 | 29 | 73 | Analog-Board; Konfigurationskommentar markiert es als unvollständig |
| 100 | 4 | 0 | 2 | 2 | 37 | 49 | erzwingt SPI |

Belege: [footer.h:12–200](../third_party/stepper-ninja/firmware/inc/footer.h#L12).
`encoder_pio_version`, `use_stepcounter`, `use_timer_interrupt`, `use_pwm`,
`pico_clock`, GPIO-Zuordnungen, `io_expanders`, `toolchanger_encoder` und `debug_mode`
beeinflussen Verhalten bzw. Interpretation, aber bei konstanten S/P/E/A nicht die
Strukturgröße. `KBMATRIX` erweitert diese Paketstrukturen ebenfalls nicht.
Andere `configurations/*` sind alternative Konfigurationen, keine aktive
Laufzeitverhandlung.

`tx_size`, `rx_size` und das Prüfsummen-Längenargument sind `uint8_t`.
Damit ist für die tatsächlichen Sender/Empfänger eine Paketgröße von höchstens
255 Byte erforderlich; darüber werden Größen abgeschnitten. Die Formeln allein
heben diese Implementierungsgrenze nicht auf.
Belege: [stepgen-ninja.c:56–57, 274–290](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L274),
[main.c:120–122](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L120),
[transmission.c:6–22](../third_party/stepper-ninja/firmware/modules/transmission.c#L6).

## 3. LinuxCNC → Schrittwort → Firmware

### 3.1 HAL-Datenweg

Das Beispiel verbindet `joint.0.motor-pos-cmd` über das Signal `Xpos` mit
`stepgen-ninja.0.stepgen.0.command`; entsprechend Y und Z. Der HAL-Treiber legt
für jeden Kanal einen `hal_float_t *command[i]` an. Der vom HAL übergebene
Callbackparameter `period` ist die Threadperiode in ns.
Belege: [Test-HAL:2–25](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/test_config/stepper-ninja.hal#L2),
[stepgen-ninja.c:98–105, 940–950](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L940).

`process-send` leert den Sendepuffer, prüft den Watchdog, setzt `enc_control`,
berechnet bei laufendem Watchdog die Schrittwörter, packt Board-Ausgänge und
gegebenenfalls PWM, trägt ID und Prüfsumme ein und ruft `_send()` auf.
`enable` und `mode` sind lokale HAL-Eingänge; sie werden nicht als eigene Felder
übertragen. `io-ready-in/out` ist ein lokaler Handshake, keine globale Sendesperre
und kein eigenes Wire-Feld.
Quelle: [stepgen-ninja.c:596–768](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L596).

### 3.2 Exakte Bedeutung von `stepgen_command[n]`

Das Feld ist ein **unsigned 32-Bit-Steuerwort für einen endlichen Schrittburst**.
Es ist weder absolute Position noch ein signed Frequenzwert.

| Bits | Bedeutung für Wort W ≠ 0 |
|---|---|
| 31 | Richtungspegel; 1 bei wachsender skalierter Position, 0 bei fallender |
| 30–10 | 21-Bit-Wartezähler D für die PIO-Low-Schleife |
| 9–0 | Schrittzähler N−1; N = (W & 0x3ff) + 1 |

`W=0` bedeutet **keinen neuen Burst einreihen**. Dies muss vor der Dekodierung
geprüft werden, sonst würde daraus fälschlich ein Schritt. Der reine Bitraum
kann 1–1024 Schritte darstellen; die HAL-Tabelle wird nur für N=1–1023 gefüllt.
Das Vorzeichen ist ein Richtungsbit, keine Zweierkomplementkodierung.

Mathematisch für gültige, nicht überlaufende Senderwerte:

```text
W = (direction << 31) | (D << 10) | (N - 1)
N = (W & 0x3ff) + 1
D = (W & 0x7fffffff) >> 10
delta_steps = (direction == 1 ? +N : -N), nur wenn W != 0
```

Belege: [stepgen-ninja.c:640–644, 657–707](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L640),
[main.c:198–214](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L198),
[freq_generator.pio:18–32](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/pio/freq_generator.pio#L18).
Die Firmware setzt `dir_pin[i]` sofort auf `W>>31`, schreibt `W&0x7fffffff`
blockierend in die PIO-TX-FIFO und führt selbst keine Positions-/Geschwindigkeitsskalierung
mehr durch. Ein Nullwort stoppt einen schon laufenden oder gepufferten Burst nicht.

### 3.3 Positionsmodus (`mode=0`, Standard)

Pro Kanal, in der tatsächlichen Reihenfolge:

1. `f_command = float(command + 10000)`. Der feste Offset 10000 ist in
   [stepgen-ninja.c:73–78](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L73) fest kodiert.
2. Beim ersten Sendelauf wird `prev_pos = int64(f_command * scale)` gesetzt.
   Die Umwandlung schneidet für darstellbare Werte gegen null ab.
3. Bei `enable=0`: Nullwort und `continue`; `prev_pos` wird danach nicht laufend
   nachgeführt, und auch `feedback` wird für diesen Kanal nicht aktualisiert.
4. `curr_pos = int64(f_command * scale)`; `f_steps = prev_pos - curr_pos`.
5. Cast nach `int16_t`, dann Betrag. Bei `prev_pos<0 && curr_pos>0` wird ein
   zusätzlicher Schritt addiert. Das ist eine Besonderheit des Originals.
6. Richtung ist 1 bei `prev_pos<curr_pos`, sonst 0. `prev_pos=curr_pos` wird
   schon vor dem Sendeversuch gespeichert.
7. Bei N>0 wird `timing[N] | (sign<<31)` übernommen, sonst Null.
8. `feedback=command`, `first_data=false`, Übertragung nach `stepgen_command[i]`.

Quelle: [stepgen-ninja.c:647–716](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L647).
`scale` ist Schritte pro HAL-Positionseinheit, z.B. Schritte/mm oder Schritte/Grad;
das Protokoll selbst benennt keine Einheit und überträgt `scale` nicht.
Negative Skalierung invertiert im Positionsmodus auch die Richtung.

Der erste absolute Sollwert wird nur als Basis übernommen und nicht als
absoluter Stand übertragen. Der Offset 10000 ist ebenfalls kein Paketfeld.
Die Zwischenumwandlung nach `float` reduziert die Präzision, gerade wegen des
großen Offsets. Positionsrundung, Enable-Lücken, Skalierungswechsel und die
Sonderbehandlung des Nulldurchgangs müssen bei einem Vergleich zur ursprünglichen
HAL-Bahn berücksichtigt werden. Wiederaktivieren kann ein großes Delta erzeugen.

Die Rückmeldung `.stepgen.i.feedback` ist eine **lokale Kopie des Sollwerts**,
keine Bestätigung ausgeführter Schritte und kein vom Pico empfangener Motorstand.
Das Test-HAL verbindet genau diesen Wert mit `motor-pos-fb`
([Test-HAL:19–21](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/test_config/stepper-ninja.hal#L19)).

### 3.4 Geschwindigkeitsmodus (`mode != 0`)

```text
velocity       = float(command)                  [Einheiten/s]
steps_per_sec  = float(velocity * scale)           [Schritte/s]
direction      = (velocity >= 0)                  [nur velocity entscheidet!]
rate           = min(abs(steps_per_sec), max_f)
N              = uint32(rate * period / 1e9)      [Abrunden, kein Restakkumulator]
W              = N > 0 ? timing[N] | direction<<31 : 0
max_f          = uint32(1 / (pulse_width * 2e-9))
```

Quelle: [stepgen-ninja.c:626–629, 687–710](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L687).
Bei 2500 ns gilt `max_f=200000` Schritte/s; bei 1 ms maximal 200 Schritte
pro Callback nach dieser Begrenzung. Kleine Geschwindigkeiten können durch das
Abrunden dauerhaft null Schritte ergeben. Ein negativer `scale` dreht hier die
Richtung **nicht** um, da diese nur aus `velocity` ermittelt wird. Auch hier ist
`feedback=command`, also in diesem Modus eine Geschwindigkeit.

Positions- und Geschwindigkeitsmodus erzeugen dasselbe Wortformat. Der Empfänger
kann den gewählten Modus aus den Paketen nicht eindeutig feststellen.

### 3.5 Timingtabelle, `pulse_width`, `pio_timing`

`pulse_width` ist ein gemeinsamer HAL-u32-Eingang in ns, kein kanalweiser Wert.
`nearest(uint16_t)` wandelt ihn zunächst auf 16 Bit ein, teilt durch
`cycle_time_ns=5`, schneidet auf einen ganzzahligen Zykluswert ab und sucht den
nächstgelegenen `high_cycles`-Wert. Bei Gleichstand gewinnt der frühere Index.
Außerhalb der Tabelle wird Index 0 bzw. 298 gewählt.
Beleg: [stepgen-ninja.c:198–205, 391–413](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L391).

Die 299 Tripel `(sety,nop,high_cycles)` können ohne Originalheader exakt aus
folgender Datendefinition rekonstruiert werden:

1. Für `nop=2..23` (äußere Schleife), `sety=2..31` (innere Schleife) erzeuge
   `(sety,nop,(nop+2)*(sety+1))`.
2. Stabil aufsteigend nach `high_cycles` sortieren.
3. Bei gleichem `high_cycles` nur das erste Tripel behalten.

Dies wurde gegen alle Tripel des Originalheaders verglichen. Quelle:
[pio_setting_generator.py:10–33](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/pio_setting_generator.py#L10).
Index 0: `(2,2,12)`, Index 235: `(24,18,500)`, Index 298: `(31,23,800)`.
Damit entspricht der Standard 2500 ns dem Index **235 = 0x00eb**.
Tabellenquelle: [pio_settings.h:15–314](../third_party/stepper-ninja/firmware/modules/inc/pio_settings.h#L15).

Nur bei Änderung von `pulse_width` wird `timing[1..1023]` neu berechnet:

```text
C = uint32(period_ns * (pico_clock / 1000) / 1000000)
H = pio_settings[nearest(pulse_width)].high_cycles
dormant_cycles = use_timer_interrupt == 0 && stepgens > 0 ? 6 : 0
D[N] = uint32(float(C / N) - H) - dormant_cycles
timing[N] = (D[N] << 10) | (N - 1)
```

`C/N` ist bereits eine Ganzzahldivision. Bei 1 ms gilt C=200000.
Das HAL-Feld `.period` wird zwar am Callbackanfang nach `total_cycles` gelesen,
für die eigentliche Tabellenerzeugung aber durch den aus dem Callbackparameter
berechneten Wert überschrieben. `.period` ist kein Wire-Feld. Eine Änderung
der Threadperiode allein erneuert die Tabelle nicht.
Quelle: [stepgen-ninja.c:67–71, 602, 630–645](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L630).

Core 1 der Firmware liest `pio_timing` und ersetzt die PIO-Instruktionen
`set y` und `nop [delay]` entsprechend `sety&31` und `nop&31`. Das erfolgt
asynchron zur Paketauswertung auf Core 0; Timingänderungen sind nicht atomar
mit dem zugehörigen Schrittburst. Quelle:
[main.c:393–410](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L393).

Das PIO-Programm liest die unteren zehn Bits nach X und lässt die übrigen Bits
als Low-Wartewert im OSR. Seine Schleifen erzeugen X+1 Pulse. Aus den Instruktionen
folgt für ein bereits eingestelltes Programm, ohne FIFO-Warten oder Eingriffe:
High-Zeit `H+2` PIO-Zyklen, Low-Zeit zwischen Pulsen `D+4`, somit Pulsabstand
`H+D+6`. `high_cycles` ist folglich der Tabellen-/Rechenwert; der Name allein
ist keine exakte Messung der Pin-High-Zeit. Das Ende eines Bursts und der nächste
`pull`/`out` bringen zusätzliche Instruktionen und gegebenenfalls Wartezeit.
Die Standardkorrektur 6 kompensiert den inneren Schleifenaufwand; im Ringmodus
fehlt diese Subtraktion. Quelle und Herleitung:
[freq_generator.pio:16–32](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/pio/freq_generator.pio#L16).
Der Firmwarecode verwendet die PIO-Standardkonfiguration ohne eigenen Taktteiler
([main.c:598–601](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L598)); Rechtsverschiebung
und Teiler 1 sind SDK-Vorgaben, ergänzend überprüft am
[offiziellen Pico-SDK-Header, `pio_get_default_sm_config`](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/hardware_pio/include/hardware/pio.h).

### 3.6 Grenzen des tatsächlich implementierten Senders

Es gibt keine allgemeine Sättigung auf 1023 Schritte: Im Positionsmodus können
`int16_t`-Umwandlung und `timing[steps]` außerhalb ihres sinnvollen Bereichs
liegen; im Geschwindigkeitsmodus schützt `max_f` bei anderen Pulsbreiten/Perioden
nicht generell gegen einen Tabellenzugriff >=1024. Zu kleine `C/N` verursachen
eine problematische Float→Unsigned-Umwandlung bzw. Unterlauf bei der Subtraktion,
und ein zu großes D kann beim Linksverschieben Richtungs-/Datenbits zerstören.
`sign<<31` wird im Original über Integer-Promotion mit signed `int` ausgewertet;
bei üblichen 32-Bit-Ints ist das eine C-Sprachstandard-Falle. Der Decoder muss
den resultierenden Bitwert unsigned behandeln. Diese Fälle haben keine portable,
wohldefinierte zusätzliche Protokollsemantik.
Belege: [stepgen-ninja.c:599, 626, 640–705](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L640).

Auch die Firmware validiert Timingindizes nicht korrekt: Der Tabellenzugriff
passiert vor der Prüfung, und verglichen wird mit `sizeof(pio_settings)` (1196
Byte), nicht mit 299 Einträgen. Für gültige Originalpakete gilt Index 0–298;
Werte darüber sind kein definiertes Sonderkommando.
Quelle: [main.c:394–402](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L394).

## 4. UDP-Verbindung, Empfang und Fehlerzustände

### 4.1 PC-Seite

`ip_address` ist ein Modulparameter mit Einträgen `IPv4:Port`, getrennt durch
Semikolon. Die Standard-IP aus `config.h` wird nicht automatisch als HAL-
Parameter eingesetzt; das Beispiel übergibt `192.168.0.177:8888` ausdrücklich.
`inet_pton(AF_INET,...)` akzeptiert numerische IPv4-Adressen, keine DNS-Namen.
Die Parserprüfung erlaubt Ports 0–65535; Port 0 ist dabei kein sinnvoller
Standardpeer. Belege: [stepgen-ninja.c:33–36, 770–837](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L770),
[Minimal-HAL:1](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/test_config/stepper-ninja-minimal.hal#L1).

Die Initialisierung in [stepgen-ninja.c:307–345](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L307)
führt aus:

1. `socket(AF_INET, SOCK_DGRAM, 0)`.
2. `bind(INADDR_ANY, htons(configured_port))`: lokale IP ist **0.0.0.0**, lokaler
   Port ist derselbe konfigurierte Port wie der entfernte Port.
3. `fcntl(..., O_NONBLOCK)`.
4. `remote_addr` auf konfigurierte Ziel-IP und Zielport setzen.
5. `SO_SNDBUF` und `SO_RCVBUF` mit angefordertem Wert 65535 setzen; Rückgabewerte
   werden nicht ausgewertet. Kein `SO_REUSEADDR`, kein `connect`, kein Socket-
   Empfangstimeout.

`sendto` sendet den 37-Byte-Puffer mit `MSG_DONTROUTE | MSG_DONTWAIT`. Das
Routingflag beschränkt die normale Nutzung auf direkt erreichbare Ziele;
der Funktionsrückgabewert wird von `process-send` ignoriert. Auch bei Sendefehler
wird die ID weitergezählt und die Positionsbasis bereits fortgeschrieben.
Belege: [stepgen-ninja.c:450–455, 755–758](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L450).

`process-recv` versucht **ein** `recvfrom` pro Aufruf; es leert nicht die gesamte
Socket-Warteschlange. Kein Paket/EAGAIN und abweichende Länge führen zu keiner
Datenübernahme. Da ohne `MSG_TRUNC` in einen genau 61 Byte großen Puffer gelesen
wird, kann ein längeres Datagramm auf 61 Byte gekürzt werden und die Längenprüfung
trotzdem bestehen. Quelle: [stepgen-ninja.c:480–515](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L480).

Besonders relevant: `recvfrom` schreibt seine Quelladresse direkt nach
`d->remote_addr`, also in das Ziel des nächsten `sendto`. Es gibt keine Prüfung
gegen die konfigurierte IP oder den Port. Das kann sogar vor einer fehlschlagenden
Längen-/Prüfsummenprüfung das zukünftige Sendeziel ändern. Ein kompatibler
Softwarepeer muss mit dieser tatsächlich verwendeten Quellportbehandlung rechnen.

**Betrieb auf demselben Host:** Der unveränderte HAL-Treiber belegt
`0.0.0.0:8888` ohne Wiederverwendungsoption. Ein zweiter normaler UDP-Socket auf
demselben Host/Netzwerk-Namespace kann deshalb nicht einfach gleichzeitig
`127.0.0.1:8888` oder eine andere lokale Adresse mit demselben Port binden.
Das ist eine Folgerung aus dem vorhandenen Bindecode, keine gelöste
Receiver-/Netzwerkarchitektur.

### 4.2 Firmware-Seite

`load_configuration()` übernimmt IP, Netzmaske, Gateway, DNS, DHCP-Kennzeichen,
Port und Timeout aus Flash. Bei ungültiger Flash-Prüfsumme werden Defaults
hergestellt. Der Initialwert `TIMEOUT_US=100000` in `main.c` ist deshalb **nicht**
der effektive Werksdefault von 1 s. Die Standard-Flashkonfiguration setzt
`.dhcp=1` (statisch). Im analysierten Kommunikationspfad wird `wizchip_setnetinfo`
verwendet; ein laufender DHCP-Aushandlungsprozess ist dort nicht implementiert.
Quellen: [flash_config.c:19–29, 118–143](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/modules/flash_config.c#L19),
[serial_terminal.c:42–52](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/modules/serial_terminal.c#L42),
[main.c:137, 486–500, 1137–1148](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L486).

WIZnet-Hardwaresocket **0** wird mit `socket(0, Sn_MR_UDP, port, 0)` geöffnet.
Es gibt auf der Firmwareseite kein POSIX-`bind()`; der Port wird beim Öffnen des
Chipsockets festgelegt. [main.c:1160–1165](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L1160).
Die IPv4-Konfiguration des Chips bestimmt die lokale Adresse. W5100S und W5500
ändern das Anwendungsformat nicht.

Core 0 pollt den aktiven Low-Interrupt-GPIO des WIZnet und ruft `_recvfrom`.
Diese eigene Funktion liest zunächst den **8-Byte-WIZnet-RX-Metadatenheader**:
4 Byte Absender-IP, 2 Byte Absenderport, 2 Byte Nutzdatenlänge, die letzten
beiden Felder Big Endian. Diese acht Bytes sind **kein Bestandteil unseres
37-Byte-UDP-Payloads** und werden nicht in dessen Prüfsumme einbezogen.
Quellen: [main.c:760–789, 988–1023](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L760).

Die Hilfsfunktion ist blockierend: Sie wartet auf RX-Daten oder Socket-Close und
auf die Fertigstellung von Chipbefehlen. Sie liefert `min(buffer_length,data_len)`.
`handle_udp` prüft nur diese zurückgegebene Länge gegen 37. Zu kurze Pakete werden
nicht an `handle_data` gegeben. Zu lange Pakete werden nicht zuverlässig als
solche abgewiesen: Nur 37 Byte werden gelesen; die Funktion entsorgt den Rest
nicht ausdrücklich. Das kann die folgende Auswertung des Chip-RX-Puffers
stören. Reguläre Sender müssen exakt die erwartete Länge senden.

`handle_data` verarbeitet die Anfrage und bereitet eine Antwort vor. Solange
`checksum_error==0`, sendet `_sendto` genau 61 Byte zurück. Es gibt keine
unabhängige periodische Firmware-UDP-Sendung. `_sendto` setzt Ziel-IP und -Port
nur beim ersten Senden nach Start/Verbindungstimeout aus der Anfragequelle;
spätere Anfragen von einem anderen Absender ändern dieses gespeicherte Chipziel
nicht. Die Funktion wartet blockierend auf `SENDOK` oder den Hardwarestatus
`TIMEOUT`; eine eigene zeitliche Softwaregrenze besitzt diese Warteschleife
nicht. Ihr Rückgabewert wird ignoriert.
Quellen: [main.c:698–722, 1015–1023](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L698).

### 4.3 Empfangsprüfung in tatsächlicher Reihenfolge

Firmware, [main.c:792–909](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L792):

1. Nach der Längenprüfung: Empfangsabstand nach `jitter` schreiben und
   `last_packet_time` aktualisieren — **vor** der Prüfsummenprüfung.
2. ID mit `rx_counter` vergleichen; bei Abweichung loggen und synchronisieren.
3. Prüfsumme prüfen. Bei Fehler `checksum_error=1` setzen.
4. Nur bei nicht gesetztem Fehler Schrittwörter anwenden/einreihen und PWM-Duty
   aktualisieren.
5. Encoder und Inputs erfassen; digitale Ausgänge und Board-Paketfunktionen
   werden auch außerhalb dieses Prüfsummengates ausgeführt.
6. Ringstatus, Antwort-ID und Antwortprüfsumme setzen.
7. UDP-Antwort nur bei nicht gesetztem Fehler senden; `rx_counter` für jede
   Anfrage mit passender zurückgegebener Länge erhöhen, auch bei Prüfsummenfehler.

Ein korrektes Folgepaket setzt `checksum_error` **nicht** zurück. Erst der
Verbindungstimeoutpfad löscht den Zustand. Fortlaufende Pakete passender Länge
können den Fehlerzustand durch ihre Zeitstempelaktualisierung aufrechterhalten.
Auch `enc_control`, `pio_timing` und Board-Aktualisierungen auf Core 1 lesen
weiter den gemeinsamen Empfangspuffer. Ein schlechtes Paket wird somit nicht
vollständig und nebenwirkungsfrei verworfen. Das ist ein Originalcodebefund,
kein zusätzliches Protokollfeature.
Quellen: [main.c:312–410, 792–889](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L312).

HAL, [stepgen-ninja.c:480–594](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L480):

1. Bei abgelaufenem Watchdog gar nicht empfangen.
2. Ein Datagramm lesen; Länge vergleichen.
3. `tx_checksum_ok` prüfen, sofern `debug_mode==0` (aktueller Standard).
4. Bei gültigem Paket `connected`, Empfangsalter, Jitter, Ringpins, Encoder und
   Board-Inputs aktualisieren. **Keine Prüfung der Antwort-ID.**

Im HAL-Prüfsummenfehlerzweig steht `d->connected = 0` statt `*d->connected = 0`
([Zeile 506](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L506)). Dadurch wird der
Pinzeiger gelöscht; der nächste gültige Empfang dereferenziert ihn in Zeile 514.
Das ist ein konkreter Fehlerpfad mit möglichem Absturz, der nicht als korrekte
Protokollbehandlung übernommen werden sollte. `connected` und `index-enable`
sind zudem als `HAL_IN` registriert, obwohl der Treiber sie beschreibt
([Zeilen 879 und 968](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L879)).

### 4.4 Watchdogs

Der PC-Watchdog zählt Aufrufe, keine Nanosekunden: `current_time++` pro
`watchdog-process`; ein gültiger Empfang setzt `last_received_time` auf diesen
Zähler. Bei `current_time-last_received_time > 10` läuft er ab. Bei einem
Watchdog-Aufruf pro 1-ms-Servoperiode sind das elf Zählerschritte seit dem letzten
Empfang, abhängig von der Funktionsreihenfolge. `process-send` und
`process-recv` brechen dann beide ab, setzen `io-ready-out=0`; der Empfangspfad
setzt zusätzlich die Ringpins zurück. Weil dann auch kein Empfang mehr das
Alter zurücksetzt, gibt es im normalen Ablauf keine automatische Erholung.
Der Logtext verlangt einen LinuxCNC-Neustart. Ohne Aufruf von `watchdog-process`
beginnt der Sender nicht regulär zu senden.
Belege: [stepgen-ninja.c:367–388, 484–490, 605–607, 623, 760–766, 855–864](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L367).

Core 1 der Firmware prüft reale Mikrosekunden seit `last_packet_time`; bei
`time_diff > TIMEOUT_US` stoppt es den Softwaretimer, leert dessen Ring, setzt
Encoder und digitale/boardspezifische Ausgänge zurück und setzt
`rx_counter=0`, `checksum_error=0`, `first_send=1`. Der Code löscht an dieser
Stelle keine PIO-TX-FIFOs und bricht laufende Schrittbursts nicht ausdrücklich
ab. Bei `use_pwm=1` enthält dieser allgemeine Timeoutzweig keine explizite
PWM-Duty-Nullsetzung. Board-spezifische Abschaltungen stehen in den jeweiligen
Modulen. Das ist vom Hardware-Reboot über `reset_with_watchdog()` zu
unterscheiden. Belege: [main.c:312–355, 674–677, 970–983](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L312).

## 5. Prüfsumme vollständig

Die Wire-Prüfsumme ist eine **Summe tabellensubstituierter Bytes modulo 256**:

```text
c = 0
für jedes Byte b des Pakets außer dem letzten:
    c = (c + jump_table[b]) mod 256
das letzte Byte muss gleich c sein
```

Kein CRC, kein XOR-Akkumulator, keine Verkettung mit dem vorherigen Paket.
Auch `packet_id`, alle ungenutzten/reservierten Arraywerte und jedes Byte der
mehrbyteigen Felder gehen ein. Nur das abschließende `checksum`-Byte wird
ausgenommen; dessen vorheriger Inhalt ist irrelevant. Im Code wird ein
`char*` gelesen, aber der Tabellenindex ausdrücklich nach `uint8_t` gewandelt,
sodass signed `char` keinen negativen Index erzeugt. Die Zuweisung nach jeder
Addition in `uint8_t checksum` reduziert modulo 256.
Quelle: [transmission.c:6–22](../third_party/stepper-ninja/firmware/modules/transmission.c#L6).

`rx_checksum_ok` prüft PC→Pico, `tx_checksum_ok` prüft Pico→PC: Die Namen sind
aus Firmwareperspektive. Der HAL-Sender berechnet selbst mit `calculate_checksum`,
der HAL-Empfänger benutzt `tx_checksum_ok`. Beide Richtungen verwenden dieselbe
Tabelle und denselben Algorithmus.

Die vollständige Tabelle, Zeilenpräfix jeweils der hexadezimale Startindex,
ist für eine Implementierung allein anhand dieser Dokumentation:

```text
00: 04 7d 84 9c 49 92 ec bb eb 22 74 d3 3e a4 dd 28
10: c9 f2 25 c8 63 26 27 dc 18 f0 4a ad e7 6c 10 83
20: 37 46 0b b3 86 a6 48 69 43 a0 d9 01 17 38 1b bd
30: 99 b7 2a ba 4f d7 07 39 7a df 6f 44 54 e4 6b 9d
40: fd 1d 41 1c 2d 76 81 5d 55 2b e2 3c 71 cc d2 61
50: 8f 4c 21 f7 c5 d4 5b c7 15 b6 0d 19 e8 e0 29 2f
60: f6 56 8c 5a 45 9b b2 93 96 31 08 b5 ab bc 7b da
70: 79 e3 7e 6d 1e 13 e5 5c c3 65 fb 33 a2 0a 53 78
80: 14 d6 f8 2e 98 16 c6 a8 00 73 97 9f b1 42 3d d1
90: 64 ef 24 de ac e9 50 d8 03 ea 3a 34 a1 20 fa 6e
a0: 7c c1 7f 80 c2 e6 a5 05 23 06 3b e1 88 36 12 95
b0: cf 68 85 94 66 b8 fc c4 75 67 0e f4 ae 47 5f fe
c0: c0 ff 4b 51 5e 30 62 f5 cb 60 ed 1a d5 87 89 ee
d0: 32 09 a9 82 02 58 a7 8a 3f be 11 cd 6a 40 0c a3
e0: 9a 8e ca 2c b0 57 77 59 aa b9 f1 b4 90 0f af 8b
f0: d0 f9 1f 4e 72 52 ce 70 91 35 f3 8d bf db 4d 9e
```

Quelle: [jump_table.h:6–23](../third_party/stepper-ninja/firmware/modules/inc/jump_table.h#L6),
Copyright/Lizenz: [LICENSE.txt:1–21](../third_party/stepper-ninja/LICENSE.txt#L1), MIT,
Copyright (c) 2025 Zsolt Viola. Die Tabelle ist eine feste Permutation von
0–255. Ihre Reihenfolge darf nicht neu erzeugt oder beliebig verändert werden.
Da die Beiträge nur addiert werden, erkennt diese Prüfsumme z.B. eine reine
Bytevertauschung nicht. Die ID verändert den Beitrag genau wie jedes andere
Byte, sie fügt keine Reihenfolgeprüfung hinzu.

Die ähnlich benannte Flash-Prüfsumme ist eine einfache Summe ohne Jump-Tabelle
und gehört nicht zum UDP-Protokoll
([flash_config.c:41–46](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/modules/flash_config.c#L41)).
Die Variablen `checksum_index` und `checksum_index_in` sowie ältere Deklarationen
wie `xor_checksum` definieren keinen zusätzlichen Wire-Algorithmus.

## 6. Paket-ID und Verlustverhalten

Der HAL-Zähler `tx_counter` ist `uint8_t`, wird in `packet_id` kopiert,
anschließend wird die Prüfsumme berechnet und gesendet; danach wird der Zähler
inkrementiert. Überlauf: 255→0. Das erfolgt auch bei fehlgeschlagenem `sendto`,
aber nicht bei den frühen Watchdog-Rückgaben. Im Initialisierungsblock wird
`tx_counter` nicht explizit gesetzt: Eine erste ID 0 setzt den üblichen
nullinitialisierten HAL-Speicher voraus. Das Protokoll verlangt keine feste
Start-ID, weil die Firmware Abweichungen synchronisiert.
Belege: [stepgen-ninja.c:190, 755–758, 839–864](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L755).

Firmwarezustand `uint8_t rx_counter=0` ist die **nächste erwartete ID**. Bei
Ungleichheit zur empfangenen ID wird `packet loss` ausgegeben und
`rx_counter=received_id` gesetzt. Die Antwort erhält diesen Wert; nach
`handle_data`/Sendeversuch wird `rx_counter++` ausgeführt. Auch hier modulo 256.
Belege: [main.c:118, 797–805, 907–908, 1015–1023](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L797).

Beispiel: Anfragen 254, 255, 0 sind fortlaufend. Auf 10 folgt 12: Firmware
erwartet 11, meldet Abweichung, antwortet mit 12 und erwartet danach 13.
Es gibt weder Wiederholung der 11 noch ein Feld mit der Anzahl verlorener
Schritte. Die Logmeldung unterscheidet Verlust, Duplikat, Umordnung oder Neustart
nicht. Duplikate und alte Pakete werden nach Synchronisierung erneut angewendet,
sofern die Prüfsumme gültig und kein Fehlerzustand aktiv ist. Verlust von genau
256 Anfragen kann durch den 8-Bit-Zähler unbemerkt bleiben.

Der HAL-Treiber liest die Antwort-ID nicht zur Plausibilisierung. Ausbleibende
Antworten erkennt er nur indirekt über den Watchdog; einzelne Lücken, Duplikate
oder vertauschte Antworten werden nicht anhand der ID verworfen. Eine passende
Antwort-ID bedeutet außerdem nur, dass die Firmware die Anfrage bearbeitet hat:
Sie bestätigt **nicht** die abgeschlossene Schrittgenerierung, und bei vollem
Schrittring kann ein verworfener Burst trotzdem eine Antwort bekommen.
Belege: [stepgen-ninja.c:480–594](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L480),
[main.c:258–271, 892–908](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L258).

## 7. Firmware → HAL: Messwerte und I/O

### 7.1 Encoder und Index

Die Firmware erhebt pro bearbeiteter Anfrage jeden Encoder und setzt dessen
eigenen `time_us_32()`-Zeitstempel. Dieser läuft nach etwa 71,58 Minuten über.
Die Zeitstempel sind weder PC-Zeit noch synchronisierte Maschinenzeit; die
Kanäle werden nacheinander gelesen. Die Zähler sind auf dem Wire signed 32 Bit.

| Modus | `encoder_counter` | `encoder_velocity` |
|---|---|---|
| Standard: Substep-PIO | `substep_state[i].raw_step` | immer 0 |
| Legacy-Quadratur | `quadrature_encoder_get_count(...)` | Zählerdifferenz seit vorheriger Paketverarbeitung, keine Hz-/RPS-Zahl |
| `use_stepcounter=1` | `step_counter_get_count(...)` | immer 0 |

Quelle: [main.c:833–865](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L833).
Obwohl das Substep-Modul intern feinere `position`-/Geschwindigkeitswerte
berechnet, werden diese nicht übertragen; nur `raw_step`. Beleg:
[quadrature_encoder_substep.c:195–208](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/quadrature_encoder_substep/quadrature_encoder_substep.c#L195).
Ein Encoder zählt externe Pins, er ist keine automatische Rückmeldung eines
gleich nummerierten Stepgens. Im Standard zeigen sogar alle drei Encoderbasen
auf `PIN_14`; nur Encoder 0 hat einen Indexpin
([config.h:38–41](../third_party/stepper-ninja/firmware/inc/config.h#L38)).

Der HAL-Empfänger bildet:

```text
raw-count = encoder_counter
position  = float(encoder_counter) / encoder.scale
dt_us     = uint32(timestamp - previous_timestamp)
delta     = Differenz der Zähler, im normalen Zweig modulo 2^32 interpretiert
velocity_input = (delta / encoder.scale) * (1e6 / dt_us)
velocity-rps   = Tiefpass(velocity_input)
velocity-rpm   = velocity-rps * 60
```

Tatsächliche Abweichungen/Details: Bei gesetztem `index-enable` verwendet der
Treiber eine Wrapkorrektur um ±`encoder.scale/2`. Falls das empfangene
`encoder_velocity` ungleich null ist oder das lokale Delta null ist, wird im
Quadraturmodus dieses Feld als Delta verwendet. Das erklärt, warum das
Standardfeld 0 bei Bewegung nicht generell null Geschwindigkeit erzwingt.
Der Tiefpass hat fest `tau=0.008`, `dt=0.0001`, also `alpha=1/81`, unabhängig
von der tatsächlichen Servoperiode. Bei Zeitdelta >2.500.000 µs wird die
Geschwindigkeit null; bei dt=0 wird nur `delta_pos` null, die vorherige
Geschwindigkeit bleibt erhalten. Initiales Sample und Indexereignis setzen
die Differenzbasis zurück und überspringen die Geschwindigkeitsberechnung.
`velocity-rps` ist nur dann tatsächlich Umdrehungen/s, wenn die Skalierung
Counts/Umdrehung ist; sonst entsprechend Einheiten/s.
Belege: [stepgen-ninja.c:240–269, 291–295, 521–584](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L521).

`enc_control` Bit i wird aus dem HAL-Pin `.encoder.i.index-enable` gesetzt.
Core 1 aktiviert den konfigurierten GPIO-Flankeninterrupt. Beim Index setzt
`gpio_callback` den Encoderzähler zurück und setzt Bit i in `index_reset_flags`.
Dieses Flag wird in der nächsten Antwort nach `interrupt_data` übernommen und
danach gelöscht; das ist ein Ereignis, kein dauerhaftes Indexpegelbit.
Der HAL-Treiber rebasiert daraufhin seine Differenzrechnung und setzt
`index-enable` zurück. Beim allerersten Sample überspringt der Initialzweig
allerdings die nachfolgende Ereignisbehandlung. Die Firmware deaktiviert den
Interrupt im Callback; solange eine neue Anfrage weiterhin `enc_control=1`
enthält, kann Core 1 ihn wieder aktivieren. Ein verlorenes Antwortpaket verliert
auch das nicht wiederholte Ereignisflag.
Belege: [stepgen-ninja.c:616–620, 535–557](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L535),
[main.c:363–389, 862–865, 920–938](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L920).

`.debug-reset` speichert im HAL lediglich `enc_offset`; die nachfolgende
Positionsberechnung zieht diesen Wert nicht ab. Es ist kein Wire-Resetkommando.
Quelle: [stepgen-ninja.c:524–533](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L524).

### 7.2 Digitale Inputs und Outputs

Im Standard erfasst die Firmware `gpio_get_all64()` in `inputs[0]` und
`inputs[1]`. Es handelt sich um **GPIO-Nummern als Bitpositionen**, nicht um
fortlaufende Eingangsindizes. Die beim Start genullten Felder `inputs[2..3]`
werden im Standardpfad nicht mehr beschrieben und bleiben null. Die Zuweisungen
nach `input_buffer[2..3]` nullen nur den separaten Diagnosepuffer.
Beleg: [main.c:445–450, 868–888](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L868).

Der HAL-Treiber liest die konfigurierten GPIOs 22, 26, 27, 28 und exportiert
`.input.gp22`, `.gp26`, `.gp27`, `.gp28` sowie jeweils `-not`.
Quellen: [config.h:43–44](../third_party/stepper-ninja/firmware/inc/config.h#L43),
[internals.h:128–131](../third_party/stepper-ninja/firmware/inc/internals.h#L128),
[breakoutboard_hal_0.c:20–35, 55–64](../third_party/stepper-ninja/hal-driver/modules/breakoutboard_hal_0.c#L55).

Outputs sind dagegen in der Reihenfolge von `out_pins` gepackt: Bit i ist
Ausgang i. Standardmäßig steht nur `PIN_11=GPIO8` in dieser Liste; HAL-Pin
`.output.gp8` wird zu `outputs[0]` Bit 0. Die Firmware wählt das Ausgangswort
jedoch nach der **GPIO-Nummer** (`output_pins[i]<32`) statt nach dem Listenindex
i. Für die Standardkonfiguration passt das; bei gemischten GPIOs über/unter 32
kann Sender-/Empfängerzuordnung auseinanderfallen.
Belege: [breakoutboard_hal_0.c:40–49, 67–81](../third_party/stepper-ninja/hal-driver/modules/breakoutboard_hal_0.c#L67),
[main.c:879–886](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L879).

### 7.3 PWM und Analogvarianten

Bei `use_pwm=1` sendet der Treiber `pwm_frequency` in Hz und `pwm_duty` als
berechnetes PWM-Zählerlevel, nicht als Prozentwert. Bei aktiviertem Kanal und
Frequenz >0 wird die Frequenz auf 1907–1.000.000 Hz beschränkt; `output` wird
mindestens auf `min-limit` angehoben. Berechnung:
`wrap=uint16(pico_clock/frequency)`, bei Frequenz <1908 wird vorher 65535
eingesetzt; `duty=uint16(round(output/maxscale * wrap))`. Es gibt keine allgemeine
obere Duty-Sättigung auf `maxscale`. Bei deaktiviertem Kanal bleibt Duty im
genullten Paket 0; Frequency wird trotzdem übertragen. Firmware setzt Duty auf
16 Bit gekürzt sofort und aktualisiert Frequency auf Core 1.
Bei 200 MHz ist die fest kodierte 1908-Hz-Grenze kein verlässlicher Schutz gegen
16-Bit-Wrapüberlauf. Quellen:
[stepgen-ninja.c:227–237, 731–752](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L731),
[main.c:418–425, 818–825](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L818),
[pwm.c:20–45](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/modules/pwm.c#L20).

Andere Boardprofile belegen dieselben Arraytypen anders:

| Board | I/O-Interpretation und Analogwerte | Quellen |
|---|---|---|
| 1 | HAL liest 16 Inputs aus `inputs[2]` Bits 0–15, sendet 8 Outputs in `outputs[0]`. Firmware liest MCP-Port B in Low-Byte, Port A in High-Byte. HAL packt zwei DAC-Werte als 16-Bit-Hälften in `analog_out[0]`, lässt `[1]=0`; Firmware liest dagegen je `analog_out[0]&0xfff` und `[1]&0xfff`: echte Sender-/Empfängerinkonsistenz! `analog_enable` wird im Sender hier nicht ausgewertet. | [breakoutboard_hal_1.c:126–164](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/modules/breakoutboard_hal_1.c#L126), [breakoutboard_1.c:97–124](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/modules/breakoutboard_1.c#L97) |
| 2 | 96 Inputs in `[0..2]`, 32 Outputs in `[0]`. Toolchanger-BCD wird HAL-seitig aus verbundenen Pins abgeleitet, kein eigenes Paketfeld. | [breakoutboard_hal_2.c:127–177](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/modules/breakoutboard_hal_2.c#L127), [breakoutboard_2.c:116–121](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/modules/breakoutboard_2.c#L116) |
| 3 | Je Kanal ein 12-Bit-DAC-Wert in `analog_out[i]`, Enablebits in `outputs[0]`. Bipolare Skalierung: 2047 + 2047·input/max(abs(min),max) + int8-offset, auf 0–4095 begrenzt. Keine digitalen HAL-Inputs in diesem Boardmodul. | [breakoutboard_hal_3.c:19–36, 118–145](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/modules/breakoutboard_hal_3.c#L19), [breakoutboard_3.c:52–64](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/modules/breakoutboard_3.c#L52) |
| 100 | 32 Inputs in `[0]`, übrige Inputwörter 0; 16 Outputs in `[0]`. Trotz `ANALOG_CH=2` setzt der zugehörige HAL-Sendecode keine Analogwerte; Firmwaremodul bedient nur digitale Expander. | [breakoutboard_hal_100.c:59–80](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/modules/breakoutboard_hal_100.c#L59), [breakoutboard_100.c:76–95](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/modules/breakoutboard_100.c#L76) |

Diese Profile sind nicht durch die Paketlänge allein identifizierbar. Aus
generischen Ausgangsbits folgt ohne HAL-Verdrahtung keine Aussage wie
„Spindel an“, „Kühlmittel an“ oder „Werkzeugwechsel“.

### 7.4 Jitter

Das Wire-Feld ist `uint32_t` und enthält den Mikrosekundenabstand zwischen zwei
Aufrufen von `handle_data`, gemessen vor der Prüfsummenprüfung. Es ist weder
Roundtripzeit noch eine bereits um die Servozeit bereinigte Abweichung. Beim
ersten Paket bezieht es sich auf die Initialisierung von `last_packet_time`.
HAL schreibt in den signed Pin `.jitter` den Ausdruck `1000 - wire_jitter`:
bei 1000 µs also 0, bei 1100 µs auf üblichen Zweierkomplementsystemen −100.
Der Ausdruck rechnet zunächst unsigned; die spätere signed-Zuweisung ist für
große Werte eine Portabilitätsstelle. Die Konstante 1000 wird nicht an eine
andere Servoperiode angepasst.
Belege: [main.c:793–795, 999](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L793),
[stepgen-ninja.c:516, 890](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L516).

## 8. Servozeit, Ringpuffer und Ausführungszeit

Das Test-HAL registriert in Reihenfolge Motion-Command-Handler, Motion-Controller,
Watchdog, `process-send`, `process-recv` im Servo-Thread. Mit `SERVO_PERIOD=1000000`
ergibt das nominell **1000 Anfragen/s** und maximal **1000 Antwortabrufe/s**.
Der Treiber besitzt keinen unabhängigen Sendetimer. Bei anderer Threadperiode
gilt `f_send=1e9/period_ns`, sofern die Funktion einmal pro Periode aufgerufen
wird und der Watchdog läuft. Eine im selben Zyklus noch nicht eingetroffene
Antwort kann erst im nächsten Callback gelesen werden. Die Firmware antwortet
ereignisgesteuert je verarbeiteter fehlerfreier Anfrage; Verluste, Wartezeiten
und Fehler reduzieren die tatsächliche Rate.
Belege: [Test-HAL:2–9](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/test_config/stepper-ninja.hal#L2),
[Test-INI:84](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/test_config/stepper-ninja.ini#L84),
[stepgen-ninja.c:497, 755–758](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L497),
[main.c:1004–1023](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L1004).

Die Beispiel-HAL-Datei ist kein nachgewiesen lauffähiger Test für diesen Checkout:
Sie referenziert u.a. PWM-Pins ohne Kanalindex und einen `scaled-count`-Pin,
während aktueller Code andere Namen exportiert und Standard-PWM deaktiviert
ist. Ihre Verbindungen zeigen den beabsichtigten Datenfluss, ersetzen aber keine
Laufzeitvalidierung. Belege: [Test-HAL:35–47](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/test_config/stepper-ninja.hal#L35),
[stepgen-ninja.c:930–971](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L930).

### Standard: `use_timer_interrupt=0`

Die Firmware schreibt Schrittwörter sofort nach Empfang/Prüfung in die jeweilige
PIO-FIFO. Es gibt keinen Software-Schrittring; `step_ring_fill=0` und
`step_ring_status=0` werden trotzdem in jedem Antwortpaket mitgesendet.
Die PIO führt endliche Bursts asynchron aus. Empfangsjitter beeinflusst den
Beginn; eine volle PIO-FIFO kann `pio_sm_put_blocking` und damit die Antwort
verzögern. Die Richtungs-GPIOs werden schon vor dem FIFO-Schreiben gesetzt,
also nicht atomar mit dem tatsächlichen Start des neuen Bursts. Das kann bei
aufgestauten Bursts/Richtungswechseln bedeutsam sein.
Belege: [main.c:198–209, 809–815, 892–905](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L198).

### Optional: `use_timer_interrupt=1`

Der Ring besitzt **drei Plätze**, jeder enthält ausschließlich das komplette
`stepgen_command[]` eines Pakets. `pio_timing`, ID, PWM und digitale Ausgänge
werden nicht mitgepuffert. Der Timer startet, sobald drei Einträge vorliegen,
mit erstem Alarm `now + step_timer_period_us`; der Anfangswert ist 1000 µs.

Bei jedem Einreihversuch wird das Empfangsintervall gemessen. Wenn es strikt
größer als 50 µs und kleiner als `TIMEOUT_US` ist, gilt:

```text
step_timer_period_us = (3 * bisherige_Periode + Empfangsintervall + 2) / 4
```

Die Division ist ganzzahlig. Die Periode wird aus Ankunftsabständen geschätzt,
nicht aus einem übertragenen `SERVO_PERIOD`. Auch bei vollem Ring wird diese
Schätzung aktualisiert. Ist Platz vorhanden, wird eingereiht und das Overflowflag
gelöscht; bei vollem Ring wird der **neue** Eintrag verworfen und Overflow gesetzt.
Bei Timerstart wird Underflow gelöscht.
Quellen: [main.c:99–110, 217–278](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L99).

Jeder Alarm entnimmt einen Eintrag und setzt den nächsten Sollalarm auf den
vorherigen Sollalarm plus aktuelle geschätzte Periode. Ist der Ring zu Beginn
des Callbacks leer, wird der Timer abgeschaltet und Underflow gesetzt. Erst
erneutes Auffüllen auf drei Einträge startet ihn wieder. Nullwörter zählen als
Ring-Einträge und ergeben beim Anwenden keine neuen Schritte. Bei Timeout wird
der Ringzustand zurückgesetzt; `step_timer_period_us` selbst wird in der
Resetfunktion nicht wieder auf 1000 gesetzt.
Quelle: [main.c:217–225, 942–982](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L942).

| Status | Maske | Interpretation |
|---|---:|---|
| ACTIVE | 0x01 | `timer_started`, keine Garantie, dass gerade ein Puls läuft |
| UNDERFLOW | 0x02 | Timer traf leeren Ring; bleibt bis Neustart/Reset gesetzt |
| OVERFLOW | 0x04 | letztes Einreihen bei vollem Ring; nächstes erfolgreiches Einreihen löscht es |
| übrige Bits | 0xf8 | werden vom Original nicht gesetzt |

`step_ring_fill` zählt noch wartende Softwareeinträge (0–3), nicht PIO-FIFO-
Wörter oder bereits ausgeführte Schritte. Der Snapshot entsteht während der
Antwortbildung; Timerinterrupts können den Zustand zwischen einzelnen Lesezugriffen
ändern. Die HAL-Pins `.stepgen.ring-fill`, `ring-active`, `ring-underflow`,
`ring-overflow` bilden diese Werte direkt ab und lösen im Treiber keine
Ratenregelung aus.
Belege: [transmission.h:10–12](../third_party/stepper-ninja/firmware/modules/inc/transmission.h#L10),
[main.c:892–905](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L892),
[stepgen-ninja.c:517–520, 891–894](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L517).

### SPI-Abgrenzung

Bei `raspberry_pi_spi=1` sind die inneren Strukturen unverändert, die
Vollduplex-Transferlänge ist `max(sizeof(PC→Pico),sizeof(Pico→PC))`, standardmäßig
also 61 Byte, mit Padding außerhalb der kürzeren Struktur. Die innere Prüfsumme
umfasst weiterhin nur die Struktur ohne ihr letztes Byte. Die gleichzeitig
übertragene Antwort wurde vor Auswertung der aktuellen Anfrage aufgebaut und
ist daher typischerweise um einen Transfer versetzt. SPI ist kein weiterer
UDP-Port und im aktuellen Profil nicht aktiv.
Belege: [transmission.h:51–53](../third_party/stepper-ninja/firmware/modules/inc/transmission.h#L51),
[stepgen-ninja.c:455–463](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L455),
[main.c:1008–1016](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L1008).

## 9. Bytebeispiele zur unabhängigen Überprüfung

Alle folgenden Nutzdaten gelten für S=4/P=1/E=3/A=0, Little Endian.
Sie wurden mit dem originalen `calculate_checksum`, `rx_checksum_ok` und
`tx_checksum_ok` sowie `sizeof`/`offsetof` geprüft.

* Anfrage mit 36 Nullbytes hat Prüfsumme `0x90`: 36 × `jump_table[0]=4`
  modulo 256. Gesamtlänge 37.
* Antwort mit 60 Nullbytes hat Prüfsumme `0xf0`. Gesamtlänge 61.
* Anfrage ID 42, Kanal 0 zehn positive Schritte, alle anderen Schritt-/I/O-Werte
  null, 1 ms Servo, `pio_timing=235`, Standard ohne Ring:
  `D=200000/10−500−6=19494`, `W=0x81309809`, Prüfsumme `0x99`.

```text
Offset 00: 09 98 30 81 00 00 00 00 00 00 00 00 00 00 00 00
Offset 10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
Offset 20: eb 00 00 2a 99
```

Antwortbeispiel: Alle Zähler, Encoderzeitstempel, Inputs und Ringfelder null;
`jitter=1000`, `packet_id=42`, Prüfsumme `0x03`:

```text
Offset 00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
Offset 10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
Offset 20: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
Offset 30: 00 00 00 00 00 e8 03 00 00 00 00 2a 03
```

Das Antwortbeispiel prüft das Layout, es behauptet keinen realen
Encoder-Abtastzustand einer laufenden Firmware. Insbesondere sind Null-Zeitstempel
für laufende Encoder kein geeignetes dauerhaftes Messsignal.

## 10. Eignung für Werkzeugbahn- und Materialsimulation

**Bedingt ausreichend für eine aus Motorschritten rekonstruierte Bahn;
allein nicht ausreichend für eine vollständige, absolute Werkzeugbahn samt
Materialsimulation.** Das folgt direkt aus den übertragenen Feldern.

Aus jedem gültigen Nichtnullwort sind Richtung und ganzzahlige Schrittanzahl
verlustfrei dekodierbar. Bei vollständiger, geordneter Paketfolge lässt sich
pro Kanal ein relativer Schrittestand akkumulieren. Mit extern bekannter
Schritte-pro-Einheit-Skalierung ergibt sich im üblichen Positionsmodus
`q_i(k)=q_i(0)+sum(delta_steps_i)/scale_i`. Das ist die quantisierte
Motor-Sollbewegung, keine Messung tatsächlich ausgeführter mechanischer Bewegung.
Bei Geschwindigkeitsmodus und negativem Scale muss dessen oben beschriebene
Richtungsbesonderheit beachtet werden.

Die PIO-Wartewerte und `pio_timing` beschreiben die beabsichtigte Pulsverteilung
innerhalb eines Bursts. Sie liefern keinen gemeinsamen absoluten Zeitstempel
der Sollbahn. Ankunftszeit ist von Netzwerk-/Schedulingjitter beeinflusst;
Ring- und FIFO-Latenzen sowie asynchrone Richtungs-/Timingänderungen sind nicht
vollständig in der Antwort abgebildet. Auch ein Nullwort enthält keine
explizite Stillstandsdauer. Die konfigurierte Servoperiode ist erforderlich,
wenn die zeitliche Sollabtastung rekonstruiert werden soll.

Im Protokoll fehlen insbesondere:

* absolute Start-/Referenzposition und absolutes Stepgen-Positionsfeedback;
* `scale`, Linear-/Winkeleinheiten, HAL-Modus und Enable-Zustand als eigene Felder;
* Kanal→Joint-/Achsen-Zuordnung und Maschinenkinematik;
* Werkstückkoordinaten, Werkzeuglängen/-radien, Werkzeugnummer und Rohteilgeometrie;
* G-Code-Blöcke, Interpolationsprimitive, explizite Bahnzeitstempel und Feedwerte;
* standardisierte Bedeutung der Ausgangsbits/PWM als Spindel-, Kühlmittel- oder
  Werkzeugwechselzustand;
* Wiederherstellung verlorener Schritte oder explizite Bestätigung eines
  vollständig ausgeführten Bursts.

Ein verlorenes relatives Schrittpaket lässt einen unbekannten Positionsfehler
zurück; folgende Pakete enthalten keine absolute Position zum Aufholen.
Duplikate können Schritte verdoppeln. Encoder könnten je nach realer
Verdrahtung zusätzliche Messwerte liefern, sind aber weder automatisch
Stepgenfeedback noch eine kodierte TCP-Bahn. Bei nichtkartesischer Kinematik
sind Motor-/Jointkoordinaten außerdem nicht unmittelbar Werkzeugkoordinaten.
Belege für verfügbare und fehlende Felder:
[transmission.h:16–48](../third_party/stepper-ninja/firmware/modules/inc/transmission.h#L16),
[stepgen-ninja.c:647–716](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/hal-driver/stepgen-ninja.c#L647),
[main.c:198–209, 797–815, 833–865](https://github.com/atrex66/stepper-ninja/blob/eb7e5dfa2e76477e606a47038b07cca5e8a4b424/firmware/src/main.c#L797).

Damit liefert das Originalprotokoll eine brauchbare Grundlage, wenn
Maschinenkonfiguration, Startreferenz und Simulationsdaten bekannt sind und
Verlust/Umordnung erkannt und berücksichtigt werden. Eine exakte Rückgewinnung
der ursprünglichen unquantisierten `motor-pos-cmd`-Werte oder eine selbständige
Materialsimulation allein aus den UDP-Payloads ist nicht möglich. Diese
Bewertung erfordert keine Änderung des Wire-Protokolls und entwirft noch keine
alternative Architektur.

Die konkrete Auswahl der später unverändert zu übernehmenden Dateien, ihre
Include-Abhängigkeiten und Lizenzhinweise stehen in
[stepper-ninja-files.md](stepper-ninja-files.md).
