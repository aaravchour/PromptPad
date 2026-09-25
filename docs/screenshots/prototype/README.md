# Writing surface prototype screenshots

The `before-*` captures were taken from the working minimalist editor before this prototype's source changes. The `after-*` captures use the same synthetic Markdown prompt, 820-pixel window height, Qt offscreen platform, temporary settings and storage, and the rebuilt `ui_capture` executable. Editor captures are 1080 pixels wide unless their name says `narrow` (640 pixels). This is the real Qt/Scintilla widget, not a drawing of the interface.

Capture a comparable editor view from the current build:

```sh
QT_QPA_PLATFORM=offscreen build/release/ui_capture /tmp/promptpad-editor.png Light 1080 editor
```

The `after-find-light.png` and `after-replace-dark.png` captures show the compact, on-demand Find and Replace controls. The `after-*-native.png` captures use Qt's Cocoa platform at Retina resolution and show the native editor surface; the macOS app menu is outside the grabbed window.
