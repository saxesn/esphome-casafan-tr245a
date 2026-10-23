#include "tr245a_button.h"

namespace esphome {
namespace tr245a {

void TR245AButton::press_action() { parent_->send_command(command_); }

}  // namespace tr245a
}  // namespace esphome
