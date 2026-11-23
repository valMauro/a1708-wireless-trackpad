// Bluetooth Classic HID (BTstack) per il trackpad.
#include <string.h>
#include "btstack.h"
#include "btstack_tlv.h"
#include "pico/cyw43_arch.h"
#define bt_init           cl_init
#define bt_connected      cl_connected
#define bt_tick           cl_tick
#define bt_stat           cl_stat
#define bt_posto          cl_posto
#define bt_scegli_posto   cl_scegli_posto
#define bt_abbina_posto   cl_abbina_posto
#define bt_stato_led      cl_stato_led
#define bt_send           cl_send
#define bt_batteria       cl_batteria
#include "bt.h"

#define NOME "Trackpad A1708"
static uint8_t sdp_hid[1200];
static uint8_t sdp_did[100];
static btstack_packet_callback_registration_t reg;
static uint16_t hid_cid;
static hci_con_handle_t con_handle = HCI_CON_HANDLE_INVALID;
static bool qos_da_inviare;
static bool sniff_da_chiedere;

/* coda dei report: [0xA1, id, dati...] */
#define CODA 32
#define SOSTITUISCI 0
#define MAXREP 48
static uint8_t coda[CODA][MAXREP];
static uint16_t coda_len[CODA];
static int c_testa, c_coda, c_num;
static uint32_t persi, inviati, coda_max, sostituiti;

bool bt_connected(void) { return hid_cid != 0; }

/* ------------------------------------------------------------------ 3 posti (come Logitech Easy-Switch)
   Ogni posto ricorda l'indirizzo di un computer abbinato (salvato in flash). Le chiavi di
   abbinamento le conserva BTstack (fino a 16). Pressione breve = passa al posto, lunga = abbina. */
#define POSTI 3
#define TAG_POSTO(i) (0x54500000u | (uint32_t)('0' + (i)))   /* 'TP0'.. */
#define TAG_CORRENTE 0x54504352u                              /* 'TPCR' */
static const btstack_tlv_t *tlv; static void *tlv_ctx;
static bd_addr_t host[POSTI]; static bool host_ok[POSTI];
static int posto;
static bool abbina;                    /* posto corrente in modalita' abbinamento */
static uint32_t abbina_fino;           /* ms: fine dell'abbinamento (poi si torna al computer salvato) */
static bool pronto, connetto;
static uint32_t prossimo_tentativo, tentativi;
static bd_addr_t appena_abbinato; static bool appena_abbinato_ok;
static uint32_t connesso_da;           /* ms del momento della connessione (per il LED) */

static uint32_t ora_ms(void) { return btstack_run_loop_get_time_ms(); }

static void salva_posti(void) {
  if (!tlv) return;
  for (int i = 0; i < POSTI; i++) {
    if (host_ok[i]) tlv->store_tag(tlv_ctx, TAG_POSTO(i), host[i], 6);
    else tlv->delete_tag(tlv_ctx, TAG_POSTO(i));
  }
  uint8_t p = (uint8_t)posto; tlv->store_tag(tlv_ctx, TAG_CORRENTE, &p, 1);
}

static void carica_posti(void) {
  btstack_tlv_get_instance(&tlv, &tlv_ctx);
  if (!tlv) { logf_("Bluetooth: memoria posti non disponibile"); return; }
  for (int i = 0; i < POSTI; i++)
    host_ok[i] = tlv->get_tag(tlv_ctx, TAG_POSTO(i), host[i], 6) == 6;
  uint8_t p = 0;
  if (tlv->get_tag(tlv_ctx, TAG_CORRENTE, &p, 1) == 1 && p < POSTI) posto = p;
  for (int i = 0; i < POSTI; i++)
    logf_("Bluetooth posto %d: %s%s", i + 1, host_ok[i] ? bd_addr_to_str(host[i]) : "vuoto", i == posto ? "  <- attuale" : "");
}

/* visibilita': ci si fa trovare da nuovi computer solo in abbinamento */
static void aggiorna_visibilita(void) {
  bool visibile = abbina || !host_ok[posto];
  gap_discoverable_control(visibile && !hid_cid ? 1 : 0);
  gap_connectable_control(1);
}

static void stacca(void) {
  if (hid_cid && con_handle != HCI_CON_HANDLE_INVALID) gap_disconnect(con_handle);
}

int bt_posto(void) { return posto; }

void bt_scegli_posto(int p) {
  if (p < 0 || p >= POSTI) return;
  bool cambio = p != posto;
  if (!cambio && hid_cid) { connesso_da = ora_ms(); return; }   /* gia' li': mostra solo il LED */
  posto = p; abbina = false; tentativi = 0; prossimo_tentativo = ora_ms() + 300;
  logf_("Bluetooth: passo al posto %d (%s)", p + 1, host_ok[p] ? bd_addr_to_str(host[p]) : "vuoto, in abbinamento");
  salva_posti();
  stacca();
  aggiorna_visibilita();
}

void bt_abbina_posto(int p) {
  if (p < 0 || p >= POSTI) return;
  if (p != posto) { posto = p; salva_posti(); }
  abbina = true; abbina_fino = ora_ms() + 180000; tentativi = 0;
  logf_("Bluetooth: posto %d in abbinamento per 3 minuti: cerca \"%s\" sul computer", p + 1, NOME);
  stacca();
  aggiorna_visibilita();
}

/* 0 = spento, 1 = acceso fisso, 2 = lampeggio lento (riconnessione), 3 = lampeggio veloce (abbinamento) */
int bt_stato_led(void) {
  if (abbina || !host_ok[posto]) return 3;
  if (hid_cid) return ora_ms() - connesso_da < 4000 ? 1 : 0;
  return tentativi < 40 ? 2 : 0;                 /* dopo un paio di minuti smette di lampeggiare */
}

static bool stesso(const bd_addr_t a, const bd_addr_t b) { return bd_addr_cmp(a, b) == 0; }

/* decide se tenere una connessione appena aperta da/verso 'a' */
static bool accetta(bd_addr_t a) {
  if (host_ok[posto] && stesso(a, host[posto]) && !abbina) return true;
  if (abbina || !host_ok[posto]) {
    int altro = -1;
    for (int i = 0; i < POSTI; i++) if (i != posto && host_ok[i] && stesso(a, host[i])) altro = i;
    bool nuovo_abbinamento = appena_abbinato_ok && stesso(a, appena_abbinato);
    /* un computer di un altro posto che si ricollega da solo non deve rubare questo posto */
    if (altro >= 0 && !nuovo_abbinamento) return false;
    if (altro >= 0) host_ok[altro] = false;
    bd_addr_copy(host[posto], a); host_ok[posto] = true; abbina = false;
    salva_posti();
    logf_("Bluetooth: posto %d abbinato a %s", posto + 1, bd_addr_to_str(a));
    return true;
  }
  return false;
}

void bt_send(uint8_t report_id, const void *data, uint16_t len) {
  if (!hid_cid || len + 2 > MAXREP) return;
  if (c_num == CODA) { c_testa = (c_testa + 1) % CODA; c_num--; persi++; }   /* butta il piu' vecchio */
  /* Niente arretrati: se c'e' gia' un report dello stesso tipo in attesa, lo sostituisco col
     piu' recente (le posizioni sono assolute, l'app ricava la velocita' dallo scan time).
     Non sostituisco un report che contiene dita sollevate, altrimenti il sollevamento si perde. */
  /* Sostituzione disattivata: ora l'app sul Mac rimette i tocchi al loro ritmo, quindi
     conviene consegnarli tutti, anche se in ritardo. */
  if (SOSTITUISCI && c_num >= 1) {
    int ult = (c_coda + CODA - 1) % CODA;
    uint8_t *q = coda[ult];
    bool ha_alzate = false;
    if (report_id == 0x01)                     /* report PTP: 5 contatti da 6 byte dopo l'ID */
      for (int i = 0; i < 5; i++) {
        uint8_t fl = q[2 + 6 * i];
        if (i < q[2 + 30 + 2] && !(fl & 0x02)) ha_alzate = true;
      }
    if (q[1] == report_id && !ha_alzate && report_id == 0x01) {
      memcpy(q + 2, data, len); coda_len[ult] = (uint16_t)(len + 2); sostituiti++;
      return;
    }
  }
  uint8_t *p = coda[c_coda];
  p[0] = 0xA1; p[1] = report_id; memcpy(p + 2, data, len);
  coda_len[c_coda] = (uint16_t)(len + 2);
  c_coda = (c_coda + 1) % CODA; c_num++;
  if ((uint32_t)c_num > coda_max) coda_max = (uint32_t)c_num;
  hid_device_request_can_send_now_event(hid_cid);
}

static int get_report(uint16_t cid, hid_report_type_t tipo, uint16_t id, int *size, uint8_t *out) {
  (void)cid;
  if (tipo != HID_REPORT_TYPE_FEATURE) return -1;
  int n = feature_get((uint8_t)id, out, 300);
  if (n <= 0) return -1;
  *size = n;
  return 1;
}

static void set_report(uint16_t cid, hid_report_type_t tipo, int size, uint8_t *rep) {
  (void)cid;
  if (tipo != HID_REPORT_TYPE_FEATURE || size < 2) return;
  feature_set(rep[0], rep[1]);
}

static void gestore(uint8_t tipo, uint16_t canale, uint8_t *pkt, uint16_t dim) {
  (void)canale; (void)dim;
  if (tipo != HCI_EVENT_PACKET) return;
  switch (hci_event_packet_get_type(pkt)) {
    case BTSTACK_EVENT_STATE:
      if (btstack_event_state_get_state(pkt) == HCI_STATE_WORKING) {
        bd_addr_t a; gap_local_bd_addr(a);
        logf_("Bluetooth pronto: %s (%s)", NOME, bd_addr_to_str(a));
        carica_posti();
        pronto = true; prossimo_tentativo = ora_ms() + 500;
        aggiorna_visibilita();
      }
      break;
    case HCI_EVENT_LINK_KEY_NOTIFICATION:
      reverse_bd_addr(&pkt[2], appena_abbinato); appena_abbinato_ok = true;
      logf_("Bluetooth: nuova chiave di abbinamento con %s", bd_addr_to_str(appena_abbinato));
      break;
    case HCI_EVENT_MODE_CHANGE:
      logf_("Bluetooth modo: %s, intervallo %u slot (%.2f ms), stato %u",
            hci_event_mode_change_get_mode(pkt) == 2 ? "SNIFF" : hci_event_mode_change_get_mode(pkt) == 0 ? "attivo" : "altro",
            hci_event_mode_change_get_interval(pkt), hci_event_mode_change_get_interval(pkt) * 0.625,
            hci_event_mode_change_get_status(pkt));
      break;
    case HCI_EVENT_QOS_SETUP_COMPLETE:
      logf_("Bluetooth QoS: stato %u, latenza %lu us", pkt[2], (unsigned long)little_endian_read_32(pkt, 15));
      break;
    case HCI_EVENT_USER_CONFIRMATION_REQUEST:
      logf_("Bluetooth: richiesta di abbinamento, accetto");
      break;
    case HCI_EVENT_HID_META:
      switch (hci_event_hid_meta_get_subevent_code(pkt)) {
        case HID_SUBEVENT_CONNECTION_OPENED:
          connetto = false;
          if (hid_subevent_connection_opened_get_status(pkt) != ERROR_CODE_SUCCESS) {
            logf_("Bluetooth: connessione fallita (%u)", hid_subevent_connection_opened_get_status(pkt));
            hid_cid = 0; return;
          }
          {
            bd_addr_t a; hid_subevent_connection_opened_get_bd_addr(pkt, a);
            hci_con_handle_t h = hid_subevent_connection_opened_get_con_handle(pkt);
            if (!accetta(a)) {
              logf_("Bluetooth: %s non e' il computer del posto %d, lo stacco", bd_addr_to_str(a), posto + 1);
              gap_disconnect(h); return;
            }
            con_handle = h;
          }
          hid_cid = hid_subevent_connection_opened_get_hid_cid(pkt);
          connesso_da = ora_ms(); tentativi = 0; appena_abbinato_ok = false;
          aggiorna_visibilita();
          qos_da_inviare = true;
          sniff_da_chiedere = true;
          c_num = c_testa = c_coda = 0;
          logf_("Bluetooth: connesso");
          break;
        case HID_SUBEVENT_CONNECTION_CLOSED:
          hid_cid = 0; con_handle = HCI_CON_HANDLE_INVALID; connetto = false;
          prossimo_tentativo = ora_ms() + 500;
          aggiorna_visibilita();
          logf_("Bluetooth: disconnesso (report persi per coda piena: %lu)", (unsigned long)persi);
          break;
        case HID_SUBEVENT_CAN_SEND_NOW:
          if (c_num) {
            hid_device_send_interrupt_message(hid_cid, coda[c_testa], coda_len[c_testa]); inviati++;
            c_testa = (c_testa + 1) % CODA; c_num--;
            if (c_num) hid_device_request_can_send_now_event(hid_cid);
          }
          break;
        default: break;
      }
      break;
    default: break;
  }
}

/* Chiede al Mac di interrogare il trackpad spesso: senza, il collegamento usa
   l'intervallo standard di 25 ms e i report arrivano a gruppi (movimento a scatti). */
void bt_stat(void) {
  if (!hid_cid) return;
  logf_("  Bluetooth 5 s: inviati %lu, sostituiti %lu, coda max %lu, persi %lu", (unsigned long)inviati,
        (unsigned long)sostituiti, (unsigned long)coda_max, (unsigned long)persi);
  inviati = coda_max = sostituiti = 0;
}

void bt_tick(void) {
  if (pronto && abbina && (int32_t)(ora_ms() - abbina_fino) > 0) {
    abbina = false; logf_("Bluetooth: abbinamento scaduto"); aggiorna_visibilita();
  }
  /* ricollegamento al computer del posto attuale */
  if (pronto && !hid_cid && !connetto && !abbina && host_ok[posto] &&
      (int32_t)(ora_ms() - prossimo_tentativo) >= 0) {
    uint16_t cid;
    uint8_t st = hid_device_connect(host[posto], &cid);
    tentativi++;
    /* primi 2 minuti ogni ~3 s, poi ogni 15 s per non consumare batteria */
    prossimo_tentativo = ora_ms() + (tentativi < 40 ? 3000 : 15000);
    if (st == ERROR_CODE_SUCCESS) connetto = true;
    if (tentativi <= 3 || tentativi % 20 == 0)
      logf_("Bluetooth: provo a collegarmi a %s (posto %d, tentativo %lu)", bd_addr_to_str(host[posto]), posto + 1, (unsigned long)tentativi);
  }
  if (qos_da_inviare && con_handle != HCI_CON_HANDLE_INVALID && hci_can_send_command_packet_now()) {
    qos_da_inviare = false;
    /* handle, flags, servizio garantito, 8000 B/s, picco 0, latenza 2500 us, variazione libera */
    hci_send_cmd(&hci_qos_setup, con_handle, 0, 2, 8000, 0, 2500, 0xFFFFFFFF);
    return;
  }
  /* Modalita' "sniff" con appuntamenti fissi ogni 7,5-11,25 ms, come le periferiche Apple:
     il Mac puo' cosi' proteggere quegli istanti dal Wi-Fi invece di interrogarci quando capita. */
  if (sniff_da_chiedere && con_handle != HCI_CON_HANDLE_INVALID && hci_can_send_command_packet_now()) {
    sniff_da_chiedere = false;
    gap_sniff_mode_enter(con_handle, 12, 18, 2, 1);   /* intervallo 12-18 slot (7,5-11,25 ms) */
  }
}

void bt_init(const uint8_t *desc, uint16_t len) {
  gap_discoverable_control(0);          /* si decide dopo aver letto i posti salvati */
  gap_set_page_timeout(0x1900);         /* 4 s per cercare un computer (predefinito 15 s) */
  gap_set_class_of_device(0x2580);                 /* periferica: dispositivo di puntamento */
  gap_set_local_name(NOME);
  gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE);
  gap_set_allow_role_switch(true);
  gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
  gap_ssp_set_auto_accept(1);
  l2cap_init();
  sdp_init();

  hid_sdp_record_t p = {
    0x2580, 33,       /* sottoclasse, paese */
    0, 1, 1, 1,       /* virtual cable, remote wake, reconnect initiate, normally connectable */
    0,                /* non dispositivo boot */
    0xFFFF, 0xFFFF, 3200,
    desc, len, NOME
  };
  memset(sdp_hid, 0, sizeof sdp_hid);
  hid_create_sdp_record(sdp_hid, sdp_create_service_record_handle(), &p);
  btstack_assert(de_get_len(sdp_hid) <= sizeof sdp_hid);
  sdp_register_service(sdp_hid);
  device_id_create_sdp_record(sdp_did, sdp_create_service_record_handle(),
                              DEVICE_ID_VENDOR_ID_SOURCE_USB, 0x1209, 0x0001, 0x0100);
  sdp_register_service(sdp_did);

  hid_device_init(0, len, desc);
  reg.callback = &gestore;
  hci_add_event_handler(&reg);
  hid_device_register_packet_handler(&gestore);
  hid_device_register_report_request_callback(&get_report);
  hid_device_register_set_report_callback(&set_report);
  hci_power_control(HCI_POWER_ON);
}

void bt_batteria(int percento) { (void)percento; }   /* nel classico non c'e' un servizio standard */
