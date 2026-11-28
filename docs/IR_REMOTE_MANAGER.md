# IR remote manager

The IR subsystem uses the ESP32-S3 native RMT peripheral for capture and transmission. PlatformIO is pinned to espressif32 8.x because ESP32IRPulseKit requires Arduino-ESP32 Core 3.x. SX1509 expanders are reserved for GPIO expansion and trackball inputs; do not connect IR RX or TX to an SX1509.

## Wiring

| Function | ESP32-S3 / module |
|---|---|
| IR transmitter signal | GPIO1 (RMT TX) |
| IR receiver OUT | GPIO2 (RMT RX) |
| IR receiver VCC | 3.3V (unless the module explicitly requires another supply) |
| IR receiver GND | GND |
| SX1509 SDA | GPIO14 |
| SX1509 SCL | GPIO21 |
| Trackball UP | SX1509 #1 IO0 |
| Trackball DOWN | SX1509 #1 IO1 |
| Trackball LEFT | SX1509 #1 IO2 |
| Trackball RIGHT | SX1509 #1 IO3 |
| Trackball select | ESP32-S3 BOOT button (GPIO0) |

Connect all grounds together. Power down before wiring. Use a transistor driver for the IR LED; GPIO1 is a logic signal and must not drive a high-current IR LED directly. Keep the IR receiver output within the ESP32-S3 3.3V input limits.

The firmware probes SX1509 addresses 0x3E and 0x3F at startup. Connect the expander before powering on or reset the board after connecting it. Trackball direction inputs are enabled when the primary expander at 0x3E is detected. The secondary expander is detected but not yet assigned functions.

## TFT screen workflow

From the home screen select **IR**. The IR menu contains:

1. **Save IR** — choose an existing remote profile or create a new profile, then point the original remote at the receiver and press one button.
2. **Universal Remote** — browse the Probonopd IRDB CSV tree by brand, device category, code set and button name. Each button opens an action screen with **Send now** and **Add to Remote Shortcuts**.
3. **Remote Shortcuts** — one combined quick-access list containing favourites from both Universal Remote and your own learned remotes. Selecting an entry transmits that button.
4. **Run IR Code** — choose either **Saved on SD** (select a remote, then a button) or the existing custom **IR Database** folder.
5. **Scripts** — choose and execute a saved script.

In a saved custom remote's button list, selecting a button also opens **Send now** / **Add to Remote Shortcuts**. Shortcuts store a reference to the original button rather than duplicating its IR data.

The screen-based recorder lets you select an existing remote profile or create a new one. New profiles are named `Remote_01`, `Remote_02`, and so on; buttons are named `Button_01`, `Button_02`, and so on within each profile. This makes recording possible without a keyboard; custom remote/button naming and on-screen script authoring are not yet implemented.

## Probonopd IRDB CSV database

Copy the repository's `codes` directory to the SD card root without renaming it. The expected layout is:

    /codes/Samsung/TV/7,7.csv
    /codes/Sony/TV/1,-1.csv
    /IR/remotes/<remote>/<button>.ir
    /IR/scripts/<name>.irs
    /IR/shortcuts.txt

The `/codes` tree is kept separate from `/IR`; the firmware reads the original CSV files directly and does not convert or overwrite them. CSV columns are `functionname,protocol,device,subdevice,function`. The browser uses the brand and category folders, then the CSV filename (for example `7,7`) as the code-set identity.

The current Universal Remote encoder supports these verified protocol families: NEC / NECx2, Sony12 / Sony15 / Sony20, JVC, RC5 and Samsung32. Other protocol names are still browsable but transmission is rejected and reported as unsupported; the firmware deliberately does not guess an encoding. NECx2 is sent as a full NEC data frame; receiver compatibility can vary by device. The library currently supplies a default repeat policy, so long-press/repeat behaviour is not yet a perfect emulation of every original remote.

Shortcuts are appended to `/IR/shortcuts.txt`. Universal entries retain the CSV path and data-row index; custom entries retain the remote and button names. Keep the corresponding source CSV or custom remote file in place: deleting or renaming a source makes its shortcut unusable.

The separate `/IR/database` browser remains for this firmware's custom `IRBLASTER1` files. The Universal Remote browser is the CSV reader for Probonopd IRDB; no network downloader or Flipper Zero / IRremote import is implemented.

Navigation: trackball UP/DOWN moves through lists, RIGHT opens/transmits, LEFT goes back. BOOT short-press advances and long-press selects; a very long press goes back.

## Serial console

Open the USB serial monitor at 115200 baud, select a newline line ending, and type HELP.

Commands are uppercase:

| Command | Purpose |
|---|---|
| LEARN <remote> <button> | Wait for one IR press and save the raw signal |
| LIST | List saved remotes and buttons |
| SEND <remote> <button> | Replay a saved button |
| COPY <srcRemote> <srcButton> <dstRemote> <dstButton> | Copy a button into another remote |
| SCRIPT <name> | Start recording a script |
| RUN <name> | Run a saved script |
| CANCEL | Cancel an active learn or script recording |
| HELP | Show command help |

Example session:

    LEARN SamsungTV POWER
    LEARN SamsungTV VOLUME_UP
    LEARN SamsungTV VOLUME_DOWN
    LIST
    SEND SamsungTV POWER
    COPY SamsungTV POWER BedroomTV POWER
    SCRIPT Evening
    SEND SamsungTV POWER
    DELAY 800
    SEND SamsungTV VOLUME_DOWN
    END
    RUN Evening

While recording a script, enter only SEND, DELAY, comment lines beginning with #, or END. Delays are limited to 60 seconds per instruction. Scripts are executed sequentially.

## Storage format

- Remotes: /IR/remotes/<remote>/<button>.ir
- Scripts: /IR/scripts/<name>.irs
- Remote shortcuts: /IR/shortcuts.txt
- Probonopd IRDB CSV tree: /codes/<brand>/<category>/<device>,<subdevice>.csv

Each button stores raw timing ticks (10 microseconds per tick), plus protocol metadata when a decoder candidate is available. Raw timings are retained even when the protocol is unknown, so the button can still be replayed.

The current decoder recognises common NEC, AEHA, Sony, Samsung, JVC, RC5 and RC6 families. Unknown signals are retained as raw captures. Air-conditioner remotes and other long stateful transmissions may require a larger capture buffer or a protocol-specific workflow; captures marked truncated are not saved.

A successful SEND means the ESP32 completed its local RMT transmission. It does not prove that the appliance received or acted on the signal.

## Implementation inspiration

The RMT capture/decode/replay interface is based on ESP32IRPulseKit, which provides raw waveform capture and replay as well as protocol candidates. The broader protocol support and raw replay approach are also informed by IRremoteESP8266.
