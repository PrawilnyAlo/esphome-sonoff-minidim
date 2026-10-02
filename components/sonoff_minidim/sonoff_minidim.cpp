#include "sonoff_minidim.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sonoff_minidim {

static const char *const TAG = "sonoff_minidim";

static const uint8_t HEADER = 0xA5;
static const uint32_t ACK_TIMEOUT_MS = 200;  // oryginalny ESP też ponawia po ~200 ms
static const uint8_t MAX_TRIES = 4;
static const uint32_t INIT_FALLBACK_MS = 5000;
static const size_t MAX_QUEUE = 16;

// Komendy ESP -> HC32
static const uint8_t CMD_HELLO_ACK = 0xC0;
static const uint8_t CMD_READ_CALIBRATION = 0xB2;
static const uint8_t CMD_READ_STATE = 0xB3;
static const uint8_t CMD_AUTO_CALIBRATION = 0xB4;
static const uint8_t CMD_CALIBRATION_RESULT_ACK = 0xC5;
static const uint8_t CMD_SET_LEVEL = 0xB6;
static const uint8_t CMD_STOP_FADE = 0xB7;
static const uint8_t CMD_FACTORY_RESET = 0xB8;
static const uint8_t CMD_DIMMING_TYPE = 0xB9;
static const uint8_t CMD_LOAD_TYPE = 0xBA;
static const uint8_t CMD_READ_POWER = 0xBB;
static const uint8_t CMD_SET_CAL_MIN = 0xBC;
static const uint8_t CMD_LEVEL_REPORT_ACK = 0xCD;
static const uint8_t CMD_CONFIG = 0xBF;
static const uint8_t CMD_SET_CAL_MAX = 0xD6;

// Ramki HC32 -> ESP
static const uint8_t RSP_HELLO = 0xB0;
static const uint8_t RSP_CALIBRATION = 0xC2;
static const uint8_t RSP_CALIBRATION_RESULT = 0xB5;
static const uint8_t RSP_FADE_STOPPED = 0xC7;
static const uint8_t RSP_POWER = 0xCB;
static const uint8_t RSP_LEVEL_REPORT = 0xBD;
static const uint8_t RSP_CALIBRATION_PROGRESS = 0xE0;

std::vector<uint8_t> SonoffMiniDim::build_(uint8_t cmd, const std::vector<uint8_t> &data) {
  std::vector<uint8_t> frame{HEADER, 0x00, static_cast<uint8_t>(5 + data.size()), cmd};
  frame.insert(frame.end(), data.begin(), data.end());
  uint8_t sum = 0;
  for (uint8_t b : frame)
    sum += b;
  frame.push_back(sum);
  return frame;
}

void SonoffMiniDim::setup() {
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    if (this->reset_on_boot_) {
      // Restart HC32, żeby na pewno przysłał hello (B0). Przy restarcie samego ESP (OTA) światło na chwilę zgaśnie.
      this->reset_pin_->digital_write(false);
      delay(20);
    }
    // Stan wysoki = HC32 pracuje (RESETB aktywny niskim)
    this->reset_pin_->digital_write(true);
  }
  this->boot_ms_ = millis();
}

void SonoffMiniDim::loop() {
  uint8_t c;
  while (this->available() && this->read_byte(&c))
    this->parse_byte_(c);

  const uint32_t now = millis();

  // HC32 mógł już działać (np. restart samego ESP po OTA) i nie wyśle hello
  if (!this->init_sent_ && now - this->boot_ms_ > INIT_FALLBACK_MS) {
    ESP_LOGW(TAG, "Brak hello (B0) od HC32 - wysyłam konfigurację bez handshake");
    this->send_init_sequence_();
  }

  if (this->waiting_) {
    if (now - this->sent_ms_ < ACK_TIMEOUT_MS)
      return;
    Pending &p = this->queue_.front();
    if (p.tries >= MAX_TRIES) {
      ESP_LOGW(TAG, "Brak odpowiedzi 0x%02X od HC32 na %s - porzucam", p.expect, format_hex_pretty(p.frame).c_str());
      this->queue_.pop_front();
      this->waiting_ = false;
    } else {
      p.tries++;
      this->write_array(p.frame);
      this->sent_ms_ = now;
      return;
    }
  }

  if (!this->queue_.empty()) {
    Pending &p = this->queue_.front();
    p.tries = 1;
    this->write_array(p.frame);
    ESP_LOGV(TAG, "TX %s", format_hex_pretty(p.frame).c_str());
    this->sent_ms_ = now;
    this->waiting_ = true;
  }
}

void SonoffMiniDim::update() { this->request_power(); }

void SonoffMiniDim::dump_config() {
  ESP_LOGCONFIG(TAG, "Sonoff Mini-Dim (HC32L021):");
  LOG_PIN("  Reset pin: ", this->reset_pin_);
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Moc", this->power_sensor_);
  LOG_SENSOR("  ", "Napięcie", this->voltage_sensor_);
  LOG_SENSOR("  ", "Prąd", this->current_sensor_);
  LOG_SENSOR("  ", "Poziom HC32", this->level_sensor_);
  LOG_SENSOR("  ", "Kalibracja min", this->cal_min_sensor_);
  LOG_SENSOR("  ", "Kalibracja max", this->cal_max_sensor_);
  LOG_SENSOR("  ", "Postęp kalibracji", this->cal_progress_sensor_);
}

void SonoffMiniDim::send_now_(uint8_t cmd, const std::vector<uint8_t> &data) {
  auto frame = build_(cmd, data);
  ESP_LOGV(TAG, "TX %s", format_hex_pretty(frame).c_str());
  this->write_array(frame);
}

void SonoffMiniDim::enqueue_(uint8_t cmd, const std::vector<uint8_t> &data, bool replace) {
  auto frame = build_(cmd, data);
  if (replace) {
    // Nie mnóż komend jasności przy szybkich zmianach - podmień tę, która jeszcze czeka
    for (size_t i = this->waiting_ ? 1 : 0; i < this->queue_.size(); i++) {
      if (this->queue_[i].frame[3] == cmd) {
        this->queue_[i].frame = frame;
        return;
      }
    }
  }
  if (this->queue_.size() >= MAX_QUEUE) {
    ESP_LOGW(TAG, "Kolejka pełna, pomijam %s", format_hex_pretty(frame).c_str());
    return;
  }
  this->queue_.push_back(Pending{frame, static_cast<uint8_t>(cmd + 0x10), 0});
}

void SonoffMiniDim::parse_byte_(uint8_t c) {
  const size_t pos = this->rx_.size();
  if (pos == 0 && c != HEADER)
    return;
  if (pos == 1 && c != 0x00) {
    this->rx_.clear();
    if (c == HEADER)
      this->rx_.push_back(c);
    return;
  }
  if (pos == 2 && (c < 5 || c > 64)) {
    this->rx_.clear();
    return;
  }
  this->rx_.push_back(c);
  if (this->rx_.size() < 3 || this->rx_.size() < this->rx_[2])
    return;

  uint8_t sum = 0;
  for (size_t i = 0; i + 1 < this->rx_.size(); i++)
    sum += this->rx_[i];
  if (sum == this->rx_.back()) {
    this->handle_frame_(this->rx_);
  } else {
    ESP_LOGW(TAG, "Zła suma kontrolna: %s", format_hex_pretty(this->rx_).c_str());
  }
  this->rx_.clear();
}

void SonoffMiniDim::handle_frame_(const std::vector<uint8_t> &frame) {
  const uint8_t cmd = frame[3];
  const std::vector<uint8_t> d(frame.begin() + 4, frame.end() - 1);
  ESP_LOGV(TAG, "RX %s", format_hex_pretty(frame).c_str());

  if (this->waiting_ && !this->queue_.empty() && cmd == this->queue_.front().expect) {
    this->queue_.pop_front();
    this->waiting_ = false;
  }

  switch (cmd) {
    case RSP_HELLO:
      if (d.size() >= 4)
        ESP_LOGI(TAG, "HC32 hello, firmware %u.%u.%u", d[1], d[2], d[3]);
      // HC32 właśnie wystartował - stare komendy z kolejki są nieaktualne
      this->queue_.clear();
      this->waiting_ = false;
      this->send_now_(CMD_HELLO_ACK, {0x00});
      this->send_init_sequence_();
      break;

    case RSP_LEVEL_REPORT:
      this->send_now_(CMD_LEVEL_REPORT_ACK, {0x00});
      if (!d.empty())
        publish_(this->level_sensor_, d[0]);
      break;

    case RSP_CALIBRATION_RESULT:
      this->send_now_(CMD_CALIBRATION_RESULT_ACK, {0x00});
      if (d.size() >= 5) {
        ESP_LOGI(TAG, "Kalibracja zakończona: status %u, min %u, max %u", d[1], d[3], d[4]);
        publish_(this->cal_min_sensor_, d[3]);
        publish_(this->cal_max_sensor_, d[4]);
      }
      publish_(this->cal_progress_sensor_, 100);
      break;

    case RSP_CALIBRATION:
      if (d.size() >= 4) {
        ESP_LOGI(TAG, "Kalibracja w HC32: %s, min %u, max %u", d[1] ? "tak" : "brak (domyślna)", d[2], d[3]);
        publish_(this->cal_min_sensor_, d[2]);
        publish_(this->cal_max_sensor_, d[3]);
      }
      break;

    case RSP_FADE_STOPPED:
      if (d.size() >= 2) {
        publish_(this->level_sensor_, d[1]);
        this->sync_light_(d[1]);
      }
      break;

    case RSP_POWER:
      if (d.size() >= 7) {
        publish_(this->current_sensor_, d[2] / 100.0f);
        publish_(this->voltage_sensor_, encode_uint16(d[3], d[4]) / 100.0f);
        publish_(this->power_sensor_, encode_uint16(d[5], d[6]) / 100.0f);
      }
      break;

    case RSP_CALIBRATION_PROGRESS:
      if (d.size() >= 2)
        publish_(this->cal_progress_sensor_, d[1]);
      break;

    default:
      break;
  }
}

void SonoffMiniDim::send_init_sequence_() {
  // Kolejność i treść jak w oryginalnym firmware (przechwycone przy starcie)
  this->init_sent_ = true;
  this->enqueue_(CMD_READ_CALIBRATION, {});
  this->enqueue_(CMD_DIMMING_TYPE, {this->dimming_type_});
  this->enqueue_(CMD_LOAD_TYPE, {this->load_type_});
  this->enqueue_(CMD_CONFIG, {0x00, 0x00, 0x00, 0x00, 0x00, 0x32});  // znaczenie nieznane
  this->enqueue_(CMD_READ_STATE, {});
  this->ready_ = true;
  if (this->has_level_ && this->last_level_ > 0)
    this->set_level(this->last_level_, this->transition_);
  this->request_power();
}

void SonoffMiniDim::set_level(uint8_t level, uint8_t transition) {
  this->last_level_ = level;
  this->has_level_ = true;
  if (!this->ready_)
    return;
  this->enqueue_(CMD_SET_LEVEL, {0x00, level, 0x00, 0x00, 0x00, transition}, true);
}

void SonoffMiniDim::set_raw_level(uint8_t level) {
  // Surowa jasność z pominięciem kalibracji (podgląd przy ręcznej kalibracji)
  if (this->ready_)
    this->enqueue_(CMD_SET_LEVEL, {0x01, level, 0x00, 0x00, 0x00, this->transition_}, true);
}

void SonoffMiniDim::start_fade(bool up, uint8_t rate) {
  if (this->ready_)
    this->enqueue_(CMD_SET_LEVEL, {0x00, static_cast<uint8_t>(up ? 0xFF : 0x01), 0x00, 0x00, 0x00, rate}, true);
}

void SonoffMiniDim::stop_fade() {
  if (this->ready_)
    this->enqueue_(CMD_STOP_FADE, {});
}

void SonoffMiniDim::set_dimming_type(uint8_t type) {
  this->dimming_type_ = type;
  if (this->ready_)
    this->enqueue_(CMD_DIMMING_TYPE, {type}, true);
}

void SonoffMiniDim::set_load_type(uint8_t type) {
  this->load_type_ = type;
  if (this->ready_)
    this->enqueue_(CMD_LOAD_TYPE, {type}, true);
}

void SonoffMiniDim::set_transition(float seconds) {
  const long n = lroundf(seconds / 0.25f) - 1;
  this->transition_ = static_cast<uint8_t>(std::max(0L, std::min(255L, n)));
}

void SonoffMiniDim::start_auto_calibration() {
  if (this->ready_) {
    publish_(this->cal_progress_sensor_, 0);
    this->enqueue_(CMD_AUTO_CALIBRATION, {0x01});
  }
}

void SonoffMiniDim::write_calibration(uint8_t min_level, uint8_t max_level) {
  if (!this->ready_)
    return;
  if (min_level >= max_level) {
    ESP_LOGW(TAG, "Kalibracja: min (%u) musi być mniejsze od max (%u)", min_level, max_level);
    return;
  }
  this->enqueue_(CMD_SET_CAL_MIN, {min_level});
  this->enqueue_(CMD_SET_CAL_MAX, {max_level});
  this->enqueue_(CMD_READ_CALIBRATION, {});
}

void SonoffMiniDim::factory_reset_hc32() {
  if (!this->ready_)
    return;
  // Jak oryginał: reset HC32, potem ponowna konfiguracja. Kasuje kalibrację.
  this->enqueue_(CMD_FACTORY_RESET, {});
  this->send_init_sequence_();
}

void SonoffMiniDim::request_power() {
  if (this->ready_)
    this->enqueue_(CMD_READ_POWER, {}, true);
}

void SonoffMiniDim::sync_light_(uint8_t level) {
  // Po płynnej zmianie z przycisku HC32 podaje, gdzie się zatrzymał - przenosimy to do encji światła
  this->last_level_ = level;
  this->has_level_ = true;
  if (this->light_state_ == nullptr)
    return;
  this->sync_pending_ = true;
  this->sync_level_ = level;
  auto call = this->light_state_->make_call();
  call.set_transition_length(0);
  if (level == 0) {
    call.set_state(false);
  } else {
    call.set_state(true);
    call.set_brightness(level / 255.0f);
  }
  call.perform();
}

bool SonoffMiniDim::consume_sync(uint8_t level) {
  const bool hit = this->sync_pending_ && level == this->sync_level_;
  this->sync_pending_ = false;
  return hit;
}

light::LightTraits SonoffMiniDimLight::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
  return traits;
}

void SonoffMiniDimLight::write_state(light::LightState *state) {
  float brightness;
  state->current_values_as_brightness(&brightness);
  uint8_t level = 0;
  if (brightness > 0.0f)
    level = static_cast<uint8_t>(std::max(1L, std::min(255L, lroundf(brightness * 255.0f))));
  // Stan przyszedł z HC32 (sync_light_) - nie odsyłamy go z powrotem
  if (this->parent_->consume_sync(level))
    return;
  this->parent_->set_level(level, this->parent_->get_transition_byte());
}

}  // namespace sonoff_minidim
}  // namespace esphome
