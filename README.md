# ultraBattery

A tiny Windows tray tool that shows the battery level of Bluetooth keyboards, mice and gamepads.

Every connected device gets its own tray icon: the battery percentage on top, a small keyboard, mouse or gamepad symbol underneath. Hover an icon for the device name.

## Features

- One icon per connected Bluetooth input device (keyboard, mouse, gamepad), found automatically
- Icons appear and disappear within seconds when a device connects or disconnects
- Battery level refreshed every 60 seconds
- Orange at 30% and below, red at 15% and below
- Follows the light or dark taskbar theme and the display scaling
- Optional start with Windows (right-click menu)
- About 31 KB, no installer, no runtime, no settings files

## Requirements

- Windows 10 or 11
- Bluetooth devices that report their battery level to Windows (Bluetooth LE battery service). If Windows shows a battery level for the device under Settings, Bluetooth & devices, ultraBattery shows it too.

## Usage

Run `ultraBattery.exe` from the folder you want to keep it in. On first launch it adds itself to the Windows startup list; untick "Start with Windows" in the right-click menu to turn that off.

Windows 11 puts new tray icons into the overflow menu (^). Drag them onto the taskbar once and Windows remembers their place.

Right-click any icon for Refresh, Start with Windows and Exit.

## Building

Needs Visual Studio 2026 with the Clang toolset and CMake 4.2 or newer.

```
cmake --preset vs
cmake --build --preset vs --config Release
```

The exe ends up in `Builds/vs/Release`.

## License

MIT, see [LICENSE](LICENSE).

## Credits

The device symbols follow the proportions of the [Font Awesome Free](https://fontawesome.com) solid icons keyboard, computer-mouse and gamepad, licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
