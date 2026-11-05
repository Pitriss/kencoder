# KEncoder

KEncoder is a Linux application for controlling the RGB backlighting of the HP OMEN Encoder keyboard (USB ID `03f0:3341`).

The project is a fork of [encoder_keyboard](https://github.com/player999/encoder_keyboard) by Taras Zakharchenko. It extends the original command-line utility with a Qt6 graphical interface designed for KDE Plasma 6.

## Features

### Encoder CLI
- Static backlighting and lighting off
- Brightness control (levels 0–3)
- Animation modes: Ripple, Left Wave, Right Wave, Breathing
- Animation speed: Slow, Medium, Fast
- USB HID communication
- udev rules for device access
- Manual page: `man encoder`

### KEncoder GUI
- Native Qt6 interface for KDE Plasma 6
- Animation, brightness and speed controls
- Optional minimize-to-system-tray behavior
- Device detection and runtime status monitoring
- Status messages for applied commands and errors
- No automatic changes to keyboard settings at startup

Reading the current lighting configuration directly from the keyboard is not yet supported.

## Build from Source

Requirements: Linux, GCC, Make, CMake and Qt6 Widgets development libraries.

### CLI

```bash
make -j"$(nproc)"
./build/cli/encoder -h
```

### GUI

```bash
cmake -S gui -B build/gui
cmake --build build/gui -j"$(nproc)"
PATH="$PWD/build/cli:$PATH" ./build/gui/kencoder
```

The GUI requires the `encoder` CLI utility.

## Debian Packages

The project provides two separate packages:

- `encoder` – command-line utility and udev rules
- `kencoder` – Qt6 graphical interface

The `kencoder` package depends on `encoder`.

GitHub Actions is configured for tag-based releases with independent CLI and GUI versions, targeting Debian 13 on amd64 and arm64.

## License

BSD 3-Clause License.

Original encoder code: Copyright (c) 2020 Taras Zakharchenko.

KEncoder additions and modifications: Copyright (c) 2026 Pitriss.

See [LICENSE](LICENSE) and [debian/copyright](debian/copyright) for details.

## Acknowledgements

**A huge thank you to Taras Zakharchenko**, the original author of [encoder_keyboard](https://github.com/player999/encoder_keyboard), for creating and sharing the software that made this project possible.

**AI disclosure:** The KEncoder-specific code and modifications in this fork were generated using AI tools, with human direction, testing and review.

The original author's work and contribution remain fully credited.
