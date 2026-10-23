#pragma once

#include "esphome/components/fan/fan.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "tr245a.h"

namespace esphome {
namespace tr245a {

class TR245AFan final : public fan::Fan, public Component, public Parented<TR245A> {
 public:
  fan::FanTraits get_traits() override { return fan::FanTraits(false, true, true, 6); }
  void setup() override;
 protected:
  void control(const fan::FanCall &call) override;

 private:
  void received_command_(const tr245a_received_t &packet, bool discarded_reverse);
  void transmitted_reverse_() { sent_reverse_ = !sent_reverse_; }
  void reverse_plan_changed_(bool pending_reverse);
  void send_command_(uint8_t code);
  bool sent_reverse_{false};  // completed TX / heard remote; never receiver feedback
};

}  // namespace tr245a
}  // namespace esphome
