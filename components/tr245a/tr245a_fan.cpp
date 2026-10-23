#include "tr245a_fan.h"

#include "esphome/core/log.h"

namespace esphome {
namespace tr245a {

static const char *const TAG = "tr245a.fan";

void TR245AFan::setup() {
  parent_->set_fan_callbacks(
      [this](uint8_t code) { send_command_(code); },
      [this](const tr245a_received_t &packet, bool discarded_reverse) {
        received_command_(packet, discarded_reverse);
      },
      [this]() { transmitted_reverse_(); },
      [this](bool pending_reverse) { reverse_plan_changed_(pending_reverse); });
  // Restore displayed values without sending at boot or replaying a toggle.
  auto restored = restore_state_();
  if (restored.has_value()) restored->apply(*this);
  if (speed < 1 || speed > 6) speed = 1;
  sent_reverse_ = direction == fan::FanDirection::REVERSE;
  publish_state();
}

void TR245AFan::received_command_(const tr245a_received_t &packet, bool discarded_reverse) {
  if (packet.command == TR245A_PAIR) return;
  // RX has discarded all pending jobs. A queued toggle never changed the motor;
  // base this update on completed transmissions and earlier remote commands.
  // Only resync the displayed direction when a toggle was heard or discarded:
  // a direction chosen in HA while the fan is off queues nothing and survives.
  if (packet.command == TR245A_REVERSE) sent_reverse_ = !sent_reverse_;
  if (packet.command == TR245A_REVERSE || discarded_reverse)
    direction = sent_reverse_ ? fan::FanDirection::REVERSE : fan::FanDirection::FORWARD;
  if (packet.command == TR245A_SPEED) {
    state = true;
    speed = packet.speed;
  } else if (packet.command == TR245A_OFF) {
    state = false;
  }
  publish_state();
}

void TR245AFan::reverse_plan_changed_(bool pending_reverse) {
  const bool displayed_reverse = sent_reverse_ != pending_reverse;
  const auto planned_direction = displayed_reverse ? fan::FanDirection::REVERSE
                                                   : fan::FanDirection::FORWARD;
  if (direction == planned_direction) return;
  direction = planned_direction;
  publish_state();
}

void TR245AFan::send_command_(uint8_t code) {
  auto call = make_call();
  if (code == CMD_UP || code == CMD_DOWN) {
    int current = speed > 0 ? speed : 1;
    int next = current + (code == CMD_UP ? 1 : -1);
    call.set_state(true).set_speed(next < 1 ? 1 : (next > 6 ? 6 : next));
  } else if (code == CMD_OFF) {
    call.set_state(false);
  } else if (code == CMD_FAN_R) {
    if (!state) {
      ESP_LOGW(TAG, "Reverse requires a running fan");
      return;
    }
    call.set_direction(direction == fan::FanDirection::FORWARD ? fan::FanDirection::REVERSE
                                                               : fan::FanDirection::FORWARD);
  } else {
    return;
  }
  call.perform();
}

void TR245AFan::control(const fan::FanCall &call) {
  bool want_on = call.get_state().value_or(state);
  int wanted_speed = call.get_speed().value_or(speed > 0 ? speed : 1);
  auto wanted_direction = call.get_direction().value_or(direction);
  if (wanted_speed < 1 || wanted_speed > 6) return;
  bool speed_command = want_on && (call.get_state().has_value() || call.get_speed().has_value());
  bool off_command = !want_on && call.get_state().has_value();
  bool want_reverse = wanted_direction == fan::FanDirection::REVERSE;
  bool planned_reverse = sent_reverse_ != parent_->pending_reverse();
  bool reverse_command = want_on && want_reverse != planned_reverse;
  // Make the optimistic direction visible to the centralized enqueue callback
  // before it publishes. On success the identical parity update is a no-op;
  // on rejection restore the unmodified state without publishing.
  const auto previous_direction = direction;
  direction = wanted_direction;
  if (!parent_->send_fan_command(want_on, wanted_speed, speed_command || off_command, reverse_command)) {
    direction = previous_direction;
    return;
  }
  state = want_on;
  speed = wanted_speed;
  publish_state();
}

}  // namespace tr245a
}  // namespace esphome
