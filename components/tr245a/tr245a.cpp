#include "tr245a.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tr245a {
static const char *const TAG = "tr245a";

uint32_t TR245A::now_(void *) { return micros(); }
bool TR245A::radio_exchange_(void *context, const uint8_t *tx, uint8_t *rx, size_t length) {
  auto *self = static_cast<TR245A *>(context);
  return self->transfer_(tx, rx, length);
}
bool TR245A::deadline_due_(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}
void TR245A::set_front_end_(void *context, tr245a_front_end_mode_t mode) {
  auto *self = static_cast<TR245A *>(context);
  self->set_amplifiers_(mode == TR245A_FRONT_END_TX, mode == TR245A_FRONT_END_RX);
}
void TR245A::set_amplifiers_(bool transmit, bool receive) {
  /* XL7105-D03 crosses TXEN -> A7700 RXSW and RXEN -> A7700 TXSW.
   * TX/RX are 10/01. Select SLEEP (11) for idle, rather than INHIBITED (00).
   * Always pass through the selected inactive state before changing direction:
   * raise pins first for A7700, lower pins first for generic active-high PA/LNA.
   */
  const bool off = front_end_ == FRONT_END_XL7105_D03;
  if (txen_pin_) txen_pin_->digital_write(off);
  if (rxen_pin_) rxen_pin_->digital_write(off);
  if (transmit) {
    if (off && rxen_pin_) rxen_pin_->digital_write(false);
    if (!off && txen_pin_) txen_pin_->digital_write(true);
  } else if (receive) {
    if (off && txen_pin_) txen_pin_->digital_write(false);
    if (!off && rxen_pin_) rxen_pin_->digital_write(true);
  }
}
bool TR245A::start_initialization_() {
  retry_scheduled_ = false;
  if (tr245a_radio_init_begin(&radio_)) return true;
  fail_permanently_(tr245a_radio_last_error(&radio_));
  return false;
}

void TR245A::finish_initialization_() {
  if (receive_ && !tr245a_radio_rx_begin(&radio_)) {
    tr245a_radio_tick_result_t result = tr245a_radio_no_result();
    result.error = tr245a_radio_last_error(&radio_);
    process_radio_result_(result);
    return;
  }
  retry_scheduled_ = false;
  maintenance_deadline_ms_ = millis() + reinitialize_interval_ms_;
  status_clear_error();
}
void TR245A::setup() {
  const bool off = front_end_ == FRONT_END_XL7105_D03;
  // Preload both output latches before enabling either output.
  for (auto *pin : {txen_pin_, rxen_pin_}) {
    if (pin) pin->digital_write(off);
  }
  // On XL7105-D03, drive RXEN HIGH first: enabling TXEN first could briefly
  // select TX (10) while RXEN is still LOW/floating. Then hold Sleep (11).
  for (auto *pin : {off ? rxen_pin_ : txen_pin_, off ? txen_pin_ : rxen_pin_}) {
    if (pin) { pin->setup(); pin->digital_write(off); }
  }
  spi_setup();
  tr245a_radio_io_t io = {this, radio_exchange_, now_, set_front_end_};
  if (!tr245a_radio_configure(&radio_, io, offset_hz_)) {
    fail_permanently_(tr245a_radio_last_error(&radio_));
    return;
  }
  (void) start_initialization_();
}
TR245A::Job TR245A::make_job_(const uint8_t payload[7], uint16_t bursts) const {
  Job job{};
  tr245a_frame(payload, job.frame.data());
  job.command = payload[2];
  // Extend only idempotent commands. REVERSE remains one press (a toggle).
  job.bursts = (job.command == TR245A_SPEED || job.command == TR245A_OFF)
                   ? bursts * repetitions_ : bursts;
  return job;
}
bool TR245A::enqueue_jobs_(const Job *jobs, size_t count) {
  if (count == 0) return true;
  if (!tr245a_radio_is_ready(&radio_) || !queue_.push_batch(jobs, count)) {
    ESP_LOGW(TAG, "Command rejected: radio unavailable or queue full");
    return false;
  }
  bool reverse_parity_changed = false;
  for (size_t i = 0; i < count; ++i) {
    if (jobs[i].command == TR245A_REVERSE) reverse_parity_changed = !reverse_parity_changed;
  }
  if (reverse_parity_changed && fan_reverse_plan_changed_callback_)
    fan_reverse_plan_changed_callback_(pending_reverse());
  return true;
}
bool TR245A::enqueue_(const uint8_t payload[7], uint16_t bursts) {
  auto job = make_job_(payload, bursts);
  return enqueue_jobs_(&job, 1);
}
bool TR245A::send_fan_command(bool on, uint8_t speed, bool update_state, bool reverse) {
  Job jobs[2];
  size_t count = 0;
  uint8_t payload[7];
  if (update_state) {
    if (on) {
      if (!tr245a_speed_payload(address_, speed, payload)) return false;
    } else {
      tr245a_payload(address_, TR245A_OFF, 0, payload);
    }
    jobs[count++] = make_job_(payload);
  }
  if (reverse) {
    tr245a_payload(address_, TR245A_REVERSE, 1, payload);
    jobs[count++] = make_job_(payload);
  }
  if (!enqueue_jobs_(jobs, count)) return false;
  if (update_state && on) retained_speed_ = speed;
  return true;
}
bool TR245A::pending_reverse() const {
  bool reverse = active_job_ && tx_command_ == TR245A_REVERSE;
  for (const auto &job : queue_) {
    if (job.command == TR245A_REVERSE) reverse = !reverse;
  }
  return reverse;
}
bool TR245A::send_speed(uint8_t speed) { return send_fan_command(true, speed, true, false); }
bool TR245A::send_off() { return send_fan_command(false, 1, true, false); }
bool TR245A::send_reverse() {
  uint8_t payload[7]; tr245a_payload(address_, TR245A_REVERSE, 1, payload);
  return enqueue_(payload);
}
bool TR245A::send_pair() {
  if (address_ == 0) { ESP_LOGW(TAG, "SET requires a nonzero learning address"); return false; }
  uint8_t payload[7]; tr245a_payload(address_, TR245A_PAIR, 255, payload);
  return enqueue_(payload, 822);
}
void TR245A::fail_permanently_(tr245a_radio_result_t error) {
  tr245a_radio_tick_result_t result = tr245a_radio_no_result();
  result.error = error;
  permanent_failure_ = true;
  process_radio_result_(result, true);
}

void TR245A::process_radio_result_(const tr245a_radio_tick_result_t &result, bool discard_waiting) {
  const bool has_error = result.error != TR245A_RADIO_OK;

  bool discarded_reverse = false;
  if (result.tx == TR245A_TX_COMPLETED) {
    if (active_job_) {
      if (fan_reverse_transmitted_callback_ && tx_command_ == TR245A_REVERSE)
        fan_reverse_transmitted_callback_();
      last_press_ms_ = millis();
    }
  } else if (result.tx == TR245A_TX_ABORTED && active_job_) {
    discarded_reverse = tx_command_ == TR245A_REVERSE;
  }

  if (result.rx == TR245A_RX_FRAME) receive_frame_(result.frame);

  if (has_error || discard_waiting) {
    for (const auto &job : queue_) {
      if (job.command == TR245A_REVERSE) discarded_reverse = !discarded_reverse;
    }
    queue_.clear();
  }

  // Keep surviving queued toggles in the displayed direction.
  bool remaining_reverse = false;
  if (!has_error && !discard_waiting) {
    for (const auto &job : queue_) {
      if (job.command == TR245A_REVERSE) remaining_reverse = !remaining_reverse;
    }
  }

  if (result.tx != TR245A_TX_NONE) {
    high_frequency_.stop();
  }
  if (has_error) {
    high_frequency_.stop();
    set_amplifiers_(false, false);
    status_set_error();
    if (permanent_failure_ || result.error == TR245A_RADIO_ARGUMENT ||
        result.error == TR245A_RADIO_NOT_READY) {
      permanent_failure_ = true;
      retry_scheduled_ = false;
      mark_failed();
      ESP_LOGE(TAG, "Permanent A7105 configuration/programming error: %s",
               tr245a_radio_error(result.error));
    } else if (!shutting_down_) {
      retry_deadline_ms_ = millis() + RETRY_INTERVAL_MS;
      retry_scheduled_ = true;
      ESP_LOGE(TAG, "A7105 error: %s; pending commands discarded; retrying in 60 s",
               tr245a_radio_error(result.error));
    }
  }
  if (fan_reverse_plan_changed_callback_ && discarded_reverse)
    fan_reverse_plan_changed_callback_(remaining_reverse);
  // Callbacks above still need the completed job's command.
  if (result.tx != TR245A_TX_NONE) {
    active_job_ = false;
    tx_command_ = 0;
  }
}

bool TR245A::available_for_new_job_() const {
  return receive_ ? radio_.runtime.state == TR245A_RADIO_RX &&
                        radio_.runtime.rx_phase == TR245A_RX_WAIT
                  : radio_.runtime.state == TR245A_RADIO_IDLE;
}
void TR245A::receive_frame_(const uint8_t frame[17]) {
  tr245a_received_t packet;
  if (!tr245a_decode_received(frame, &packet)) return;
  if (!tr245a_rx_is_new(&seen_, frame, millis(), dedup_ms_)) return;
  if (packet.address == address_) {
    // A later physical press supersedes older jobs that have not started.
    bool discarded_reverse = false;
    if (packet.command != TR245A_PAIR) {
      for (const auto &job : queue_) {
        if (job.command == TR245A_REVERSE) discarded_reverse = !discarded_reverse;
      }
      queue_.clear();
    }
    if (packet.command == TR245A_SPEED) retained_speed_ = packet.speed;
    // Direct publication, never FanCall: hearing the remote must not echo RF.
    if (fan_received_callback_) fan_received_callback_(packet, discarded_reverse);
  }
  receive_callbacks_.call(packet.address, packet.command, packet.value);
}
void TR245A::loop() {
  if (shutting_down_ || permanent_failure_) return;
  uint32_t now_ms = millis();
  if (radio_.runtime.state == TR245A_RADIO_ERROR ||
      radio_.runtime.state == TR245A_RADIO_UNINITIALIZED) {
    if (retry_scheduled_ && deadline_due_(now_ms, retry_deadline_ms_))
      (void) start_initialization_();
    return;
  }
  if (radio_.runtime.state == TR245A_RADIO_STOPPED) return;

  const tr245a_radio_state_t state_before = radio_.runtime.state;
  if (state_before == TR245A_RADIO_INITIALIZING || state_before == TR245A_RADIO_TX ||
      state_before == TR245A_RADIO_RX) {
    const auto result = tr245a_radio_tick(&radio_);
    process_radio_result_(result);
    if (result.error != TR245A_RADIO_OK) return;
    if (state_before == TR245A_RADIO_INITIALIZING) {
      if (radio_.runtime.state == TR245A_RADIO_IDLE) finish_initialization_();
      return;
    }
    if (state_before == TR245A_RADIO_TX) return;
  }

  if (!available_for_new_job_()) return;
  if (reinitialize_interval_ms_ != 0 && !active_job_ && queue_.empty() &&
      deadline_due_(now_ms, maintenance_deadline_ms_)) {
    (void) start_initialization_();
    return;
  }
  if (active_job_ || queue_.empty() || millis() - last_press_ms_ < MINIMUM_JOB_GAP_MS) return;

  const Job job = queue_.front();
  queue_.pop();
  active_job_ = true;
  tx_command_ = job.command;
  if (!tr245a_radio_tx_begin(&radio_, job.frame.data(), job.bursts)) {
    tr245a_radio_tick_result_t result = tr245a_radio_no_result();
    result.tx = TR245A_TX_ABORTED;
    result.error = tr245a_radio_last_error(&radio_);
    process_radio_result_(result, true);
    return;
  }
  high_frequency_.start();
}
void TR245A::on_shutdown() {
  if (shutting_down_) return;
  shutting_down_ = true;
  retry_scheduled_ = false;
  high_frequency_.stop();
  tr245a_radio_tick_result_t result = tr245a_radio_no_result();
  if (radio_.io.set_front_end != nullptr) result = tr245a_radio_stop(&radio_);
  else set_amplifiers_(false, false);
  process_radio_result_(result, true);
}
void TR245A::send_command(uint8_t code, uint8_t presses) {
  if (presses < 1 || presses > 16) return;
  for (uint8_t i = 0; i < presses; ++i) {
    if (code == CMD_PAIR) { send_pair(); return; }
    if (fan_command_callback_) {
      if (code > CMD_FAN_R) return;
      fan_command_callback_(code);
    } else if (code == CMD_UP) send_speed(retained_speed_ < 6 ? retained_speed_ + 1 : 6);
    else if (code == CMD_DOWN) send_speed(retained_speed_ > 1 ? retained_speed_ - 1 : 1);
    else if (code == CMD_OFF) send_off();
    else if (code == CMD_FAN_R) send_reverse();
  }
}
}  // namespace tr245a
}  // namespace esphome
