# AeroVox 2000

Plugin audio per voce (VST3 + Audio Unit) per **Windows** e **macOS**.
Funziona in FL Studio, Logic Pro, Ableton Live, Cubase, Studio One, Reaper, GarageBand e tutti i DAW VST3/AU.

![AeroVox 2000](docs/screenshot.png)

## Moduli

Il segnale passa in quest'ordine: **Vocoder → Resonance EQ → Fresh Air → Reverb → Output**.
Ogni modulo ha il suo tasto **ON/OFF** con LED.

| Modulo | Cosa fa | Controlli |
|---|---|---|
| **Vocoder** | Vocoder a 20 bande. La tua voce (ingresso) "modella" il suono del carrier. | **Carrier** (Synth MIDI / Noise / Sidechain), **Pitch**, **Release**, **Mix** |
| **Resonance EQ // AUTO** | Analizza lo spettro in tempo reale, trova da solo le risonanze fastidiose (frequenze che "fischiano" o rimbombano) e le abbassa solo quando servono. Il display mostra lo spettro (azzurro) e i tagli applicati (arancione). | **Depth** (quanto taglia), **Sensitivity** (quanto è facile che una frequenza venga considerata risonanza), **Speed** (velocità di reazione) |
| **Fresh Air** | Aria e brillantezza: high-shelf morbido + leggero exciter armonico sugli acuti. | **Amount**, **Freq** |
| **Reverb** | Riverbero stereo con pre-delay e taglio dei bassi sul segnale riverberato (resta pulito). | **Size**, **Damping**, **Pre-Delay**, **Mix** |
| **Output** | Volume finale + meter IN/OUT a LED. | **Gain** |

Comandi: trascina le manopole su/giù, **doppio clic = valore di default**. Tutti i parametri sono automatizzabili dal DAW.

### Usare il vocoder

- **Synth (MIDI)** – suona note MIDI sulla traccia del plugin: il vocoder canta le note (fino a 8 voci, accordi ok). Se non arriva nessuna nota usa la nota del knob **Pitch** (effetto "robot").
  - *FL Studio*: crea un canale MIDI Out, imposta la stessa porta MIDI nelle impostazioni del wrapper del plugin (icona ingranaggio > Settings > MIDI input port).
  - *Logic Pro*: nel plugin, menu in alto a destra **Side Chain** > scegli la traccia strumento/MIDI da cui prendere le note.
- **Noise** – voce sussurrata / robotica rumorosa, non serve MIDI.
- **Sidechain** – usa un'altra traccia (es. un synth o un pad) come carrier tramite l'ingresso sidechain del DAW.

## Scaricare le versioni Windows e Mac

Le versioni pronte vengono compilate automaticamente da **GitHub Actions** (macchine Windows e Mac vere):

1. Crea un repository su GitHub e carica il contenuto di questa cartella (anche con *Add file > Upload files* dal sito).
2. Vai su **Actions**: parte da solo il workflow *Build AeroVox 2000* (circa 10 minuti).
3. A fine build, nella pagina del workflow, sezione **Artifacts**, scarichi:
   - `AeroVox2000-Windows` → `AeroVox 2000.vst3` + versione standalone `.exe`
   - `AeroVox2000-macOS` → `AeroVox 2000.component` (AU, per Logic), `AeroVox 2000.vst3`, app standalone e installer `.pkg` (Universal: Apple Silicon + Intel)
4. Per una release con i file allegati: crea un tag tipo `v1.0.0` (*Releases > Draft a new release*).

Le istruzioni d'installazione sono nel file `LEGGIMI.txt` dentro ogni zip.

## Compilare in locale (opzionale)

Serve CMake 3.22+ e: **Windows** Visual Studio 2022 (C++), **Mac** Xcode.

```bash
cmake -B build                       # Mac: aggiungi -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release
```

I plugin finiscono in `build/AeroVox2000_artefacts/Release/`. JUCE viene scaricato in automatico.

Test automatici (Linux/Mac): `cmake -B build -DAEROVOX_BUILD_TESTS=ON`, poi
`AeroVoxTestHost "<percorso>/AeroVox 2000.vst3"` verifica trasparenza, riduzione delle risonanze, Fresh Air, riverbero, vocoder e salvataggio dello stato.

## Dettagli tecnici

- Formati: VST3 (Win/Mac), AU (Mac), Standalone. Windows 64-bit, macOS 10.13+ Universal.
- Latenza: 2048 campioni a 44.1/48 kHz (4096 a 88.2/96 kHz), compensata automaticamente dal DAW. È dovuta all'analisi FFT dell'EQ automatico.
- CPU: circa 3% di un core con tutti i moduli attivi.
- Mono e stereo, sidechain opzionale mono/stereo.

## Licenza

Basato su [JUCE 8](https://juce.com). JUCE è gratuito sotto licenza AGPLv3 (devi pubblicare il codice sorgente se distribuisci il plugin) oppure con una licenza JUCE (il piano *Personal/Starter* è gratuito sotto una certa soglia di fatturato). Se vuoi vendere il plugin a codice chiuso, controlla i termini su juce.com.
