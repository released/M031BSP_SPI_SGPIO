# SGPIO Protocol Contract

## Role

This file is the shared SGPIO interface contract between an SGPIO initiator/master and this M032 target/slave implementation. It records wire-level behavior, frame shape, decode policy, and known limits so firmware, host tooling, and MCU target code do not drift.

- Initiator/master: external SGPIO source. The current companion Pico implementation is one concrete initiator.
- Target/slave: this M032 PA3 SPI0_SS-gated, SPI0 SGPIO receiver.
- Project README remains the M032 user-facing document. This contract may mention the companion initiator only where it affects wire-level compatibility.

## Wiring

| Initiator pin / signal | Signal | M032 pin | M032 function |
| --- | --- | --- | --- |
| SCLK | SCLK | PA2 | SPI0_CLK input |
| SDATA OUT | SDATA OUT | PA0 | SPI0_MOSI input |
| SLOAD | SLOAD | PA3 | SPI0_SS active-low frame gate |
| GND | Ground | GND | Ground |

PA0 and PA2 are routed to SPI0 slave RX. PA3 is routed to SPI0_SS because the M031/M032 SPI0 slave requires hardware SS active and the selected M032 package has no QSPI slave 3-wire path. The old PA6/PA7/PC6 GPIO-sampling wiring is not supported by this SPI0 receiver path.

## Standards Boundary

This project treats the SGPIO test flow as a standards-oriented implementation:

- `PUBLISHED SFF-8485.PDF`: Serial GPIO bus framing and signal timing.
- `PUBLISHED SFF-8489.PDF`: IBPI interpretation of per-slot drive LED state bits.

`SLOAD L0..L3 Raw` and `SDataOut` slot bits are intentionally separated:

- `SLOAD L0..L3 Raw`: the 4-bit frame-level vendor-specific field carried on the `SLOAD` line immediately after the restart marker. The legacy GPIO-sampling receiver can expose it as raw/common inspection data; this SPI0_SS receiver cannot sample it because PA3 is the hardware SS input.
- `SDataOut` slot bits: the per-slot bits commonly interpreted by IBPI as `ACT`, `LOCATE`, and `FAIL`.
- High-level product meaning for vendor-specific fields is not standardized here; it must come from the target backplane/enclosure requirement.

## Initiator Frame Shape

The initiator must drive `SLOAD` and `SDATA OUT` with stable setup/hold around each SCLK rising edge. The current SPI0 receiver latches SDATAOUT in hardware on the SCLK rising edge.

Each clock emits one pair:

- `SLOAD` bit
- `SDATA OUT` bit

Frame sequence:

1. `SLOAD` is idle high before a frame.
2. `SLOAD` falls low and remains low for at least five SCLK rising edges as the low-sync run.
3. A restart marker is indicated when `SLOAD=1` appears after the low-sync run.
4. The marker clock is not part of the slot data stream.
5. Four `SLOAD L0..L3 Raw` bits exist on the next four SCLK rising edges in the general SGPIO frame shape.
6. `SDataOut` slot bits start immediately after the marker, concurrent with the `L0..L3` clocks.
7. Slot bit order is `ACT`, `LOCATE`, `FAIL`.
8. Frame padding may be present after the configured slot stream.
9. This SPI0_SS receiver requires `SLOAD` to return low and remain low during the SDATAOUT payload; nonzero/high `SLOAD L0..L3` intervals would make SPI0_SS inactive and stop SPI shifting.

## Current M032 SPI0_SS + SPI0 Implementation

The current M032 reference slave is RX-only and uses PA3/SLOAD as SPI0_SS to gate SPI0 SDATAOUT capture.

- The first PA3/SLOAD low section is treated as low-sync and waits for the payload section.
- The SLOAD restart-marker high pulse makes SPI0_SS inactive, so the marker clock is not stored as SDATAOUT payload.
- The next PA3/SLOAD low section starts SPI0 RX capture.
- SPI0 captures PA0/SDATAOUT on PA2/SCLK rising edges in 8-bit LSB-first units.
- PA3/SLOAD is not sampled on every SCLK edge in this path, so `SLOAD L0..L3 Raw` is reported invalid.
- `SPI0_IRQHandler()` handles SPI0_SS active events, drains RX FIFO, and updates raw bits. It does not print, decode, or busy-wait.
- `SGPIO_Process()` finalizes a frame after a short SPI0 RX gap timeout, rearms SPI0, copies the ISR-built frame, decodes `ACT/LOCATE/FAIL`, and prints rate-limited diagnostics.
- Captured bit counts outside `24`, `26`, `32`, `42`, or `48` are rejected as invalid.
- Single-frame glitches are not accepted as the reported state. A decoded frame must match repeated stable content before it updates the last reported result.
- The M031/M032 SPI0 block does not expose the slave 3-wire `SLV3WIRE` control used by the M251 reference. PA3 is routed to `SPI0_SS`; SLOAD must stay low during the SDATAOUT payload or SPI0 will stop shifting.
- No `SDATA IN` target-to-initiator TX is implemented in this SPI0 RX stage.

Expected startup log:

```text
PA3/SLOAD SPI0_SS frame gate
PA0/SDATAOUT SPI0_MOSI input
PA2/SCLK SPI0_CLK input
SGPIO SPI0 SS-gated RX path, no SDATAIN TX
```

Expected frame log shape:

```text
[SGPIO RX] frame #N
  capture: bits=.. bytes=.. valid=1 overflow=0 dropped=.. low_sync=..
  sload: L0..L3=0x. valid=0
  masks: ACT=0x.... LOCATE=0x.... FAIL=0x....
  raw: .. .. ..
  bits: S0=... S1=... ...
  slots: ACT=... LOCATE=... FAIL=...

```

Slot decode rule:

- The SDATAOUT bit stream starts on the first SCLK rising edge after the restart marker.
- Each slot consumes three bits in this order: `ACT`, `LOCATE`, `FAIL`.
- SFF-8485 names these slot bits as `ODx.y`.
- Current mapping is `ODx.0 -> Slot x ACT`, `ODx.1 -> Slot x LOCATE`, and `ODx.2 -> Slot x FAIL`.
- For each mask, bit 0 maps to Slot 0, bit 1 maps to Slot 1, and so on.
- Example: `ACT=0x0005` means Slot 0 and Slot 2 have the Activity bit asserted.
- Raw bytes are printed LSB-first in capture order, not normal MSB-first binary display order.
- Example: `raw: 38 8E C3 00` decodes to `bits: S0=000 S1=111 S2=000 S3=111 S4=000 S5=111 S6=000 S7=011`.
- In each `Sx=abc` triplet, `a=ACT`, `b=LOCATE`, and `c=FAIL`.
- The README contains the full `OD0.0` through `OD15.2` raw-bit table.

## Timing

Validated initiator reference:

- Clock: `48000 Hz`
- Periodic interval: `100 ms`
- Default slot count: `8`

M032 receiver:

- PA3/SLOAD is SPI0_SS active-low in the fixed SPI0 wiring.
- PA2/SCLK clocks SPI0 and is not used as a GPIO interrupt.
- After the restart marker, the initiator must drive SLOAD low and keep it low while SDATAOUT payload clocks are present.
- A frame is finalized by `SGPIO_Process()` when no SPI0 RX data arrives for `SGPIO_FRAME_GAP_TIMEOUT_MS`.
- SGPIO frame debug logging is rate-limited: the first frame is printed, then at most once per second. This prevents UART `printf` back-pressure from starving `TimerService_Dispatch()` and stopping the heartbeat LED while periodic send is enabled.
- Each complete SGPIO frame/debug block ends with one blank line so TeraTerm logs stay readable during periodic send.

## Bring-Up Checklist

1. Connect all SGPIO wires and common GND.
2. Configure the initiator for a valid SGPIO frame.
3. Start periodic SGPIO frame output.
4. Confirm M032 UART log prints matching masks:
   - `ACT=0x....`
   - `LOCATE=0x....`
   - `FAIL=0x....`
   - `bits: S0=... S1=...`
   - `slots: ACT=... LOCATE=... FAIL=...`
5. Confirm the M032 heartbeat LED keeps toggling before and after SGPIO frame traffic starts.

## Known Limits

- SFF-8485 defines the field position and transport, not a fixed high-level product meaning for vendor-specific data.
- The current M032 reference slave is RX-only and validates `SDataOut` and IBPI slot bits first. `SLOAD L0..L3 Raw` is not sampled in the SPI0 path.
- `SDATA IN` target-to-initiator TX is outside the current M032 sample.
- The validation platform verifies transport, bit placement, and raw decode. Product semantics must be defined by the enclosure/backplane design.
