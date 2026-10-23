#pragma once

#include <cstddef>
#include <cstdint>

#include "esphome/components/spi/spi.h"

namespace esphome {
namespace tr245a {

class Tr245aSpiTransport
    : public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                            spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_4MHZ> {
 protected:
  bool transfer_(const uint8_t *tx, uint8_t *rx, size_t len);

 private:
  class ChipSelectGuard {
   public:
    explicit ChipSelectGuard(Tr245aSpiTransport *transport) : transport_(transport) {}
    ~ChipSelectGuard() { transport_->disable(); }

   private:
    Tr245aSpiTransport *transport_;
  };

  alignas(4) uint8_t tx_buffer_[68]{};
  alignas(4) uint8_t rx_buffer_[68]{};
};

}  // namespace tr245a
}  // namespace esphome
