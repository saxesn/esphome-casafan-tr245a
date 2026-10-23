#pragma once

#include "esphome/components/button/button.h"
#include "esphome/core/helpers.h"
#include "tr245a.h"

namespace esphome {
namespace tr245a {

class TR245AButton final : public button::Button, public Parented<TR245A> {
 public:
  void set_command(uint8_t code) { command_ = code; }

 protected:
  void press_action() override;

 private:
  uint8_t command_{0};
};

}  // namespace tr245a
}  // namespace esphome
