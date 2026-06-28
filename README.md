# M031BSP_SPI_SGPIO

M031 SGPIO target/slave example for validating SGPIO initiator traffic.

Update: `2026/06/23`

## Overview

- MCU / Series: `M031`
- Board: `M032 EVB or compatible M031/M032 target board`
- Toolchain: `Keil uVision5`
- Purpose:
  - Capture SGPIO frames from an external SGPIO initiator/master.
  - Decode `SDataOut` per-slot `ACT / LOCATE / FAIL` bits.
  - Report `SLOAD L0..L3 Raw` as invalid in this SPI0_SS path because PA3 is used as hardware SS, not sampled as GPIO on every SCLK edge.
  - Keep SGPIO RX processing interrupt-driven and non-blocking so the heartbeat LED and main loop keep running.

## Hardware

- Debug UART:
  - `UART0`
  - `PB12 = UART0_RXD`, `PB13 = UART0_TXD`
  - Terminal setting: `115200 8N1`
- Heartbeat LED:
  - `PB14`
  - Toggles from the timer service; if it stops, SGPIO handling is blocking the main loop or IRQ path.
- Main peripheral(s):
  - SGPIO target/slave receive path implemented by PA3/SLOAD as SPI0_SS plus SPI0 slave RX.
  - Capture is based on SPI0 sampling of PA0/SDATAOUT by PA2/SCLK while PA3/SLOAD is SS-active low.
  - No `SDATA IN` target-to-initiator transmit path is implemented in this stage.
- Test equipment:
  - External SGPIO initiator/master.
  - Logic analyzer for `SCLK`, `SLOAD`, and `SDATA OUT`.

## Pin Map

| Function | Pin | Direction | Note |
| --- | --- | --- | --- |
| `UART0_RXD` | `PB12` | Input | Debug UART RX |
| `UART0_TXD` | `PB13` | Output | Debug UART TX |
| `HEARTBEAT_LED` | `PB14` | Output | Toggles while main loop is alive |
| `SGPIO_SLOAD` | `PA3` | Input | `SPI0_SS`, active-low frame gate |
| `SGPIO_SDATAOUT` | `PA0` | Input | `SPI0_MOSI`, captured by SPI0 RX |
| `SGPIO_SCLK` | `PA2` | Input | `SPI0_CLK` |
| `GND` | `GND` | Ground | Common ground with SGPIO initiator |

The SPI0 receiver path is fixed to `PA0/PA2/PA3`. The old `PA6/PA7/PC6` GPIO-sampling wiring is not supported by this project.

External SGPIO wiring:

| Initiator signal | Direction into M032 | M032 pin |
| --- | --- | --- |
| `SCLK` | Input clock | `PA2 SPI0_CLK` |
| `SDATA OUT` | Input data | `PA0 SPI0_MOSI` |
| `SLOAD` | Input frame marker | `PA3 SPI0_SS` |
| `GND` | Common reference | `GND` |

## Build Environment

- BSP / SDK: Nuvoton M031 BSP.
- IDE / compiler: Keil uVision5 project.
- Project file: [`SampleCode/Template/Keil/Template.uvprojx`](SampleCode/Template/Keil/Template.uvprojx)
- Main source path: [`SampleCode/Template`](SampleCode/Template)
- Output image: Keil project output under `SampleCode/Template/Keil/obj/`.

## Project Layout

- [`SampleCode/Template/main.c`](SampleCode/Template/main.c)
  - System init, UART0 init, timer service, heartbeat LED, and calls to `SGPIO_Init()` / `SGPIO_Process()`.
- [`SampleCode/Template/sgpio_slave.c`](SampleCode/Template/sgpio_slave.c)
  - SGPIO PA3 SPI0_SS frame gate, SPI0 RX capture, decode, stability filter, and debug log.
- [`SampleCode/Template/sgpio_slave.h`](SampleCode/Template/sgpio_slave.h)
  - SGPIO pin macros and public API.
- [`SampleCode/Template/timer_service.c`](SampleCode/Template/timer_service.c)
  - Timer task dispatch used by the heartbeat path.
- [`docs/SGPIO_PROTOCOL_CONTRACT.md`](docs/SGPIO_PROTOCOL_CONTRACT.md)
  - Cross-project SGPIO protocol contract between the SGPIO initiator and this M032 target.

## Firmware Behavior

### Power-on

Expected boot log includes the timer task IDs and SGPIO pin-role messages:

```text
task1 id = 0
task2 id = 1
task3 id = 2
PA3/SLOAD SPI0_SS frame gate
PA0/SDATAOUT SPI0_MOSI input
PA2/SCLK SPI0_CLK input
SGPIO SPI0 SS-gated RX path, no SDATAIN TX
```

Reference power-on log:

<img src="./Log_power_on.jpg" alt="M032 SGPIO power-on UART log" width="900">

### Main Loop

The main loop must remain lightweight:

1. `TimerService_Dispatch()` keeps periodic tasks alive.
2. `SGPIO_Process()` only finalizes completed frames, copies the ISR-built result, decodes it, runs application behavior, and prints rate-limited logs.
3. SGPIO bit capture is not done in the polling loop; PA3/SLOAD is SPI0_SS and SPI0 IRQ drains the RX FIFO.
4. `printf()` must not be called from the SGPIO ISR.

### Application Hook

Accepted SGPIO frames are copied into `local_frame`, decoded into `act_mask / locate_mask / fail_mask`, and then passed to the application hook:

```c
sgpio_app_frame(&local_frame);
```

`sgpio_app_frame()` walks each valid slot and calls `sgpio_app_slot(slot, act, locate, fail)`. `sgpio_app_slot()` is the intended place to add product behavior with a slot-based `switch` statement.

Example use:

```c
case 0U:
    if (act != 0U)
    {
        /* Slot 0 ACT asserted: set the expected device/GPIO/LED state here. */
    }
    if (locate != 0U)
    {
        /* Slot 0 LOCATE asserted: set the expected device/GPIO/LED state here. */
    }
    if (fail != 0U)
    {
        /* Slot 0 FAIL asserted: set the expected device/GPIO/LED state here. */
    }
    break;
```

The application hook runs after a stable frame is accepted and is intentionally outside the debug `printf()` rate limit. Keep hook code non-blocking. Prefer setting outputs to the decoded state instead of toggling on every repeated SGPIO frame, unless repeated toggling is the intended behavior.

### SGPIO Signal Handling Flow

The current receiver routes `SLOAD` to SPI0_SS and uses SPI0 as the SDATAOUT shift receiver.

1. `SLOAD` is normally high before a frame.
2. The SGPIO initiator drives `SLOAD` low for the low-sync run.
3. The first SPI0_SS active interrupt marks the low-sync section and waits for the data section.
4. The restart-marker high pulse makes SPI0_SS inactive, so SPI0 does not store the marker bit as slot data.
5. The next SPI0_SS active interrupt starts SDATAOUT capture.
6. SPI0 captures `SDATA OUT` from PA0 on PA2/SCLK rising edges in 8-bit LSB-first units.
7. `SLOAD L0..L3 Raw` is not decoded in the SPI0 path because PA3 is not sampled on every SCLK edge.
8. Each slot consumes three `SDATA OUT` bits in this order:
    - bit 0: `ACT`
    - bit 1: `LOCATE`
    - bit 2: `FAIL`
9. `SGPIO_Process()` finalizes the frame after `SGPIO_FRAME_GAP_TIMEOUT_MS` with no new SPI0 RX data, rearms SPI0, then decodes and logs the result.

```mermaid
flowchart TD
    A["Idle: SLOAD high / SPI0_SS inactive"] --> B["SLOAD low / SPI0_SS active"]
    B --> C["First SS active: wait for payload"]
    C --> D["Restart marker: SLOAD high / SPI0_SS inactive"]
    D --> E["Next SS active: start SPI0 RX capture"]
    E --> F["SPI0 captures SDATAOUT on SPI0_MOSI with SPI0_CLK"]
    F --> G["Store SDATAOUT slot triplets: ACT, LOCATE, FAIL"]
    G --> N{"No SPI0 RX before frame-gap timeout?"}
    N -->|No| F
    N -->|Yes| O["SGPIO_Process finalizes frame"]
    O --> P["Copy ISR result and decode masks"]
    P --> Q["Run sgpio_app_frame application hook"]
    Q --> R["Rate-limit debug log"]
```

Important ISR rule:

- `SPI0_IRQHandler()` handles SPI0_SS active events and drains the SPI0 RX FIFO.
- `SPI0_IRQHandler()` must remain short; it does not print or decode.
- PA3/SLOAD in-frame `L0..L3` values are not sampled by this path. The log reports `sload valid=0`.
- Because PA3 is hardware SPI0_SS, SLOAD must stay low during the SDATAOUT payload. If SLOAD toggles high during payload, SPI0 will stop shifting during that high interval.

## Raw Bit Decode

Raw SGPIO bytes are printed in capture order and decoded __LSB-first__.

For a raw byte:

```text
raw byte = 0x38
binary   = 0011 1000  (normal MSB-left display)
LSB bits = bit0..bit7 = 0,0,0,1,1,1,0,0
```

The SGPIO decoder consumes the __LSB__ bit stream:

```text
bit index = byte_index * 8 + bit_in_byte
bit value = (raw[byte_index] >> bit_in_byte) & 0x01
```

Slot mapping:

```text
Slot N ACT    = bit (N * 3 + 0)
Slot N LOCATE = bit (N * 3 + 1)
Slot N FAIL   = bit (N * 3 + 2)
```

SFF-8485 OD bit position mapping:

- `ODx.y` is the SFF-8485 SDataOut bit position name.
- `x` is the slot / drive index.
- `y` is the bit index inside that slot triplet.
- Current decoder semantics map `ODx.0 -> ACT`, `ODx.1 -> LOCATE`, and `ODx.2 -> FAIL`.

| SFF-8485 bit | Raw bit index | Raw byte.bit | Current semantic | Mask bit |
| --- | ---: | --- | --- | ---: |
| `OD0.0` | 0 | `raw[0].bit0` | `Slot 0 ACT` | 0 |
| `OD0.1` | 1 | `raw[0].bit1` | `Slot 0 LOCATE` | 0 |
| `OD0.2` | 2 | `raw[0].bit2` | `Slot 0 FAIL` | 0 |
| `OD1.0` | 3 | `raw[0].bit3` | `Slot 1 ACT` | 1 |
| `OD1.1` | 4 | `raw[0].bit4` | `Slot 1 LOCATE` | 1 |
| `OD1.2` | 5 | `raw[0].bit5` | `Slot 1 FAIL` | 1 |
| `OD2.0` | 6 | `raw[0].bit6` | `Slot 2 ACT` | 2 |
| `OD2.1` | 7 | `raw[0].bit7` | `Slot 2 LOCATE` | 2 |
| `OD2.2` | 8 | `raw[1].bit0` | `Slot 2 FAIL` | 2 |
| `OD3.0` | 9 | `raw[1].bit1` | `Slot 3 ACT` | 3 |
| `OD3.1` | 10 | `raw[1].bit2` | `Slot 3 LOCATE` | 3 |
| `OD3.2` | 11 | `raw[1].bit3` | `Slot 3 FAIL` | 3 |
| `OD4.0` | 12 | `raw[1].bit4` | `Slot 4 ACT` | 4 |
| `OD4.1` | 13 | `raw[1].bit5` | `Slot 4 LOCATE` | 4 |
| `OD4.2` | 14 | `raw[1].bit6` | `Slot 4 FAIL` | 4 |
| `OD5.0` | 15 | `raw[1].bit7` | `Slot 5 ACT` | 5 |
| `OD5.1` | 16 | `raw[2].bit0` | `Slot 5 LOCATE` | 5 |
| `OD5.2` | 17 | `raw[2].bit1` | `Slot 5 FAIL` | 5 |
| `OD6.0` | 18 | `raw[2].bit2` | `Slot 6 ACT` | 6 |
| `OD6.1` | 19 | `raw[2].bit3` | `Slot 6 LOCATE` | 6 |
| `OD6.2` | 20 | `raw[2].bit4` | `Slot 6 FAIL` | 6 |
| `OD7.0` | 21 | `raw[2].bit5` | `Slot 7 ACT` | 7 |
| `OD7.1` | 22 | `raw[2].bit6` | `Slot 7 LOCATE` | 7 |
| `OD7.2` | 23 | `raw[2].bit7` | `Slot 7 FAIL` | 7 |
| `OD8.0` | 24 | `raw[3].bit0` | `Slot 8 ACT` | 8 |
| `OD8.1` | 25 | `raw[3].bit1` | `Slot 8 LOCATE` | 8 |
| `OD8.2` | 26 | `raw[3].bit2` | `Slot 8 FAIL` | 8 |
| `OD9.0` | 27 | `raw[3].bit3` | `Slot 9 ACT` | 9 |
| `OD9.1` | 28 | `raw[3].bit4` | `Slot 9 LOCATE` | 9 |
| `OD9.2` | 29 | `raw[3].bit5` | `Slot 9 FAIL` | 9 |
| `OD10.0` | 30 | `raw[3].bit6` | `Slot 10 ACT` | 10 |
| `OD10.1` | 31 | `raw[3].bit7` | `Slot 10 LOCATE` | 10 |
| `OD10.2` | 32 | `raw[4].bit0` | `Slot 10 FAIL` | 10 |
| `OD11.0` | 33 | `raw[4].bit1` | `Slot 11 ACT` | 11 |
| `OD11.1` | 34 | `raw[4].bit2` | `Slot 11 LOCATE` | 11 |
| `OD11.2` | 35 | `raw[4].bit3` | `Slot 11 FAIL` | 11 |
| `OD12.0` | 36 | `raw[4].bit4` | `Slot 12 ACT` | 12 |
| `OD12.1` | 37 | `raw[4].bit5` | `Slot 12 LOCATE` | 12 |
| `OD12.2` | 38 | `raw[4].bit6` | `Slot 12 FAIL` | 12 |
| `OD13.0` | 39 | `raw[4].bit7` | `Slot 13 ACT` | 13 |
| `OD13.1` | 40 | `raw[5].bit0` | `Slot 13 LOCATE` | 13 |
| `OD13.2` | 41 | `raw[5].bit1` | `Slot 13 FAIL` | 13 |
| `OD14.0` | 42 | `raw[5].bit2` | `Slot 14 ACT` | 14 |
| `OD14.1` | 43 | `raw[5].bit3` | `Slot 14 LOCATE` | 14 |
| `OD14.2` | 44 | `raw[5].bit4` | `Slot 14 FAIL` | 14 |
| `OD15.0` | 45 | `raw[5].bit5` | `Slot 15 ACT` | 15 |
| `OD15.1` | 46 | `raw[5].bit6` | `Slot 15 LOCATE` | 15 |
| `OD15.2` | 47 | `raw[5].bit7` | `Slot 15 FAIL` | 15 |

Example:

```text
raw: 38 8E C3 00

0x38 LSB bits: 0 0 0 1 1 1 0 0
0x8E LSB bits: 0 1 1 1 0 0 0 1
0xC3 LSB bits: 1 1 0 0 0 0 1 1
0x00 LSB bits: 0 0 0 0 0 0 0 0
```

Grouped by slot triplets:

```text
S0=000
S1=111
S2=000
S3=111
S4=000
S5=111
S6=000
S7=011
```

In each `Sx=abc` triplet:

- `a = ACT`
- `b = LOCATE`
- `c = FAIL`

The log also prints mask form:

```text
masks: ACT=0x002A LOCATE=0x00AA FAIL=0x00AA
slots: ACT=1,3,5 LOCATE=1,3,5,7 FAIL=1,3,5,7
```

Mask bit meaning:

- bit 0 = Slot 0
- bit 1 = Slot 1
- bit 2 = Slot 2
- and so on

## Captured Decode Examples

Logic analyzer channels used by the captured examples:

- `CH0 : SCLOCK`
- `CH1 : SDATA OUT`
- `CH2 : SLOAD`

Decode rule reminder:

- `SLOAD` low run plus the `SLOAD=1` restart marker defines the frame boundary.
- In this SPI0_SS build, the restart-marker high pulse makes SS inactive and is not stored as SDATAOUT payload.
- `SDATA OUT` is captured by SPI0 on `SCLOCK` rising edges after `SLOAD` returns low for the payload section.
- Each slot is decoded as a 3-bit triplet: `Sx=ACT,LOCATE,FAIL`.
- Raw bytes are decoded LSB-first, so `raw: 38 8E C3 00` does not read like a normal MSB-left binary string.

| Example | Raw bytes | Slot triplets | Masks | Decoded active slots |
| --- | --- | --- | --- | --- |
| 8-slot alternating | `38 8E C3 00` | `S0=000 S1=111 S2=000 S3=111 S4=000 S5=111 S6=000 S7=011` | `ACT=0x002A LOCATE=0x00AA FAIL=0x00AA` | `ACT=1,3,5`, `LOCATE=1,3,5,7`, `FAIL=1,3,5,7` |
| 8-slot mixed | `78 9C 24 00` | `S0=000 S1=111 S2=100 S3=011 S4=100 S5=100 S6=100 S7=100` | `ACT=0x00F6 LOCATE=0x000A FAIL=0x000A` | `ACT=1,2,4,5,6,7`, `LOCATE=1,3`, `FAIL=1,3` |
| 16-slot repeating | `11 15 51 11 15 51` | `S0=100 S1=010 S2=001 S3=010 S4=100 S5=010 S6=001 S7=010`, `S8=100 S9=010 S10=001 S11=010 S12=100 S13=010 S14=001 S15=010` | `ACT=0x1111 LOCATE=0xAAAA FAIL=0x4444` | `ACT=0,4,8,12`, `LOCATE=1,3,5,7,9,11,13,15`, `FAIL=2,6,10,14` |

### Example: `38 8E C3 00`

The LA annotation groups `SDATA OUT` bits into slot triplets after the SLOAD restart marker. The UART log confirms the same raw bytes, triplets, masks, and slot list.

<img src="./LA_38_8E_C3_00.jpg" alt="SGPIO logic analyzer decode for raw 38 8E C3 00" width="960">

Current SPI0_SS logic analyzer capture:

<img src="./LA_38_8E_C3_00%232.jpg" alt="SGPIO SPI0_SS logic analyzer capture for raw 38 8E C3 00" width="960">

<img src="./Log_38_8E_C3_00.jpg" alt="M032 SGPIO UART decode log for raw 38 8E C3 00" width="900">

### Example: `78 9C 24 00`

This example verifies that mixed slot states are not limited to all-on/all-off patterns. Slot 2 and slots 4 through 7 assert only `ACT`, while slots 1 and 3 assert all three signals differently according to the triplet values.

<img src="./LA_78_9C_24_00.jpg" alt="SGPIO logic analyzer decode for raw 78 9C 24 00" width="960">

Current SPI0_SS logic analyzer capture:

<img src="./LA_78_9C_24_00%232.jpg" alt="SGPIO SPI0_SS logic analyzer capture for raw 78 9C 24 00" width="960">

<img src="./Log_78_9C_24_00.jpg" alt="M032 SGPIO UART decode log for raw 78 9C 24 00" width="900">

### Example: `11 15 51 11 15 51`

This is a full 16-slot capture. The log prints `S0..S7` and `S8..S15` separately so the 48-bit SDataOut stream can be checked without wrapping into an unreadable single line.

<img src="./LA_11_15_51_11_15_51.jpg" alt="SGPIO logic analyzer decode for raw 11 15 51 11 15 51" width="960">

Current SPI0_SS logic analyzer capture:

<img src="./LA_11_15_51_11_15_51%232.jpg" alt="SGPIO SPI0_SS logic analyzer capture for raw 11 15 51 11 15 51" width="960">

<img src="./Log_11_15_51_11_15_51.jpg" alt="M032 SGPIO UART decode log for raw 11 15 51 11 15 51" width="900">

## Configuration

Main config files:

- [`SampleCode/Template/sgpio_slave.h`](SampleCode/Template/sgpio_slave.h)
- [`SampleCode/Template/sgpio_slave.c`](SampleCode/Template/sgpio_slave.c)

Important public SGPIO macros:

```c
#define SGPIO_SLAVE_MAX_SLOTS            (16U)
#define SGPIO_SLAVE_RX_MAX_BYTES         (8U)
```

The SPI0 receiver does not expose a selectable SGPIO pin map. PA0/PA2/PA3 are fixed because those pins map to SPI0_MOSI/SPI0_CLK/SPI0_SS on the selected M032 target.

Important internal timing/filter macros:

```c
#define SGPIO_LOW_SYNC_MIN_BITS            (5U)
#define SGPIO_DATA_BITS_PER_SLOT           (3U)
#define SGPIO_FRAME_GAP_TIMEOUT_MS         (5UL)
#define SGPIO_FRAME_ARM_TIMEOUT_MS         (20UL)
#define SGPIO_FRAME_LOG_FIRST_N            (1UL)
#define SGPIO_FRAME_LOG_MIN_INTERVAL_MS    (1000UL)
#define SGPIO_FRAME_STABLE_REQUIRED        (2U)
#define SGPIO_UNSTABLE_LOG_MIN_INTERVAL_MS (3000UL)
#define SGPIO_SPI_RX_UNIT_BITS             (8U)
```

## Test Flow

1. Connect the SGPIO initiator `SCLK/SDATA OUT/SLOAD/GND` to M032 `PA2/PA0/PA3/GND`.
2. Open a UART terminal on M032 `UART0`, `115200 8N1`.
3. Power on or reset M032.
4. Confirm the SGPIO startup log appears.
5. Confirm `PB14` heartbeat LED continues toggling.
6. Drive a valid SGPIO frame from the initiator.
7. Confirm the M032 UART log decodes the expected masks and slot list.
8. If frames are unstable, lower initiator `SCLK`, shorten wiring, and confirm the waveform with a logic analyzer.

## Validation

UART log should show valid frames such as:

```text
[SGPIO RX] frame #N
  capture: bits=26 bytes=4 valid=1 overflow=0 dropped=0 low_sync=5
  sload: L0..L3=0x0 valid=0
  masks: ACT=0x002A LOCATE=0x00AA FAIL=0x00AA
  raw: 38 8E C3 00
  bits: S0=000 S1=111 S2=000 S3=111 S4=000 S5=111 S6=000 S7=011
  slots: ACT=1,3,5 LOCATE=1,3,5,7 FAIL=1,3,5,7
```

For an 8-slot byte-padded SPI capture, the same payload may appear as `bits=32 bytes=4`; the extra padded bits should decode as zero.

External initiator result:

- Changing the initiator slot pattern should change M032 `ACT / LOCATE / FAIL` masks.

Scope / logic analyzer:

- `SLOAD` idles high between frames.
- A frame starts after `SLOAD` stays low for at least five `SCLK` rising edges.
- The `SLOAD=1` high pulse marks the restart marker and makes SPI0_SS inactive.
- After the restart marker, `SLOAD` must return low and remain low while SDATAOUT payload clocks are present.
- `SDATA OUT` must be stable around each `SCLK` rising edge.

## Troubleshooting

- If `PB14` heartbeat stops:
  - Check for excessive `printf()` output.
  - Keep SGPIO debug log rate-limited.
  - Confirm no busy-wait or FIFO-drain loop was reintroduced into `SGPIO_Process()`.
- If logs show frequent `unstable frame ignored`:
  - Lower the initiator `SCLK`.
  - Check common GND and wire length.
  - Capture `SCLK`, `SLOAD`, and `SDATA OUT` with a logic analyzer.
- If decoded slots are shifted:
  - Confirm `SLOAD` low-sync and restart marker are visible.
  - Confirm the initiator still uses five low-sync clocks before the restart marker.
  - Confirm data is interpreted LSB-first.
  - Confirm slot order is `ACT`, `LOCATE`, `FAIL`.
- If `valid=0` or `bits` is unexpected:
  - Check whether the frame ended too early or too late.
  - Review `SGPIO_FRAME_GAP_TIMEOUT_MS`.
  - Confirm the initiator slot count matches the intended test.

## Notes

- This implementation is RX-only for the M032 target.
- `SDATA IN` target-to-initiator transmit is intentionally not implemented yet.
- `PA0` is routed to `SPI0_MOSI`; `PA2` is routed to `SPI0_CLK`; `PA3` is routed to `SPI0_SS`.
- The first SLOAD low section is treated as low-sync; the next SLOAD low after the restart-marker high pulse starts SPI0 RX capture.
- `SLOAD L0..L3 Raw` is not sampled in the SPI0 path and is reported as invalid.
- The selected M032 package has no QSPI. This build intentionally uses SPI0 only.
- This M031/M032 SPI0 block does not expose the slave 3-wire `SLV3WIRE` control used by the M251 reference. PA3 is therefore routed to `SPI0_SS`; SLOAD must stay low during the SDATAOUT payload or SPI0 will stop shifting.
- The old `PA6/PA7/PC6` GPIO-sampling wiring is not available in this SPI0 receiver build.
- Keep `SGPIO_Process()` non-blocking. It should finalize/copy/decode/print only.
- Keep `sgpio_app_frame()` and `sgpio_app_slot()` non-blocking. Use them for slot `ACT / LOCATE / FAIL` application behavior after stable frame decode.
- Keep capture ownership in the PA3 SPI0_SS gate plus SPI0 RX path unless the timing model is re-evaluated.
- Before publishing, confirm this README contains no local absolute paths or sensitive directory names.

## Related Files

- [`SampleCode/Template/main.c`](SampleCode/Template/main.c)
- [`SampleCode/Template/sgpio_slave.c`](SampleCode/Template/sgpio_slave.c)
- [`SampleCode/Template/sgpio_slave.h`](SampleCode/Template/sgpio_slave.h)
- [`SampleCode/Template/timer_service.c`](SampleCode/Template/timer_service.c)
- [`SampleCode/Template/Keil/Template.uvprojx`](SampleCode/Template/Keil/Template.uvprojx)
- [`docs/SGPIO_PROTOCOL_CONTRACT.md`](docs/SGPIO_PROTOCOL_CONTRACT.md)

## Reference Specifications

- SFF-8485: Serial GPIO bus framing and signal timing.
- SFF-8489: IBPI interpretation for drive slot indicators.

## Revision

- `2026/06/23`: routed PA3/SLOAD to SPI0_SS because M032 SPI0 slave needs hardware SS active and the selected M032 package has no QSPI slave 3-wire path.
- `2026/06/23`: replaced QSPI capture with SPI0 capture because the selected M032 package has no QSPI.
- `2026/06/23`: intermediate test changed SGPIO RX path to PA3 GPIO falling IRQ plus SPI peripheral SDATAOUT capture on PA0/PA2; superseded by the PA3 SPI0_SS path above.
- `2026/06/23`: documented `sgpio_app_frame()` / `sgpio_app_slot()` application hook for slot `ACT / LOCATE / FAIL` behavior.
- `2026/06/23`: fixed the SGPIO SPI0 receiver wiring to `PA2/PA0/PA3`.
- `2026/06/19`: initial README draft for shared GPIO port ISR SGPIO target/slave implementation.
