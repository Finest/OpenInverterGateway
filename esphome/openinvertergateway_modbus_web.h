#pragma once

#include "esphome.h"
#include "esphome/components/modbus/modbus_definitions.h"
#include "esphome/components/modbus_controller/modbus_controller.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/web_server_base/web_server_base.h"

#include <ESPAsyncWebServer.h>
#include <cstdlib>
#include <vector>

namespace openinvertergateway {
namespace modbus_web {

static constexpr const char *MODBUS_FORM_HTML = R"=====(
<!DOCTYPE HTML><html>
<head>
  <meta charset='utf-8'>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Growatt Inverter</title>
</head>
<body>
  <h2>Growatt Post Communication Modbus</h2>
  <form action="/postCommunicationModbus_p" method="POST">
    <input type="text" name="reg" placeholder="Register ID"></br>
    <input type="text" name="val" placeholder="Input Value (16bit only!)"></br>
    <select name="type">
      <option value="16b" selected>16b</option>
      <option value="32b">32b</option>
    </select></br>
    <select name="operation">
      <option value="R" selected>Read</option>
      <option value="W">Write</option>
    </select></br>
    <select name="registerType">
      <option value="I" selected>Input Register</option>
      <option value="H">Holding Register</option>
    </select></br>
    <input type="submit" value="go">
  </form>
  <p><small>ESPHome queues Modbus commands asynchronously. The final answer is also published to <b>Modbus Last Result</b>.</small></p>
  <a href=".">back</a>
</body>
</html>
)=====";

inline bool parse_u16_(const String &value, uint16_t *out) {
  if (value.length() == 0)
    return false;
  char *end = nullptr;
  unsigned long parsed = std::strtoul(value.c_str(), &end, 0);
  if (end == value.c_str() || *end != '\0' || parsed > 0xFFFFUL)
    return false;
  *out = static_cast<uint16_t>(parsed);
  return true;
}

inline void publish_result_(esphome::text_sensor::TextSensor *result, const char *msg) {
  if (result != nullptr)
    result->publish_state(msg);
}

inline void add_authenticated_route_(const char *uri, WebRequestMethodComposite method,
                                     ArRequestHandlerFunction on_request) {
  auto *handler = new AsyncCallbackWebHandler();
  handler->setUri(uri);
  handler->setMethod(method);
  handler->onRequest(on_request);
  esphome::web_server_base::global_web_server_base->add_handler(handler);
}

inline void register_routes(esphome::modbus_controller::ModbusController *ctrl,
                            esphome::text_sensor::TextSensor *result) {
  using esphome::modbus::EntityType;
  using esphome::modbus_controller::ModbusCommandItem;
  using esphome::web_server_base::global_web_server_base;

  if (global_web_server_base == nullptr) {
    publish_result_(result, "Modbus WebGUI route registration failed: web server not ready");
    return;
  }

  // Register through WebServerBase instead of AsyncWebServer::on() so these
  // custom endpoints inherit ESPHome's optional web_server auth middleware.
  add_authenticated_route_("/postCommunicationModbus", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", MODBUS_FORM_HTML);
  });

  add_authenticated_route_("/postCommunicationModbus_p", HTTP_POST,
             [ctrl, result](AsyncWebServerRequest *request) {
               if (!request->hasParam("reg", true) || !request->hasParam("val", true)) {
                 request->send(400, "text/plain", "400: Invalid Request");
                 return;
               }

               const String reg_arg = request->getParam("reg", true)->value();
               const String val_arg = request->getParam("val", true)->value();
               const String operation = request->hasParam("operation", true)
                                            ? request->getParam("operation", true)->value()
                                            : String("R");
               const String type = request->hasParam("type", true) ? request->getParam("type", true)->value()
                                                                    : String("16b");
               const String register_type_arg = request->hasParam("registerType", true)
                                                    ? request->getParam("registerType", true)->value()
                                                    : String("I");

               uint16_t reg = 0;
               uint16_t val = 0;
               if (!parse_u16_(reg_arg, &reg) || !parse_u16_(val_arg, &val)) {
                 request->send(400, "text/plain", "400: Invalid Request");
                 return;
               }

               const bool is_read = operation == "R";
               const bool is_holding = register_type_arg == "H";
               const bool is_32bit = type == "32b";

               if (is_read) {
                 const uint16_t count = is_32bit ? 2 : 1;
                 const auto register_type = is_holding ? EntityType::HOLDING : EntityType::INPUT_REGISTER;

                 char queued[128];
                 snprintf(queued, sizeof(queued), "Queued read %u-bit %s register %u",
                          is_32bit ? 32 : 16, is_holding ? "holding" : "input", reg);
                 publish_result_(result, queued);

                 ctrl->queue_command(ModbusCommandItem::create_read_command(
                     ctrl, register_type, reg, count,
                     [result, reg, count, is_holding](EntityType register_type, uint16_t start_address,
                                                      std::span<const uint8_t> data) {
                       char msg[192];
                       if (data.size() < count * 2) {
                         snprintf(msg, sizeof(msg),
                                  "Read %u-bit %s register %u impossible - not connected?",
                                  count == 2 ? 32 : 16, is_holding ? "holding" : "input", reg);
                         publish_result_(result, msg);
                         return;
                       }

                       uint32_t value = (static_cast<uint32_t>(data[0]) << 8) | data[1];
                       if (count == 2) {
                         value = (static_cast<uint32_t>(data[0]) << 24) |
                                 (static_cast<uint32_t>(data[1]) << 16) |
                                 (static_cast<uint32_t>(data[2]) << 8) | data[3];
                       }

                       snprintf(msg, sizeof(msg), "Read %u-bit %s register %u with value %lu",
                                count == 2 ? 32 : 16, is_holding ? "holding" : "input", reg,
                                static_cast<unsigned long>(value));
                       publish_result_(result, msg);
                     }));

                 request->send(200, "text/plain", queued);
                 return;
               }

               if (!is_holding) {
                 const char *msg = "It is not possible to write into input registers";
                 publish_result_(result, msg);
                 request->send(200, "text/plain", msg);
                 return;
               }

               if (is_32bit) {
                 const char *msg = "Writing to double (32b) registers not supported";
                 publish_result_(result, msg);
                 request->send(200, "text/plain", msg);
                 return;
               }

               char queued[128];
               snprintf(queued, sizeof(queued), "Queued write holding register %u to a value of %u", reg, val);
               publish_result_(result, queued);

               auto cmd = ModbusCommandItem::create_write_single_command(ctrl, reg, val);
               cmd.on_data_func = [result, reg, val](EntityType register_type, uint16_t start_address,
                                                     std::span<const uint8_t> data) {
                 char msg[192];
                 if (data.size() >= 4) {
                   const uint16_t ack_reg = (static_cast<uint16_t>(data[0]) << 8) | data[1];
                   const uint16_t ack_val = (static_cast<uint16_t>(data[2]) << 8) | data[3];
                   snprintf(msg, sizeof(msg),
                            "Wrote holding register %u to a value of %u! (ack register %u = %u)",
                            reg, val, ack_reg, ack_val);
                 } else {
                   snprintf(msg, sizeof(msg), "Wrote holding register %u to a value of %u!", reg, val);
                 }
                 publish_result_(result, msg);
               };
               ctrl->queue_command(cmd);

               request->send(200, "text/plain", queued);
             });

  publish_result_(result, "Modbus WebGUI routes available at /postCommunicationModbus");
}

}  // namespace modbus_web
}  // namespace openinvertergateway
