#include "tr245a_spi_transport.h"

#include <cstring>

namespace esphome {
namespace tr245a {

bool Tr245aSpiTransport::transfer_(const uint8_t *tx, uint8_t *rx, size_t len) {
  if (tx == nullptr || rx == nullptr || len == 0 || len > 65) return false;

  // ESP32 SPI DMA may access complete 32-bit words. Keep both DMA directions
  // in separate, padded, aligned storage and clock only the requested bytes.
  std::memcpy(tx_buffer_, tx, len);
  {
    this->enable();
    ChipSelectGuard chip_select(this);
    this->delegate_->transfer(tx_buffer_, rx_buffer_, len);
  }
  std::memcpy(rx, rx_buffer_, len);
  return true;  // ESPHome's void SPI API only confirms that transfer was requested.
}

}  // namespace tr245a
}  // namespace esphome
