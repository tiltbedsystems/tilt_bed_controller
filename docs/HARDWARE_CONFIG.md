# HARDWARE_CONFIG.md

Authoritative record of the Simplicity Studio component + pin configuration
for the SiSDK 2026.6.0 `tilt_bed_controller` project. Reconstructed from the
old 4.2.2 `.slcp`/`.pintool` and corrected during the D1 migration
(2026-07-05). **This file — not any chat transcript or tool recap — is the
source of truth for what to add in the configurator.** Every pin should be
cross-checked against the board schematic before committing, especially the
motor and I2C pins.

Target die: EFR32BG22 (inside module **BGM220SC22HNA2**).

---

## 1. Software Components to add in the Configurator

The SoC Empty base already provides `nvm3_default` and `sleeptimer` (pulled
in by the Bluetooth stack) and the base `device_init`. Everything below is
what must be *added*. **Instance names are effectively part of the code's
API** — the generated `sl_..._<instance>` symbols are referenced by name in
the source, so copy each name verbatim.

| Component (search term) | Instance name(s) | Used for |
|---|---|---|
| IADC (emlib IADC peripheral) | (singleton, no instance) | Motor current sensing (`adc_currents.c`) |
| I2CSPM | `on_board`, `off_board` | `on_board` = onboard I/O expander (`exp_board.c`, PI4IOE5V6416) + accelerometer (`mc3479.c`); `off_board` = external switch-panel expander (`exp_ui.c`) |
| IO Stream: USART | `io_stream` | Debug/serial console (printf) |
| IO Stream: Retarget STDIO | (none) | Routes printf/stdio to `io_stream` |
| (IO Stream core) | (none) | Auto-pulled dependency of the above; let Studio resolve it |
| PWM | `motor1`, `motor2`, `motor3`, `motor4` | Speed control for the 4 linear-actuator motors — see §3 for the timer mapping |
| Simple Button | `local_button`, `fault_int`, `ui_int` | `local_button` = onboard user button; `ui_int` = switch-panel expander interrupt; `fault_int` = motor-driver fault interrupt |
| Simple LED | `local_LED` | Onboard status LED (active-low) |

Already present (verify config unchanged, do not re-add):
`nvm3_default` (settings/bed-position storage, `nvm3_functions.c`),
`sleeptimer` (timing).

Preprocessor define: the old project defined **`DEBUG_EFM`** globally —
re-add via Project properties / preprocessor defines if the source still
relies on it (confirm during the compile-fix loop).

*Note on the IO Stream entries:* the exact companion-component names may
differ slightly in SiSDK 2026.6. Install "IO Stream: USART" (instance
`io_stream`) and "IO Stream: Retarget STDIO", and let Studio pull whatever
core dependency it wants. Do not hunt for a separate un-named "IO Stream:"
component — that was a garbled artifact in an earlier tool recap.

## 2. Pin assignments (Pin Tool)

Cross-referenced from the old `.pintool` and generated pin/peripheral
configs. **Verify every row against the schematic before committing** — the
motor and I2C rows especially. Suggested order to avoid allocation
conflicts: USART first, then the PWM timers, then the GPIO buttons + LED +
reset line (this mirrors how the old Pin Tool resolved allocation).

| Pin | Signal | Peripheral / instance | Function |
|---|---|---|---|
| PA00 | RST_EXP_N | GPIO, reserved output | Active-low reset to the I/O expander(s) |
| PA03 | LOCAL_BUTTON | Simple Button → `local_button` | Onboard user button |
| PA04 | LOCAL_LED | Simple LED → `local_LED` | Onboard status LED |
| PB00 | USART0 TX | IO Stream USART → `io_stream` | Debug console TX |
| PB01 | USART0 RX | IO Stream USART → `io_stream` | Debug console RX |
| PB02 | SPEED_MOTOR_3 | PWM → `motor3` (TIMER2) | Motor 3 PWM speed |
| PB03 | SPEED_MOTOR_2 | PWM → `motor2` (TIMER1) | Motor 2 PWM speed |
| PB04 | SPEED_MOTOR_1 | PWM → `motor1` (TIMER0) | Motor 1 PWM speed |
| PC00 | UI_INT | Simple Button → `ui_int` | Switch-panel expander interrupt |
| PC01 | FAULT_INT | Simple Button → `fault_int` | Motor-driver fault interrupt |
| PC02 | LOCAL_SDA | I2CSPM → `on_board` (I2C0 SDA) | Onboard expander + accelerometer bus |
| PC03 | LOCAL_SCL | I2CSPM → `on_board` (I2C0 SCL) | Onboard expander + accelerometer bus |
| PC06 | SPEED_MOTOR_4 | PWM → `motor4` (TIMER3) | Motor 4 PWM speed |
| PD02 | EXTERNAL_SCL | I2CSPM → `off_board` (I2C1 SCL) | External switch-panel expander bus |
| PD03 | EXTERNAL_SDA | I2CSPM → `off_board` (I2C1 SDA) | External switch-panel expander bus |

**Dead entry to ignore:** the old `config/pin_config.h` contains a stray
`#define _PORT gpioPortA` / `#define _PIN 1` (PA01) with no signal name and
no corresponding `.pintool` entry — a vestigial blank custom-pin-name
artifact. There is no "PA01" signal to reproduce.

## 3. PWM timer mapping (DECISIONS #25) — one motor per timer

The old firmware shared TIMER0 across three motors via hand-edited config
headers, because `sl_pwm` cannot model multi-channel-per-timer. **That
approach is abandoned.** Each motor now gets its own timer, so every motor
is a clean single-instance `sl_pwm` with auto-generated config that
configurator regeneration cannot corrupt.

| Motor | Timer | CC | Pin |
|---|---|---|---|
| motor1 | TIMER0 | CC0 | PB04 |
| motor2 | TIMER1 | CC0 | PB03 |
| motor3 | TIMER2 | CC0 | PB02 |
| motor4 | TIMER3 | CC0 | PC06 |

**TIMER4 is the reserved one in the final configuration** — one general-
purpose timer is claimed by a component in the SoC Empty base (most likely
sleeptimer), and which timer it claims proved unstable while the
configuration was being entered (it shifted from TIMER3 to TIMER4). This
table reflects what the Pin Tool actually accepted, verified against a
screenshot of the completed pin configuration (2026-07-05). TIMER3 and
TIMER4 are functionally identical, so the shift is inconsequential — but if
future configurator work ever reshuffles the timer reservation again, the
rule is: keep one dedicated timer per motor, update this table to match
reality, and re-run the REGRESSION.md §2b per-motor checks.

Hardware facts (EFR32BG22): five general-purpose timers —
TIMER0 (32-bit) and TIMER1–4 (16-bit), each 3-channel, +DTI, EM1,
PWM-capable. The BLE stack uses the radio protimer / secondary RTC, leaving
all five general-purpose timers to the application.

PWM frequency: **24 kHz**, unchanged (inside the MPS-recommended 20–100 kHz
window for the motor drivers).

## 4. Verification hooks (fold into REGRESSION.md at D1)

- **Per-motor PWM check (new, for #25):** drive each motor individually —
  motor1 up/down alone, then motor2, motor3, motor4 — confirming the correct
  physical actuator moves at the correct speed and direction. This isolates
  the new per-timer mapping from the rest of the SDK migration.
- All pre-existing REGRESSION.md items still apply and must pass on the
  migrated firmware **before any BLE glue is written** (DECISIONS #20).
