#ifndef BT_H
#define BT_H
#include <stdint.h>
#include <stdbool.h>
void bt_init(const uint8_t *desc, uint16_t len);
bool bt_connected(void);
void bt_tick(void);
void bt_stat(void);
int  bt_posto(void);                 /* 0..2 */
void bt_scegli_posto(int p);         /* pressione breve */
void bt_abbina_posto(int p);         /* pressione lunga */
int  bt_stato_led(void);             /* 0 spento, 1 fisso, 2 lento, 3 veloce */
void bt_send(uint8_t report_id, const void *data, uint16_t len);   /* in coda, inviato appena possibile */
void bt_batteria(int percento);      /* livello per il servizio Batteria (solo BLE) */
/* tecnologia: Bluetooth classico o BLE, salvata in flash; cambia = passa all'altra */
bool bt_scegli_tecnologia(bool cambia);
bool bt_ble(void);
/* forniti da main.c */
int  feature_get(uint8_t id, uint8_t *buf, int max);
void feature_set(uint8_t id, uint8_t val);
void logf_(const char *fmt, ...);
#endif
