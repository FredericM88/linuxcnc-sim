# Stepper-Ninja: Originalbestand und Übernahmeliste

Analysebasis: lokaler Checkout `stepper-ninja/`, Commit
`eb7e5dfa2e76477e606a47038b07cca5e8a4b424`, geprüft am 26.09.2026.
Die ursprünglich hier empfohlene Übernahme ist umgesetzt: Die unten genannten
neun Dateien liegen bytegleich unter `third_party/stepper-ninja/`, einschließlich
Lizenz; `UPSTREAM.md` und `SHA256SUMS` halten Herkunft und Integrität fest.
`transmission.c` wird als C11 übersetzt und über `OriginalProtocol.hpp` an den
C++20-Simulator angebunden. Phase 1–3 sind abgeschlossen und real mit LinuxCNC
getestet.

Zusätzlich enthält `stepper-ninja/` die unveränderten Originalquellen als
Referenzbestand, für den Original-HAL-Build und den Test `original-hal-inputs`.
Die Unterscheidung zwischen minimalem Produktionsbestand und Originalreferenz
sowie die Git-Aufnahme ist in [repository.md](repository.md) dokumentiert.
Die folgenden Abschnitte bewahren die ursprüngliche Auswahlbegründung; „Nein“
bedeutet dort keine Abhängigkeit des Simulator-Produktionsbuilds, nicht das
Löschen einer bewusst aufbewahrten Originalreferenz.
Das tatsächliche Format und die Fehlerpfade beschreibt
[stepper-ninja-protocol.md](stepper-ninja-protocol.md).

## 1. Konkrete Empfehlung

Für einen späteren Linux-C++-Empfänger sollten zunächst diese Originaldateien
als unveränderter, auf den genannten Commit festgelegter Bestand übernommen
werden:

1. `firmware/modules/inc/transmission.h`
2. `firmware/modules/transmission.c`
3. `firmware/modules/inc/jump_table.h`
4. `firmware/inc/config.h`
5. `firmware/inc/internals.h`
6. `firmware/inc/footer.h`
7. `firmware/inc/kbmatrix.h`
8. `firmware/modules/inc/pio_settings.h` für die Interpretation des PIO-Timings
9. `LICENSE.txt` als vollständiger Lizenz- und Copyrightnachweis

Der eigentliche Wire-Kern sind die ersten drei Dateien. Die vier
Konfigurationsheader sind erforderlich, wenn `transmission.h` einschließlich
seiner ursprünglichen Include-Kette 1:1 erhalten bleiben soll. `pio_settings.h`
ist für Schrittanzahl/Richtung allein entbehrlich, für eine zum Original passende
Timinginterpretation aber sinnvoll. Die Firmware-Hardwareinitialisierung und
der LinuxCNC-HAL-Treiber sind keine notwendigen Receiver-Abhängigkeiten.

## 2. Zwingender Protokollkern und dessen Include-Abhängigkeiten

Alle Dateipfade in den Tabellen beziehen sich auf `stepper-ninja/`.

| Datei | Übernehmen? | Unverändert? | Grund und konkrete Quelle |
|---|---|---|---|
| `firmware/modules/inc/transmission.h` | Ja, zwingend | Ja | Beide gepackten Paketstrukturen, Statusbits, Prüffunktionsdeklarationen; [Zeilen 10–58](../stepper-ninja/firmware/modules/inc/transmission.h#L10). Kein eigenes Ersatzformat definieren. |
| `firmware/modules/transmission.c` | Ja, zwingend | Ja | Komplette Wire-Prüfsumme und beide Richtungsprüfungen, hardwareunabhängig; [Zeilen 1–22](../stepper-ninja/firmware/modules/transmission.c#L1). |
| `firmware/modules/inc/jump_table.h` | Ja, zwingend | Ja | Alle 256 Substitutionswerte sind Protokollkonstanten; [Zeilen 4–23](../stepper-ninja/firmware/modules/inc/jump_table.h#L4). |
| `firmware/inc/config.h` | Ja, für originale Include-Kette | Ja, als Snapshot des passenden Senderprofils | Bestimmt S/P/E/A, Transport und Verhaltensparameter; inkludiert internals/footer/kbmatrix; [Zeilen 3, 20–82](../stepper-ninja/firmware/inc/config.h#L20). |
| `firmware/inc/internals.h` | Ja, transitive Abhängigkeit | Ja | GPIO-/Boardkonstanten und Encoder-Moduskonstanten; [Zeilen 4–6, 14–145](../stepper-ninja/firmware/inc/internals.h#L4). Unter Linux wird `hardware/i2c.h` nicht inkludiert. |
| `firmware/inc/footer.h` | Ja, transitive Abhängigkeit | Ja | Boardabhängige Neudefinitionen insbesondere `ANALOG_CH`, Kanalzahlen sowie Takt/Flags; [Zeilen 12–217](../stepper-ninja/firmware/inc/footer.h#L12). Nicht durch angenommene Defaults ersetzen. |
| `firmware/inc/kbmatrix.h` | Ja, wenn `config.h` unverändert bleibt | Ja | Von config.h unbedingt inkludiert, Standard aktiviert `KBMATRIX`; Header definiert Tastenmatrixdaten, aber keine Wire-Felder; [Zeilen 1–30](../stepper-ninja/firmware/inc/kbmatrix.h#L1). Semantisch nicht zum Protokoll nötig, technisch Teil der unveränderten Include-Kette. |
| `LICENSE.txt` | Ja | Vollständig | MIT-Lizenz und Copyright (c) 2025 Zsolt Viola; Aufbewahrungsanforderung [Zeilen 3–13](../stepper-ninja/LICENSE.txt#L3). |

Die Include-Kette lautet:

```text
transmission.c
  ├── transmission.h
  │     ├── stdint.h, stdbool.h
  │     └── config.h
  │           ├── internals.h  → unter Linux kein Pico-Hardwareinclude
  │           ├── footer.h
  │           └── kbmatrix.h
  └── jump_table.h → stdint.h
```

Die C-Standardheader sind Systemabhängigkeiten und werden nicht aus Stepper-Ninja
kopiert. Diese Include-Kette benötigt unter Linux weder LinuxCNC-Header noch
Pico-SDK, WIZnet-Library, GPIO-, PWM- oder Encoderbibliothek.

## 3. Sinnvoll wiederverwendbar bzw. als Referenz aufzubewahren

| Datei/Abschnitt | Übernehmen? | Unverändert? | Grund |
|---|---|---|---|
| `firmware/modules/inc/pio_settings.h` | Ja, empfohlen | Ja | Originaltabelle mit 299 Index→Timing-Tripeln; [Zeilen 9–314](../stepper-ninja/firmware/modules/inc/pio_settings.h#L9). `pio_timing` im Paket verweist auf genau diese Reihenfolge. |
| `firmware/pio/freq_generator.pio` | Optional als Referenzdatei | Ja, falls archiviert | Autoritative Bedeutung der zehn Zählerbits und Wartezyklen; [Zeilen 18–32](../stepper-ninja/firmware/pio/freq_generator.pio#L18). Kein ausführbarer Host-Receiver-Code. |
| `hal-driver/pio_setting_generator.py` | Optional, Entwicklungsreferenz | Ja, falls archiviert | Reproduziert die PIO-Tabelle; [Zeilen 10–33](../stepper-ninja/hal-driver/pio_setting_generator.py#L10). Kein Laufzeitbedarf, Generator jetzt nicht ausführen, da er eine Headerdatei schreibt. |
| `firmware/src/main.c:198–214` | Semantik übernehmen, keine ganze Datei | Hardwareaufrufe nicht unverändert als Hostcode | Nullwortbehandlung, Richtungsbit und Maske 0x7fffffff. Referenz für den späteren Decoder; [Quelle](../stepper-ninja/firmware/src/main.c#L198). |
| `firmware/src/main.c:245–278, 942–983` | Nur falls Ringmodus nachgebildet werden soll | Algorithmus als Referenz, Hardwaretimer nicht 1:1 | Ringkapazität 3, Start-/Verlustverhalten und Zeitfilter; [Quelle](../stepper-ninja/firmware/src/main.c#L245). Aktuell standardmäßig aus. |
| `firmware/src/main.c:792–909, 988–1023` | Als Verhaltensreferenz | Nicht ungeprüft als Hostcode | Reihenfolge von ID-/Checksumprüfung, Antwortaufbau und Senden; [Quelle](../stepper-ninja/firmware/src/main.c#L792). Enthält Hardwarezugriffe und im Protokolldokument beschriebene Fehlerzustände. |
| `hal-driver/modules/breakoutboard_hal_0.c:55–81` | Zuordnungsreferenz | Kein separater Host-Laufzeitimport | Zeigt GPIO-Input-Bits und geordnete Output-Bits des aktuellen Profils; [Quelle](../stepper-ninja/hal-driver/modules/breakoutboard_hal_0.c#L55). |
| Boardpaare `hal-driver/modules/breakoutboard_hal_{1,2,3,100}.c` und `firmware/modules/breakoutboard_{1,2,3,100}.c` | Nur bei späterer Unterstützung des betreffenden Profils | Semantik prüfen, nicht pauschal portieren | Andere I/O-/Analogbelegung; u.a. Analog-Packing-Inkonsistenz zwischen [HAL-Board 1:152–164](../stepper-ninja/hal-driver/modules/breakoutboard_hal_1.c#L152) und [Firmware-Board 1:115–117](../stepper-ninja/firmware/modules/breakoutboard_1.c#L115). |

Das Wurzelheader `pio_settings.h` sollte nicht zusätzlich zur kanonischen
`firmware/modules/inc/pio_settings.h` importiert werden. Seine Tabellendaten
sind im untersuchten Commit identisch; ein doppelter Bestand erschwert jedoch
die eindeutige Herkunft. Die HAL-Symlinks sind ebenfalls keine weiteren
Originalimplementierungen: Die tatsächlichen Ziel-Dateien übernehmen,
keine zweite unabhängige Kopie desselben Headers. Beleg für die gemeinsamen
Ziele: [make_symlinks.sh:31–38](../stepper-ninja/hal-driver/make_symlinks.sh#L31).

## 4. Hardware-/Pico-Code ohne Bedarf im UDP-Softwareempfänger

| Datei/Gruppe | Übernehmen? | Unverändert? | Grund |
|---|---|---|---|
| `firmware/src/main.c` als Ganzes, `firmware/inc/main.h` | Nein | – | Pico-Cores, Timer, GPIO, DMA, PIO und WIZnet sind eng eingebunden; [main.h:7–44](../stepper-ninja/firmware/inc/main.h#L7). Nur die oben genannten Abschnitte dienen der Semantik. |
| `firmware/modules/pio_utils.c`, `inc/pio_utils.h` | Nein | – | Zuteilung echter PIO-Blöcke/State-Machines. |
| `firmware/modules/pwm.c`, `inc/pwm.h` | Nein | – | Hardware-PWM; [pwm.c:4–45](../stepper-ninja/firmware/modules/pwm.c#L4). Die Interpretation der Wire-Felder ist bereits dokumentiert. |
| `firmware/modules/breakoutboard*.c`, `inc/breakoutboard.h`, `mcp4725.c`, `inc/mcp4725.h` | Nein als Host-Laufzeitcode | – | I²C-Expander/DAC-Hardware; Boardbelegungen gegebenenfalls als Referenz lesen. |
| `firmware/modules/flash_config.c`, `inc/flash_config.h` | Nein | – | Flashspeicherung und eigene Flash-Prüfsumme sind kein UDP-Wire-Protokoll; [flash_config.c:19–46](../stepper-ninja/firmware/modules/flash_config.c#L19). |
| `firmware/modules/serial_terminal.c`, `inc/serial_terminal.h`, `flash_program.c`, `sh1106.c`, `inc/sh1106.h` | Nein | – | Gerätekonfiguration, Flash-/Displayfunktionen; aus `serial_terminal.c:42–52` sind nur wirksame Netzwerkparameter relevant. |
| `firmware/quadrature_encoder_substep/*`, `firmware/pio/quadrature_encoder*.pio`, `step_counter.pio` | Nein als Laufzeitcode | – | Zählen reale elektrische Signale über PIO. Für synthetische Wire-Zähler nicht erforderlich; zusätzliche eigene Lizenzhinweise beachten. |
| `firmware/ioLibrary_Driver.zip`, `firmware/ioLibraryDriver.sh`, daraus entpackte WIZnet-Bibliothek | Nein | – | Chip-Netzwerktreiber statt Host-UDP; kein Bestandteil der Protokollkern-Abhängigkeiten. CMake entpackt ihn automatisch: [Firmware-CMakeLists.txt:30–46](../stepper-ninja/firmware/CMakeLists.txt#L30). |
| `firmware/CMakeLists.txt`, `pico_sdk_import.cmake`, Pico-Buildskripte | Nein | – | Bauen Gerätefirmware und erzeugen PIO-Header; [CMakeLists.txt:62–113](../stepper-ninja/firmware/CMakeLists.txt#L62). Kein notwendiges Buildsystem für den zukünftigen Empfänger. |

## 5. LinuxCNC-/Sendercode

| Datei/Abschnitt | Übernehmen? | Unverändert? | Grund |
|---|---|---|---|
| `hal-driver/stepgen-ninja.c` / Alias `stepper-ninja.c` | Nein in den Receiver | Sender bleibt Original | LinuxCNC-RTAPI, HAL-Pins, Socket, Watchdog, Sollwert→Schrittwort-Berechnung. Maßgebliche Referenz: [Zeilen 596–758](../stepper-ninja/hal-driver/stepgen-ninja.c#L596). Empfänger verarbeitet bereits kodierte Befehle. |
| `stepgen-ninja.c:391–413, 630–716` | Nur Analyse-/Vergleichsreferenz | Nicht als zweiter Sender implementieren | Timingauswahl, Positions-/Geschwindigkeitsmodus, lokale Skalierung. Gehört nicht zur minimalen Dekodierung. |
| `hal-driver/hal_pin_macros.h`, `hal_util.*`, `modules/breakoutboard_hal_*.c` | Nein als Receiver-Abhängigkeit | – | Erzeugen/bedienen LinuxCNC-HAL-Pins; keine Drahtstrukturen. |
| `hal-driver/bcm2835.c`, `bcm2835.h`, `bcm2835rt.h` | Nein | – | Raspberry-Pi-SPI-Alternative; auf Standard-UDP-Pfad ausgeschlossen durch [stepgen-ninja.c:17–24](../stepper-ninja/hal-driver/stepgen-ninja.c#L17). Separater GPL-/kommerzieller Lizenzhinweis. |
| Andere HAL-Komponenten (`pid-ninja`, `plc-ninja`, `polygon-ninja`, Guards usw.) | Nein | – | Zusätzliche LinuxCNC-Funktionen, keine Abhängigkeiten von transmission.c/h. |
| `hal-driver/test_config/stepper-ninja.hal`, `.ini`, Minimal-HAL | Referenz, nicht Receivercode | Original archiviert lassen | Verdrahtung von `motor-pos-cmd` und 1-ms-Beispiel. Die größere HAL-Datei enthält veraltete Pinbezeichnungen; kein bestandener Integrationstest. |
| `hal-driver/CMakeLists.txt`, Install-/Symlinkskripte | Nein | – | Bauen/installieren RT-HAL-Komponenten bzw. verändern Symlinks. Für die Dokumentationsaufgabe nicht ausgeführt. |
| `utility/benchmark.c`, `utility/jump_table.py` | Nicht erforderlich | – | Hilfsprogramme; die normative Tabelle und Prüfsummenimplementierung liegen in den Firmware-Modulen. |

## 6. Einbindung in ein späteres C++-Projekt ohne Protokolländerung

`transmission.c` ist eigenständig kompilierbarer C-Code. Der HAL-Treiber bindet
ihn mit `#include "transmission.c"` ein
([stepgen-ninja.c:13–14](../stepper-ninja/hal-driver/stepgen-ninja.c#L13)); die
Firmware kompiliert ihn als eigene Quelldatei
([Firmware-CMakeLists.txt:71](../stepper-ninja/firmware/CMakeLists.txt#L71)).
Für eine spätere Integration ist eine separate C-Übersetzungseinheit möglich,
ohne die Originaldatei zu verändern. Eine C++-Anbindung muss C-Linkage herstellen,
weil der Originalheader keinen `extern "C"`-Block enthält. Nicht zugleich die
C-Datei textuell einbinden und separat linken, sonst entstehen doppelte Symbole.

Bei direktem C++-Include sind insbesondere `kbmatrix.h` mit `char*`-Stringliteralen
und die Nullarrays bei `pwm_count=0` Portabilitätsstellen. Auch `internals.h`
setzt außerhalb von Linux ein Pico-Hardwareinclude voraus. Diese Eigenschaften
sind Gründe für eine kleine spätere Integrationsgrenze außerhalb der unveränderten
Originaldateien; sie erfordern keine neue Wire-Struktur.

Das aktuelle `config.h` definiert Kanalzahlen und Flags direkt. Ein beliebiges
`-Dstepgens=...` ersetzt diese Werte daher nicht zuverlässig; `footer.h` kann sie
erneut überschreiben. Die zu übernehmende Konfiguration muss zur tatsächlich
gebauten Senderkonfiguration passen. Board 0 und Board 1 haben beispielsweise
beide 37 Byte lange Anfragen, aber unterschiedliche Felder/Offsets.

Für einen künftigen Build sind die hier verifizierten Werte 37/61 Byte,
Alignment 1, die Feld-Offsets und die Bytebeispiele im Protokolldokument die
passenden Kompatibilitätskriterien. Geprüft wurde nur der Protokollkern mit
Host-C-Compiler, keine Firmware-, LinuxCNC- oder C++-Gesamtintegration.

Die Funktionsdeklaration `pwm_calculate_wrap` in `transmission.h:55` zwingt
übrigens nicht zur Übernahme von `pwm.c`: `transmission.c` ruft sie nicht auf.
Auch die Bitmap-/GPIO-Makros der Konfigurationsheader erzeugen allein keine
Hardwareabhängigkeit des Linux-C-Protokollkerns.

## 7. Lizenzbefund der konkreten Dateien und Abhängigkeiten

Dies ist eine Bestandsaufnahme der lokalen Lizenztexte, keine Aussage über
untersuchte externe Versionen oder über das gesamte Repository unter einer
einzigen Lizenz.

| Bestand | Gefundener Hinweis | Konsequenz für die Dateiauswahl |
|---|---|---|
| `transmission.c`, `transmission.h`, `jump_table.h`, `pio_settings.h`, `config.h`, `internals.h`, `footer.h`, `kbmatrix.h` | Keine abweichenden eigenen Lizenz-/SPDX-Köpfe in diesen Dateien. Repository-Lizenz: MIT, Copyright (c) 2025 Zsolt Viola, [LICENSE.txt:1–21](../stepper-ninja/LICENSE.txt#L1). | Für den empfohlenen Bestand diese Repository-Lizenz vollständig mitführen; Herkunft/Commit erhalten. Keine WIZnet-, HAL- oder Pico-SDK-Implementierung wird dabei mit eingebunden. |
| `firmware/pio/freq_generator.pio` | Copyright (c) 2025 Viola Zsolt, MIT, [Zeilen 1–4](../stepper-ninja/firmware/pio/freq_generator.pio#L1). | Unveränderte Referenzdatei unter diesem Hinweis aufbewahren. |
| `firmware/src/main.c` | Autor Viola Zsolt, Lizenz MIT, [Zeilen 41–45](../stepper-ninja/firmware/src/main.c#L41). | Bei Übernahme wesentlicher Abschnitte MIT-Hinweise erhalten; Hardwareabhängigkeiten nicht automatisch mit übernehmen. |
| `hal-driver/stepgen-ninja.c` | `MODULE_AUTHOR("Viola Zsolt")`, `MODULE_LICENSE("MIT")`, [Zeilen 52–54](../stepper-ninja/hal-driver/stepgen-ninja.c#L52). | Senderreferenz; die Moduldeklaration lizenziert enthaltene Drittbibliotheken nicht um. |
| Quadratur-/Substep-Quellen | Raspberry Pi (Trading) Ltd., 2023, `SPDX-License-Identifier: BSD-3-Clause`, z.B. [quadrature_encoder_substep.c:1–5](../stepper-ninja/firmware/quadrature_encoder_substep/quadrature_encoder_substep.c#L1), [Header:1–4](../stepper-ninja/firmware/quadrature_encoder_substep/quadrature_encoder_substep.h#L1), [quadrature_encoder.pio:1–3](../stepper-ninja/firmware/pio/quadrature_encoder.pio#L1). | Nicht im empfohlenen Receiverbestand. Bei späterer Übernahme eigene BSD-Hinweise zusätzlich zur Projektlizenz berücksichtigen. |
| WIZnet-Code im vorhandenen ZIP | `ioLibrary_Driver/Ethernet/socket.c:25–53`: Copyright 2013 WIZnet, drei Bedingungen zu Quell-/Binärweitergabe und Namensnutzung samt Disclaimer; außerdem `ioLibrary_Driver/license.txt`. | ZIP nur lesend geprüft, nicht entpackt. Eigener BSD-3-Clause-artiger Lizenztext; nicht als MIT behandeln. Für den Host-Protokollkern unnötig. |
| `hal-driver/bcm2835.h` / `.c` | Mike McCauley; Header beschreibt GPL v3 oder kommerzielle Lizenz, [Zeilen 358–371](../stepper-ninja/hal-driver/bcm2835.h#L358), Copyright am Dateianfang. | Keinesfalls wegen der Repository-MIT-Lizenz als MIT mitimportieren. Für UDP-Receiver nicht benötigt. |
| Pico-SDK und LinuxCNC-Systemheader/-bibliotheken | Externe Build-Abhängigkeiten der ursprünglichen Firmware/HAL, nicht Bestandteil der minimalen Include-Kette unter Linux. | Keine pauschale Lizenzfreigabe aus dieser Untersuchung; im empfohlenen Protokollbestand nicht erforderlich. |

Die MIT-Datei fordert in [Zeilen 12–13](../stepper-ninja/LICENSE.txt#L12), dass
Copyright- und Erlaubnistext bei Kopien oder wesentlichen Softwareteilen
erhalten bleiben. Deshalb gehört `LICENSE.txt` ausdrücklich zur späteren
Übernahme. Die hier genannten Drittkomponenten sind gerade kein Grund,
den kleinen Protokollkern mit sämtlichen Geräte-/HAL-Abhängigkeiten zu kopieren.

## 8. Identität der empfohlenen Quelldateien

Diese SHA-256-Werte identifizieren die gelesenen Originaldateien. Es wurden
keine Kopien unter einem neuen Quellcodeverzeichnis angelegt.

| Datei | SHA-256 |
|---|---|
| `firmware/modules/inc/transmission.h` | `ce5d14302546b230af8d816e1e910bd68c44758757e9ebbfde178b10b61baa0f` |
| `firmware/modules/transmission.c` | `2dce5aad8976bbb9c2a839ad8bc4836563432fad6369b804a81dd81d91aefe57` |
| `firmware/modules/inc/jump_table.h` | `41de0312ff09eea2829606c86e6bab006598f52b6e5a7d9a24dbaee2aa19c8a1` |
| `firmware/modules/inc/pio_settings.h` | `389a2a0f597de681370ae709b92039d516b359e7a13197da2eed51283ba6ff6c` |
| `firmware/inc/config.h` | `16307b3c65a5540e27c00c59f685e7b4cefabdcac431a1eff5a3d0d735a80bb3` |
| `firmware/inc/internals.h` | `6627e5edb9b115aad1b67c61d56669c5a4e6645acbcca5e8fe9cde4693151496` |
| `firmware/inc/footer.h` | `59ccdf9416c3d2cdacc8eef145e9dcaadede004f2ecabd0bc7852718ba319d14` |
| `firmware/inc/kbmatrix.h` | `d552115db4eebd7b1f62d24cc5b580d189ae3a46cf700d3d8725f27eaa24018b` |
| `LICENSE.txt` | `3e7269addbad2d05de2b53254c7ac7096cc93bb2b132c921f53dc44ceddecd46` |
