# Licences in the macOS bundle

This directory accompanies the Apple Silicon app produced with Homebrew Qt 6.11.2.
It records the libraries observed in the bundle and the licence files copied from
the installed Homebrew packages. The application's original code is MIT licensed;
see `PromptPad-MIT.txt`. Scintilla and Lexilla have their own bundled notices.

QtBase, Qt5Compat and QtSvg 6.11.2 provide the bundled Qt frameworks and plugins.
They are used under the open-source LGPL 3.0 option; see `LGPL-3.0.txt` and
`GPL-3.0.txt`, and [Qt's licensing documentation](https://doc.qt.io/qt-6/licensing.html).
Qt frameworks and plugins remain separate dynamic libraries inside
`PromptPad.app/Contents/Frameworks` and `PromptPad.app/Contents/PlugIns`. Users
may replace them with compatible builds. Qt source for the corresponding
release is available from [Qt's source archive](https://download.qt.io/archive/qt/6.11/6.11.2/).
Qt components also contain third-party code; consult
[Qt's attribution list](https://doc.qt.io/qt-6/licenses-used-in-qt.html).

| Bundled dependency | Homebrew version | Licence file here |
| --- | --- | --- |
| Brotli | 1.2.0 | `brotli-LICENSE` |
| GLib | 2.88.3 | `LGPL-2.1.txt` |
| ICU | 78.3 | `icu4c@78-LICENSE` |
| OpenSSL | 3.6.4 | `openssl@3-LICENSE.txt` |
| PCRE2 | 10.48 | `pcre2-LICENCE.md`, `pcre2-COPYING` |
| FreeType | 2.14.3 | `freetype-LICENSE.TXT` |
| HarfBuzz | 14.4.0 | `harfbuzz-COPYING` |
| Graphite2 | 1.3.15 | `graphite2-LICENSE`, `graphite2-COPYING` |
| libjpeg-turbo | 3.2.0 | `jpeg-turbo-LICENSE.md` |
| libpng | 1.6.58 | `libpng-LICENSE` |
| zstd | 1.5.7 | `zstd-LICENSE`, `zstd-COPYING` |
| libb2 | 0.98.1 | `libb2-COPYING` |
| D-Bus | 1.16.2 | `dbus-COPYING` |
| gettext | 1.0 | `gettext-COPYING` |
| md4c | 0.6.0 | `md4c-LICENSE.md` |
| double-conversion | 3.4.0 | `double-conversion-LICENSE`, `double-conversion-COPYING` |

Package contents may change as Homebrew or Qt changes. Audit the actual
framework, plugin and dylib inventory when packaging another version. This
inventory and bundled texts are distribution information, not legal advice or
a claim that a third-party legal review has been completed.
