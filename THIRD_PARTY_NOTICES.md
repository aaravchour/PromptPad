# Third-party notices and distribution

PromptPad's original source is MIT licensed; see [LICENSE](LICENSE).

| Component | Version used here | Use | Licence evidence |
| --- | --- | --- | --- |
| Scintilla | 5.6.6 | Upstream Qt editor integration and editing engine, built into the binary | [vendored licence](vendor/scintilla/License.txt) |
| Lexilla | 5.5.3 | Markdown lexer, built into the binary | [vendored licence](vendor/lexilla/License.txt) |
| Qt 6 | 6.11.2 for the observed macOS build; compatible Qt 6.6+ required | Dynamically linked Core, Widgets, Network, Sql, Concurrent and Core5Compat modules; the packaging candidate additionally carries QtSvg, QtDBus and platform/SQLite/image plugins | [Qt licensing](https://doc.qt.io/qt-6/licensing.html), [Qt Core5Compat](https://doc.qt.io/qt-6/qtcore5-index.html) |
| SQLite | The version supplied by the selected Qt SQLite driver | Local metadata, draft snapshots and checkpoints | [SQLite copyright](https://www.sqlite.org/copyright.html) |

The vendored Scintilla and Lexilla source archives were obtained from their upstream release downloads. SHA-256: Scintilla `b6b08598c68fac90990d010c1142494d707530602b5320753274d045c2b02189`; Lexilla `4d9e64263c337034a06f9c67f330c605764cac02aee83c06f6c21f9527a71628`.

Qt offers commercial and open-source licensing choices. The Qt modules used by this project have module-specific terms; distributors must choose a valid licence route, ship the relevant licence texts and notices, preserve users' rights to replace or relink LGPL libraries where applicable, and check the exact Qt binaries and plugins included in their package. Dynamic linking by itself does not complete that work. PromptPad does not claim a legal compliance review or ship a Developer ID signed release. The local Homebrew build uses Qt's installed libraries. `scripts/package-macos.sh` creates an ad-hoc signed bundle, copies the observed Homebrew dependency licence files and includes a [bundle inventory](licenses/BUNDLED_COMPONENTS.md). Distribution on another Qt or Homebrew version needs a fresh inventory and licence check.

The observed macOS bundle also contains transitive dynamic libraries from Brotli, GLib, ICU, OpenSSL, PCRE2, FreeType, HarfBuzz, Graphite2, libjpeg, libpng, zstd, libb2, dbus, gettext, md4c and double-conversion. This list is an inventory from the local `Contents/Frameworks` directory, not a licence clearance. The package script verifies the ad-hoc signature; it does not claim that this inventory is complete for every Qt installation or fulfil redistribution terms.

There are no bundled fonts, icon packs, model files or network services. The reference design image is a development input and is not shipped as an application asset.
