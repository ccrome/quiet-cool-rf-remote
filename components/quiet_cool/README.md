# quiet_cool ESPHome component

Controls a QuietCool whole-house fan through a CC1101 433 MHz transceiver,
with bidirectional state sync and over-the-air pairing.

```yaml
# example configuration:

spi:
  clk_pin: 18
  mosi_pin: 23
  miso_pin: 19

fan:
  - platform: quiet_cool
    id: quietcool_fan_id
    name: QuietCool fan
    cs_pin: 15
    gdo0_pin: 13
    gdo2_pin: 12
    # remote_id: [0x2D, 0xD4, 0x06, 0xCB, 0x00, 0xF7, 0xF2]  # optional; omit to pair OTA
    # speed_count: 3

button:
  - platform: template
    name: "Pair Remote"
    on_press:
      - lambda: id(quietcool_fan_id).start_pairing();
```

With no `remote_id` configured and nothing paired, the device opens a 60s
pairing window on boot — press any button on the physical remote to pair.

## Files

- `fan/protocol.{h,cpp}` — pure packet encode/decode + register math (host-testable, see `tests/`)
- `fan/cc1101.{h,cpp}` — minimal CC1101 driver over ESPHome's SPI bus
- `fan/radio_task.{h,cpp}` — FreeRTOS task owning all radio I/O (interrupt-driven RX, queued TX, WAKE polling)
- `fan/quiet_cool.{h,cpp}` — the ESPHome fan component (state sync, pairing, HA integration)
