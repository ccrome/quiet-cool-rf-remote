# Quiet Cool RF Remote with ESPHOME
Operate your remote controlled QuietCool Fan from ESPHome or Arduino.


This project implements an RF remote control system for Quiet Cool fans using an ESP32 microcontroller and CC1101 RF module.
The system can control various fan speeds and modes through RF signals.

Each RF remote has a unique 7-byte ID built in — but you **don't need to
decode yours**: on first boot the device enters pairing mode and learns the
ID from any button press on your physical remote (see
[Pairing](#pairing-recommended)).


![QuietCool Remote](images/quietcool_fan.png)

# TL;DR -- get it running on ESPHome
Get a generateic CC1101 board.  Like this [CC1101 Board](https://www.amazon.com/dp/B0D3W9GVRQ?ref=ppx_yo2ov_dt_b_fed_asin_title)

![CC1101](images/41x16EagV+L._SL1000_.jpg)


Make these connections from your ESP32 to your CC1101 chip/board.

NOTE: the pinouts on these boards seem to be not reliable.  I had to trace them manually to figure out which header pin went to which CC1101 pin.  

| Module Pin | CC1101 Pin Name | CC1101 Pin #| ESP32 Pin |
|------------|-----------------|-------------|-----------|
| 1          | GND             | 16,19,PAD   | GND       |
| 2          | VCC             | -           | 3.3V      |
| 3          | GDO0            | 6           | GPIO13    |
| 4          | CSn             | 7           | GPIO15    |
| 5          | SCK             | 1           | GPIO18    |
| 6          | MOSI            | 20          | GPIO23    |
| 7          | MISO            | 2           | GPIO19    |
| 8          | GDO2            | 3           | GPIO12    |


## TL;DR -- get it running on ESPHome from ESPHome Builder on HomeAssistant

* Go to ESPHome Builder
* Click "+ NEW DEVICE" at the bottom of the screen
* Click `CONTINUE`
* name your fan
* click the ESP32 type you have.
* click `SKIP`
* on your new entry, click `EDIT`
* add the following to the bottom of the file:
```yaml
spi:
  clk_pin: 18
  mosi_pin: 23
  miso_pin: 19

external_components:
  - source:
      type: git
      url: https://github.com/adamrb/quiet-cool-rf-remote.git
      ref: main
    components: [ quiet_cool ]

fan:
  - platform: quiet_cool
    id: quietcool_fan_id
    name: QuietCool fan
    cs_pin: 15
    gdo0_pin: 13
    gdo2_pin: 12
# optional variables
#    remote_id: [0x2D, 0xD4, 0x06, 0xCB, 0x00, 0xF7, 0xF2]
#    center_freq_mhz: 433.897
#    deviation_khz: 10

button:
  - platform: template
    name: "Pair Remote"
    on_press:
      - lambda: id(quietcool_fan_id).start_pairing();
```
* click `INSTALL` and install it in the normal ESPHome ways...
* on first boot with no `remote_id`, the device enters **pairing mode** for 60
  seconds — press any button on your QuietCool remote nearby and its ID is
  learned and saved to flash. Done.

## TL;DR -- get it running on ESPHome, building locally

Create a `secrets.yaml` file
```yaml
wifi_ssid: <ssid>
wifi_password: <wifi password>
api_encryption_key: <api key>
ota_password: <ota password>
ap_password: <fallback hotspot password>
```


compile, upload, and run
```
esphome compile quietcool-fan-example.yaml && \
esphome upload --device /dev/ttyUSB0 quietcool-fan-example.yaml  && \
esphome logs --device /dev/ttyUSB0 quietcool-fan-example.yaml
```

If everything is working, you should see
```
[I][quietcool.cc1101]: CC1101 detected (VERSION=0x14)
[I][quietcool.radio]: Radio task started
[C][quiet_cool.fan]:   Paired Remotes: 1
[C][quiet_cool.fan]:     [0] 2D.D4.06.CB.03.E6.45 (7)
[D][quiet_cool.fan]: Radio: MARCSTATE=0x0D pkts=12 dropped=0 overflows=0 tx_fail=0
```
`MARCSTATE=0x0D` means the radio is listening; the `Radio:` diagnostics line
repeats every 10 seconds. With no remote configured or paired you'll instead
see the pairing prompt — press any button on your remote within 60 seconds.

# Configuration

## Pairing (recommended)

You no longer need to sniff your remote's ID with an SDR. Leave `remote_id`
out of the YAML and the device enters **pairing mode** on first boot: for 60
seconds it accepts any valid QuietCool packet and stores the sender's ID in
flash. Press any button on your physical remote while near the ESP32 and
you're paired.

To pair later (or pair additional remotes, up to 4), expose a button:

```yaml
button:
  - platform: template
    name: "Pair Remote"
    on_press:
      - lambda: id(quietcool_fan_id).start_pairing();
```

Paired IDs survive reboots. `id(quietcool_fan_id).clear_paired_remotes()`
forgets them all.

### Setting the ID manually

If you already know your remote's 7-byte ID (e.g. from an RTL-SDR capture),
set `remote_id:` in YAML — it is stored as the primary (TX) identity.

### Example Configuration

```yaml
fan:
  - platform: quiet_cool
    id: quietcool_fan_id
    name: QuietCool fan
    cs_pin: 15
    gdo0_pin: 13
    gdo2_pin: 12
    # Optional:
    # remote_id: [0x2D, 0xD4, 0x06, 0xCB, 0x00, 0xF7, 0xF2]
    # center_freq_mhz: 433.897
    # deviation_khz: 10
    # speed_count: 3
```

---

### QuietCool YAML Configuration Options

| Option            | Required | Type         | Default    | Description                                                                 |
|-------------------|----------|--------------|-----------|-----------------------------------------------------------------------------|
| `name`            | Yes      | string       |           | The name of the fan in Home Assistant.                                      |
| `platform`        | Yes      | string       |           | Must be `quiet_cool`.                                                       |
| `cs_pin`          | Yes      | int          |           | SPI chip select pin for CC1101.                                             |
| `gdo0_pin`        | Yes      | int          |           | GDO0 pin from CC1101 (packet RX interrupt + TX status).                     |
| `gdo2_pin`        | Yes      | int          |           | GDO2 pin from CC1101 (wired but currently unused).                          |
| `remote_id`       | No       | list[hex]    |           | 7-byte remote ID. Omit to use pairing mode instead (see above).             |
| `center_freq_mhz` | No       | float        | 433.897    | Center frequency in MHz for RF transmission.                                |
| `deviation_khz`   | No       | float        | 10         | Frequency deviation (spread) in kHz for FSK modulation.                     |
| `speed_count`     | No       | int          | 3          | Number of fan speeds: 2 (LOW/HIGH) or 3 (LOW/MEDIUM/HIGH).                 |

**Note:** You must also define the SPI bus pins in your YAML:

```yaml
spi:
  clk_pin: 18
  mosi_pin: 23
  miso_pin: 19
```

## Architecture

The component runs all radio I/O in a **dedicated FreeRTOS task** so packet
reception never competes with WiFi, the Home Assistant API, or a Bluetooth
proxy for main-loop time:

- **Interrupt-driven RX**: the CC1101's GDO0 line raises an interrupt at each
  packet end; the radio task drains the FIFO within microseconds, decodes the
  packet, and hands the result to ESPHome through a queue. Remote button
  presses are captured even while the main loop is stalled.
- **Queued, non-blocking TX**: commands from Home Assistant are queued to the
  radio task; the ESPHome loop never waits on the radio. GDO0 waits are
  bounded, so a wiring fault logs an error instead of rebooting the device.
- **Bidirectional state sync**: commands from the physical remote (and the
  fan's own responses) update Home Assistant state, with 1-second
  deduplication of the remote's burst repeats.
- **WAKE polling**: the radio task queries the fan's state every 30 seconds,
  and ~2 seconds after every transmitted command — so if the fan didn't hear
  a command, Home Assistant corrects itself instead of lying.
- **Pairing**: remote IDs are learned over the air and persisted to flash.
- **Pure protocol core**: packet encode/decode lives in `protocol.{h,cpp}`
  with no hardware dependencies, covered by host-side unit tests (`tests/`).

## Unit tests

```
make -C tests run
```

Builds with plain `g++` (Catch2 vendored) and covers packet round-trips,
golden packets from the original RF captures, corruption rejection, pairing
ID extraction, speed mapping, and CC1101 register math. They run in CI on
every push.

# Run it on arduino (legacy)
The `arduino/` directory is the original proof-of-concept and is **not
maintained**; it predates the ESPHome component and uses a different
(bit-banged) TX approach. The arduino code doesn't do much by itself.  But it gets you going:

```
cd arduino
platformio run --target upload
```

## Hardware Requirements

- ESP32 DOIT DevKit V1
- CC1101 RF Module
- Wiring connections as follows:
  ```
  CC1101 Pin -> ESP32 Pin
  GND       -> GND
  VCC       -> 3.3V
  GDO0      -> GPIO13
  CSn       -> GPIO15
  SCK       -> GPIO18
  MOSI      -> GPIO23
  MISO      -> GPIO19
  GDO2      -> GPIO12
  ```

## Case
Here's a case for the parts that I used.

The source is here [in OnShape](https://cad.onshape.com/documents/23ba2be84b2f4dcf9bfd73fc/v/3fa4bea9b4bb4353b99b35e1/e/0985333c7a3d43ad0383c966)

### Step File

* The whole design: [QuietCoolCase.step](3d/step/QuietCoolCase.step)
* Or just the Top: [QuietCoolCaseTop.step](3d/step/QuietCoolCaseTop.step)
* and just the Bottom: [QuietCoolCaseBottom.step](3d/step/QuietCoolCaseBottom.step)

## Software Requirements

- ESPHome (or PlatformIO for the legacy arduino demo)


## Features

- Controls Quiet Cool fan system via RF signals
- Supports multiple fan speeds and timer durations
  - Speed
    - L
    - M
    - H
  - Duration
    - 1
    - 2
    - 4
    - 8
    - 12
    - off
    - on
- **Configurable remote ID** for compatibility with different remotes
- **Bidirectional state sync** — physical remote commands and WAKE polling keep Home Assistant in sync with the fan

# Reverse Engineering
I used an RTL-SDR.COM SDR like this:
![RTL-SDR v3](images/rtl-sdrv3-500.jpg)

And for software, I used Universal Radio Haacker.  Here's a recording of all the signals as they progress from H 1 2 4 8 12 ON OFF -> to medium -> low.  It was recorded with these settings

![Settings](images/record-settings.png)

[Download the raw recording](recordings/RTL-SDR-20250510_080358-433_92MHz-1MSps-1MHz.complex16s.gz)

![Here's the signal demodulated](images/urh-demodulated.png)

![Spectrum Analysis](images/spectrum.png)

## Modulation
This remote uses FSK modulation at 433.92MHz.  The CC1101 RF module ended up transmitting high, so I reduced the frequency in the code to 433.897 to make it work.  If things don't work, check the actual frequency.


## CC1101 Configuration

The system operates at 433.897 MHz and uses 2-FSK modulation. The CC1101 module is configured for:
- Fixed-length packet mode (20 bytes)
- Hardware sync word detection (0x15AA)
- Hardware preamble insertion (TX) and sync stripping (RX)
- APPEND_STATUS enabled (RSSI + LQI appended to RX packets)
- 10 dBm TX power

## Building and Uploading

```pio run --target upload```

# TODO
* Implement the timers in the esphome UI

## License
MIT License

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.
