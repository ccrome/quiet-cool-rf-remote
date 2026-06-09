# CC1101 RX Debug Log

## Goal
Reliably capture consecutive packets from the physical QuietCool remote via CC1101. State sync to HA works when packets ARE decoded, but we're missing button presses.

## Hardware
- ESP32 + CC1101 transceiver
- QuietCool remote sends 3-packet bursts per button press (WAKE + CMD + CMD)
- Each packet: 20 data bytes + 2 APPEND_STATUS = 22 bytes in FIFO
- Data rate: 2.398 kbps, ~67ms per packet, ~18ms gap between packets
- CC1101 RX FIFO: 64 bytes

## Configuration (unchanged unless noted)
- RXOFF_MODE=11 (stay in RX after packet) — since iteration 1a
- IOCFG0=0x06 (GDO0 = sync word sent/received)
- Fixed 20-byte packet length, sync word 0x15AA
- No CRC (remote uses duplicated command bytes instead)
- MCSM0 FS_AUTOCAL=01 (calibrate on IDLE->RX)
- SyncMode=2 (16/16) — iterations 0-2; SyncMode=1 (15/16) — iteration 3+
- PQT=0 (explicit since iteration 3; was implicitly 0 via Init() before)

## Iteration History

### Iteration 0: RXOFF_MODE=00 (IDLE after packet)
- **Capture rate:** ~30%
- **Issue:** 800us IDLE->RX blind time misses CMD packets in burst

### Iteration 1a: RXOFF_MODE=11 + no GDO0 gate
- **Capture rate:** ~80% (5/6 packets)
- **Issue:** 1 corrupted packet (0x1F instead of 0x9F) — TI FIFO errata hit

### Iteration 1b: RXOFF_MODE=11 + strict GDO0 gate
- **Capture rate:** ~50% (2/4 presses)
- **Issue:** GDO0 HIGH for ~67ms per packet, only ~18ms LOW windows. ESPHome polling loop misses narrow windows. 3+ unread packets overflow 64-byte FIFO.

### Iteration 2: Dual-condition FIFO read + burst drain loop
- **Changes:**
  - Double-read RXBYTES (TI recommendation, use lower value)
  - Dual condition: read when rxbytes>=44 (2+ pkts, safe without GDO0) OR rxbytes>=22 && GDO0==LOW
  - While loop drains all complete packets per loop() call (max 5)
  - FIFO alignment recovery: flush if 0 < residual < 22 after drain
  - Strict command validation in processPacket() (reject invalid speed/duration nibbles)
  - Diagnostic counters: rx_packet_count_, overflow_count_, gdo0_blocked_count_ (logged every 10s)
- **Compile:** SUCCESS
- **Upload:** SUCCESS (compiled Feb 13 2026, 20:36:51)
- **Capture rate:** ~33% (1 packet per press instead of 3)
- **Observations:**
  - `gdo0_blocked=0` — GDO0 gate was NEVER the bottleneck. Every time rxbytes>=22, GDO0 was already LOW.
  - `overflows=0` — No FIFO overflows detected.
  - No "FIFO misaligned" warnings fired.
  - Only 1 packet received per button press (WAKE from one press, OFF from another).
  - Packets 2 and 3 of each burst never appeared in FIFO.
  - MARCSTATE=0x0D (RX) consistently — radio stays in RX.
  - **Conclusion: The dual-condition change had zero effect.** The problem is NOT FIFO read timing or GDO0 blocking. The CC1101 is failing to re-sync on the sync word for subsequent packets in a burst after receiving packet 1.

### Iteration 3: Relaxed sync mode (15/16 bits) + PQT=0 (CURRENT)
- **Root cause hypothesis:** After the ~18ms inter-packet gap, the CC1101's bit synchronizer loses lock. When the next packet starts with 0x15 (00010101 — not a clean preamble byte), the bit sync isn't perfectly re-locked. With strict 16/16 sync word match, even 1-bit error causes the entire packet to be missed.
- **Research (TI E2E, errata SWRZ020, forums):**
  - CC1101 sync detection failure after inter-packet gaps is a known issue
  - Bit synchronizer needs preamble-like transitions to re-lock; 0x15 is marginal
  - PQT (Preamble Quality Threshold) gates sync detection — if nonzero, CC1101 won't even search for sync without valid preamble
  - 15/16 sync mode tolerates 1-bit error, significantly improves re-acquisition
  - PKTCTRL1 Init() default sets PQT=0 (0x04), but later library calls could change it
- **Changes:**
  - `setSyncMode(1)` — 15/16 sync word bits (was 2 = 16/16)
  - `setPQT(0)` — explicit, ensures no preamble quality gate
  - Register readback logging at init: MDMCFG2 (sync mode), PKTCTRL1 (PQT), SYNC words, BSCFG, AGCCTRL
- **Validation:** Look for `Register readback: MDMCFG2=0x__ (SYNC_MODE=1)` in boot logs to confirm setting applied
- **Compile:** SUCCESS
- **Upload:** SUCCESS (OTA)
- **Capture rate:** ~50-60% (10 real packets from ~8 presses; up from ~17% in iteration 2)
- **Observations:**
  - **Major improvement:** Got 2-3 packets per press (was 1). First time ever capturing 3/3 burst packets (3x WAKE at 20:59:51-52).
  - `overflows=0`, `gdo0_blocked=0` — no FIFO issues.
  - **New problem: consistent bit-7 corruption** on ~20% of packets:
    - 0x9F (LOW) → 0x1F — MSB flipped: `1001_1111` → `0001_1111`
    - 0xBF (HIGH) → 0x3F — MSB flipped: `1011_1111` → `0011_1111`
    - Both have good RSSI (-74/-75 dBm, LQI=19-20) — not noise
    - Hypothesis: When 15/16 sync matches with 1-bit error, data framing shifts by 1 bit, causing systematic MSB corruption in command bytes
  - Command validation correctly caught both corrupted packets (never reached HA state).
  - More noise false-syncs (RSSI=-106/-109, LQI=127) — expected with relaxed sync. ID check filters them.
  - Register readback not visible in API logs (runs during setup() before WiFi connects). Need to move to dump_config() or periodic log.
- **Conclusion:** 15/16 sync mode confirms the hypothesis — bit sync re-lock IS the problem. But the 1-bit tolerance introduces ~20% corruption rate. Need a way to either (a) improve bit sync re-lock quality, or (b) correct the 1-bit shift in software.

### Iteration 3b: FOCCFG=0x1D (wide AFC) + bit-7 correction + PREP filtering
- **Research:** Internet search confirmed 15/16 sync does NOT cause bit-shifted data. MSB corruption is from bit synchronizer not being properly locked — poor bit sync shifts sampling from optimal eye opening, MSB corrupted first.
- **Changes:**
  - `FOCCFG=0x1D` (was 0x16) — wider frequency offset compensation, max post-sync gain
  - `BSCFG=0x1C` — already at recommended value, no change
  - Bit-7 correction in processPacket(): when speed fails validation, try `cmd | 0x80` and re-validate
  - PREP packet filtering: duration==0x00 packets logged as "PREP (wake preamble, ignored)", don't set last_rx_cmd_.valid
  - Register readback moved to first 10s periodic log (visible via API)
- **Confirmed registers:** `MDMCFG2=0x01(SYNC_MODE=1) PKTCTRL1=0x04(PQT=0) FOCCFG=0x1D BSCFG=0x1C`
- **Capture rate:** ~60-70% of real packets, ~28 packets from ~8 presses
- **Observations:**
  - **PREP filtering working:** `RX: LOW PREP (wake preamble, ignored)` — no longer sets HA state incorrectly
  - **Bit-7 correction working:** `BIT7 CORRECTED 0x1F -> 0x9F` and `0x3F -> 0xBF` — recovered packets that were previously rejected
  - **Remaining problem:** Still miss CMD packets in some bursts. Got 3x WAKE but 0x CMD for 2 consecutive presses (21:18:41 and 21:18:58). HA stuck at last known state.
  - **Pattern:** WAKE packets (first in burst) are reliably captured. CMD packets (later in burst) still have ~30-40% miss rate.
  - **New discovery — remote burst structure:** WAKE(0x66) x3 → PREP(speed+0x00) x1 → CMD(speed+0x0F) x1-2. The CMD packets come last and are most likely to be missed by sync re-acquisition issues.
  - Some FIFO misalignment detected and recovered (3 residual bytes).

#### Remaining problem: CMD packets at end of burst missed
The remote sends WAKE first, PREP second, CMD last. The CC1101 reliably syncs on WAKE (first packet after long silence) but struggles to re-sync on PREP and CMD (later packets with only ~18ms gaps). Even with 15/16 sync and wide FOCCFG, ~30-40% of CMD packets are lost.

#### Next ideas for iteration 4:
1. **30/32 sync word with 0xAAAA15AA** — Use 4-byte sync with the AA preamble included, giving the bit sync more time to lock before the critical 0x15 byte. Would need to adjust packet length and offsets.
2. **Infer CMD from PREP** — If we receive PREP (speed+0x00) but miss CMD, we know which speed the user selected. Could use PREP as a fallback signal.
3. **Async/transparent mode** — Bypass CC1101 packet engine entirely, do sync detection in software on ESP32. Full control but much more complex.
4. **AGC tuning** — AGCCTRL registers control gain settling speed. Faster AGC settling after inter-packet gap could help re-acquisition.

## Test Protocol
1. Wait for clean MARCSTATE=0x0D in logs
2. Press: Off -> High -> Low -> Off with 2-3 second gaps
3. Expected: every press produces `RX pkt:` + decoded command + state sync
4. Watch for: `gdo0_blocked` count (should stay low), `FIFO misaligned` warnings, `RX: INVALID` logs

## Key Diagnostic Lines to Watch
```
Register readback: MDMCFG2=0x__ (SYNC_MODE=1) -- confirm sync mode applied (boot only)
  SYNC=0x15AA, BSCFG=0x__, AGCCTRL=...       -- bit sync and AGC settings (boot only)
CC1101 MARCSTATE: 0x0D (0x0D = RX)          -- confirms radio in RX
RX stats: pkts=N, overflows=N, gdo0_blocked=N -- packet/error counters
RX pkt: AA AA AA ...                          -- raw packet hex
RX: LOW / HIGH / OFF / WAKE                   -- decoded command
RX sync: ON speed=N / OFF                     -- state pushed to HA
RX FIFO overflow - recovering                 -- overflow hit
FIFO misaligned (N residual bytes) - flushing -- alignment recovery fired
RX: INVALID speed/duration nibble             -- corruption caught by validation
```
