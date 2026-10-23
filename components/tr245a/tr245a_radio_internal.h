#ifndef TR245A_RADIO_INTERNAL_H
#define TR245A_RADIO_INTERNAL_H

/* A7105 registers and SPI helpers; included by tr245a_radio.h. */
#include "tr245a_radio_types.h"

enum {
  TR245A_A7105_REG_MODE = 0x00,
  TR245A_A7105_REG_CALIBRATION = 0x02,
  TR245A_A7105_REG_FIFO_DATA = 0x05,
  TR245A_A7105_REG_ID_DATA = 0x06,
  TR245A_A7105_REG_DATA_RATE = 0x0E,
  TR245A_A7105_REG_CHANNEL = 0x0F,
  TR245A_A7105_REG_PLL_I = 0x11,
  TR245A_A7105_REG_PLL_II = 0x12,
  TR245A_A7105_REG_PLL_III = 0x13,
  TR245A_A7105_REG_IF_CALIBRATION = 0x22,
  TR245A_A7105_REG_VCO_CURRENT_CALIBRATION = 0x24,
  TR245A_A7105_REG_VCO_BANK_CALIBRATION = 0x25,
  TR245A_A7105_READ_FLAG = 0x40,
  TR245A_A7105_STROBE_IDLE = 0xA0,
  TR245A_A7105_STROBE_PLL = 0xB0,
  TR245A_A7105_STROBE_RX = 0xC0,
  TR245A_A7105_STROBE_TX = 0xD0,
  TR245A_A7105_STROBE_FIFO_WRITE = 0xE0,
  TR245A_A7105_STROBE_FIFO_READ = 0xF0,
  TR245A_A7105_MAX_TRANSFER_DATA = 64,
  TR245A_A7105_MAX_INIT_TRANSFERS = 3,
  TR245A_A7105_MAX_BURSTS = 1024,
  TR245A_A7105_MAX_FREQUENCY_OFFSET_HZ = 500000,
  TR245A_A7105_RESET_DELAY_US = 10000,
  TR245A_A7105_SETTLING_US = 150,
  TR245A_A7105_CALIBRATION_TIMEOUT_US = 100000,
  TR245A_A7105_TX_TIMEOUT_US = 2000,
  TR245A_A7105_RX_REARM_US = 60000,
};

static const uint8_t TR245A_A7105_INIT[0x33] = {
  0x00,0x62,0x00,0x08,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x19,0x05,0x00,0x14,
  0x9E,0x4B,0x00,0x02,0x16,0x2B,0x12,0x4B,
  0x62,0x80,0x80,0x00,0x0A,0x32,0xC3,0x07,
  0x13,0xC3,0x00,0x00,0x00,0x00,0x3B,0x00,
  0x1F,0x47,0x80,0x03,0x01,0x45,0x18,0x00,
  0x01,0x0F,0x00
};

static inline bool tr245a_radio_due(uint32_t now, uint32_t deadline) {
  return (int32_t)(now - deadline) >= 0;
}
static inline bool tr245a_radio_transfer(tr245a_radio_t *r, const uint8_t *tx, uint8_t *rx, size_t len) {
  if (!r->io.exchange(r->io.context, tx, rx, len)) { r->runtime.error = TR245A_RADIO_SPI; return false; }
  return true;
}
static inline bool tr245a_radio_strobe(tr245a_radio_t *r, uint8_t command) {
  uint8_t rx = 0;
  return tr245a_radio_transfer(r, &command, &rx, 1);
}
static inline bool tr245a_radio_write(tr245a_radio_t *r, uint8_t reg, uint8_t value) {
  const uint8_t tx[2] = {reg, value}; uint8_t rx[2];
  return tr245a_radio_transfer(r, tx, rx, 2);
}
static inline bool tr245a_radio_read(tr245a_radio_t *r, uint8_t reg, uint8_t *value) {
  const uint8_t tx[2] = {(uint8_t)(reg | TR245A_A7105_READ_FLAG), 0}; uint8_t rx[2];
  if (!tr245a_radio_transfer(r, tx, rx, 2)) return false;
  *value = rx[1]; return true;
}

/* A7105 v1.5 section 9.2.1: MODE bit 7 is unimplemented, CER/XER are
 * bits 4/3, and TRER/TRSR are bits 0/1. The operational RX/TX paths keep
 * the chip and crystal enabled, including the automatic return to Standby.
 * Do not require PLLER: it is not a lock indication. CRC/FEC flags can be
 * retained and must not invalidate an otherwise usable status byte. */
static inline bool tr245a_radio_operating_mode_valid(uint8_t mode) {
  return (mode & 0x98) == 0x18;
}
static inline bool tr245a_radio_read_operating_mode(tr245a_radio_t *r, bool transmit, uint8_t *mode) {
  if (!tr245a_radio_read(r, TR245A_A7105_REG_MODE, mode)) return false;
  if (!tr245a_radio_operating_mode_valid(*mode) ||
      ((*mode & 1) != 0 && (((*mode & 2) != 0) != transmit))) {
    r->runtime.error = TR245A_RADIO_STATUS;
    return false;
  }
  return true;
}
static inline bool tr245a_radio_write_bytes(tr245a_radio_t *r, uint8_t reg,
                                            const uint8_t *bytes, size_t len) {
  if (len > TR245A_A7105_MAX_TRANSFER_DATA) { r->runtime.error = TR245A_RADIO_ARGUMENT; return false; }
  uint8_t tx[65], rx[65]; tx[0] = reg; memcpy(tx + 1, bytes, len);
  return tr245a_radio_transfer(r, tx, rx, len + 1);
}

static inline tr245a_radio_tick_result_t tr245a_radio_failure(tr245a_radio_t *r,
                                                              tr245a_radio_result_t error) {
  tr245a_radio_tick_result_t result = tr245a_radio_no_result();
  if (error == TR245A_RADIO_OK) error = TR245A_RADIO_SPI;
  r->runtime.error = error;
  r->io.set_front_end(r->io.context, TR245A_FRONT_END_OFF);
  {
    tr245a_radio_result_t saved = r->runtime.error;
    (void) tr245a_radio_strobe(r, TR245A_A7105_STROBE_IDLE);
    r->runtime.error = saved;
  }
  result.error = error;
  if (r->runtime.state == TR245A_RADIO_TX) {
    result.tx = r->runtime.pending_tx != TR245A_TX_NONE ? r->runtime.pending_tx : TR245A_TX_ABORTED;
    result.completed_bursts = r->runtime.tx_completed_bursts;
    result.strobe_attempted = r->runtime.strobe_attempted;
  }
  r->runtime.pending_tx = TR245A_TX_NONE;
  r->runtime.state = TR245A_RADIO_ERROR;
  return result;
}

#endif
