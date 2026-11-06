# HP OMEN Encoder (Woody2): HID Lighting Protocol

- **Project:** KEncoder (`encoder` CLI and `kencoder` GUI)
- **Device:** HP OMEN Encoder, USB VID:PID `03f0:3341`
- **Investigation date:** 2026-11-05
- **Evidence:** existing Linux CLI source, HP OMEN Command Center `1101.2609.11.0` .NET IL, and controlled HID tests on an actual keyboard
- **Status:** write commands confirmed at the transport level; independent readback of **effect family and brightness** confirmed; **animation speed and wave direction remain unknown**.

> This document describes the protocol **observed and validated so far**, not a complete firmware specification. A returned configuration value is a live value reported by the device, not an optical measurement of the LEDs. No firmware-reset, firmware-update, or flash-writing command is recommended or authorized by this document.

## 1. Device identification

The Windows application includes an `AccessoryList.json` entry identifying the device as:

| Field | Value |
| --- | --- |
| Device type | `Keyboard_Woody2` |
| USB vendor ID | `0x03f0` (HP), decimal 1008 |
| USB product ID | `0x3341`, decimal 13121 |
| HID interface selector | `mi_02` (USB interface 2) |
| Supported feature identifiers | `Woody2Lighting`, `Woody2Macro` |
| Background component | `KeyboardV3Bg` |
| Device event type | `HID` |
| Internal HP lighting type | `53` (`0x35`) |

During the investigation, the lighting interface appeared as `/dev/hidraw14`, on USB interface 2, using `hid-generic`. **Never hard-code the hidraw number:** it can change after replugging or a reboot. The other keyboard interfaces were not opened or read by the protocol investigation.

The observed interface had interrupt OUT endpoint `0x04` and interrupt IN endpoint `0x83`, both with a 64-byte maximum packet size. The reported poll interval was 1 ms. These endpoint assignments came from interface enumeration; a new usbmon capture of the successful readback operation was not made.

### HID report descriptor

The observed descriptor is exactly 30 bytes:

```text
06 13 ff 09 01 a1 01 15 00 26 ff 00 75 08 95 40
09 02 81 02 09 03 91 02 0a 00 ff b1 02 c0
```

Interpretation:

- Vendor usage page `0xff13`, application usage `0x01`.
- Logical data range `0..255`, report size 8 bits, report count 64.
- Input usage `0x02`, Output usage `0x03`, Feature usage `0xff00`.
- No explicit HID Report ID in the descriptor: the implicit report number is **0**.
- The first bytes of vendor messages, such as `0x0c`, `0x03`, and `0x80`, are **vendor opcodes**, not HID Report IDs.

The legacy setting-command path finds candidate `/dev/hidraw*` nodes using `HIDIOCGRAWINFO` and a 30-byte descriptor-size check. The new `--status` path additionally validates the **full descriptor bytes** observed on `mi_02`. A separate explicit USB-interface-number check would provide further hardening; do not assume it is already implemented.

## 2. Framing and Linux transport

The application protocol uses **64-byte vendor payloads**, normally zero-padded. The already-shipped CLI sends the known setting commands through `write(fd, payload, 64)` and receives 64-byte responses through `read(fd, ..., 64)`.

The newly validated `GetDeviceInfo` readback was sent via Linux `hidraw` with a **65-byte write buffer**:

```text
00 80 01 00 00 00 ... 00    (65 bytes total)
^^
implicit HID report number 0 at the Linux HID API boundary
```

The corresponding 64-byte vendor payload is `80 01` followed by 62 zero bytes. In all 18 verified readback probes, `write()` returned 65, and exactly one correlated `read()` returned 64 bytes **without a report-number prefix**. Do not confuse indices into the 65-byte write buffer with indices into the 64-byte reply payload.

The readback command is an **Output report followed by an Input report**, not `HIDIOCGFEATURE` and not a control-transfer `GET_REPORT` request. The existing setting path and the newly discovered readback path use different host-side write-buffer conventions; preserve these observations rather than assuming that every command needs the same buffer length.

A production implementation must bound the operation with nonblocking I/O or polling deadlines, validate all transfer lengths, correlate the response, and close the descriptor on failure. The existing CLI's `send_data()` has blocking `read()` calls and its 20 ms `usleep()` **after** a read is not a timeout.

## 3. Known lighting WRITE commands

All tables below show the 64-byte **vendor payload**; unspecified bytes are zero. Their meaning is derived from `cli/keyboard_control.c` and controlled tests, not from a published HP protocol specification.

### 3.1 Brightness

```text
Offset:  00 01 02 03 04 05 ... 63
Bytes:   0c 00 01 00 VV 00 ... 00
```

`VV` is inverted relative to the user-visible level:

| User-facing brightness | Raw value at offset 4 |
| ---: | ---: |
| 0 | `0x03` |
| 1 | `0x02` |
| 2 | `0x01` |
| 3 | `0x00` |

Formula: `raw = 3 - user_brightness`. The range is strictly 0 through 3.

### 3.2 Static lighting / key mask

```text
04 01 12 00 ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff 01 00 ... 00
```

The prefix is `04 01 12 00`. The next **16 bytes**, offsets 4..19, are `ff`; offset 20 is `01`. This command enables the known full-key static lighting configuration. Related existing functions use the same prefix with a 17-byte mask (`light_keymask`) or set an individual bit (`light_single_key_by_id`). These mask variations were inspected in source but not separately exercised in the readback test matrix.

#### Legacy single-key example: R (unverified key mapping)

The earlier `encoder` project's `protocol_examples` file labels this 64-byte vendor payload as `Single Key R enable` (all omitted trailing bytes are zero):

```text
Offset:  00 01 02 03 04 05 06 07 ... 63
Bytes:   04 01 12 00 00 00 08 00 ... 00
```

In the current `light_single_key_by_id()` implementation, the mask byte is at offset `4 + id / 8`, and the bit is `id % 8`. Offset 6 with bit 3 set therefore corresponds to **key ID 19**. The association **ID 19 = R** comes only from the legacy example: it was *not independently verified on the physical keyboard*. Preserve it as a historical clue, not as a validated key map.

The same legacy file uses brightness labels inconsistent with the verified user-facing brightness scale, has reversed Wave direction labels, and omits a follow-up packet from its Right Wave example. It is not suitable as an authoritative collection of test vectors.

### 3.3 Animated modes

```text
Offset:  00 01 02 03 04 05 06 07 08 09 10 ... 63
Bytes:   03 00 24 00 MM 00 00 SS 00 DD 00 ... 00
```

| Field | Known values |
| --- | --- |
| `MM` at offset 4 | `02` Breathing, `09` Ripple, `0a` Wave |
| `SS` at offset 7 | `00` Slow, `01` Medium, `02` Fast |
| `DD` at offset 9 | `01` Breathing / Ripple, `02` Right Wave, `03` Left Wave |

**Direction correction (user-observed on HP OMEN Encoder `03f0:3341`):** the original `encoder` CLI mapped direction byte `02` to `left_wave` and `03` to `right_wave`, but the physical animation traveled in the opposite direction. The current `kcm-develop` CLI has corrected this: `03` means `left_wave`, and `02` means `right_wave`. The verified `80 01` readback identifies both as the Wave family (`0a`) and **cannot** independently confirm the direction.

After the animation command, the CLI sends a second 64-byte packet:

```text
0a 00 02 00 ac 53 00 ... 00
```

Its exact internal meaning is not fully proven. It is part of the known working command sequence. Avoid naming it a flash-write or a successful commit without more evidence.

### 3.4 Lighting off

First packet:

```text
03 00 24 00 00 ... 00
```

Second packet:

```text
0a 00 02 00 ac 53 00 ... 00
```

**Existing CLI caveat:** `encoder -o` first calls `set_brightness()` with its default brightness level **3**, then sends the Off sequence. Consequently, the device's readback after `encoder -o` reported `Off` and raw brightness `00` (user-level 3). That brightness field does not indicate visible light while Off.

## 4. Replies to the existing WRITE commands

The observed write replies are 64 bytes long. They match the first two bytes of the outgoing command and include a fixed trailing header at offsets 2..5:

| Write family | Observed reply prefix (remainder zero) |
| --- | --- |
| Brightness `0c 00 ...` | `0c 00 03 80 ec ac` |
| Static / mask `04 01 ...` | `04 01 03 80 ec ac` |
| Animation / Off `03 00 ...` | `03 00 03 80 ec ac` |
| Follow-up `0a 00 ...` | `0a 00 03 80 ec ac` |

The HP SDK's `BasicIO_ForHidSDK.MatchPattern` examines the `ec ac` bytes in the response, supporting an **ACK-like / command-result framing** interpretation. The `03 80` bytes are not independently decoded here, and these replies **do not contain the chosen brightness, speed, or effect**. A matching header or successful process exit must never be presented as an independent configuration readback.

Controlled earlier tests recorded 23 setting writes with 23 matching replies, and separate observations of the installed CLI produced the same reply families. This demonstrates reply correlation, not an independent optical or firmware-state confirmation.

## 5. Verified READBACK: `GetDeviceInfo` (`80 01`)

### 5.1 Request

A read-only request traced from the HP .NET implementation and validated on the physical keyboard:

```text
Linux hidraw write buffer, length 65:
  index 00: 00           (implicit report number)
  index 01: 80           (vendor command)
  index 02: 01           (command index)
  index 03..64: 00      (62 zero bytes)

64-byte vendor payload:
  80 01 00 ... 00
```

The application sends this fixed query without including desired lighting settings. It was tested with no setting command in the same process and also after configuration changes made by a separate `encoder` process.

### 5.2 Reply structure

Every validated response was exactly 64 bytes long and began with:

```text
Offset:   00 01 02 03 04 05 06 07 08 09 10 11 12 ... 63
Example:  80 01 29 d0 01 23 00 00 01 11 02 EE BB ...
```

`EE` and `BB` represent the *changing* effect and brightness values, not literal fixed bytes.

| Reply offset | Field | Verified interpretation |
| ---: | --- | --- |
| 0..1 | `80 01` | Correlates response to `GetDeviceInfo` |
| 2..9 | `29 d0 01 23 00 00 01 11` | Observed on the tested device; detailed meaning unverified |
| 10 | `02` | HP SDK command-template / format selector (`_profileNo`); **not proved to be an active user profile** |
| 11 | Effect code | Confirmed current effect family |
| 12 | Raw brightness | Confirmed current brightness encoding: `user = 3 - raw` |
| 13..63 | Other device data | Do not assign semantics without further evidence |

The 18 measured responses differed **only at offsets 11 and 12**. The HP IL parser reads offsets 10, 11, and 12 *after the report-prefix normalization*, consistent with the observed 64-byte Linux `read()` payload.

### 5.3 Known effect codes

| `reply[11]` | Meaning supported by controlled tests |
| ---: | --- |
| `0x00` | Off |
| `0x01` | Static |
| `0x02` | Breathing |
| `0x09` | Ripple |
| `0x0a` | Wave, **direction unknown** |
| Other | Unknown; do not guess |

The same `0x0a` reply was observed for Left Wave and Right Wave when brightness and speed were equal. All three tested Ripple speeds likewise yielded identical complete replies. **Neither direction nor speed is recoverable from the validated `GetDeviceInfo` response.** Another firmware-supported read request is not ruled out, but none is presently validated.

### 5.4 Controlled hardware matrix

In every row, a separate worker process reopened the verified lighting interface and sent the same `80 01` request. No per-process cache or profile was shared with the setting command.

| Setting applied by separate process | Effect byte | Raw brightness | User brightness |
| --- | ---: | ---: | ---: |
| Baseline, before this test's setting changes | `01` | `02` | 1 |
| Static, brightness 1 | `01` | `02` | 1 |
| Static, brightness 3 | `01` | `00` | 3 |
| Static, brightness 0 | `01` | `03` | 0 |
| Ripple Slow, brightness 2 | `09` | `01` | 2 |
| Ripple Medium, brightness 2 | `09` | `01` | 2 |
| Ripple Fast, brightness 2 | `09` | `01` | 2 |
| Breathing Fast, brightness 2 | `02` | `01` | 2 |
| Left Wave Slow, brightness 2 | `0a` | `01` | 2 |
| Right Wave Slow, brightness 2 | `0a` | `01` | 2 |
| Off, after the CLI's implicit brightness 3 | `00` | `00` | 3 |
| Final restored Static, brightness 1 | `01` | `02` | 1 |

There were **18 successful fresh-process readback probes** across the two logged test sequences, including additional control reads after Off and Wave. Each returned one correlated 64-byte reply; the table consolidates repeated observations. The final restored value was verified by a fresh readback.

For the tested keyboard, independent device-reported **On/Off, effect family, and brightness** are supported. No independent measurement of actual LED emission was performed.

### 5.5 What was *not* a status read

Previously tested generic HID operations do not expose the changing configuration:

- A passive `read()` without a preceding request: 18/18 timed out (750 ms), no unsolicited data.
- `HIDIOCGFEATURE` with implicit report number 0: 35/35 returned an invariant 65-byte buffer `00 00 00 00 ff`, then zeros. This is the ioctl API buffer representation, **not** a verified lighting status.
- `HIDIOCGINPUT` and `HIDIOCGOUTPUT` with report number 0: 18/18 each returned `EPIPE` (`errno=32`).
- Setting-command ACK-like replies: do not encode the selected effect or brightness.
- HP application's local `SingleColorStorageModel` / `RegLoadMainSettings()` records: stored preferences, not independent readback.

The validated `80 01` request supersedes the earlier conclusion that *no* readback was known. It does **not** establish speed, wave direction, or generic Feature-report-based readback.

## 6. HP Windows implementation provenance

The analysis used the `AD2F1837.OMENCommandCenter` Microsoft package, version `1101.2609.11.0`, architecture x64. The archive had valid ZIP structure and contained `AppxSignature.p7x`; the presence of a signature file is not the same as independent signature validation. Its download SHA-256 was:

```text
6f814900dccd893166094bc8c2504ac43c66413aa2d1f59ea65d5f08c301110e
```

Relevant static IL observations (MethodDef indices are within each assembly; offsets refer to IL bodies):

| Assembly / method | Observed behavior |
| --- | --- |
| `HP.Omen.AccessoryKeyboardLightingModuleV3.dll`, `Woody2LightingControlModel::get_Type` (MethodDef 210) | Returns 53 (`0x35`) |
| `HP.Omen.Core.UI.Common.dll`, `OpenDeviceBase::get_Handle` (5628) | Routes type 53 through `McuKeyboardHelper.OpenDevice(PID, VID, interface)` |
| `McuSDK2.dll`, `OpenDevice` / `MoveNext` (89 / 898) | Constructs HID identity and calls `DetectCommandTemplate` |
| `McuSDK2.dll`, `DetectCommandTemplate::MoveNext` (900) | Gets device-info request, executes it, reads response, selects protocol template |
| `McuSDK2.dll`, `KeyboardCommandHelper.GetDeviceInfo` (527) / V2 (563) | Builds the `80 01` request using a 65-byte report buffer |
| `McuSDK2.dll`, parsers (538 / 569) | Read `_profileNo = data[10]`, effect `data[11]`, brightness `data[12]` |
| `McuSDK2.dll`, `BasicIO_ForHidSDK` (746), `ExecuteCommand` (1112) | Converts raw report representation to HIDSDK transfer; sends and reads data |
| `HP.Omen.Features.Hardware.HidMailroom.Background.dll`, mailbox callback | Alternative 65-byte report-number-aware transfer via Windows `WriteFile` / `ReadFile` |

The SDK chooses the V2 keyboard command-template parser for the tested device's format selector `2`. Its default command template is a keyboard-specific one with 64-byte device payloads. The HP caller path to `80 01` was traced for `Keyboard_Woody2` / type 53, rather than assumed from a similarly named generic SDK method.

A different generic `GeneralCommandHelper.GetLightingEffect` builds a candidate involving opcode `0x83`. **No call path from Woody2 to that command was validated, and `0x83` was not sent to the keyboard.** Do not invent or brute-force additional requests by manipulating these values.

HP's `StoreLightingEffectDataToFlash` is a distinct operation. The V3 lighting capability metadata indicates `AlwaysStoreToFlash` for type 53. This does **not** imply `80 01` writes flash; the traced query path did not contain that operation. Do not assume all lighting writes are volatile or safe to repeat indiscriminately.

## 7. Asynchronous reports from the keyboard

The HP `KeyboardV3Bg` background code interprets events associated with Woody2. In that event-processing code the following `ButtonNumber` values are recognized:

| `ButtonNumber` | HP-side interpretation |
| ---: | --- |
| 17 | Brightness-change notification, value carried in `ButtonStatus` |
| 32 | Lighting Off/On event |
| 33 | Breathing |
| 34 | Wave Left to Right |
| 35 | Wave Right to Left |
| 36 | Ripple |
| 37 | Static |

These are **event identifiers**, not bytes at known positions in the `80 01` reply. They suggest that the Windows background service can track mode changes that do not appear in the polled response, but a separate Linux event-report transport for them has not yet been fully documented or tested. Do not monitor ordinary keyboard/input interfaces to obtain these events without a new, scoped investigation.

## 8. Limitations and safety constraints

- The results come from one confirmed HP OMEN Encoder and one software build. Other firmware revisions may behave differently.
- A readback parser must first verify VID/PID, USB interface and descriptor; it must not bind to arbitrary `/dev/hidraw*` nodes.
- Reading configuration must send **only** the validated `80 01` query, not a preceding brightness write. The legacy CLI's normal execution path always writes brightness; `--status` uses an early, independent read-only branch.
- Do not infer success solely from `write()` returning the requested byte count, matching the first two reply bytes, or CLI exit code 0.
- Validate packet length and response identity, handle timeouts and partial reads, and reject malformed replies rather than returning guessed defaults.
- Do not decode speed or wave direction from the current reply; both are **unknown**. Device-reported brightness during Off should be labeled as a reported field, not visible luminosity.
- Do not send unknown vendor opcodes, USB control requests, firmware/reset commands, or flash commands as exploratory probes.
- Preserve the existing udev ACL design; avoid root when normal user permissions work.
- Do not record ordinary keypresses or data from the keyboard's typing interfaces.

## 9. KEncoder implementation and verification status (2026-11-06)

The following features are implemented on branch `kcm-develop` (commit `84f43a9`):

- The CLI provides `encoder --status [--json]` as a separate **read-only device-state query**, without preceding brightness or effect-setting commands. It identifies the intended HP device using USB VID/PID and the complete 30-byte HID descriptor, sends the observed 65-byte `00 80 01 ...` query, uses a bounded 2-second read deadline (up to 8 replies), and rejects malformed or unsupported replies.
- Both the standalone Qt6 Widgets GUI and the optional Plasma 6 KCM use the shared QtCore-only `gui/lighting_status.h` JSON parser. They request actual device state asynchronously at startup and after successful settings writes, without treating default, requested, or unknown values as hardware measurements.
- The readback parser, CLI mock, headless GUI self-test, and GUI startup mock check passed. The startup mock verified that launching the GUI requests only `--status --json`, not a lighting-setting command.
- The actual HP OMEN Encoder returned the following CLI output with exit status 0; the user also confirmed that the Qt GUI and KCM operate on the device:

```json
{"source":"hardware","enabled":true,"brightness":1,"mode":"static","direction":null,"speed":null,"partial":true,"effect_code":1}
```

- Both Debian **amd64** packages (`encoder` and `kencoder`) passed a local packaging audit. The optional KCM plugin is included in `kencoder`; KF6 components remain in `Suggests`, and the standalone GUI has no direct KF6 shared-library dependencies. This is not a claim that an arm64 build was tested in that audit.

Illustrative machine-readable result for **Wave** with brightness 2 (schema example, not an additional hardware test of the final CLI):

```json
{
  "source": "hardware",
  "enabled": true,
  "brightness": 2,
  "mode": "wave",
  "direction": null,
  "speed": null,
  "partial": true,
  "effect_code": 10
}
```

Wave direction and animation speed **remain unavailable** from the validated readback command. Off is a distinct mode; static brightness 0 is not Off. Unknown effect codes or malformed replies must produce an explicit error instead of a fabricated default. Startup readback is a HID query, not a lighting-setting operation; it must not write firmware or flash.

### Evidence locations

The original diagnostic work recorded detailed host tests and IL disassembly in the repository's volatile `.log/` directory:

- `woody2-readback-q7j394m0.jsonl`
- `woody2-readback-kn1083ih.jsonl`
- `woody2-il-evidence-ilai2wsn.txt`
- `woody2-readback-report-ripzglgu.txt`
- `hid-state-matrix-do3c6r80.jsonl`
- `hid-state-matrix-pgfxpgg9.jsonl`

These filenames record provenance; they are not a promise that every volatile log is present in a release checkout. The validated behavior is summarized above so the documentation is useful without the local captures.

### External technical reference

Linux kernel documentation: [HIDRAW - Raw Access to USB and Bluetooth Human Interface Devices](https://www.kernel.org/doc/html/latest/hid/hidraw.html).
