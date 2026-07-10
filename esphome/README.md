# ESPHome port: OpenInverterGateway for ShineWiFi-X

This folder contains the ESPHome port target for a Growatt ShineWiFi-X stick.

## Defaults

- Board: `esp07s` / ESP8266, matching the upstream `ShineWifiX` PlatformIO environment.
- UART/Modbus: GPIO1 TX + GPIO3 RX, `115200 8N1`, Modbus address `1`.
- MQTT: **disabled**. ESPHome native API is used instead.
- Direct Modbus communication: **enabled by design** via ESPHome `modbus_controller` sensors/writes. The original firmware's `ENABLE_MODBUS_COMMUNICATION=1` maps to exposed Modbus read/write entities here.
- Growatt register map: protocol `1.24`, copied from upstream `Growatt124.cpp`.

## Compile locally

```bash
docker run --rm -v "$PWD":/config -w /config esphome/esphome:2026.4.4 compile esphome/openinvertergateway-shinewifix.yaml
```

Before flashing, replace the placeholder Wi-Fi substitutions in the YAML or override them in your own package/secrets setup.

## Notes

Serial logging is disabled (`logger.baud_rate: 0`) because the inverter uses the hardware UART.
