#pragma once
#include <array>
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"
#include "tr245a_radio.h"
#include "tr245a_receiver.h"
#include "tr245a_job_queue.h"
#include "tr245a_spi_transport.h"

namespace esphome {
namespace tr245a {
enum TR245ACommand : uint8_t { CMD_UP, CMD_DOWN, CMD_OFF, CMD_FAN_R, CMD_PAIR };
enum TR245AFrontEnd : uint8_t { FRONT_END_GENERIC, FRONT_END_XL7105_D03 };
class TR245A final : public Tr245aSpiTransport, public Component {
 public:
  void set_txen_pin(GPIOPin *pin) { txen_pin_ = pin; }
  void set_rxen_pin(GPIOPin *pin) { rxen_pin_ = pin; }
  void set_front_end(uint8_t kind) { front_end_ = static_cast<TR245AFrontEnd>(kind); }
  void set_address(uint16_t address) { address_ = address; }
  void set_frequency_offset(int32_t offset) { offset_hz_ = offset; }
  void set_receive(bool enabled) { receive_ = enabled; }
  void set_receive_deduplication(uint32_t ms) { dedup_ms_ = ms; }
  void set_transmit_repetitions(uint8_t count) { repetitions_ = count; }
  // Zero disables periodic maintenance, without disabling the 60-second error retry.
  void set_reinitialize_interval(uint32_t ms) { reinitialize_interval_ms_ = ms; }
  void add_on_receive_callback(std::function<void(uint16_t, uint8_t, uint8_t)> callback) {
    receive_callbacks_.add(std::move(callback));
  }
  void set_fan_callbacks(std::function<void(uint8_t)> command,
                         std::function<void(const tr245a_received_t &, bool)> received,
                         std::function<void()> reverse_transmitted,
                         std::function<void(bool)> reverse_plan_changed) {
    fan_command_callback_ = std::move(command);
    fan_received_callback_ = std::move(received);
    fan_reverse_transmitted_callback_ = std::move(reverse_transmitted);
    fan_reverse_plan_changed_callback_ = std::move(reverse_plan_changed);
  }
  void setup() override;
  void loop() override;
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void send_command(uint8_t code, uint8_t presses = 1);
  bool send_speed(uint8_t speed);
  bool send_off();
  bool send_reverse();
  bool send_pair();
  bool send_fan_command(bool on, uint8_t speed, bool update_state, bool reverse);
  bool pending_reverse() const;

 private:
  static constexpr uint32_t DEFAULT_DEDUPLICATION_MS = 120;
  static constexpr uint32_t DEFAULT_REINITIALIZE_INTERVAL_MS = 30U * 60U * 1000U;
  static constexpr uint32_t RETRY_INTERVAL_MS = 60U * 1000U;
  static constexpr uint32_t MINIMUM_JOB_GAP_MS = 120;
  struct Job { std::array<uint8_t, 17> frame; uint16_t bursts; uint8_t command; };
  Job make_job_(const uint8_t payload[7], uint16_t bursts = TR245A_BURSTS) const;
  bool enqueue_jobs_(const Job *jobs, size_t count);
  bool enqueue_(const uint8_t payload[7], uint16_t bursts = TR245A_BURSTS);
  static uint32_t now_(void *);
  static bool radio_exchange_(void *, const uint8_t *, uint8_t *, size_t);
  static void set_front_end_(void *, tr245a_front_end_mode_t);
  static bool deadline_due_(uint32_t now, uint32_t deadline);
  void set_amplifiers_(bool transmit, bool receive);
  bool start_initialization_();
  void finish_initialization_();
  void process_radio_result_(const tr245a_radio_tick_result_t &result, bool discard_waiting = false);
  bool available_for_new_job_() const;
  void fail_permanently_(tr245a_radio_result_t error);
  void receive_frame_(const uint8_t frame[17]);
  GPIOPin *txen_pin_{nullptr}, *rxen_pin_{nullptr};
  TR245AFrontEnd front_end_{FRONT_END_GENERIC};
  tr245a_radio_t radio_{};
  uint8_t tx_command_{0};  // command byte of the active transmission
  tr245a_rx_dedup_t seen_{};
  uint16_t address_{0};
  int32_t offset_hz_{0};
  uint8_t retained_speed_{1};
  uint8_t repetitions_{1};
  bool receive_{true};
  uint32_t dedup_ms_{DEFAULT_DEDUPLICATION_MS};
  uint32_t reinitialize_interval_ms_{DEFAULT_REINITIALIZE_INTERVAL_MS};
  uint32_t retry_deadline_ms_{0};
  uint32_t maintenance_deadline_ms_{0};
  uint32_t last_press_ms_{0};
  TR245AJobQueue<Job, 16> queue_;
  bool active_job_{false};
  bool retry_scheduled_{false};
  bool shutting_down_{false};
  bool permanent_failure_{false};
  std::function<void(uint8_t)> fan_command_callback_;
  std::function<void(const tr245a_received_t &, bool)> fan_received_callback_;
  std::function<void()> fan_reverse_transmitted_callback_;
  std::function<void(bool)> fan_reverse_plan_changed_callback_;
  HighFrequencyLoopRequester high_frequency_;
  CallbackManager<void(uint16_t, uint8_t, uint8_t)> receive_callbacks_;
};
class TR245AReceiveTrigger final : public Trigger<uint16_t, uint8_t, uint8_t> {
 public:
  explicit TR245AReceiveTrigger(TR245A *parent) {
    parent->add_on_receive_callback([this](uint16_t address, uint8_t command, uint8_t value) {
      this->trigger(address, command, value);
    });
  }
};
}  // namespace tr245a
}  // namespace esphome
