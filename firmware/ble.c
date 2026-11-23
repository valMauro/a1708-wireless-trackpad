// Bluetooth Low Energy: HID su GATT (HOGP) per il trackpad, con servizio Batteria standard.
// Stessa interfaccia di bt.c (le funzioni bt_* qui si chiamano le_*, le sceglie bt_scelta.c).
#include <string.h>
#include "btstack.h"
#include "btstack_tlv.h"
#include "ble/gatt-service/battery_service_server.h"
#include "ble/gatt-service/device_information_service_server.h"
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"

#define bt_init           le_init
#define bt_connected      le_connected
#define bt_tick           le_tick
#define bt_stat           le_stat
#define bt_posto          le_posto
#define bt_scegli_posto   le_scegli_posto
#define bt_abbina_posto   le_abbina_posto
#define bt_stato_led      le_stato_led
#define bt_send           le_send
#define bt_batteria       le_batteria
#include "bt.h"
#include "trackpad.h"                     /* database GATT generato da trackpad.gatt */

#define NOME "Trackpad A1708"

/* maniglie del database (vedi trackpad.gatt) */
#define H_PROTOCOLLO   ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_PROTOCOL_MODE_01_VALUE_HANDLE
#define H_TOCCO        ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_REPORT_01_VALUE_HANDLE
#define H_TOCCO_CCC    ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_REPORT_01_CLIENT_CONFIGURATION_HANDLE
#define H_MOUSE        ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_REPORT_02_VALUE_HANDLE
#define H_MOUSE_CCC    ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_REPORT_02_CLIENT_CONFIGURATION_HANDLE
#define H_CAPS         ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_REPORT_03_VALUE_HANDLE
#define H_MODO         ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_REPORT_04_VALUE_HANDLE
#define H_SELETTORE    ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_REPORT_05_VALUE_HANDLE
#define H_CERTIFICATO  ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_REPORT_06_VALUE_HANDLE
#define H_MAPPA        ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_REPORT_MAP_01_VALUE_HANDLE
#define H_CONTROLLO    ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_HID_CONTROL_POINT_01_VALUE_HANDLE

static const uint8_t *descrittore; static uint16_t descrittore_len;
static uint16_t len_tocco, len_mouse;           /* dimensioni dei report di ingresso (senza ID) */
static hci_con_handle_t con = HCI_CON_HANDLE_INVALID;
static bool cifrato;                            /* collegamento cifrato (abbinato) */
static uint16_t ccc_tocco, ccc_mouse;
static uint8_t protocollo = 1;                  /* 1 = report */
static uint16_t mtu = 23;
static btstack_packet_callback_registration_t reg_hci, reg_sm;

/* coda delle notifiche: [maniglia(2) | dati] */
#define CODA 32
#define MAXREP 48
static uint8_t coda[CODA][MAXREP];
static uint16_t coda_len[CODA], coda_h[CODA];
static int c_testa, c_coda, c_num;
static uint32_t persi, inviati, coda_max, aggiornamenti;
static uint16_t intervallo_125us;               /* intervallo di connessione attuale (unita' 1,25 ms) */

/* ------------------------------------------------------------------ 3 posti
   Ogni posto ha un suo indirizzo (casuale statico, salvato in flash): ogni computer si abbina
   all'indirizzo di quel posto e si ricollega solo quando il trackpad usa quell'indirizzo,
   come fanno le Logitech Easy-Switch. Abbinare di nuovo un posto = indirizzo nuovo. */
#define POSTI 3
#define TAG_IND(i)   (0x54504C00u | (uint32_t)('0' + (i)))   /* 'TPL0'.. */
#define TAG_CORRENTE 0x54504352u                             /* 'TPCR' (comune col classico) */
static const btstack_tlv_t *tlv; static void *tlv_ctx;
static bd_addr_t ind[POSTI]; static bool usato[POSTI];      /* usato = gia' abbinato a un computer */
static int posto;
static bool abbina; static uint32_t abbina_fino;
static bool pronto;
static uint32_t pubblico_da;                                /* inizio della pubblicita' attuale */
static bool pubblico_lento;
static uint32_t connesso_da;

static uint32_t ora_ms(void) { return btstack_run_loop_get_time_ms(); }

static void nuovo_indirizzo(int i) {
  pico_unique_board_id_t id; pico_get_unique_board_id(&id);
  uint32_t r = ora_ms() * 2654435761u ^ ((uint32_t)id.id[0] << 24 | id.id[1] << 16 | id.id[2] << 8 | id.id[3]);
  for (int k = 0; k < 6; k++) { r = r * 1103515245u + 12345u + id.id[(k + 4) % 8]; ind[i][k] = (uint8_t)(r >> 16); }
  ind[i][0] |= 0xC0;                     /* indirizzo casuale "statico": i due bit alti a 1 */
}

static void salva(void) {
  if (!tlv) return;
  for (int i = 0; i < POSTI; i++) {
    uint8_t v[7]; memcpy(v, ind[i], 6); v[6] = usato[i];
    tlv->store_tag(tlv_ctx, TAG_IND(i), v, 7);
  }
  uint8_t p = (uint8_t)posto; tlv->store_tag(tlv_ctx, TAG_CORRENTE, &p, 1);
}

static void carica(void) {
  btstack_tlv_get_instance(&tlv, &tlv_ctx);
  bool cambiato = false;
  for (int i = 0; i < POSTI; i++) {
    uint8_t v[7];
    if (tlv && tlv->get_tag(tlv_ctx, TAG_IND(i), v, 7) == 7) { memcpy(ind[i], v, 6); usato[i] = v[6]; }
    else { nuovo_indirizzo(i); usato[i] = false; cambiato = true; }
  }
  uint8_t p = 0;
  if (tlv && tlv->get_tag(tlv_ctx, TAG_CORRENTE, &p, 1) == 1 && p < POSTI) posto = p;
  if (cambiato) salva();
  for (int i = 0; i < POSTI; i++)
    logf_("BLE posto %d: indirizzo %s, %s%s", i + 1, bd_addr_to_str(ind[i]), usato[i] ? "abbinato" : "libero",
          i == posto ? "  <- attuale" : "");
}

/* pubblicita': veloce (30 ms) per il primo minuto, poi lenta (500 ms) per risparmiare batteria */
static void pubblica(bool veloce) {
  if (!pronto) return;
  gap_advertisements_enable(0);
  bool visibile = abbina || !usato[posto];
  static uint8_t dati[31];
  uint8_t n = 0;
  dati[n++] = 2; dati[n++] = BLUETOOTH_DATA_TYPE_FLAGS; dati[n++] = visibile ? 0x06 : 0x04;
  dati[n++] = 3; dati[n++] = BLUETOOTH_DATA_TYPE_APPEARANCE; dati[n++] = 0xC2; dati[n++] = 0x03;
  dati[n++] = 3; dati[n++] = BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS;
  dati[n++] = ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE & 0xFF; dati[n++] = ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE >> 8;
  uint8_t ln = (uint8_t)strlen(NOME);
  dati[n++] = (uint8_t)(ln + 1); dati[n++] = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME; memcpy(dati + n, NOME, ln); n += ln;
  gap_random_address_set(ind[posto]);       /* indirizzo del posto (statico casuale) */
  bd_addr_t nulla; memset(nulla, 0, 6);
  uint16_t intv = veloce ? 0x0030 : 0x0320;           /* 30 ms / 500 ms */
  gap_advertisements_set_params(intv, intv, 0 /* ADV_IND */, 0, nulla, 0x07, 0x00);
  gap_advertisements_set_data(n, dati);
  gap_advertisements_enable(1);
  pubblico_da = ora_ms(); pubblico_lento = !veloce;
}

/* Le iscrizioni alle notifiche (CCCD) vanno ricordate per ogni computer abbinato: quando
   si ricollega dopo un riavvio del trackpad non le riscrive e senza di loro restiamo muti. */
#define TAG_CCC(i) (0x54504E00u | (uint32_t)('0' + (i)))   /* 'TPN0'.. */
static void salva_ccc(void) {
  if (!tlv) return;
  uint8_t v[4] = { (uint8_t)ccc_tocco, (uint8_t)(ccc_tocco >> 8), (uint8_t)ccc_mouse, (uint8_t)(ccc_mouse >> 8) };
  tlv->store_tag(tlv_ctx, TAG_CCC(posto), v, 4);
}
static void carica_ccc(void) {
  uint8_t v[4];
  if (tlv && tlv->get_tag(tlv_ctx, TAG_CCC(posto), v, 4) == 4) {
    if (!ccc_tocco) ccc_tocco = (uint16_t)(v[0] | v[1] << 8);
    if (!ccc_mouse) ccc_mouse = (uint16_t)(v[2] | v[3] << 8);
  }
}

static void stacca(void) { if (con != HCI_CON_HANDLE_INVALID) gap_disconnect(con); }

bool bt_connected(void) { return con != HCI_CON_HANDLE_INVALID && cifrato && (ccc_tocco || ccc_mouse); }
int bt_posto(void) { return posto; }

void bt_scegli_posto(int p) {
  if (p < 0 || p >= POSTI) return;
  if (p == posto && con != HCI_CON_HANDLE_INVALID) { connesso_da = ora_ms(); return; }
  posto = p; abbina = false; salva();
  logf_("BLE: passo al posto %d (%s)", p + 1, usato[p] ? "abbinato" : "libero, in abbinamento");
  if (con != HCI_CON_HANDLE_INVALID) stacca(); else pubblica(true);   /* dopo il distacco riparte da solo */
}

void bt_abbina_posto(int p) {
  if (p < 0 || p >= POSTI) return;
  posto = p; abbina = true; abbina_fino = ora_ms() + 180000;
  nuovo_indirizzo(p); usato[p] = false; salva();       /* indirizzo nuovo: il vecchio computer non si ricollega piu' */
  logf_("BLE: posto %d in abbinamento per 3 minuti (nuovo indirizzo %s): cerca \"%s\" sul computer",
        p + 1, bd_addr_to_str(ind[p]), NOME);
  if (con != HCI_CON_HANDLE_INVALID) stacca(); else pubblica(true);
}

int bt_stato_led(void) {
  if (abbina || !usato[posto]) return 3;
  if (con != HCI_CON_HANDLE_INVALID) return ora_ms() - connesso_da < 4000 ? 1 : 0;
  return pubblico_lento ? 0 : 2;
}

void bt_batteria(int percento) {
  static int ultimo = -1;
  if (percento < 0) percento = 0;
  if (percento > 100) percento = 100;
  if (percento != ultimo) { battery_service_server_set_battery_value((uint8_t)percento); ultimo = percento; }
}

void bt_send(uint8_t report_id, const void *data, uint16_t len) {
  if (!bt_connected() || len > MAXREP) return;
  uint16_t h;
  if (report_id == 0x01 && ccc_tocco) h = H_TOCCO;
  else if (report_id == 0x07 && ccc_mouse) h = H_MOUSE;
  else return;
  if (c_num == CODA) { c_testa = (c_testa + 1) % CODA; c_num--; persi++; }
  memcpy(coda[c_coda], data, len); coda_len[c_coda] = len; coda_h[c_coda] = h;
  c_coda = (c_coda + 1) % CODA; c_num++;
  if ((uint32_t)c_num > coda_max) coda_max = (uint32_t)c_num;
  att_server_request_can_send_now_event(con);
}

/* ------------------------------------------------------------------ lettura / scrittura GATT */
static uint8_t tmp[300];

static uint16_t leggi(hci_con_handle_t h, uint16_t att, uint16_t offset, uint8_t *buf, uint16_t dim) {
  (void)h;
  int n;
  switch (att) {
    case H_PROTOCOLLO: return att_read_callback_handle_byte(protocollo, offset, buf, dim);
    case H_MAPPA:      return att_read_callback_handle_blob(descrittore, descrittore_len, offset, buf, dim);
    case H_TOCCO_CCC:  return att_read_callback_handle_little_endian_16(ccc_tocco, offset, buf, dim);
    case H_MOUSE_CCC:  return att_read_callback_handle_little_endian_16(ccc_mouse, offset, buf, dim);
    case H_TOCCO:      memset(tmp, 0, len_tocco); return att_read_callback_handle_blob(tmp, len_tocco, offset, buf, dim);
    case H_MOUSE:      memset(tmp, 0, len_mouse); return att_read_callback_handle_blob(tmp, len_mouse, offset, buf, dim);
    case H_CAPS: case H_MODO: case H_SELETTORE: case H_CERTIFICATO: {
      uint8_t id = att == H_CAPS ? 0x02 : att == H_MODO ? 0x03 : att == H_SELETTORE ? 0x04 : 0x06;
      n = feature_get(id, tmp, sizeof tmp);
      if (n <= 0) n = 0;
      return att_read_callback_handle_blob(tmp, (uint16_t)n, offset, buf, dim);
    }
    default: return 0;
  }
}

static int scrivi(hci_con_handle_t h, uint16_t att, uint16_t modo_tr, uint16_t offset, uint8_t *buf, uint16_t dim) {
  (void)h; (void)offset;
  if (modo_tr != ATT_TRANSACTION_MODE_NONE) return 0;
  switch (att) {
    case H_TOCCO_CCC: ccc_tocco = little_endian_read_16(buf, 0); salva_ccc(); logf_("BLE: notifiche tocchi %s", ccc_tocco ? "attive" : "spente"); break;
    case H_MOUSE_CCC: ccc_mouse = little_endian_read_16(buf, 0); salva_ccc(); logf_("BLE: notifiche mouse %s", ccc_mouse ? "attive" : "spente"); break;
    case H_PROTOCOLLO: if (dim) protocollo = buf[0]; break;
    case H_MODO:       if (dim) feature_set(0x03, buf[0]); break;
    case H_SELETTORE:  if (dim) feature_set(0x04, buf[0]); break;
    case H_CONTROLLO:  break;                       /* sospensione / ripresa: non serve */
    default: break;
  }
  return 0;
}

/* ------------------------------------------------------------------ eventi */
static void gestore(uint8_t tipo, uint16_t canale, uint8_t *pkt, uint16_t dim) {
  (void)canale; (void)dim;
  if (tipo != HCI_EVENT_PACKET) return;
  switch (hci_event_packet_get_type(pkt)) {
    case BTSTACK_EVENT_STATE:
      if (btstack_event_state_get_state(pkt) == HCI_STATE_WORKING) {
        carica();
        pronto = true;
        logf_("BLE pronto: %s", NOME);
        pubblica(true);
      }
      break;
    case HCI_EVENT_META_GAP:
      if (hci_event_gap_meta_get_subevent_code(pkt) == GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
        if (gap_subevent_le_connection_complete_get_status(pkt) != 0) break;
        con = gap_subevent_le_connection_complete_get_connection_handle(pkt);
        cifrato = false; ccc_tocco = ccc_mouse = 0; mtu = 23;
        c_num = c_testa = c_coda = 0;
        intervallo_125us = gap_subevent_le_connection_complete_get_conn_interval(pkt);
        connesso_da = ora_ms();
        logf_("BLE: collegato (intervallo %u.%02u ms)", intervallo_125us * 125 / 100, (intervallo_125us * 125) % 100);
      }
      break;
    case HCI_EVENT_LE_META:
      if (hci_event_le_meta_get_subevent_code(pkt) == HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE) {
        intervallo_125us = hci_subevent_le_connection_update_complete_get_conn_interval(pkt);
        aggiornamenti++;
        logf_("BLE: intervallo di connessione %u.%02u ms, latenza %u",
              intervallo_125us * 125 / 100, (intervallo_125us * 125) % 100,
              hci_subevent_le_connection_update_complete_get_conn_latency(pkt));
      }
      break;
    case HCI_EVENT_ENCRYPTION_CHANGE:
    case HCI_EVENT_ENCRYPTION_CHANGE_V2:
      /* stesso formato all'inizio: stato, maniglia, cifratura attiva */
      if (hci_event_encryption_change_get_connection_handle(pkt) == con &&
          hci_event_encryption_change_get_status(pkt) == 0 &&
          hci_event_encryption_change_get_encryption_enabled(pkt)) {
        cifrato = true;
        if (!usato[posto]) {
          usato[posto] = true; abbina = false; salva();
          if (tlv) tlv->delete_tag(tlv_ctx, TAG_CCC(posto));     /* computer nuovo: niente iscrizioni vecchie */
          logf_("BLE: posto %d abbinato", posto + 1);
        } else carica_ccc();
        /* chiede appuntamenti ogni 7,5-11,25 ms, senza saltarne (latenza 0) */
        gap_request_connection_parameter_update(con, 6, 9, 0, 300);
        logf_("BLE: collegamento cifrato");
      }
      break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
      if (hci_event_disconnection_complete_get_connection_handle(pkt) == con) {
        con = HCI_CON_HANDLE_INVALID; cifrato = false; ccc_tocco = ccc_mouse = 0;
        logf_("BLE: scollegato (motivo 0x%02x, notifiche perse %lu)",
              hci_event_disconnection_complete_get_reason(pkt), (unsigned long)persi);
        pubblica(true);
      }
      break;
    case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
      mtu = att_event_mtu_exchange_complete_get_MTU(pkt);
      logf_("BLE: MTU %u", mtu);
      break;
    case ATT_EVENT_CAN_SEND_NOW:
      while (c_num) {
        uint16_t lm = (uint16_t)(mtu - 3);
        uint16_t l = coda_len[c_testa] < lm ? coda_len[c_testa] : lm;
        if (att_server_notify(con, coda_h[c_testa], coda[c_testa], l) != ERROR_CODE_SUCCESS) break;
        inviati++; c_testa = (c_testa + 1) % CODA; c_num--;
        if (!att_server_can_send_packet_now(con)) break;
      }
      if (c_num) att_server_request_can_send_now_event(con);
      break;
    case SM_EVENT_JUST_WORKS_REQUEST:
      sm_just_works_confirm(sm_event_just_works_request_get_handle(pkt));
      logf_("BLE: abbinamento richiesto, accetto");
      break;
    case SM_EVENT_PAIRING_COMPLETE:
      logf_("BLE: abbinamento %s (stato %u)", sm_event_pairing_complete_get_status(pkt) == 0 ? "riuscito" : "fallito",
            sm_event_pairing_complete_get_status(pkt));
      break;
    default: break;
  }
}

void bt_stat(void) {
  if (con == HCI_CON_HANDLE_INVALID) return;
  logf_("  BLE 5 s: inviati %lu, coda max %lu, persi %lu, intervallo %u.%02u ms, MTU %u", (unsigned long)inviati,
        (unsigned long)coda_max, (unsigned long)persi, intervallo_125us * 125 / 100, (intervallo_125us * 125) % 100, mtu);
  inviati = coda_max = 0;
}

void bt_tick(void) {
  if (!pronto) return;
  if (abbina && (int32_t)(ora_ms() - abbina_fino) > 0) {
    abbina = false; logf_("BLE: abbinamento scaduto");
    if (con == HCI_CON_HANDLE_INVALID) pubblica(true);
  }
  if (con == HCI_CON_HANDLE_INVALID && !pubblico_lento && !abbina && usato[posto] && ora_ms() - pubblico_da > 60000)
    pubblica(false);
}

void bt_init(const uint8_t *desc, uint16_t len) {
  descrittore = desc; descrittore_len = len;
  len_tocco = (uint16_t)btstack_hid_get_report_size_for_id(0x01, HID_REPORT_TYPE_INPUT, desc, len);
  len_mouse = (uint16_t)btstack_hid_get_report_size_for_id(0x07, HID_REPORT_TYPE_INPUT, desc, len);

  l2cap_init();
  sm_init();
  sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
  sm_set_authentication_requirements(SM_AUTHREQ_SECURE_CONNECTION | SM_AUTHREQ_BONDING);
  att_server_init(profile_data, &leggi, &scrivi);
  battery_service_server_init(100);
  device_information_service_server_init();
  device_information_service_server_set_manufacturer_name("Fai da te");
  device_information_service_server_set_model_number(NOME);
  device_information_service_server_set_pnp_id(0x02, 0x1209, 0x0001, 0x0100);   /* sorgente USB, VID, PID, versione */

  reg_hci.callback = &gestore; hci_add_event_handler(&reg_hci);
  reg_sm.callback = &gestore;  sm_add_event_handler(&reg_sm);
  att_server_register_packet_handler(&gestore);
  gap_set_local_name(NOME);
  hci_power_control(HCI_POWER_ON);
}
