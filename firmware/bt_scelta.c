// Sceglie tra Bluetooth classico (bt.c) e BLE (ble.c). La scelta e' salvata in flash.
#include <stdbool.h>
#include <stdint.h>
#include "btstack_tlv.h"
#include "bt.h"

#define TAG_TECNOLOGIA 0x54504D44u   /* 'TPMD': 0 classico, 1 BLE */
static bool ble;

#define DICHIARA(p) \
  void p##_init(const uint8_t *, uint16_t); bool p##_connected(void); void p##_tick(void); void p##_stat(void); \
  int p##_posto(void); void p##_scegli_posto(int); void p##_abbina_posto(int); int p##_stato_led(void); \
  void p##_send(uint8_t, const void *, uint16_t); void p##_batteria(int);
DICHIARA(cl)
DICHIARA(le)

bool bt_scegli_tecnologia(bool cambia) {
  const btstack_tlv_t *tlv; void *ctx;
  btstack_tlv_get_instance(&tlv, &ctx);
  uint8_t v = 0;
  if (tlv) tlv->get_tag(ctx, TAG_TECNOLOGIA, &v, 1);
  ble = v == 1;
  if (cambia) {
    ble = !ble; v = ble ? 1 : 0;
    if (tlv) tlv->store_tag(ctx, TAG_TECNOLOGIA, &v, 1);
  }
  logf_("Bluetooth: tecnologia %s%s", ble ? "BLE (Low Energy)" : "classica", cambia ? " (appena cambiata)" : "");
  return ble;
}

bool bt_ble(void) { return ble; }
void bt_init(const uint8_t *d, uint16_t l) { if (ble) le_init(d, l); else cl_init(d, l); }
bool bt_connected(void) { return ble ? le_connected() : cl_connected(); }
void bt_tick(void) { if (ble) le_tick(); else cl_tick(); }
void bt_stat(void) { if (ble) le_stat(); else cl_stat(); }
int  bt_posto(void) { return ble ? le_posto() : cl_posto(); }
void bt_scegli_posto(int p) { if (ble) le_scegli_posto(p); else cl_scegli_posto(p); }
void bt_abbina_posto(int p) { if (ble) le_abbina_posto(p); else cl_abbina_posto(p); }
int  bt_stato_led(void) { return ble ? le_stato_led() : cl_stato_led(); }
void bt_send(uint8_t id, const void *d, uint16_t l) { if (ble) le_send(id, d, l); else cl_send(id, d, l); }
void bt_batteria(int p) { if (ble) le_batteria(p); else cl_batteria(p); }
