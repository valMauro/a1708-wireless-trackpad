#ifndef PTP_H
#define PTP_H
#include <stdint.h>
/* Report ID dell'interfaccia Precision Touchpad */
#define RID_TOUCH      0x01
#define RID_CAPS       0x02
#define RID_INPUTMODE  0x03
#define RID_FUNCSWITCH 0x04
#define RID_HQA        0x06
#define RID_MOUSE_BT   0x07   /* solo nel descrittore Bluetooth */
#define RID_VENDORCTL  0x10   /* non dichiarato: percorso "heartbeat" dell'app Mac */

#define PTP_MAX_CONTACTS 5
/* Trackpad MacBookPro14,1 (SPI 05ac:0276): x -6243..6749, y -170..7685 */
#define TP_XMIN  (-6243)
#define TP_XMAX  (6749)
#define TP_YMIN  (-170)
#define TP_YMAX  (7685)
#define LOG_XMAX (TP_XMAX - TP_XMIN)   /* 12992 */
#define LOG_YMAX (TP_YMAX - TP_YMIN)   /* 7855  */
#define PHYS_X_CENTI_MM 1340   /* 134,0 mm, in unita' di 0,01 cm */
#define PHYS_Y_CENTI_MM 810    /* 81,0 mm */

typedef struct __attribute__((packed)) {
  uint8_t  flags;      /* bit0 confidence, bit1 tip */
  uint8_t  id;
  uint16_t x;
  uint16_t y;
} ptp_contact_t;

typedef struct __attribute__((packed)) {
  ptp_contact_t c[PTP_MAX_CONTACTS];
  uint16_t scan_time;  /* unita' 100 us */
  uint8_t  count;
  uint8_t  button;
} ptp_report_t;        /* 35 byte, senza Report ID */

extern uint8_t const desc_hid_mouse[];
extern uint16_t const desc_hid_mouse_len;
extern uint8_t const desc_hid_ptp[];
extern uint16_t const desc_hid_ptp_len;
extern uint8_t const ptp_hqa_blob[256];
#endif
