# Codex -> DNESP32S3 speaker notifier

This starter project turns a Codex task completion into an audible notification on
the ALIENTEK DNESP32S3 development board.

## Data path

```text
Codex stop hook
  -> Windows PowerShell sends one UDP packet
  -> local network broadcast/unicast, port 3333
  -> ESP32-S3 receives CODEX_DONE|token|message
  -> ES8388 codec + MD8002A amplifier
  -> onboard 8 ohm / 1 W speaker plays three notes
```

UDP broadcast is used for the first version because the board does not need a
fixed IP address. If a router blocks broadcast packets, put the board's IP in
`pc-hook/esp32-notify.json` instead.

## Confirmed board resources

The values below come from the local DNESP32S3 manuals:

| Signal | ESP32-S3 GPIO |
| --- | ---: |
| I2C SDA | 41 |
| I2C SCL | 42 |
| ES8388 MCLK | 3 |
| ES8388 BCLK/SCK | 46 |
| ES8388 LRCK/WS | 9 |
| ESP32 audio output -> ES8388 SDIN | 10 |
| ES8388 SDOUT -> ESP32 audio input | 14 |

The board uses an ES8388 codec, an MD8002A power amplifier, and an onboard
8 ohm / 1 W speaker. The firmware only enables the playback path.

## 1. Build and flash the firmware

Use ESP-IDF 5.1.x, matching the board documentation.

```powershell
cd firmware
idf.py set-target esp32s3
idf.py menuconfig
```

Open `Codex ESP32 Notifier` and set:

- Wi-Fi SSID
- Wi-Fi password
- UDP port (leave at 3333 unless needed)
- Shared token (change `change-me` to a private random string)

Then build, flash, and monitor:

```powershell
idf.py build
idf.py -p COMx flash monitor
```

Successful startup prints the acquired IP address and `Listening for Codex`.

## 2. Test the PC-to-board path before installing the hook

Copy the example config:

```powershell
Copy-Item pc-hook\esp32-notify.example.json pc-hook\esp32-notify.json
```

Edit `pc-hook/esp32-notify.json` so its token matches the firmware. Then run:

```powershell
'{}' | powershell.exe -NoProfile -ExecutionPolicy Bypass -File pc-hook\esp32-notify.ps1
```

The board should play three ascending notes. If it does not:

1. Confirm the PC and ESP32 are on the same non-guest Wi-Fi/LAN.
2. Allow PowerShell/UDP on the Windows firewall when prompted.
3. Replace `255.255.255.255` in the JSON with the board's printed IP.
4. Check that the token and port match.

## 3. Install the Codex completion hook

After the manual test works, run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File pc-hook\install-user-hook.ps1
```

The installer preserves existing hooks and appends this command to the user-level
`stop` event. It does not replace the existing `clawd-on-desk` hook on this PC.

Codex watches the hook file and normally reloads it automatically. If the hook is
not listed in the Hooks settings/output channel, restart Codex.

## Protocol

The UTF-8 payload is deliberately small and inspectable:

```text
CODEX_DONE|shared-token|Codex task finished
```

The ESP32 ignores packets with the wrong prefix or token. This token prevents
accidental triggers; it is not encryption. Keep the notifier on a trusted LAN.

## Natural next upgrade

Once the tone path is stable, replace `audio_notify_play()` with playback of a
short WAV file (for example, `Codex has finished`) or add text-to-speech. The
local IDF manual's Chapter 41, `Music Player Experiment`, already contains the
board vendor's WAV/ES8388/I2S playback implementation.

