# Firmware

*[English](../en/firmware.md)*

Firmware in C per il Raspberry Pi Pico W (RP2040), fatto con pico-sdk, TinyUSB e BTstack.
Sorgenti in [`firmware/`](../../firmware), file pronti in
[`firmware/prebuilt/`](../../firmware/prebuilt):

| File | Scheda |
|---|---|
| `trackpad-a1708_pico_w.uf2` | Raspberry Pi Pico W / WH (RP2040), **provato** |
| `trackpad-a1708_pico2_w.uf2` | Raspberry Pi Pico 2 W (RP2350), si compila, non provato con questa versione |

## Caricare il firmware

**La prima volta** (o a scocca aperta): tieni premuto il tasto **BOOTSEL** del Pico, collega la
micro-USB del Pico al computer e trascina il file `.uf2` sull'unità **RPI-RP2**.

**Dopo, dalla USB-C della scheda** (serve la modifica 2, i fili dati USB): a scheda spenta, tieni
premuti i **tasti 1 e 3** e collega il cavo USB-C al computer. I tre LED si accendono un attimo e
compare l'unità RPI-RP2. Trascinaci il file.

Abbinamenti e impostazioni sono salvati in flash e restano dopo gli aggiornamenti.

## Uso

### Collegamenti

- **USB**: cavo a un computer → USB HID. Il cavo carica anche la batteria.
- **Bluetooth**: senza cavo, o con un caricatore da muro. Due tecnologie, scelte all'accensione:
  - **Bluetooth Low Energy** (consigliato): HID over GATT, con il servizio Batteria standard, così
    il livello compare nel widget batteria di macOS e in Windows.
  - **Bluetooth classico**: HID su BR/EDR, come il Magic Trackpad.

  **Per cambiare**: spegni, tieni premuto il **tasto 2**, accendi. Il LED 2 lampeggia **3 volte
  veloce** = ora BLE; resta **acceso 1 secondo** = ora classico. La scelta resta salvata. Ogni
  tecnologia ha i suoi abbinamenti.
- Col cavo collegato a un computer, una pressione breve su un tasto passa al Bluetooth (il cavo
  allora carica soltanto e porta la seriale di diagnostica). Scollega e ricollega per tornare alla
  USB.

### Tre computer

I tasti 1, 2 e 3 scelgono uno slot, come sui dispositivi Logitech Easy-Switch:

- **Pressione breve**: passa a quello slot. Il trackpad si ricollega a quel computer in 1–4 s.
- **Tenuto 3 s**: modalità abbinamento per quello slot (3 minuti). Poi cerca **"Trackpad A1708"**
  nelle impostazioni Bluetooth del computer.

In BLE ogni slot usa un suo indirizzo Bluetooth. Un computer si ricollega solo quando è scelto il
suo slot, e riabbinare uno slot gli dà un indirizzo nuovo (togli la vecchia voce da quel computer).

### LED

| Cosa vedi | Significato |
|---|---|
| 1, 2 o 3 LED accesi per 2 s all'accensione | livello batteria (> ⅔, ⅓–⅔, < ⅓) |
| Lampeggio lento (~1 al secondo) | sta cercando il computer di quello slot |
| Lampeggio veloce | modalità abbinamento |
| Acceso 4 s, poi spento | appena collegato |
| Acceso col cavo collegato | in carica; si spegne a carica completa |
| 2 lampi brevi ogni 5 s | batteria bassa (< 3,50 V) |
| 3 lampi brevi ogni 2 s | batteria quasi scarica (< 3,35 V) |
| Tutti e tre accesi un attimo, tasti 1+3 premuti all'accensione | modalità aggiornamento (unità RPI-RP2) |
| Spento mentre lo usi | normale: collegato e funzionante |

### Risparmio energetico

- Dopo 1 s senza dita il trackpad viene letto ogni 8 ms invece che ogni 1 ms, e il Pico dorme tra
  una lettura e l'altra.
- **Riposo** dopo 15 minuti senza dita né tasti. Al trackpad viene detto che il "coperchio è
  chiuso" e smette di leggere il tocco. In questo stato non sente le dita e non vibra, quindi
  **premi un tasto qualsiasi per svegliarlo** (quella pressione non cambia slot).

## Cosa fa il firmware

- **Legge il trackpad** via SPI (modo 0, 4 MHz, pacchetti da 256 byte, interrupt disabilitati
  durante il trasferimento) a 1 kHz durante l'uso. Il trackpad manda ~125 frame/s.
- **Controlla ogni messaggio** come il driver Linux `applespi`: CRC del pacchetto, CRC del
  messaggio, lunghezza del messaggio rispetto al numero di dita, ricomposizione dei messaggi su più
  pacchetti.
- **Gestione del clic**: una macchina a stati sul bit di clic grezzo. Una pressione vale subito, ma
  entro 120 ms da un rilascio deve durare ≥ 40 ms. Così scarta la breve pressione "fantasma" che il
  trackpad può dare ~60 ms dopo un rilascio. Un rilascio vuole 30 ms senza il bit.
- **Finestra della vibrazione**: per 120 ms dopo ogni cambio del bit di clic (quando il trackpad
  vibra) non si creano né si tolgono dita e le posizioni restano ferme. Durante la vibrazione il
  trackpad può segnalare dita fantasma e spostare un dito vero di qualche mm per un frame o due.
  Come su un Mac, il puntatore non si muove mentre clicchi.
- **Le dita nuove** devono comparire in 2 frame di fila.
- **Si presenta** come Windows Precision Touchpad (5 contatti, 134 × 81 mm, scan time, tasto) più
  una collezione mouse usata finché il computer non passa in modalità PTP. Per il BLE la report map
  è compattata a 511 byte: una Report Map BLE non può superare i 512 byte.
- **Batteria**: pin di stato del TP4056 e partitore su GP28; la percentuale è stimata dalla
  tensione.

## Compilare

Serve: [pico-sdk](https://github.com/raspberrypi/pico-sdk) 2.1.1 (con i suoi sottomoduli:
TinyUSB, BTstack, cyw43-driver), CMake, Ninja o Make, `arm-none-eabi-gcc` (provato con 14.2.1),
Python 3 (per compilare il database GATT).

```
export PICO_SDK_PATH=/percorso/di/pico-sdk
cd firmware
mkdir build && cd build
cmake -G Ninja -DPICO_BOARD=pico_w ..     # oppure -DPICO_BOARD=pico2_w
ninja
# -> trackpad_bt.uf2
```

| File | Contenuto |
|---|---|
| `main.c` | SPI, avvio del trackpad, elaborazione dei frame, filtri, tasti, LED, batteria, risparmio energetico, USB |
| `usb_descriptors.c`, `ptp.h` | descrittori USB e formato dei report PTP |
| `bt.c` | HID Bluetooth classico (BTstack), 3 slot |
| `ble.c`, `trackpad.gatt` | HID BLE over GATT, servizi Batteria e Device Information, 3 slot |
| `bt_scelta.c` | sceglie classico o BLE all'accensione |

## Diagnostica

Il firmware scrive un log su una porta seriale USB (115200 baud, ma va bene qualunque valore). Per
leggerlo mentre usi il Bluetooth: collega il cavo al computer, premi brevemente un tasto (il
trackpad passa al Bluetooth e il cavo resta per il log), poi:

```
cat /dev/cu.usbmodem*          # macOS
```

Ogni 5 s scrive frame, clic, fantasmi filtrati e dita fantasma, tensione della batteria e, in BLE,
l'intervallo di connessione.
