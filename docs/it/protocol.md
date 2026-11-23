# Trackpad A1708: note sul protocollo SPI

*[English](../en/protocol.md)*

Quello che abbiamo imparato leggendo il driver Linux `applespi` e catturando un MacBook Pro vero
(MacBookPro14,1, trackpad `05ac:0276`) con un analizzatore logico. È tutto implementato in
[`firmware/main.c`](../../firmware/main.c).

## Parte elettrica

- SPI **modo 0**, MSB per primo. Le letture funzionano a 4 MHz; i comandi si mandano a 1 MHz.
- **Anticipo del CS**: aspetta ~35–50 µs tra CS basso e il primo fronte di clock, e ~50 µs dopo
  l'ultimo. Senza, i dati arrivano sfasati.
- Ogni trasferimento è di esattamente **256 byte**. Non fermare il clock a metà pacchetto; il
  firmware disabilita gli interrupt durante il trasferimento.
- `IF_EN` abilita l'interfaccia SPI. `LID` deve stare **alto** ("coperchio aperto"): con LID basso
  il trackpad smette di leggere il tocco, ignora le dita e non vibra (~11 mA risparmiati, usato come
  modalità riposo).
- La vibrazione vuole **12 V su PPVIN** ed è generata dal trackpad da solo alla pressione e al
  rilascio. L'alimentazione deve essere solida: i cali durante l'impulso rovinano i dati di tocco e
  di forza.

## Avvio

1. 3,3 V e 5 V presenti, `IF_EN` basso per 100 ms.
2. `LID` alto, `IF_EN` alto, aspetta 200 ms.
3. Leggi qualche pacchetto (il trackpad si presenta).
4. Comando `0x1020` al dispositivo `0xD0` (richiesta info). Risposta attesa `ac 27 68 d5`.
5. Comando `0x0252` con dati `02 01` al dispositivo `0x02`: **modalità multitouch attiva**. Risposta
   `ac 27 68 d5`.
6. Lettura continua: manda il pacchetto di lettura (`0x80`, zeri, CRC) e guarda cosa torna.

## Pacchetto (256 byte)

| Offset | Dimensione | Campo |
|---|---|---|
| 0 | 1 | flag: `0x20` = dati dal trackpad, `0x40` = scrittura, `0x80` = richiesta di lettura / niente |
| 1 | 1 | dispositivo: `0x01` tastiera, `0x02` touchpad, `0xD0` info |
| 2 | 2 | offset di questo pezzo nel messaggio (LE) |
| 4 | 2 | byte rimanenti dopo questo pezzo (LE) |
| 6 | 2 | lunghezza dei dati del messaggio in questo pacchetto (LE, ≤ 246) |
| 8 | 246 | messaggio (o un suo pezzo) |
| 254 | 2 | CRC-16 dei byte 0–253 (LE) |

Il CRC è **CRC-16/ARC** (polinomio `0xA001` riflesso, valore iniziale 0). Calcolato sui dati più il
loro CRC dà 0. I messaggi lunghi (tante dita) sono divisi su più pacchetti: si ricompongono in base
all'offset finché "rimanenti" vale 0.

## Messaggio

| Offset | Dimensione | Campo |
|---|---|---|
| 0 | 2 | tipo (`0x0210` per i dati del touchpad) |
| 2 | 1 | 0 |
| 3 | 1 | contatore |
| 4 | 2 | lunghezza della risposta |
| 6 | 2 | lunghezza dati = lunghezza del messaggio − 10 |
| 8 | n | dati |
| 8+n | 2 | CRC-16 del messaggio |

Un messaggio del touchpad è valido quando `lunghezza dati + 2 == 48 + 30 × numero_di_dita`.

### Dati del touchpad

| Offset | Campo |
|---|---|
| 1 | cliccato (clic fisico, deciso dai sensori di forza del trackpad) |
| 30 | numero di dita |
| 31 | cliccato (seconda copia) |
| 48 + 30·i | dito *i*, 30 byte |

Dito (little endian, 16 bit): `origin, abs_x, abs_y, rel_x, rel_y, tool_major, tool_minor,
orientation, touch_major, touch_minor, unused[2], pressure, multi, crc16`.

- Intervallo delle coordinate: **x −6243…6749, y −170…7685**; y cresce verso la cerniera (va
  invertita per le coordinate dello schermo). Dimensioni fisiche circa 134 × 81 mm.
- Un dito è presente quando **`touch_major > 0`**. Non fidarti della pressione: con 3–4 dita, le
  dita leggere segnano pressione 0.
- `touch_major` o `touch_minor` ≥ ~2500 è un palmo (nessuna confidenza).

## Stranezze

- Durante ogni impulso di vibrazione (pressione e rilascio) i dati sono inaffidabili fino a
  ~100 ms. Il bit di clic può tremolare, compaiono dita fantasma (spesso in fila alla stessa y) e
  le dita vere possono saltare di qualche mm per un frame o due. Il 99 % delle dita fantasma
  compare entro 100 ms da un cambio del bit di clic. Peggiora molto con un'alimentazione a 12 V
  debole.
- I frame arrivano a ~125 Hz durante il tocco. Senza tocco, le richieste di lettura non restituiscono
  niente (`0x80`).
