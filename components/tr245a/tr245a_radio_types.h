#ifndef TR245A_RADIO_TYPES_H
#define TR245A_RADIO_TYPES_H

#include "tr245a_protocol.h"

typedef enum {
  TR245A_RADIO_OK = 0, TR245A_RADIO_ARGUMENT, TR245A_RADIO_SPI,
  TR245A_RADIO_READBACK, TR245A_RADIO_CALIBRATION, TR245A_RADIO_TIMEOUT,
  TR245A_RADIO_STATUS,
  TR245A_RADIO_NOT_READY = 7
} tr245a_radio_result_t;
typedef enum {
  TR245A_RADIO_UNINITIALIZED, TR245A_RADIO_INITIALIZING, TR245A_RADIO_IDLE,
  TR245A_RADIO_RX, TR245A_RADIO_TX, TR245A_RADIO_ERROR, TR245A_RADIO_STOPPED
} tr245a_radio_state_t;
typedef enum { TR245A_FRONT_END_OFF, TR245A_FRONT_END_TX, TR245A_FRONT_END_RX } tr245a_front_end_mode_t;
typedef enum { TR245A_TX_NONE, TR245A_TX_COMPLETED, TR245A_TX_ABORTED } tr245a_tx_result_t;
typedef enum { TR245A_RX_NONE, TR245A_RX_FRAME, TR245A_RX_INVALID } tr245a_rx_result_t;

typedef struct {
  void *context;
  bool (*exchange)(void *, const uint8_t *, uint8_t *, size_t);
  uint32_t (*now_us)(void *);
  void (*set_front_end)(void *, tr245a_front_end_mode_t);
} tr245a_radio_io_t;
typedef struct { int32_t frequency_offset_hz; } tr245a_radio_config_t;

typedef enum {
  TR245A_INIT_RESET, TR245A_INIT_RESET_WAIT, TR245A_INIT_REGISTERS,
  TR245A_INIT_ACCESS_CODE, TR245A_INIT_READ_CHANNEL, TR245A_INIT_READ_RATE,
  TR245A_INIT_READ_ID, TR245A_INIT_FREQUENCY, TR245A_INIT_CAL_IDLE,
  TR245A_INIT_CAL_START, TR245A_INIT_CAL_PLL_WAIT, TR245A_INIT_CAL_WAIT,
  TR245A_INIT_CAL_IF_FLAG, TR245A_INIT_CAL_VCO_CURRENT_FLAG,
  TR245A_INIT_CAL_VCO_BANK_FLAG, TR245A_INIT_FINISH
} tr245a_radio_init_phase_t;
typedef enum {
  TR245A_TX_PLL_START, TR245A_TX_PLL_WAIT, TR245A_TX_FIFO_PREPARE,
  TR245A_TX_BURST_WAIT, TR245A_TX_STROBE, TR245A_TX_STATUS_WAIT,
  TR245A_TX_FINISH_IDLE, TR245A_TX_RESUME_IDLE, TR245A_TX_RESUME_RX
} tr245a_radio_tx_phase_t;
typedef enum { TR245A_RX_WAIT, TR245A_RX_REARM_IDLE, TR245A_RX_REARM_STROBES } tr245a_radio_rx_phase_t;

typedef struct {
  tr245a_radio_state_t state;
  tr245a_radio_result_t error;
  tr245a_radio_init_phase_t init_phase;
  tr245a_radio_tx_phase_t tx_phase;
  tr245a_radio_rx_phase_t rx_phase;
  uint32_t deadline_us, rx_started_us, tx_started_us;
  uint16_t tx_requested_bursts, tx_completed_bursts;
  uint8_t init_register, calibration_bit;
  uint8_t frame[TR245A_FRAME_SIZE];
  tr245a_tx_result_t pending_tx;
  bool resume_rx, strobe_attempted, observed_busy, first_sample, first_late;
} tr245a_radio_runtime_t;

typedef struct {
  tr245a_tx_result_t tx;
  tr245a_radio_result_t error;
  tr245a_rx_result_t rx;
  uint8_t frame[TR245A_FRAME_SIZE];
  uint16_t completed_bursts;
  bool strobe_attempted;
} tr245a_radio_tick_result_t;

typedef struct {
  tr245a_radio_io_t io;
  tr245a_radio_config_t config;
  tr245a_radio_runtime_t runtime;
} tr245a_radio_t;

#endif
