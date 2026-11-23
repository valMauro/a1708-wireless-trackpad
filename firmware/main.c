// Trackpad Force Touch MacBook Pro A1708 (SPI) -> USB Precision Touchpad + mouse di riserva.
// Raspberry Pi Pico 2 W. Collegamenti: vedi COLLEGAMENTI in fondo al LEGGIMI.
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "pico/bootrom.h"
#include "tusb.h"
#include "ptp.h"
#include "pico/cyw43_arch.h"
#include "bt.h"

#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19
#define PIN_IFEN 20
#define PIN_LID  22
#define SPI_HZ_CMD  1000000   /* comandi: velocita' sicura (provata in MicroPython) */
static uint32_t spi_hz_lettura = 4000000;   /* letture: si abbassa da sola se ci sono errori */

/* ------------------------------------------------------------------ diagnostica */
static char logbuf[6000]; static int loglen;   /* i messaggi aspettano che il terminale si colleghi */
static void log_flush(void) {
  if (!loglen || !tud_cdc_connected()) return;
  uint32_t n = tud_cdc_write(logbuf, (uint32_t)loglen);
  memmove(logbuf, logbuf + n, (size_t)(loglen - (int)n)); loglen -= (int)n;
  tud_cdc_write_flush();
}
void logf_(const char *fmt, ...) {
  char b[200];
  va_list ap; va_start(ap, fmt); int n = vsnprintf(b, sizeof b - 2, fmt, ap); va_end(ap);
  if (n <= 0) return;
  if (n > (int)sizeof b - 3) n = (int)sizeof b - 3;
  b[n++] = '\r'; b[n++] = '\n';
  if (loglen + n > (int)sizeof logbuf) return;
  memcpy(logbuf + loglen, b, (size_t)n); loglen += n;
  log_flush();
}
static void hex_(const char *tit, const uint8_t *d, int n) {
  char b[160]; int k = 0;
  for (int i = 0; i < n && k < 150; i++) k += snprintf(b + k, sizeof b - (size_t)k, "%02x ", d[i]);
  logf_("%s %s", tit, b);
}

static void attendi_ms(uint32_t ms) {          /* attesa che continua a servire l'USB */
  absolute_time_t fine = make_timeout_time_ms(ms);
  while (!time_reached(fine)) { tud_task(); cyw43_arch_poll(); log_flush(); sleep_us(100); }
}

/* ------------------------------------------------------------------ CRC16 (0xA001) */
static uint16_t crc_tab[256];
static void crc_init(void) {
  for (int i = 0; i < 256; i++) {
    uint16_t c = (uint16_t)i;
    for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1;
    crc_tab[i] = c;
  }
}
static uint16_t crc16(const uint8_t *d, int n) {
  uint16_t c = 0;
  while (n--) c = (c >> 8) ^ crc_tab[(c ^ *d++) & 0xFF];
  return c;
}

/* ------------------------------------------------------------------ SPI trackpad */
static uint8_t pkt_lettura[256], rx[256];
static uint8_t contatore_cmd;
static uint32_t ritardo_stato_us = 100;
/* tempi misurati sul Mac con l'analizzatore: ~35 us tra CS giu' e il primo clock, ~40 us alla fine */
#define CS_ANTICIPO_US 50
#define CS_CODA_US     50

static void leggi_pacchetto(void) {
  /* interruzioni sospese durante il trasferimento: una pausa del clock a meta' pacchetto
     (per esempio per servire la radio) sembra rovinare i dati del trackpad */
  uint32_t irq = save_and_disable_interrupts();
  gpio_put(PIN_CS, 0); busy_wait_us(CS_ANTICIPO_US);
  spi_write_read_blocking(spi0, pkt_lettura, rx, 256);
  busy_wait_us(CS_CODA_US);
  gpio_put(PIN_CS, 1);
  restore_interrupts(irq);
}

static bool comando(uint8_t dev, uint16_t tipo, const uint8_t *carico, uint16_t lung, uint16_t rsp_len) {
  uint8_t p[256] = {0}, st[4];
  uint8_t *m = p + 8;
  m[0] = tipo & 0xFF; m[1] = tipo >> 8; m[2] = 0; m[3] = contatore_cmd++;
  uint16_t r = rsp_len ? rsp_len : lung;
  m[4] = r & 0xFF; m[5] = r >> 8; m[6] = lung & 0xFF; m[7] = lung >> 8;
  memcpy(m + 8, carico, lung);
  uint16_t c = crc16(m, 8 + lung); m[8 + lung] = c & 0xFF; m[9 + lung] = c >> 8;
  uint16_t ml = 10 + lung;
  p[0] = 0x40; p[1] = dev; p[6] = ml & 0xFF; p[7] = ml >> 8;
  c = crc16(p, 254); p[254] = c & 0xFF; p[255] = c >> 8;
  hex_("  invio:", p, 8 + ml);
  gpio_put(PIN_CS, 0); sleep_us(CS_ANTICIPO_US);
  spi_write_blocking(spi0, p, 256);
  sleep_us(ritardo_stato_us);
  spi_read_blocking(spi0, 0, st, 4);
  sleep_us(CS_CODA_US);
  gpio_put(PIN_CS, 1);
  for (int i = 0; i < 60; i++) {                                        /* svuota le risposte */
    attendi_ms(2); leggi_pacchetto();
    if (rx[0] != 0x80) hex_("  risposta:", rx, 24);
  }
  bool ok = st[0] == 0xAC && st[1] == 0x27 && st[2] == 0x68 && st[3] == 0xD5;
  logf_("comando %04x (attesa %lu us): stato %02x %02x %02x %02x %s", tipo, (unsigned long)ritardo_stato_us, st[0], st[1], st[2], st[3], ok ? "OK" : "ERRORE");
  return ok;
}

static bool avvia_trackpad(void) {
  spi_set_baudrate(spi0, SPI_HZ_CMD);
  gpio_put(PIN_IFEN, 0); attendi_ms(100);
  gpio_put(PIN_LID, 1);                 /* coperchio "aperto": senza, il trackpad ignora le dita */
  gpio_put(PIN_IFEN, 1); attendi_ms(200);
  for (int i = 0; i < 50; i++) {
    attendi_ms(2); leggi_pacchetto();
    if (i < 3 || rx[0] != 0x80) hex_(i < 3 ? "  lettura:" : "  spontaneo:", rx, 16);
  }
  bool a = comando(0xD0, 0x1020, NULL, 0, 0x0200);
  const uint8_t mt_on[2] = { 0x02, 0x01 };            /* 0x0102 little endian */
  bool b = comando(0x02, 0x0252, mt_on, 2, 0);
  spi_set_baudrate(spi0, spi_hz_lettura);
  return a && b;
}

/* ------------------------------------------------------------------ modalita' USB */
#define MODO_MOUSE 0
#define MODO_PTP   3
static volatile uint8_t modo = MODO_MOUSE;
static volatile bool heartbeat_attivo = false;
static volatile absolute_time_t scadenza_heartbeat;

/* ------------------------------------------------------------------ dita */
typedef struct { bool attivo; int16_t x, y; uint16_t press; bool conf; uint8_t assente; } slot_t;
#define FRAME_TOLLERANZA 2   /* un dito che manca per 1-2 frame non viene dato per sollevato */
static slot_t slot[PTP_MAX_CONTACTS];
static bool pulsante;
static absolute_time_t ultimo_frame;

typedef struct { int16_t x, y; uint16_t press; bool conf; } dito_t;

bool forza_bt;   /* GP15 a massa all'avvio: Bluetooth anche col cavo, USB solo per la diagnostica */
static bool bt_scelto;   /* col cavo inserito e' stato premuto un tasto: si usa il Bluetooth, il cavo solo ricarica */
static bool usb_hid(void) { return tud_mounted() && !forza_bt && !bt_scelto; }
void tud_mount_cb(void) { bt_scelto = false; }   /* cavo (ri)collegato a un computer: torna l'USB */

/* ------------------------------------------------------------------ batteria e ricarica (scheda v2)
   GP26 = CHRG del TP4056 (a massa mentre carica), GP27 = STDBY (a massa a carica finita),
   GP28 = batteria / 2 (partitore 100k + 100k).  Senza cavo entrambi restano alti (pull-up). */
#define PIN_CHRG  26
#define PIN_STDBY 27
#define ADC_BATT  2                     /* GP28 */
#define BATT_BASSA_MV   3500            /* ~10%: doppio lampeggio ogni 5 s */
#define BATT_CRITICA_MV 3350            /* ~3%: triplo lampeggio ogni 2 s */
static uint16_t batt_mv;                /* media mobile, 0 = non ancora misurata */
static uint8_t batt_livello;            /* 0 ok, 1 bassa, 2 critica */

static uint16_t batt_misura(void) {
  uint32_t s = 0;
  adc_select_input(ADC_BATT);
  for (int i = 0; i < 16; i++) s += adc_read();
  return (uint16_t)((s / 16) * 3300u * 2u / 4095u);
}
static void batt_init(void) {
  gpio_init(PIN_CHRG);  gpio_set_dir(PIN_CHRG, GPIO_IN);  gpio_pull_up(PIN_CHRG);
  gpio_init(PIN_STDBY); gpio_set_dir(PIN_STDBY, GPIO_IN); gpio_pull_up(PIN_STDBY);
  adc_init(); adc_gpio_init(28);
  /* Il partitore ha un condensatore da 100 nF (C10) che all'accensione era tirato giu' dalla
     resistenza di pull-down del piedino: serve qualche decina di ms perche' si ricarichi.
     Prima la prima misura veniva circa la meta' (1,6 V) e la percentuale saliva piano piano. */
  attendi_ms(60);
  batt_mv = batt_misura();
}
bool batt_in_carica(void)    { return !gpio_get(PIN_CHRG); }
bool batt_carica_finita(void) { return !gpio_get(PIN_STDBY); }
/* percentuale stimata dalla tensione (Li-ion, a riposo o con poco carico) */
int batt_percento(void) {
  static const uint16_t mv[]  = { 3300, 3400, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200 };
  static const uint8_t  pc[]  = {    0,    5,   10,   20,   35,   50,   65,   80,   92,  100 };
  if (batt_mv <= mv[0]) return 0;
  for (int i = 1; i < 10; i++)
    if (batt_mv <= mv[i]) return pc[i - 1] + (pc[i] - pc[i - 1]) * (batt_mv - mv[i - 1]) / (mv[i] - mv[i - 1]);
  return 100;
}
static void batt_tick(void) {           /* chiamata spesso: misura 1 volta al secondo */
  static uint32_t ultimo, log_ultimo; static uint8_t sotto, sopra;
  uint32_t ora = to_ms_since_boot(get_absolute_time());
  if (ora - ultimo < 1000) return;
  ultimo = ora;
  /* media lenta, ma nei primi 5 s e quando si attacca o stacca il cavo si segue subito la misura */
  static uint8_t veloce = 5; static bool carica_prima;
  bool carica_ora = batt_in_carica() || batt_carica_finita();
  if (carica_ora != carica_prima) { carica_prima = carica_ora; veloce = 5; }
  if (veloce) { veloce--; batt_mv = batt_misura(); }
  else batt_mv = (uint16_t)((batt_mv * 7u + batt_misura()) / 8u);
  if (batt_in_carica() || batt_carica_finita()) { batt_livello = 0; sotto = 0; }
  else {
    uint8_t nuovo = batt_mv < BATT_CRITICA_MV ? 2 : batt_mv < BATT_BASSA_MV ? 1 : 0;
    if (nuovo > batt_livello) { if (++sotto >= 10) { batt_livello = nuovo; sotto = 0; } }   /* 10 s di fila */
    else sotto = 0;
    if (batt_livello && batt_mv > BATT_BASSA_MV + 100) { if (++sopra >= 10) { batt_livello = 0; sopra = 0; } }
    else sopra = 0;
  }
  bt_batteria(batt_percento());          /* BLE: la vedono macOS e Windows */
  if (ora - log_ultimo >= 60000 || !log_ultimo) {
    log_ultimo = ora;
    logf_("batteria %u mV (%d%%)%s", batt_mv, batt_percento(),
          batt_in_carica() ? " in carica" : batt_carica_finita() ? " carica finita" : "");
  }
}

/* ------------------------------------------------------------------ 3 tasti + 3 LED dei posti
   tasto tra GPx e GND (pull-up interno); LED: GPx -> resistenza 1 kohm -> LED -> GND */
static const uint8_t PIN_TASTO[3] = { 10, 11, 12 };   /* piedini 14, 15, 16 */
static const uint8_t PIN_LEDP[3]  = { 6, 7, 8 };      /* piedini 9, 10, 11 */
#define PRESSIONE_LUNGA_MS 3000

static void tasti_init(void) {
  for (int i = 0; i < 3; i++) {
    gpio_init(PIN_TASTO[i]); gpio_set_dir(PIN_TASTO[i], GPIO_IN); gpio_pull_up(PIN_TASTO[i]);
    gpio_init(PIN_LEDP[i]);  gpio_set_dir(PIN_LEDP[i], GPIO_OUT); gpio_put(PIN_LEDP[i], 0);
  }
}

static void tasti(void) {
  static uint32_t ultimo, inizio[3]; static uint8_t stabile[3], conta[3]; static bool lunga_fatta[3];
  uint32_t ora = to_ms_since_boot(get_absolute_time());
  if (ora - ultimo < 5) return;
  ultimo = ora;
  for (int i = 0; i < 3; i++) {
    uint8_t giu = !gpio_get(PIN_TASTO[i]);
    if (giu != stabile[i]) { if (++conta[i] < 4) continue; }   /* antirimbalzo 20 ms */
    else { conta[i] = 0; }
    if (giu != stabile[i]) {
      stabile[i] = giu; conta[i] = 0;
      if (giu) { inizio[i] = ora; lunga_fatta[i] = false; }
      else if (!lunga_fatta[i]) {                             /* rilasciato: pressione breve */
        if (tud_mounted() && !forza_bt) bt_scelto = true;
        bt_scegli_posto(i);
      }
    }
    if (stabile[i] && !lunga_fatta[i] && ora - inizio[i] >= PRESSIONE_LUNGA_MS) {
      lunga_fatta[i] = true;
      if (tud_mounted() && !forza_bt) bt_scelto = true;
      bt_abbina_posto(i);
    }
  }
  /* LED dei posti: solo quello attuale, spento col cavo in USB */
  int st = usb_hid() ? 0 : bt_stato_led();
  bool acceso;
  if (st == 2 || st == 3) acceso = (st == 2 && (ora / 500) & 1) || (st == 3 && (ora / 150) & 1);   /* attesa / abbinamento */
  else if (batt_in_carica()) acceso = true;                                 /* in carica: LED del posto fisso */
  else if (batt_livello == 2) { uint32_t f = ora % 2000; acceso = f < 100 || (f > 250 && f < 350) || (f > 500 && f < 600); }
  else if (batt_livello == 1) { uint32_t f = ora % 5000; acceso = f < 120 || (f > 320 && f < 440); }
  else acceso = st == 1;
  for (int i = 0; i < 3; i++) gpio_put(PIN_LEDP[i], i == bt_posto() && acceso);
}

static ptp_report_t rep_ptp, rep_in_attesa;
static bool ptp_in_attesa;

static uint16_t clampu(int v, int max) { return (uint16_t)(v < 0 ? 0 : v > max ? max : v); }

static void invia_ptp(const ptp_report_t *r) {
  if (usb_hid()) {
    if (tud_hid_n_ready(1)) tud_hid_n_report(1, RID_TOUCH, r, sizeof *r);
    else { rep_in_attesa = *r; ptp_in_attesa = true; }
  } else if (bt_connected()) {
    bt_send(RID_TOUCH, r, sizeof *r);
  }
}

/* Costruisce e invia il report PTP; 'alzati' = slot da segnalare come sollevati */
static void report_ptp(const bool alzati[PTP_MAX_CONTACTS]) {
  memset(&rep_ptp, 0, sizeof rep_ptp);
  uint8_t n = 0;
  for (int i = 0; i < PTP_MAX_CONTACTS; i++) {
    if (!slot[i].attivo && !alzati[i]) continue;
    ptp_contact_t *c = &rep_ptp.c[n++];
    c->flags = (uint8_t)((slot[i].conf ? 1 : 0) | (slot[i].attivo ? 2 : 0));
    c->id = (uint8_t)i;
    c->x = clampu(slot[i].x - TP_XMIN, LOG_XMAX);
    c->y = clampu(TP_YMAX - slot[i].y, LOG_YMAX);
  }
  rep_ptp.count = n;
  rep_ptp.button = pulsante ? 1 : 0;
  rep_ptp.scan_time = (uint16_t)(time_us_32() / 100);
  invia_ptp(&rep_ptp);
}

/* ---------------------------- modalita' mouse (quando l'app Mac non e' attiva) */
#define VELOCITA     0.12f
#define SCORRIMENTO  0.012f
static int m_dita_prima;
static float m_cx, m_cy, m_picco, rx_, ry_, rv_, rh_;
static int m_frame;
static bool m_pulsante_prima;

static void mouse_frame(void) {
  int n = 0; float sx = 0, sy = 0, sp = 0;
  for (int i = 0; i < PTP_MAX_CONTACTS; i++)
    if (slot[i].attivo) { n++; sx += slot[i].x; sy += slot[i].y; sp += slot[i].press; }
  int8_t dx = 0, dy = 0, ruota = 0, pan = 0;
  if (n == 0) { m_dita_prima = 0; }
  else {
    float cx = sx / n, cy = sy / n, p = sp / n;
    if (n != m_dita_prima) { m_frame = 0; m_picco = p; rx_ = ry_ = rv_ = rh_ = 0; }
    else {
      m_frame++;
      m_picco = p > m_picco * 0.97f ? p : m_picco * 0.97f;
      float ddx = cx - m_cx, ddy = -(cy - m_cy);          /* y del trackpad cresce verso l'alto */
      bool fermo = m_frame <= 2 || (p < m_picco * 0.6f && !pulsante);
      if (!fermo) {
        if (n == 1 || pulsante) {
          rx_ += ddx * VELOCITA; ry_ += ddy * VELOCITA;
          int ix = (int)rx_, iy = (int)ry_; rx_ -= ix; ry_ -= iy;
          dx = (int8_t)(ix > 127 ? 127 : ix < -127 ? -127 : ix);
          dy = (int8_t)(iy > 127 ? 127 : iy < -127 ? -127 : iy);
        } else if (n == 2) {
          rv_ += ddy * SCORRIMENTO; rh_ += -ddx * SCORRIMENTO;
          int iv = (int)rv_, ih = (int)rh_; rv_ -= iv; rh_ -= ih;
          ruota = (int8_t)iv; pan = (int8_t)ih;
        }
      }
    }
    m_cx = cx; m_cy = cy; m_dita_prima = n;
  }
  if (dx || dy || ruota || pan || pulsante != m_pulsante_prima) {
    if (usb_hid()) {
      if (!tud_hid_n_ready(0)) return;
      tud_hid_n_mouse_report(0, 0, pulsante ? MOUSE_BUTTON_LEFT : 0, dx, dy, ruota, pan);
    } else if (bt_connected()) {
      uint8_t m[5] = { (uint8_t)(pulsante ? 1 : 0), (uint8_t)dx, (uint8_t)dy, (uint8_t)ruota, (uint8_t)pan };
      bt_send(RID_MOUSE_BT, m, sizeof m);
    }
    m_pulsante_prima = pulsante;
  }
}

/* ---------------------------- elaborazione di un frame del trackpad */
static uint32_t n_frame, n_crc_err, n_vuoti;

/* Controlli sul messaggio come il driver Linux applespi (verificati sui pacchetti catturati dal Mac:
   tipo 0x0210, lunghezza campo = pacchetto - 10, campo + 2 = 48 + 30 * dita, CRC del messaggio = 0).
   I messaggi lunghi (molte dita o palmo) arrivano su piu' pacchetti: si ricompongono. */
static uint32_t n_rif_crc, n_rif_lung, n_rif_multi, n_multi, n_tipo_altro;
static bool controlli = true;          /* si spengono da soli se nessun messaggio li supera */
static uint8_t msg_buf[1024]; static int msg_salvato;

static bool messaggio_valido(const uint8_t *m, int n) {
  if (n < 10 + 48) { n_rif_lung++; return false; }
  if (crc16(m, n) != 0) { n_rif_crc++; return false; }
  int campo = m[6] | m[7] << 8, nf = m[8 + 30];
  if (campo != n - 10 || campo + 2 != 48 + 30 * nf) { n_rif_lung++; return false; }
  if ((m[0] | m[1] << 8) != 0x0210) n_tipo_altro++;
  return true;
}

static uint32_t n_click, n_fantasmi, n_pause, n_finestre, n_dita_scartate;   /* diagnostica (ogni 5 s nel log) */

/* Ogni volta che il trackpad vibra (quando lui stesso vede premere o rilasciare) per ~100 ms manda dati
   sballati: dita finte, spesso in fila, e il bit del clic che si accende e spegne. Visto nel registro
   dell'app del 17/11: il 99% delle dita finte nasce entro 100 ms da un cambio del bit del clic.
   Dopo ogni cambio del bit si apre una "finestra" di FINESTRA_US in cui:
   - non nascono dita nuove e non se ne alzano;
   - un dito esistente si aggancia solo vicino a dove era (le dita finte sono altrove). */
#define FINESTRA_US     120000
#define RAGGIO_BASE     500         /* ~5 mm */
#define RAGGIO_PASSO    250         /* + ~2,5 mm per ogni frame in cui il dito e' mancato */
#define RAGGIO_MAX      2500        /* ~25 mm: abbinamento normale fuori dalla finestra */
#define CONFERMA_FRAME  2           /* un dito nuovo deve esserci 2 frame di fila ... */
#define CONFERMA_RAGGIO 1200        /* ... nello stesso punto (~12 mm) */
static absolute_time_t fine_finestra;
static bool in_finestra(void) { return !time_reached(fine_finestra); }

/* Clic: macchina a stati sul bit grezzo.
   - premuto: vale subito, ma se arriva entro BLOCCO_US da un rilascio deve durare CONFERMA_US
     (la "seconda pressione" fantasma dopo il rilascio dura ~20 ms e arriva ~60 ms dopo);
   - rilasciato: solo dopo RILASCIO_US di fila senza clic (il bit sfarfalla durante la vibrazione). */
#define BLOCCO_US    120000
#define CONFERMA_US   40000
#define RILASCIO_US   30000
enum { P_SU, P_ATTESA, P_GIU };
static uint8_t stato_p;
static bool grezzo_prec;
static absolute_time_t t_cambio, t_rilascio;

static void aggiorna_pulsante(bool grezzo) {
  absolute_time_t ora = get_absolute_time();
  if (grezzo != grezzo_prec) {
    grezzo_prec = grezzo; t_cambio = ora;
    if (!in_finestra()) n_finestre++;
    fine_finestra = delayed_by_us(ora, FINESTRA_US);
  }
  int64_t fermo = absolute_time_diff_us(t_cambio, ora);       /* da quanto il bit e' in questo stato */
  switch (stato_p) {
    case P_SU:
      if (grezzo) {
        if (absolute_time_diff_us(t_rilascio, ora) >= BLOCCO_US) { stato_p = P_GIU; pulsante = true; n_click++; }
        else stato_p = P_ATTESA;
      }
      break;
    case P_ATTESA:
      if (!grezzo) { stato_p = P_SU; n_fantasmi++; }
      else if (fermo >= CONFERMA_US) { stato_p = P_GIU; pulsante = true; n_click++; }
      break;
    default:
      if (!grezzo && fermo >= RILASCIO_US) { stato_p = P_SU; pulsante = false; t_rilascio = ora; }
      break;
  }
}

typedef struct { int16_t x, y; uint8_t visto; } candidato_t;
static candidato_t cand[10]; static int ncand;

static void elabora_frame(const uint8_t *d, int dlen) {
  aggiorna_pulsante(d[1] != 0);
  bool fin = in_finestra();
  int nf = d[30];
  dito_t cur[10]; int nc = 0;
  for (int f = 0; f < nf && nc < 10; f++) {
    int o = 48 + 30 * f;
    if (o + 26 > dlen) break;
    uint16_t press = (uint16_t)(d[o + 24] | d[o + 25] << 8);
    int16_t tmaj = (int16_t)(d[o + 16] | d[o + 17] << 8), tmin = (int16_t)(d[o + 18] | d[o + 19] << 8);
    /* Come il driver Linux (applespi): un dito c'e' se ha area di contatto (touch_major > 0).
       Prima si usava la pressione > 0, ma la pressione viene dai sensori di forza e con 3-4 dita
       quelle appoggiate leggere leggono 0: sparivano e ricomparivano, e le gesture saltavano. */
    if (tmaj <= 0) continue;
    cur[nc].x = (int16_t)(d[o + 2] | d[o + 3] << 8);
    cur[nc].y = (int16_t)(d[o + 4] | d[o + 5] << 8);
    cur[nc].conf = tmaj < 2500 && tmin < 2500;                 /* palmo = non affidabile */
    cur[nc].press = press;
    nc++;
  }
  /* abbinamento al piu' vicino tra dita attuali e slot attivi */
  bool usato_c[10] = {0}, usato_s[PTP_MAX_CONTACTS] = {0};
  for (;;) {
    int64_t best = INT64_MAX; int bc = -1, bs = -1;
    for (int c = 0; c < nc; c++) if (!usato_c[c])
      for (int s = 0; s < PTP_MAX_CONTACTS; s++) if (slot[s].attivo && !usato_s[s]) {
        int64_t r = RAGGIO_MAX;
        if (fin) { r = RAGGIO_BASE + (int64_t)RAGGIO_PASSO * slot[s].assente; if (r > RAGGIO_MAX) r = RAGGIO_MAX; }
        int64_t ddx = cur[c].x - slot[s].x, ddy = cur[c].y - slot[s].y, d2 = ddx * ddx + ddy * ddy;
        if (d2 < r * r && d2 < best) { best = d2; bc = c; bs = s; }
      }
    if (bc < 0) break;
    usato_c[bc] = usato_s[bs] = true;
    /* In finestra (subito dopo che il trackpad ha vibrato per un clic) la posizione resta ferma:
       in quei 100 ms il trackpad a volte sposta il dito di qualche mm per un paio di frame e il
       puntatore saltava proprio mentre si cliccava. Come sul Mac, durante il clic non si muove. */
    if (!fin) { slot[bs].x = cur[bc].x; slot[bs].y = cur[bc].y; }
    slot[bs].press = cur[bc].press; slot[bs].conf = cur[bc].conf;
    slot[bs].assente = 0;
  }
  bool alzati[PTP_MAX_CONTACTS] = {0};
  for (int s = 0; s < PTP_MAX_CONTACTS; s++)
    if (slot[s].attivo && !usato_s[s]) {
      /* in finestra nessun dito si alza: resta fermo dov'era finche' i dati tornano buoni */
      if (fin) { if (slot[s].assente < 250) slot[s].assente++; continue; }
      /* dito sparito: se ce ne sono ancora altri sul trackpad aspetto un paio di frame
         (resta fermo nell'ultima posizione); se non c'e' piu' nessun dito e' un sollevamento vero */
      if (nc > 0 && ++slot[s].assente <= FRAME_TOLLERANZA) continue;
      slot[s].attivo = false; alzati[s] = true;
    }
  /* dita nuove: mai in finestra; fuori, solo se presenti 2 frame di fila nello stesso punto */
  candidato_t nuovi[10]; int nn = 0;
  for (int c = 0; c < nc; c++) if (!usato_c[c]) {
    if (fin) { n_dita_scartate++; continue; }
    uint8_t visto = 1;
    for (int k = 0; k < ncand; k++) {
      int32_t ddx = cur[c].x - cand[k].x, ddy = cur[c].y - cand[k].y;
      if (cand[k].visto && ddx * ddx + ddy * ddy < CONFERMA_RAGGIO * CONFERMA_RAGGIO) {
        visto = (uint8_t)(cand[k].visto + 1); cand[k].visto = 0; break;
      }
    }
    if (visto < CONFERMA_FRAME) { nuovi[nn++] = (candidato_t){ cur[c].x, cur[c].y, visto }; continue; }
    for (int s = 0; s < PTP_MAX_CONTACTS; s++) if (!slot[s].attivo && !alzati[s]) {
      slot[s] = (slot_t){ true, cur[c].x, cur[c].y, cur[c].press, cur[c].conf, 0 }; break;
    }
  }
  memcpy(cand, nuovi, sizeof(candidato_t) * (size_t)nn); ncand = nn;
  if (modo == MODO_PTP) report_ptp(alzati); else mouse_frame();
}

static void tutte_alzate(void) {
  bool alzati[PTP_MAX_CONTACTS] = {0}, qualcuno = false;
  for (int s = 0; s < PTP_MAX_CONTACTS; s++)
    if (slot[s].attivo) { slot[s].attivo = false; alzati[s] = true; qualcuno = true; }
  bool era_premuto = pulsante;
  pulsante = false; ncand = 0;
  if (era_premuto) { stato_p = P_SU; t_rilascio = get_absolute_time(); }
  if (!qualcuno && !era_premuto) return;
  if (modo == MODO_PTP) report_ptp(alzati); else mouse_frame();
}

/* ------------------------------------------------------------------ main */
/* descrittore Bluetooth: mouse (report 7) + Precision Touchpad nello stesso descrittore */
static const uint8_t desc_mouse_bt[] = {
  0x05,0x01, 0x09,0x02, 0xA1,0x01, 0x85,RID_MOUSE_BT, 0x09,0x01, 0xA1,0x00,
  0x05,0x09, 0x19,0x01, 0x29,0x03, 0x15,0x00, 0x25,0x01, 0x95,0x03, 0x75,0x01, 0x81,0x02,
  0x95,0x01, 0x75,0x05, 0x81,0x03,
  0x05,0x01, 0x09,0x30, 0x09,0x31, 0x09,0x38, 0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x03, 0x81,0x06,
  0x05,0x0C, 0x0A,0x38,0x02, 0x95,0x01, 0x81,0x06,
  0xC0, 0xC0
};
static uint8_t desc_bt[700];

/* Descrittore per il BLE: la "Report Map" del Bluetooth Low Energy non puo' superare 512 byte.
   Stessi report e stessi campi del descrittore classico (verificato), ma compattato: le dita 2-5
   non ripetono le impostazioni globali gia' valide e il mouse non ha la sottocollezione "pointer". */
static const uint8_t desc_ble[] = {
  0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x07, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x25, 0x01,
  0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x03, 0x05, 0x01, 0x09, 0x30,
  0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06, 0x05, 0x0C,
  0x0A, 0x38, 0x02, 0x95, 0x01, 0x81, 0x06, 0xC0, 0x05, 0x0D, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01,
  0x09, 0x22, 0xA1, 0x02, 0x09, 0x47, 0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02,
  0x81, 0x02, 0x95, 0x06, 0x81, 0x03, 0x25, 0x07, 0x75, 0x08, 0x09, 0x51, 0x95, 0x01, 0x81, 0x02,
  0x05, 0x01, 0x26, 0xC0, 0x32, 0x75, 0x10, 0x55, 0x0E, 0x65, 0x11, 0x09, 0x30, 0x35, 0x00, 0x46,
  0x3C, 0x05, 0x95, 0x01, 0x81, 0x02, 0x46, 0x2A, 0x03, 0x26, 0xAF, 0x1E, 0x09, 0x31, 0x81, 0x02,
  0xC0, 0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02, 0x09, 0x47, 0x09, 0x42, 0x25, 0x01, 0x75, 0x01, 0x95,
  0x02, 0x81, 0x02, 0x95, 0x06, 0x81, 0x03, 0x25, 0x07, 0x75, 0x08, 0x09, 0x51, 0x95, 0x01, 0x81,
  0x02, 0x05, 0x01, 0x26, 0xC0, 0x32, 0x75, 0x10, 0x09, 0x30, 0x46, 0x3C, 0x05, 0x81, 0x02, 0x46,
  0x2A, 0x03, 0x26, 0xAF, 0x1E, 0x09, 0x31, 0x81, 0x02, 0xC0, 0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02,
  0x09, 0x47, 0x09, 0x42, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0x95, 0x06, 0x81, 0x03,
  0x25, 0x07, 0x75, 0x08, 0x09, 0x51, 0x95, 0x01, 0x81, 0x02, 0x05, 0x01, 0x26, 0xC0, 0x32, 0x75,
  0x10, 0x09, 0x30, 0x46, 0x3C, 0x05, 0x81, 0x02, 0x46, 0x2A, 0x03, 0x26, 0xAF, 0x1E, 0x09, 0x31,
  0x81, 0x02, 0xC0, 0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02, 0x09, 0x47, 0x09, 0x42, 0x25, 0x01, 0x75,
  0x01, 0x95, 0x02, 0x81, 0x02, 0x95, 0x06, 0x81, 0x03, 0x25, 0x07, 0x75, 0x08, 0x09, 0x51, 0x95,
  0x01, 0x81, 0x02, 0x05, 0x01, 0x26, 0xC0, 0x32, 0x75, 0x10, 0x09, 0x30, 0x46, 0x3C, 0x05, 0x81,
  0x02, 0x46, 0x2A, 0x03, 0x26, 0xAF, 0x1E, 0x09, 0x31, 0x81, 0x02, 0xC0, 0x05, 0x0D, 0x09, 0x22,
  0xA1, 0x02, 0x09, 0x47, 0x09, 0x42, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0x95, 0x06,
  0x81, 0x03, 0x25, 0x07, 0x75, 0x08, 0x09, 0x51, 0x95, 0x01, 0x81, 0x02, 0x05, 0x01, 0x26, 0xC0,
  0x32, 0x75, 0x10, 0x09, 0x30, 0x46, 0x3C, 0x05, 0x81, 0x02, 0x46, 0x2A, 0x03, 0x26, 0xAF, 0x1E,
  0x09, 0x31, 0x81, 0x02, 0xC0, 0x05, 0x0D, 0x55, 0x0C, 0x66, 0x01, 0x10, 0x47, 0xFF, 0xFF, 0x00,
  0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00, 0x75, 0x10, 0x95, 0x01, 0x09, 0x56, 0x81, 0x02, 0x65, 0x00,
  0x55, 0x00, 0x35, 0x00, 0x45, 0x00, 0x09, 0x54, 0x15, 0x00, 0x25, 0x7F, 0x95, 0x01, 0x75, 0x08,
  0x81, 0x02, 0x05, 0x09, 0x09, 0x01, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02, 0x95, 0x07,
  0x81, 0x03, 0x05, 0x0D, 0x85, 0x02, 0x09, 0x55, 0x09, 0x59, 0x75, 0x04, 0x95, 0x02, 0x25, 0x0F,
  0xB1, 0x02, 0x06, 0x00, 0xFF, 0x85, 0x06, 0x09, 0xC5, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08,
  0x96, 0x00, 0x01, 0xB1, 0x02, 0xC0, 0x05, 0x0D, 0x09, 0x0E, 0xA1, 0x01, 0x65, 0x00, 0x55, 0x00,
  0x35, 0x00, 0x45, 0x00, 0x85, 0x03, 0x09, 0x22, 0xA1, 0x02, 0x09, 0x52, 0x15, 0x00, 0x25, 0x0A,
  0x75, 0x08, 0x95, 0x01, 0xB1, 0x02, 0xC0, 0x09, 0x22, 0xA1, 0x00, 0x85, 0x04, 0x09, 0x57, 0x09,
  0x58, 0x75, 0x01, 0x95, 0x02, 0x25, 0x01, 0xB1, 0x02, 0x95, 0x06, 0xB1, 0x03, 0xC0, 0xC0,
};

static void led(bool on) { static int s = -1; if (s != on) { cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on); s = on; } }
static bool tasto_giu(void) { for (int i = 0; i < 3; i++) if (!gpio_get(PIN_TASTO[i])) return true; return false; }

int main(void) {
  /* bt19 provava 48 MHz: con il Pico rallentato il Bluetooth non si collegava piu' (il chip radio
     CYW43 parla col Pico con un programma PIO tarato sulla frequenza normale). Si resta a 125 MHz. */
  crc_init();
  tasti_init();
  gpio_init(15); gpio_set_dir(15, GPIO_IN); gpio_pull_up(15); sleep_ms(2);
  forza_bt = !gpio_get(15);
  /* tasti 1 e 3 tenuti premuti all'accensione: modalita' aggiornamento (disco RPI-RP2 sull'USB),
     cosi' il firmware si aggiorna dalla USB-C anche a scocca chiusa, senza il pulsante BOOTSEL */
  if (!gpio_get(PIN_TASTO[0]) && !gpio_get(PIN_TASTO[2])) {
    for (int i = 0; i < 3; i++) gpio_put(PIN_LEDP[i], 1);
    sleep_ms(300);
    reset_usb_boot(0, 0);
  }
  tusb_init();
  if (cyw43_arch_init() != 0) logf_("ERRORE: chip radio non inizializzato");
  for (int i = 0; i < 3; i++) {             /* firma della versione 8: 3 lampi veloci all'avvio */
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); attendi_ms(80);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); attendi_ms(120);
  }
  /* tasto 2 tenuto premuto all'accensione: passa da Bluetooth classico a BLE e viceversa.
     LED 2: 3 lampi veloci = ora BLE; acceso 1 secondo = ora classico. */
  { bool cambia = !gpio_get(PIN_TASTO[1]) && gpio_get(PIN_TASTO[0]) && gpio_get(PIN_TASTO[2]);
    bool ble = bt_scegli_tecnologia(cambia);
    if (cambia) {
      if (ble) for (int i = 0; i < 3; i++) { gpio_put(PIN_LEDP[1], 1); attendi_ms(120); gpio_put(PIN_LEDP[1], 0); attendi_ms(150); }
      else { gpio_put(PIN_LEDP[1], 1); attendi_ms(1000); gpio_put(PIN_LEDP[1], 0); }
      while (!gpio_get(PIN_TASTO[1])) attendi_ms(10);   /* aspetta che il tasto venga lasciato */
      attendi_ms(300);
    } }
  batt_init();                               /* livello all'accensione: 1, 2 o 3 LED per 2 secondi */
  { int p = batt_percento(); int n = p > 66 ? 3 : p > 33 ? 2 : 1;
    for (int i = 0; i < 3; i++) gpio_put(PIN_LEDP[i], i < n);
    attendi_ms(2000);
    for (int i = 0; i < 3; i++) gpio_put(PIN_LEDP[i], 0); }
  uint16_t lbt = 0;
  memcpy(desc_bt, desc_mouse_bt, sizeof desc_mouse_bt); lbt += sizeof desc_mouse_bt;
  memcpy(desc_bt + lbt, desc_hid_ptp, desc_hid_ptp_len); lbt += desc_hid_ptp_len;
  if (bt_ble()) bt_init(desc_ble, sizeof desc_ble); else bt_init(desc_bt, lbt);

  gpio_init(PIN_CS);   gpio_set_dir(PIN_CS, GPIO_OUT);   gpio_put(PIN_CS, 1);
  gpio_init(PIN_IFEN); gpio_set_dir(PIN_IFEN, GPIO_OUT); gpio_put(PIN_IFEN, 0);
  gpio_init(PIN_LID);  gpio_set_dir(PIN_LID, GPIO_OUT);  gpio_put(PIN_LID, 1);
  spi_init(spi0, SPI_HZ_CMD);
  spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);

  pkt_lettura[0] = 0x80;
  uint16_t c = crc16(pkt_lettura, 254); pkt_lettura[254] = c & 0xFF; pkt_lettura[255] = c >> 8;

  attendi_ms(500);                       /* alimentazione del trackpad stabile */
  static const uint32_t ritardi[] = { 100, 300, 1000, 3000, 30 };
  int tentativo = 0;
  for (;;) {
    ritardo_stato_us = ritardi[tentativo % 5];
    if (avvia_trackpad()) break;
    logf_("avvio fallito, riprovo"); tentativo++; attendi_ms(2000);
  }
  logf_("trackpad pronto%s", forza_bt ? " (Bluetooth forzato, USB solo diagnostica)" : "");

  absolute_time_t prossima = get_absolute_time(), stat = make_timeout_time_ms(5000);
  ultimo_frame = get_absolute_time();
  /* Risparmio energetico (bt18):
     - senza dita da piu' di 1 s il trackpad si legge ogni 8 ms invece che ogni millisecondo
       (il primo tocco arriva al massimo 8 ms dopo), e tra una lettura e l'altra il Pico dorme;
     - "riposo": dopo RIPOSO_MS senza dita e senza tasti il trackpad viene messo a dormire come col
       coperchio del MacBook chiuso (LID basso). Si sveglia premendo un tasto (o, se il trackpad lo
       segnala anche a coperchio chiuso, toccandolo: lo scopriamo dal registro). */
  #define RIPOSO_MS (15u * 60u * 1000u)        /* 15 minuti senza dita ne' tasti */
  bool riposo = false;
  absolute_time_t ultima_attivita = get_absolute_time();
  for (;;) {
    tud_task(); cyw43_arch_poll(); bt_tick(); log_flush(); batt_tick();
    if (tasto_giu()) {
      ultima_attivita = get_absolute_time();
      if (riposo) {                      /* il tasto che sveglia non cambia posto */
        riposo = false; logf_("riposo finito (tasto): sveglio il trackpad");
        while (tasto_giu()) attendi_ms(10);
        avvia_trackpad(); ultimo_frame = ultima_attivita = get_absolute_time();
        continue;
      }
    }
    tasti();
    /* computer Bluetooth nuovo: si riparte in modalita' mouse finche' lui (o l'app) non chiede il PTP */
    { static bool era = false; bool ora_c = bt_connected();
      if (ora_c && !era && !usb_hid()) { modo = MODO_MOUSE; heartbeat_attivo = false; }
      era = ora_c; }
    /* LED: spento con USB, lampeggia in attesa del Bluetooth, acceso se connesso.
       Prova tasti: mentre un tasto e' premuto il LED fa il contrario (lampeggio rapidissimo). */
    if (tasto_giu()) led((to_ms_since_boot(get_absolute_time()) / 60) & 1);
    else if (usb_hid()) led(false);
    else if (bt_connected()) led(false);   /* LED verde del Pico spento quando collegato: 1-2 mA risparmiati */
    else led((to_ms_since_boot(get_absolute_time()) / 500) & 1);
    if (ptp_in_attesa && tud_hid_n_ready(1)) {
      tud_hid_n_report(1, RID_TOUCH, &rep_in_attesa, sizeof rep_in_attesa); ptp_in_attesa = false;
    }
    if (heartbeat_attivo && time_reached(scadenza_heartbeat)) {
      heartbeat_attivo = false; modo = MODO_MOUSE; logf_("app Mac silenziosa: torno mouse");
    }
    if (!time_reached(prossima)) {
      /* a batteria il Pico dorme fino alla prossima lettura (lo sveglia anche la radio) */
      if (!tud_mounted()) cyw43_arch_wait_for_work_until(prossima);
      continue;
    }
    int64_t senza_dita = absolute_time_diff_us(ultimo_frame, get_absolute_time());
    if (!riposo && !usb_hid() && absolute_time_diff_us(ultima_attivita, get_absolute_time()) > (int64_t)RIPOSO_MS * 1000
        && senza_dita > (int64_t)RIPOSO_MS * 1000) {
      riposo = true; gpio_put(PIN_LID, 0); tutte_alzate();
      logf_("riposo: trackpad a dormire (coperchio chiuso)");
    }
    uint32_t passo = riposo ? 100000 : senza_dita > 1000000 ? 8000 : 1000;   /* una lettura al ms mentre si usa */
    prossima = delayed_by_us(prossima, passo);
    if (absolute_time_diff_us(prossima, get_absolute_time()) > 5 * (int64_t)passo) prossima = get_absolute_time();

    leggi_pacchetto();
    if (rx[0] == 0x20 && rx[1] == 0x02) {
      if (crc16(rx, 254) != (uint16_t)(rx[254] | rx[255] << 8)) { n_crc_err++; continue; }
      int off = rx[2] | rx[3] << 8, rem = rx[4] | rx[5] << 8, lung = rx[6] | rx[7] << 8;
      if (lung > 246) { n_rif_lung++; continue; }
      const uint8_t *msg = rx + 8; int mlen = lung;
      if (controlli) {
        if (off || rem) {
          n_multi++;
          if (off != msg_salvato || off + lung > (int)sizeof msg_buf) { msg_salvato = 0; n_rif_multi++; continue; }
          memcpy(msg_buf + off, rx + 8, (size_t)lung); msg_salvato += lung;
          if (rem) continue;
          msg = msg_buf; mlen = msg_salvato; msg_salvato = 0;
        } else msg_salvato = 0;
        if (!messaggio_valido(msg, mlen)) continue;
      } else if (lung < 8 + 32) continue;
      n_frame++; ultimo_frame = ultima_attivita = get_absolute_time();
      if (riposo) {                      /* tocco col trackpad a riposo: lo sveglio */
        riposo = false; gpio_put(PIN_LID, 1);
        logf_("riposo finito (tocco)");
      }
      elabora_frame(msg + 8, mlen - 8);
    } else {
      n_vuoti++;
      /* niente frame: dopo 30 ms le dita si considerano alzate, ma col clic premuto si aspetta
         fino a 300 ms (durante la vibrazione il trackpad puo' fermarsi un attimo): prima qui il clic
         veniva rilasciato e poi ripremuto = doppio clic, menu contestuale che si chiude da solo */
      int64_t fermo = absolute_time_diff_us(ultimo_frame, get_absolute_time());
      if (fermo > (pulsante ? 300000 : 30000)) { if (pulsante) n_pause++; tutte_alzate(); }
    }
    if (time_reached(stat)) {
      if (n_click || n_fantasmi || n_pause || n_finestre)
        logf_("5 s: clic %lu, pressioni fantasma %lu, vibrazioni %lu, dita finte scartate %lu, pause col clic %lu, batteria %u mV",
              (unsigned long)n_click, (unsigned long)n_fantasmi, (unsigned long)n_finestre,
              (unsigned long)n_dita_scartate, (unsigned long)n_pause, batt_mv);
      uint32_t rif = n_rif_crc + n_rif_lung + n_rif_multi;
      if (rif || n_multi || n_tipo_altro)
        logf_("5 s: messaggi scartati %lu (CRC %lu, lunghezza %lu, pezzi %lu), su piu' pacchetti %lu, tipo diverso %lu",
              (unsigned long)rif, (unsigned long)n_rif_crc, (unsigned long)n_rif_lung, (unsigned long)n_rif_multi,
              (unsigned long)n_multi, (unsigned long)n_tipo_altro);
      if (controlli && n_frame == 0 && rif >= 100) {
        controlli = false; logf_("nessun messaggio supera i controlli: controlli disattivati");
      }
      n_click = n_fantasmi = n_pause = n_finestre = n_dita_scartate = 0;
      n_rif_crc = n_rif_lung = n_rif_multi = n_multi = n_tipo_altro = 0;
      logf_("5 s: frame %lu, errori CRC %lu, SPI %lu kHz, modo %s, via %s", (unsigned long)n_frame, (unsigned long)n_crc_err,
            (unsigned long)(spi_hz_lettura / 1000), modo == MODO_PTP ? "PTP" : "mouse",
            usb_hid() ? "USB" : bt_connected() ? (bt_ble() ? "BLE" : "Bluetooth") : "nessuna");
      bt_stat();
      /* in Bluetooth gli errori vengono dalla radio, non dalla velocita': rallentare peggiora */
      if (!bt_connected() && n_crc_err > 2 && n_crc_err * 50 > n_frame && spi_hz_lettura > 1000000) {
        spi_hz_lettura /= 2; spi_set_baudrate(spi0, spi_hz_lettura);
        logf_("troppi errori: SPI scende a %lu kHz", (unsigned long)(spi_hz_lettura / 1000));
      }
      n_frame = n_crc_err = n_vuoti = 0; stat = make_timeout_time_ms(5000);
    }
  }
}

/* ------------------------------------------------------------------ callback HID */
int feature_get(uint8_t id, uint8_t *buf, int max) {
  switch (id) {
    case RID_CAPS:       if (max < 1) return 0; buf[0] = PTP_MAX_CONTACTS | (0 << 4); return 1;   /* clickpad */
    case RID_HQA:        if (max < 256) return 0; memcpy(buf, ptp_hqa_blob, 256); return 256;
    case RID_INPUTMODE:  if (max < 1) return 0; buf[0] = modo; return 1;
    case RID_FUNCSWITCH: if (max < 1) return 0; buf[0] = 0x03; return 1;
    case RID_VENDORCTL:  if (max < 1) return 0; buf[0] = (uint8_t)(modo | (heartbeat_attivo ? 0x80 : 0)); return 1;
    default: return 0;
  }
}

void feature_set(uint8_t id, uint8_t val) {
  if (id == RID_INPUTMODE) {
    modo = val == MODO_PTP ? MODO_PTP : MODO_MOUSE; heartbeat_attivo = false;
    logf_("modalita' (spec): %s", modo == MODO_PTP ? "PTP" : "mouse");
  } else if (id == RID_VENDORCTL) {
    modo = (val & 0x7F) == MODO_PTP ? MODO_PTP : MODO_MOUSE;
    heartbeat_attivo = (val & 0x80) && modo == MODO_PTP;
    scadenza_heartbeat = make_timeout_time_ms(12000);
    logf_("modalita' (app): %s%s", modo == MODO_PTP ? "PTP" : "mouse", heartbeat_attivo ? " con heartbeat" : "");
  }
}

uint16_t tud_hid_get_report_cb(uint8_t inst, uint8_t id, hid_report_type_t tipo, uint8_t *buf, uint16_t len) {
  if (inst != 1 || tipo != HID_REPORT_TYPE_FEATURE) return 0;
  int n = feature_get(id, buf, len);
  return (uint16_t)(n > 0 ? n : 0);
}

void tud_hid_set_report_cb(uint8_t inst, uint8_t id, hid_report_type_t tipo, uint8_t const *buf, uint16_t len) {
  if (inst != 1 || tipo != HID_REPORT_TYPE_FEATURE || len < 1) return;
  feature_set(id, buf[0]);
}
