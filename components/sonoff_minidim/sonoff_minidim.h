#pragma once

#include <deque>
#include <vector>

#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace sonoff_minidim {

// Protokół ESP32-C3 <-> HC32L021 w Sonoff Mini-Dim (Matter), 115200 8N1.
// Ramka: A5 00 <długość całej ramki> <cmd> <dane...> <suma8 poprzednich bajtów>.
// Potwierdzenie/odpowiedź = kod komendy + 0x10. Szczegóły w NOTATKI.md.
class SonoffMiniDim : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_reset_on_boot(bool value) { this->reset_on_boot_ = value; }
  void set_power_sensor(sensor::Sensor *s) { this->power_sensor_ = s; }
  void set_voltage_sensor(sensor::Sensor *s) { this->voltage_sensor_ = s; }
  void set_current_sensor(sensor::Sensor *s) { this->current_sensor_ = s; }
  void set_level_sensor(sensor::Sensor *s) { this->level_sensor_ = s; }
  void set_calibration_min_sensor(sensor::Sensor *s) { this->cal_min_sensor_ = s; }
  void set_calibration_max_sensor(sensor::Sensor *s) { this->cal_max_sensor_ = s; }
  void set_calibration_progress_sensor(sensor::Sensor *s) { this->cal_progress_sensor_ = s; }

  // Wywoływane z lambd w YAML
  void set_level(uint8_t level, uint8_t transition);
  void set_raw_level(uint8_t level);
  void start_fade(bool up, uint8_t rate);
  void stop_fade();
  void set_dimming_type(uint8_t type);  // 0 = leading edge, 1 = trailing edge
  void set_load_type(uint8_t type);     // 0 auto, 1 LED, 3 żarówka/halogen, 4 ELV
  void set_transition(float seconds);   // czas = (n + 1) * 0,25 s
  uint8_t get_transition_byte() const { return this->transition_; }
  void start_auto_calibration();
  void write_calibration(uint8_t min_level, uint8_t max_level);
  void factory_reset_hc32();
  void request_power();
  bool is_ready() const { return this->ready_; }

  void register_light(light::LightState *state) { this->light_state_ = state; }
  bool consume_sync(uint8_t level);

 protected:
  struct Pending {
    std::vector<uint8_t> frame;
    uint8_t expect;
    uint8_t tries;
  };

  static std::vector<uint8_t> build_(uint8_t cmd, const std::vector<uint8_t> &data);
  void send_now_(uint8_t cmd, const std::vector<uint8_t> &data);
  void enqueue_(uint8_t cmd, const std::vector<uint8_t> &data, bool replace = false);
  void parse_byte_(uint8_t c);
  void handle_frame_(const std::vector<uint8_t> &frame);
  void send_init_sequence_();
  void sync_light_(uint8_t level);
  static void publish_(sensor::Sensor *s, float value) {
    if (s != nullptr)
      s->publish_state(value);
  }

  GPIOPin *reset_pin_{nullptr};
  bool reset_on_boot_{false};
  sensor::Sensor *power_sensor_{nullptr};
  sensor::Sensor *voltage_sensor_{nullptr};
  sensor::Sensor *current_sensor_{nullptr};
  sensor::Sensor *level_sensor_{nullptr};
  sensor::Sensor *cal_min_sensor_{nullptr};
  sensor::Sensor *cal_max_sensor_{nullptr};
  sensor::Sensor *cal_progress_sensor_{nullptr};
  light::LightState *light_state_{nullptr};

  std::vector<uint8_t> rx_;
  std::deque<Pending> queue_;
  bool waiting_{false};
  uint32_t sent_ms_{0};
  uint32_t boot_ms_{0};

  bool ready_{false};
  bool init_sent_{false};
  uint8_t dimming_type_{1};
  uint8_t load_type_{0};
  uint8_t transition_{7};  // 2 s, jak fabrycznie
  uint8_t last_level_{0};
  bool has_level_{false};
  bool sync_pending_{false};
  uint8_t sync_level_{0};
};

class SonoffMiniDimLight : public light::LightOutput {
 public:
  void set_parent(SonoffMiniDim *parent) { this->parent_ = parent; }
  light::LightTraits get_traits() override;
  void setup_state(light::LightState *state) override { this->parent_->register_light(state); }
  void write_state(light::LightState *state) override;

 protected:
  SonoffMiniDim *parent_{nullptr};
};

}  // namespace sonoff_minidim
}  // namespace esphome
