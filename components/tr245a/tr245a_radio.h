#ifndef TR245A_RADIO_H
#define TR245A_RADIO_H

/* Shared, nonblocking A7105 core. The FIFO contains payload + software CRC
 * (9 bytes). COMPLETED means local TX completion, not receiver acknowledgement. */
#include "tr245a_radio_types.h"

static inline const char *tr245a_radio_error(tr245a_radio_result_t error) {
  switch (error) {
    case TR245A_RADIO_OK: return "ok";
    case TR245A_RADIO_ARGUMENT: return "invalid_argument";
    case TR245A_RADIO_SPI: return "spi_failed";
    case TR245A_RADIO_READBACK: return "readback_failed";
    case TR245A_RADIO_CALIBRATION: return "calibration_failed";
    case TR245A_RADIO_TIMEOUT: return "state_timeout";
    case TR245A_RADIO_STATUS: return "unexpected_mode";
    default: return "not_ready";
  }
}
static inline tr245a_radio_tick_result_t tr245a_radio_no_result(void) {
  tr245a_radio_tick_result_t result;
  memset(&result, 0, sizeof(result));
  result.error = TR245A_RADIO_OK;
  return result;
}
#include "tr245a_radio_internal.h"

static inline bool tr245a_radio_configure(tr245a_radio_t *r, tr245a_radio_io_t io,
                                          int32_t offset_hz) {
  if (r == NULL) return false;
  tr245a_radio_runtime_t runtime;
  memset(&runtime, 0, sizeof(runtime));
  runtime.state = TR245A_RADIO_UNINITIALIZED;
  runtime.error = TR245A_RADIO_OK;
  r->io = io; r->config.frequency_offset_hz = offset_hz; r->runtime = runtime;
  if (io.exchange == NULL || io.now_us == NULL || io.set_front_end == NULL ||
      offset_hz < -TR245A_A7105_MAX_FREQUENCY_OFFSET_HZ || offset_hz > TR245A_A7105_MAX_FREQUENCY_OFFSET_HZ) {
    r->runtime.error = TR245A_RADIO_ARGUMENT; return false;
  }
  return true;
}
static inline bool tr245a_radio_is_ready(const tr245a_radio_t *r) {
  return r != NULL && (r->runtime.state == TR245A_RADIO_IDLE || r->runtime.state == TR245A_RADIO_RX ||
                       r->runtime.state == TR245A_RADIO_TX);
}
static inline bool tr245a_radio_is_transmitting(const tr245a_radio_t *r) {
  return r != NULL && r->runtime.state == TR245A_RADIO_TX;
}
static inline bool tr245a_radio_is_listening(const tr245a_radio_t *r) {
  return r != NULL && r->runtime.state == TR245A_RADIO_RX;
}
static inline tr245a_radio_result_t tr245a_radio_last_error(const tr245a_radio_t *r) {
  return r == NULL ? TR245A_RADIO_ARGUMENT : r->runtime.error;
}

static inline bool tr245a_radio_init_begin(tr245a_radio_t *r) {
  if (r == NULL || r->io.exchange == NULL || r->io.now_us == NULL || r->io.set_front_end == NULL ||
      r->config.frequency_offset_hz < -TR245A_A7105_MAX_FREQUENCY_OFFSET_HZ ||
      r->config.frequency_offset_hz > TR245A_A7105_MAX_FREQUENCY_OFFSET_HZ ||
      r->runtime.state == TR245A_RADIO_TX) {
    if (r != NULL) r->runtime.error = TR245A_RADIO_ARGUMENT;
    return false;
  }
  tr245a_radio_runtime_t runtime;
  memset(&runtime, 0, sizeof(runtime));
  runtime.state = TR245A_RADIO_INITIALIZING; runtime.error = TR245A_RADIO_OK;
  runtime.init_phase = TR245A_INIT_RESET; runtime.init_register = 1; runtime.calibration_bit = 1;
  r->runtime = runtime;
  r->io.set_front_end(r->io.context, TR245A_FRONT_END_OFF);
  return true;
}

static inline tr245a_radio_tick_result_t tr245a_radio_tick_init(tr245a_radio_t *r) {
  tr245a_radio_tick_result_t result = tr245a_radio_no_result(); uint8_t value;
  switch (r->runtime.init_phase) {
    case TR245A_INIT_RESET:
      if (!tr245a_radio_write(r, TR245A_A7105_REG_MODE, 0)) return tr245a_radio_failure(r, r->runtime.error);
      r->runtime.deadline_us = r->io.now_us(r->io.context) + TR245A_A7105_RESET_DELAY_US;
      r->runtime.init_phase = TR245A_INIT_RESET_WAIT; break;
    case TR245A_INIT_RESET_WAIT:
      if (tr245a_radio_due(r->io.now_us(r->io.context), r->runtime.deadline_us))
        r->runtime.init_phase = TR245A_INIT_REGISTERS;
      break;
    case TR245A_INIT_REGISTERS: {
      unsigned transfers = 0;
      while (r->runtime.init_register < sizeof(TR245A_A7105_INIT) &&
             transfers < TR245A_A7105_MAX_INIT_TRANSFERS) {
        uint8_t reg = r->runtime.init_register++;
        if (reg == TR245A_A7105_REG_FIFO_DATA || reg == TR245A_A7105_REG_ID_DATA) continue;
        ++transfers;
        if (!tr245a_radio_write(r, reg, TR245A_A7105_INIT[reg]))
          return tr245a_radio_failure(r, r->runtime.error);
      }
      if (r->runtime.init_register >= sizeof(TR245A_A7105_INIT)) r->runtime.init_phase = TR245A_INIT_ACCESS_CODE;
      break;
    }
    case TR245A_INIT_ACCESS_CODE:
      if (!tr245a_radio_write_bytes(r, TR245A_A7105_REG_ID_DATA, TR245A_ACCESS_CODE, 4)) return tr245a_radio_failure(r, r->runtime.error);
      r->runtime.init_phase = TR245A_INIT_READ_CHANNEL; break;
    case TR245A_INIT_READ_CHANNEL:
      if (!tr245a_radio_read(r, TR245A_A7105_REG_CHANNEL, &value)) return tr245a_radio_failure(r, r->runtime.error);
      if (value != 0x14) return tr245a_radio_failure(r, TR245A_RADIO_READBACK);
      r->runtime.init_phase = TR245A_INIT_READ_RATE; break;
    case TR245A_INIT_READ_RATE:
      if (!tr245a_radio_read(r, TR245A_A7105_REG_DATA_RATE, &value)) return tr245a_radio_failure(r, r->runtime.error);
      if (value != 0) return tr245a_radio_failure(r, TR245A_RADIO_READBACK);
      r->runtime.init_phase = TR245A_INIT_READ_ID; break;
    case TR245A_INIT_READ_ID: {
      const uint8_t tx[5] = {(uint8_t) (TR245A_A7105_REG_ID_DATA | TR245A_A7105_READ_FLAG),0,0,0,0}; uint8_t rx[5];
      if (!tr245a_radio_transfer(r, tx, rx, 5)) return tr245a_radio_failure(r, r->runtime.error);
      if (memcmp(rx + 1, TR245A_ACCESS_CODE, 4) != 0) return tr245a_radio_failure(r, TR245A_RADIO_READBACK);
      r->runtime.init_phase = TR245A_INIT_FREQUENCY; break;
    }
    case TR245A_INIT_FREQUENCY: {
      int64_t numerator = (int64_t)r->config.frequency_offset_hz * 65536;
      int64_t delta = (numerator + (numerator >= 0 ? 16000000 : -16000000)) / 32000000;
      uint32_t word = (uint32_t)((75LL << 16) + 2 + delta);
      if (!tr245a_radio_write(r, TR245A_A7105_REG_PLL_I, (uint8_t)(word >> 16)) ||
          !tr245a_radio_write(r, TR245A_A7105_REG_PLL_II, (uint8_t)(word >> 8)) || !tr245a_radio_write(r, TR245A_A7105_REG_PLL_III, (uint8_t)word))
        return tr245a_radio_failure(r, r->runtime.error);
      r->runtime.init_phase = TR245A_INIT_CAL_IDLE; break;
    }
    case TR245A_INIT_CAL_IDLE:
      if (!tr245a_radio_strobe(r, TR245A_A7105_STROBE_IDLE)) return tr245a_radio_failure(r, r->runtime.error);
      r->runtime.init_phase = TR245A_INIT_CAL_START; break;
    case TR245A_INIT_CAL_START:
      if (r->runtime.calibration_bit == 2) {
        if (!tr245a_radio_strobe(r, TR245A_A7105_STROBE_PLL)) return tr245a_radio_failure(r, r->runtime.error);
        r->runtime.deadline_us = r->io.now_us(r->io.context) + TR245A_A7105_SETTLING_US;
        r->runtime.init_phase = TR245A_INIT_CAL_PLL_WAIT;
      } else {
        uint8_t bit = r->runtime.calibration_bit;
        if (!tr245a_radio_write(r, TR245A_A7105_REG_CALIBRATION, bit)) return tr245a_radio_failure(r, r->runtime.error);
        r->runtime.deadline_us = r->io.now_us(r->io.context) + TR245A_A7105_CALIBRATION_TIMEOUT_US;
        r->runtime.init_phase = TR245A_INIT_CAL_WAIT;
      }
      break;
    case TR245A_INIT_CAL_PLL_WAIT:
      if (!tr245a_radio_due(r->io.now_us(r->io.context), r->runtime.deadline_us)) break;
      if (!tr245a_radio_write(r, TR245A_A7105_REG_CALIBRATION, 2)) return tr245a_radio_failure(r, r->runtime.error);
      r->runtime.deadline_us = r->io.now_us(r->io.context) + TR245A_A7105_CALIBRATION_TIMEOUT_US;
      r->runtime.init_phase = TR245A_INIT_CAL_WAIT; break;
    case TR245A_INIT_CAL_WAIT:
      if (!tr245a_radio_read(r, TR245A_A7105_REG_CALIBRATION, &value)) return tr245a_radio_failure(r, r->runtime.error);
      if ((value & r->runtime.calibration_bit) == 0) {
        r->runtime.calibration_bit = (uint8_t)(r->runtime.calibration_bit << 1);
        r->runtime.init_phase = r->runtime.calibration_bit <= 4 ? TR245A_INIT_CAL_START : TR245A_INIT_CAL_IF_FLAG;
      } else if (tr245a_radio_due(r->io.now_us(r->io.context), r->runtime.deadline_us))
        return tr245a_radio_failure(r, TR245A_RADIO_CALIBRATION);
      break;
    case TR245A_INIT_CAL_IF_FLAG:
      if (!tr245a_radio_read(r, TR245A_A7105_REG_IF_CALIBRATION, &value)) return tr245a_radio_failure(r, r->runtime.error);
      if (value & 0x10) return tr245a_radio_failure(r, TR245A_RADIO_CALIBRATION);
      r->runtime.init_phase = TR245A_INIT_CAL_VCO_CURRENT_FLAG; break;
    case TR245A_INIT_CAL_VCO_CURRENT_FLAG:
      if (!tr245a_radio_read(r, TR245A_A7105_REG_VCO_CURRENT_CALIBRATION, &value)) return tr245a_radio_failure(r, r->runtime.error);
      if (value & 0x10) return tr245a_radio_failure(r, TR245A_RADIO_CALIBRATION);
      r->runtime.init_phase = TR245A_INIT_CAL_VCO_BANK_FLAG; break;
    case TR245A_INIT_CAL_VCO_BANK_FLAG:
      if (!tr245a_radio_read(r, TR245A_A7105_REG_VCO_BANK_CALIBRATION, &value)) return tr245a_radio_failure(r, r->runtime.error);
      if (value & 0x08) return tr245a_radio_failure(r, TR245A_RADIO_CALIBRATION);
      r->runtime.init_phase = TR245A_INIT_FINISH; break;
    case TR245A_INIT_FINISH:
      r->io.set_front_end(r->io.context, TR245A_FRONT_END_OFF);
      if (!tr245a_radio_strobe(r, TR245A_A7105_STROBE_IDLE)) return tr245a_radio_failure(r, r->runtime.error);
      r->runtime.state = TR245A_RADIO_IDLE; break;
  }
  return result;
}

static inline bool tr245a_radio_rx_begin(tr245a_radio_t *r) {
  if (r == NULL || r->runtime.state != TR245A_RADIO_IDLE) {
    if (r != NULL) r->runtime.error = TR245A_RADIO_NOT_READY;
    return false;
  }
  r->runtime.error = TR245A_RADIO_OK;
  r->io.set_front_end(r->io.context, TR245A_FRONT_END_OFF);
  if (!tr245a_radio_strobe(r, TR245A_A7105_STROBE_IDLE)) goto failed;
  r->io.set_front_end(r->io.context, TR245A_FRONT_END_RX);
  if (!tr245a_radio_strobe(r, TR245A_A7105_STROBE_FIFO_READ) || !tr245a_radio_strobe(r, TR245A_A7105_STROBE_RX)) goto failed;
  r->runtime.rx_started_us = r->io.now_us(r->io.context);
  r->runtime.rx_phase = TR245A_RX_WAIT; r->runtime.state = TR245A_RADIO_RX; return true;
failed:
  (void)tr245a_radio_failure(r, r->runtime.error); return false;
}

static inline bool tr245a_radio_tx_begin(tr245a_radio_t *r, const uint8_t frame[TR245A_FRAME_SIZE],
                                         uint16_t bursts) {
  if (r == NULL || frame == NULL) return false;
  if (r->runtime.state != TR245A_RADIO_IDLE && r->runtime.state != TR245A_RADIO_RX) {
    r->runtime.error = TR245A_RADIO_NOT_READY;
    return false;
  }
  if (r->runtime.state == TR245A_RADIO_RX &&
      r->runtime.rx_phase != TR245A_RX_WAIT) {
    r->runtime.error = TR245A_RADIO_NOT_READY;
    return false;
  }
  if (!tr245a_frame_valid(frame, TR245A_FRAME_SIZE) || bursts < 1 || bursts > TR245A_A7105_MAX_BURSTS) {
    r->runtime.error = TR245A_RADIO_ARGUMENT; return false;
  }
  r->runtime.error = TR245A_RADIO_OK;
  memcpy(r->runtime.frame, frame, TR245A_FRAME_SIZE);
  r->runtime.tx_requested_bursts = bursts; r->runtime.tx_completed_bursts = 0;
  r->runtime.resume_rx = r->runtime.state == TR245A_RADIO_RX;
  r->runtime.strobe_attempted = false; r->runtime.pending_tx = TR245A_TX_NONE;
  r->runtime.tx_phase = TR245A_TX_PLL_START;
  r->runtime.state = TR245A_RADIO_TX; return true;
}

static inline tr245a_radio_tick_result_t tr245a_radio_tick_tx(tr245a_radio_t *r) {
  tr245a_radio_tick_result_t result = tr245a_radio_no_result(); uint8_t state;
  switch (r->runtime.tx_phase) {
    case TR245A_TX_PLL_START:
      r->io.set_front_end(r->io.context, TR245A_FRONT_END_OFF);
      if (r->runtime.resume_rx && !tr245a_radio_strobe(r, TR245A_A7105_STROBE_IDLE)) return tr245a_radio_failure(r, r->runtime.error);
      if (!tr245a_radio_strobe(r, TR245A_A7105_STROBE_PLL)) return tr245a_radio_failure(r, r->runtime.error);
      r->runtime.deadline_us = r->io.now_us(r->io.context) + TR245A_A7105_SETTLING_US;
      r->runtime.tx_phase = TR245A_TX_PLL_WAIT; break;
    case TR245A_TX_PLL_WAIT:
      if (!tr245a_radio_due(r->io.now_us(r->io.context), r->runtime.deadline_us)) break;
      r->io.set_front_end(r->io.context, TR245A_FRONT_END_TX);
      r->runtime.deadline_us = r->io.now_us(r->io.context);
      r->runtime.tx_phase = TR245A_TX_FIFO_PREPARE; break;
    case TR245A_TX_FIFO_PREPARE:
      if (!tr245a_radio_strobe(r, TR245A_A7105_STROBE_FIFO_WRITE) || !tr245a_radio_write_bytes(r, TR245A_A7105_REG_FIFO_DATA, r->runtime.frame + 8, TR245A_FIFO_SIZE))
        return tr245a_radio_failure(r, r->runtime.error);
      r->runtime.tx_phase = TR245A_TX_BURST_WAIT; break;
    case TR245A_TX_BURST_WAIT:
      if (tr245a_radio_due(r->io.now_us(r->io.context), r->runtime.deadline_us)) r->runtime.tx_phase = TR245A_TX_STROBE;
      break;
    case TR245A_TX_STROBE:
      r->runtime.tx_started_us = r->io.now_us(r->io.context); r->runtime.strobe_attempted = true;
      if (!tr245a_radio_strobe(r, TR245A_A7105_STROBE_TX)) return tr245a_radio_failure(r, r->runtime.error);
      r->runtime.deadline_us = r->io.now_us(r->io.context) + TR245A_A7105_TX_TIMEOUT_US;
      r->runtime.observed_busy = false; r->runtime.first_sample = true; r->runtime.first_late = false;
      r->runtime.tx_phase = TR245A_TX_STATUS_WAIT; break;
    case TR245A_TX_STATUS_WAIT: {
      const uint32_t minimum_us = TR245A_FRAME_SIZE * 8U * 1000000U / TR245A_BAUD;
      // A task can be preempted after SPI sampled busy but before now_us().
      // Only a read begun after the deadline can prove that TX is still stuck.
      const uint32_t sample_started_us = r->io.now_us(r->io.context);
      if (!tr245a_radio_read_operating_mode(r, true, &state)) return tr245a_radio_failure(r, r->runtime.error);
      uint32_t now = r->io.now_us(r->io.context), elapsed = now - r->runtime.tx_started_us;
      if (r->runtime.first_sample) { r->runtime.first_late = elapsed >= minimum_us; r->runtime.first_sample = false; }
      if ((state & 1) != 0) r->runtime.observed_busy = true;
      if ((state & 1) == 0 && elapsed >= minimum_us && (r->runtime.observed_busy || r->runtime.first_late)) {
        ++r->runtime.tx_completed_bursts;
        r->runtime.deadline_us = r->runtime.tx_started_us + TR245A_BURST_PERIOD_US;
        if (r->runtime.tx_completed_bursts < r->runtime.tx_requested_bursts)
          r->runtime.tx_phase = TR245A_TX_FIFO_PREPARE;
        else { r->runtime.pending_tx = TR245A_TX_COMPLETED; r->runtime.tx_phase = TR245A_TX_FINISH_IDLE; }
      } else if (tr245a_radio_due(sample_started_us, r->runtime.deadline_us))
        return tr245a_radio_failure(r, TR245A_RADIO_TIMEOUT);
      break;
    }
    case TR245A_TX_FINISH_IDLE:
      r->io.set_front_end(r->io.context, TR245A_FRONT_END_OFF);
      if (!tr245a_radio_strobe(r, TR245A_A7105_STROBE_IDLE)) return tr245a_radio_failure(r, r->runtime.error);
      if (r->runtime.resume_rx) r->runtime.tx_phase = TR245A_TX_RESUME_IDLE;
      else {
        result.tx = r->runtime.pending_tx; result.completed_bursts = r->runtime.tx_completed_bursts;
        result.strobe_attempted = r->runtime.strobe_attempted; r->runtime.pending_tx = TR245A_TX_NONE;
        r->runtime.state = TR245A_RADIO_IDLE;
      }
      break;
    case TR245A_TX_RESUME_IDLE:
      if (!tr245a_radio_strobe(r, TR245A_A7105_STROBE_IDLE)) return tr245a_radio_failure(r, r->runtime.error);
      r->io.set_front_end(r->io.context, TR245A_FRONT_END_RX);
      r->runtime.tx_phase = TR245A_TX_RESUME_RX; break;
    case TR245A_TX_RESUME_RX:
      if (!tr245a_radio_strobe(r, TR245A_A7105_STROBE_FIFO_READ) || !tr245a_radio_strobe(r, TR245A_A7105_STROBE_RX)) return tr245a_radio_failure(r, r->runtime.error);
      r->runtime.rx_started_us = r->io.now_us(r->io.context);
      result.tx = r->runtime.pending_tx; result.completed_bursts = r->runtime.tx_completed_bursts;
      result.strobe_attempted = r->runtime.strobe_attempted; r->runtime.pending_tx = TR245A_TX_NONE;
      r->runtime.rx_phase = TR245A_RX_WAIT; r->runtime.state = TR245A_RADIO_RX; break;
  }
  return result;
}

static inline tr245a_radio_tick_result_t tr245a_radio_tick_rx(tr245a_radio_t *r) {
  tr245a_radio_tick_result_t result = tr245a_radio_no_result(); uint8_t mode;
  switch (r->runtime.rx_phase) {
    case TR245A_RX_WAIT: {
      uint32_t elapsed = r->io.now_us(r->io.context) - r->runtime.rx_started_us;
      if (elapsed < TR245A_A7105_SETTLING_US) break;
      if (!tr245a_radio_read_operating_mode(r, false, &mode)) return tr245a_radio_failure(r, r->runtime.error);
      if (mode & 1) { if (elapsed >= TR245A_A7105_RX_REARM_US) r->runtime.rx_phase = TR245A_RX_REARM_IDLE; break; }
      {
        const uint8_t tx[10] = {(uint8_t) (TR245A_A7105_REG_FIFO_DATA | TR245A_A7105_READ_FLAG),0,0,0,0,0,0,0,0,0}; uint8_t rx[10];
        if (!tr245a_radio_strobe(r, TR245A_A7105_STROBE_FIFO_READ) || !tr245a_radio_transfer(r, tx, rx, 10))
          return tr245a_radio_failure(r, r->runtime.error);
        memset(result.frame, 0x55, 4);
        memcpy(result.frame + 4, TR245A_ACCESS_CODE, 4);
        memcpy(result.frame + 8, rx + 1, 9);
        result.rx = tr245a_frame_valid(result.frame, TR245A_FRAME_SIZE)
                        ? TR245A_RX_FRAME : TR245A_RX_INVALID;
        r->runtime.rx_phase = TR245A_RX_REARM_IDLE;
      }
      break;
    }
    case TR245A_RX_REARM_IDLE:
      r->io.set_front_end(r->io.context, TR245A_FRONT_END_OFF);
      if (!tr245a_radio_strobe(r, TR245A_A7105_STROBE_IDLE)) return tr245a_radio_failure(r, r->runtime.error);
      r->io.set_front_end(r->io.context, TR245A_FRONT_END_RX);
      r->runtime.rx_phase = TR245A_RX_REARM_STROBES; break;
    case TR245A_RX_REARM_STROBES:
      if (!tr245a_radio_strobe(r, TR245A_A7105_STROBE_FIFO_READ) || !tr245a_radio_strobe(r, TR245A_A7105_STROBE_RX)) return tr245a_radio_failure(r, r->runtime.error);
      r->runtime.rx_started_us = r->io.now_us(r->io.context);
      r->runtime.rx_phase = TR245A_RX_WAIT; break;
  }
  return result;
}

static inline tr245a_radio_tick_result_t tr245a_radio_tick(tr245a_radio_t *r) {
  if (r == NULL) { tr245a_radio_tick_result_t result = tr245a_radio_no_result(); result.error = TR245A_RADIO_ARGUMENT; return result; }
  if (r->runtime.state == TR245A_RADIO_INITIALIZING) return tr245a_radio_tick_init(r);
  if (r->runtime.state == TR245A_RADIO_TX) return tr245a_radio_tick_tx(r);
  if (r->runtime.state == TR245A_RADIO_RX) return tr245a_radio_tick_rx(r);
  return tr245a_radio_no_result();
}

static inline tr245a_radio_tick_result_t tr245a_radio_stop(tr245a_radio_t *r) {
  if (r == NULL) { tr245a_radio_tick_result_t result = tr245a_radio_no_result(); result.error = TR245A_RADIO_ARGUMENT; return result; }
  tr245a_radio_tick_result_t result = tr245a_radio_no_result();
  if (r->io.set_front_end == NULL) {
    result.error = TR245A_RADIO_ARGUMENT;
    r->runtime.error = TR245A_RADIO_ARGUMENT;
    r->runtime.state = TR245A_RADIO_STOPPED;
    return result;
  }
  if (r->runtime.state == TR245A_RADIO_TX) {
    result.tx = r->runtime.pending_tx != TR245A_TX_NONE ? r->runtime.pending_tx : TR245A_TX_ABORTED;
    result.completed_bursts = r->runtime.tx_completed_bursts; result.strobe_attempted = r->runtime.strobe_attempted;
  }
  r->io.set_front_end(r->io.context, TR245A_FRONT_END_OFF);
  if (r->io.exchange != NULL) {
    tr245a_radio_result_t saved = r->runtime.error;
    if (!tr245a_radio_strobe(r, TR245A_A7105_STROBE_IDLE)) result.error = r->runtime.error;
    r->runtime.error = saved;
  }
  r->runtime.state = TR245A_RADIO_STOPPED;
  r->runtime.pending_tx = TR245A_TX_NONE;
  return result;
}

#endif
