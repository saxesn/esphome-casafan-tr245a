#ifndef TR245A_RECEIVER_H
#define TR245A_RECEIVER_H
#include "tr245a_protocol.h"

typedef struct {
  uint16_t address;
  uint8_t command, value, speed;
} tr245a_received_t;

/* A CRC alone is not a supported command. Unknown flags/values never change HA. */
static inline bool tr245a_decode_received(const uint8_t frame[17], tr245a_received_t *out) {
  if (!out || !tr245a_frame_valid(frame, 17) || !tr245a_payload_valid(frame + 8) ||
      frame[13] != 0 || frame[14] != 0x80) return false;
  uint8_t command = frame[10], value = frame[11];
  if (!((command == TR245A_OFF && value == 0) ||
        (command == TR245A_SPEED && value >= 1 && value <= 6) ||
        (command == TR245A_REVERSE && value == 1) ||
        (command == TR245A_PAIR && value == 255))) return false;
  out->address = (uint16_t)((frame[8] << 8) | frame[9]);
  out->command = command;
  out->value = value;
  out->speed = command == TR245A_SPEED ? (uint8_t)(7 - value) : 0;
  return true;
}

/* Remember the latest command per remote address, including CRC. In particular,
 * eleven REVERSE bursts mean one toggle, even when two remotes interleave.
 * Repeated presses are distinguishable only by the observed RF silence. */
typedef struct {
  uint8_t fifo[9];
  uint32_t last_ms;
  bool used;
} tr245a_rx_seen_t;
typedef struct { tr245a_rx_seen_t entries[4]; } tr245a_rx_dedup_t;

static inline bool tr245a_rx_is_new(tr245a_rx_dedup_t *seen, const uint8_t frame[17],
                                    uint32_t now_ms, uint32_t gap_ms) {
  unsigned slot = 0;
  uint32_t oldest = 0;
  for (unsigned i = 0; i < 4; ++i) {
    tr245a_rx_seen_t *entry = &seen->entries[i];
    if (entry->used && memcmp(entry->fifo, frame + 8, 2) == 0) {
      bool duplicate = memcmp(entry->fifo, frame + 8, 9) == 0 &&
                       (uint32_t)(now_ms - entry->last_ms) < gap_ms;
      memcpy(entry->fifo, frame + 8, 9);
      entry->last_ms = now_ms;
      return !duplicate;
    }
    if (!entry->used) { slot = i; oldest = UINT32_MAX; }
    else if (oldest != UINT32_MAX && (uint32_t)(now_ms - entry->last_ms) >= oldest) {
      oldest = now_ms - entry->last_ms;
      slot = i;
    }
  }
  memcpy(seen->entries[slot].fifo, frame + 8, 9);
  seen->entries[slot].last_ms = now_ms;
  seen->entries[slot].used = true;
  return true;
}
#endif
