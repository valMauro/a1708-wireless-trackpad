# macOS: l'app companion

*[English](../en/macos.md)*

macOS non ha un driver per i Windows Precision Touchpad, quindi senza aiuto il trackpad funziona
solo come un mouse (puntatore e clic, niente gesti). L'app
[macos-trackpad-companion](https://github.com/mathias-sm/macos-trackpad-companion) legge i tocchi
grezzi e li trasforma in eventi nativi di macOS: puntatore, scorrimento fluido con inerzia, zoom,
rotazione, cambio scrivania e Mission Control che seguono le dita 1:1.

Questo progetto fornisce una **modifica** (patch) per la versione 0.9.3 (commit `1c1bc17`) in
[`macos-companion/`](../../macos-companion). Aggiunge:

- **Le curve di accelerazione di Apple.** Puntatore e scorrimento usano le curve parametriche del
  trackpad integrato di un MacBook (`HIDAccelCurves` / `HIDScrollAccelCurves`, algoritmo di
  IOHIDFamily). La curva la sceglie *Impostazioni di Sistema → Trackpad → Velocità di
  puntamento*, come per il trackpad integrato. L'accelerazione è applicata alla velocità totale,
  non asse per asse.
- **Nessun primo millimetro perso.** Con il tocco per fare clic disattivato, il puntatore parte
  subito; in origine il primo 1 mm / 150 ms di ogni movimento veniva scartato.
- **Il tocco per fare clic segue l'impostazione di macOS.** Se è attivo, i tocchi più brevi di
  40 ms sono ignorati (sfioramenti del bordo, rimbalzi).
- **Clic fisico con due dita = clic destro**, come sui trackpad Apple.
- **I movimenti sotto soglia si accumulano** invece di essere scartati, così i movimenti molto
  lenti e precisi funzionano.
- **Buffer anti-scatti adattivo per il Bluetooth**: al massimo 20 ms, in base al 90° percentile del
  ritardo di consegna.
- **Gesti del Dock di macOS 27**: cambio scrivania e Mission Control tramite un `IOHIDEvent`
  allegato, come richiede macOS 27.

## Compilare e installare

Servono gli strumenti da riga di comando di Xcode e Rust (`rustup`).

```
git clone https://github.com/mathias-sm/macos-trackpad-companion.git
cd macos-trackpad-companion
git checkout 1c1bc17
git apply /percorso/di/questo/repo/macos-companion/macos-trackpad-companion.patch
INSTALL=1 INSTALL_DIR=/Applications ./scripts/bundle.sh
open "/Applications/Trackpad Companion.app"
```

Oppure usa [`macos-companion/build.sh`](../../macos-companion/build.sh), che fa lo stesso e firma
con un certificato stabile (sotto).

Al primo avvio concedi **Monitoraggio input** e **Accessibilità**, poi chiudi e riapri l'app
(il Monitoraggio input vale solo dopo un riavvio dell'app).

### Mantenere i permessi tra una compilazione e l'altra

Un'app firmata ad-hoc sembra un'app nuova a macOS dopo ogni compilazione, quindi i permessi vanno
concessi ogni volta. Un certificato personale di firma del codice lo evita:

1. Apri **Accesso Portachiavi** → menu *Accesso Portachiavi → Assistente Certificato → Crea un
   certificato…*
2. Nome `Trackpad Companion Dev`, tipo di identità **Radice autofirmata**, tipo di certificato
   **Firma codice**. Crea.
3. Doppio clic sul certificato → **Attendibilità** → *Firma codice*: **Fidati sempre**.
4. Compila con `SIGN_IDENTITY="Trackpad Companion Dev"` (lo fa `build.sh`).

Se i permessi si bloccano ("Negato" anche con l'interruttore attivo), azzerali e concedili di
nuovo:

```
tccutil reset ListenEvent net.guemez.trackpad-companion
tccutil reset Accessibility net.guemez.trackpad-companion
```

## Regolazioni

- **Velocità del puntatore**: *Impostazioni di Sistema → Trackpad → Velocità di puntamento*
  sceglie la curva Apple. La *Cursor → Sensitivity* dell'app è un moltiplicatore in più:
  **25 = identico a un MacBook**.
- **Velocità di scorrimento**: la *Scroll → Sensitivity* dell'app moltiplica la curva di
  scorrimento Apple: **20 = come Apple**.
- **Tocco per fare clic**: segue *Impostazioni di Sistema → Trackpad*.

## Bluetooth fluido su un Mac

Su un MacBook Wi-Fi e Bluetooth condividono la stessa radio e antenna. Se il Wi-Fi del Mac è sui
**2,4 GHz**, il Bluetooth ha meno tempo a disposizione e i tocchi arrivano a scatti. **Collega il
Mac a una rete a 5 GHz.** Anche AWDL (AirDrop, AirPlay, Handoff) ruba tempo alla radio:
`sudo ifconfig awdl0 down` è una prova veloce, annullata dal riavvio o usando AirDrop. In BLE il
Mac concede un intervallo di connessione di 7,5 ms. Il cavo resta sempre la soluzione più fluida.

## Limiti noti

- "Scuoti il puntatore per individuarlo" non si attiva: il puntatore è mosso con eventi sintetici.
- Senza l'app (per esempio nella schermata di login) il trackpad funziona come un mouse.
