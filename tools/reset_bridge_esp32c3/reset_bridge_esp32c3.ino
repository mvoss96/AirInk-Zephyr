/*
 * AirInk reset-bridge — ESP32-C3 as USB-serial log reader + nRF52 RST trigger.
 *
 * One USB device does two jobs, so it replaces the separate CP210x log adapter:
 *   1) Bridges the AirInk debug UART to USB CDC  -> host reads printk logs.
 *   2) On a host command, pulses the nRF52840 RESET line to enter the nice!nano
 *      UF2 bootloader (double-tap) or just reboot the app.
 *
 * The nice!nano bootloader on this board has NO DFU/FRST pins (board.h: "no
 * buttons"), so double-reset within its ~500 ms window is the way into UF2.
 * The ESP32-C3 GPIO is 3.3 V, so it drives the nRF RESET directly.
 *
 * Wiring (ESP32-C3 <-> AirInk, COMMON GND required; nRF powered by the PPK2):
 *   UART_RX_PIN (GPIO20 = "RX0" pad)  <-  AirInk TX  (P1.04)   // log stream
 *   RST_PIN     (GPIO1)               ->  AirInk RESET         // open-drain
 *   GND                               <-> GND
 * (RX-only: we never send to the nRF, so no TX line is wired.)
 *
 * Host protocol (newline-terminated lines over USB CDC, host -> ESP32):
 *   "TAP"    -> two resets ~250 ms apart  => enter UF2 bootloader (NICENANO drive)
 *   "RESET"  -> single reset              => reboot the AirInk app
 *   "ID"     -> replies with BRIDGE_ID    => lets the host find the right COM port
 * Anything the nRF sends is forwarded verbatim to USB CDC. Other host bytes are
 * ignored (host->nRF passthrough is intentionally off for now).
 *
 * Build: Arduino "ESP32C3 Dev Module" (or your C3 board),
 *        Tools -> "USB CDC On Boot: Enabled"  (so `Serial` == USB CDC).
 */

#include <Arduino.h>

// Distinctive banner so the host can identify this device among the COM ports.
static const char BRIDGE_ID[] = "AirInk-reset-bridge v1 (ESP32-C3)";

// --- adjust to your board; AVOID the C3 strapping pins GPIO2/8/9 ---
static const int UART_RX_PIN = 20;  // "RX0" pad on most C3 boards; <- AirInk TX (P1.04)
static const int RST_PIN     = 10;   // -> AirInk RESET
static const uint32_t NRF_BAUD = 115200;

// nRF RESET is active-low with a pull-up. Emulate open-drain: drive LOW to reset,
// return to Hi-Z (INPUT) to release so we never fight the pull-up / reset cap.
static void rstAssert()  { pinMode(RST_PIN, OUTPUT); digitalWrite(RST_PIN, LOW); }
static void rstRelease() { pinMode(RST_PIN, INPUT); }

static void pulseReset(uint16_t low_ms) {
  rstAssert();
  delay(low_ms);
  rstRelease();
}

static void doubleTap() {
  // Two resets inside the bootloader's double-reset window (~500 ms).
  pulseReset(30);
  delay(220);
  pulseReset(30);
}

static String cmd;

void setup() {
  rstRelease();                 // never hold the nRF in reset while we boot
  Serial.begin(115200);         // USB CDC to the host
  Serial1.begin(NRF_BAUD, SERIAL_8N1, UART_RX_PIN, -1);  // RX only (no TX to nRF)

  // Best-effort boot banner (lost if the host isn't attached yet -- that's what
  // the "ID" command is for). Prefixed with '#' so it's easy to tell apart from
  // the forwarded nRF log lines.
  Serial.print("# ");
  Serial.println(BRIDGE_ID);
}

void loop() {
  // nRF -> host: forward the log stream byte-for-byte.
  while (Serial1.available()) {
    Serial.write(Serial1.read());
  }

  // host -> ESP32: line-based commands.
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if      (cmd == "TAP")   doubleTap();
      else if (cmd == "RESET") pulseReset(30);
      else if (cmd == "ID")    Serial.println(BRIDGE_ID);
      cmd = "";
    } else if (cmd.length() < 16) {
      cmd += c;
    }
  }
}
