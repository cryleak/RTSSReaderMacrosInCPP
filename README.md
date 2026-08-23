# RTSS Reader Macros

Windows-native GTA V macro tool with a Direct2D/DirectWrite settings window. It has no HTML, CSS, WebView2, Qt, or other UI runtime dependency.

Requirements:

- Windows 10/11
- Visual C++ Redistributable
- RivaTuner Statistics Server with GTA V or GTA V Enhanced registered

The GUI starts visible and can be hidden to the notification area. Settings are saved to `%LOCALAPPDATA%\RTSSReaderMacros\config.ini` after pressing **Save**. Macro triggers, GTA controls, `use_cursor_macros`, and `repress_left_click` can be captured in the UI.

The RTSS indicator distinguishes waiting for GTA, RTSS unavailable, GTA not registered in RTSS, and ready. The reader periodically re-detects the process and reinitializes when switching between Legacy/Enhanced or restarting the same executable.

To run the lightweight settings/key-chord check from a developer build:

```text
RTSSReaderMacros.exe --self-test
```
