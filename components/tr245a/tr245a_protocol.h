#ifndef TR245A_WIRE_PROTOCOL_H
#define TR245A_WIRE_PROTOCOL_H

/* TR245A packet layout and CRC. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TR245A_FRAME_SIZE 17U
#define TR245A_PAYLOAD_SIZE 7U
#define TR245A_FIFO_SIZE 9U
#define TR245A_BAUD 500000U
#define TR245A_BURSTS 11U
/* Measured interval between packet starts: 4876.7 us. */
#define TR245A_BURST_PERIOD_US 4876U

enum { TR245A_OFF = 0x00, TR245A_SPEED = 0x10, TR245A_REVERSE = 0x20, TR245A_PAIR = 0xFF };
static const uint8_t TR245A_ACCESS_CODE[4] = {0x54, 0x75, 0xC5, 0x2A};

static inline uint16_t tr245a_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0x1D0F;
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
  }
  return crc;
}

static inline void tr245a_payload(uint16_t address, uint8_t command, uint8_t value, uint8_t out[7]) {
  out[0] = (uint8_t)(address >> 8);
  out[1] = (uint8_t)address;
  out[2] = command;
  out[3] = value;
  out[4] = (uint8_t)(out[0] + out[1] + command + value);
  out[5] = 0x00;  /* observed flags; meaning not generalized */
  out[6] = 0x80;
}

static inline bool tr245a_speed_payload(uint16_t address, uint8_t speed, uint8_t out[7]) {
  if (speed < 1 || speed > 6 || out == NULL) return false;
  tr245a_payload(address, TR245A_SPEED, (uint8_t)(7 - speed), out);
  return true;
}

static inline void tr245a_frame(const uint8_t payload[7], uint8_t out[17]) {
  memset(out, 0x55, 4);
  memcpy(out + 4, TR245A_ACCESS_CODE, 4);
  memcpy(out + 8, payload, 7);
  uint16_t crc = tr245a_crc16(payload, 7);
  out[15] = (uint8_t)(crc >> 8);
  out[16] = (uint8_t)crc;
}

static inline bool tr245a_frame_valid(const uint8_t *frame, size_t len) {
  if (frame == NULL || len != 17) return false;
  for (unsigned i = 0; i < 4; ++i) if (frame[i] != 0x55) return false;
  if (memcmp(frame + 4, TR245A_ACCESS_CODE, 4) != 0) return false;
  return tr245a_crc16(frame + 8, 7) == (uint16_t)((frame[15] << 8) | frame[16]);
}

static inline bool tr245a_payload_valid(const uint8_t payload[7]) {
  return payload != NULL && (uint8_t)(payload[0] + payload[1] + payload[2] + payload[3]) == payload[4];
}

#endif
