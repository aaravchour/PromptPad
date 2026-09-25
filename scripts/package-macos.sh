#!/bin/sh
# Local packaging candidate. Ad-hoc signing permits local execution; it is not a release signature.
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_app="$repo_dir/build/release/promptpad.app"
candidate="$repo_dir/dist/PromptPad.app"
deploy_log="$repo_dir/dist/macdeployqt.log"

test -d "$source_app" || { echo "Build the release preset first." >&2; exit 1; }
command -v macdeployqt >/dev/null || { echo "macdeployqt is required." >&2; exit 1; }
command -v brew >/dev/null || { echo "This local packaging script expects Homebrew Qt." >&2; exit 1; }
qt_svg="$(brew --prefix qtsvg)/lib/QtSvg.framework"
test -d "$qt_svg" || { echo "Install the Homebrew qtsvg module first." >&2; exit 1; }

mkdir -p "$repo_dir/dist/lib"
# Homebrew splits QtSvg from qtbase; macdeployqt needs this path to resolve its SVG plugin.
ln -sfn "$qt_svg" "$repo_dir/dist/lib/QtSvg.framework"
rm -rf "$candidate"
ditto "$source_app" "$candidate"
if ! macdeployqt "$candidate" -no-codesign -verbose=1 >"$deploy_log" 2>&1; then
    cat "$deploy_log" >&2
    exit 1
fi
if grep -q '^ERROR:' "$deploy_log"; then
    cat "$deploy_log" >&2
    exit 1
fi
rm "$repo_dir/dist/lib/QtSvg.framework"
rmdir "$repo_dir/dist/lib"
mkdir -p "$candidate/Contents/Resources/licenses"
cp "$repo_dir/LICENSE" "$candidate/Contents/Resources/licenses/PromptPad-MIT.txt"
cp "$repo_dir/THIRD_PARTY_NOTICES.md" "$candidate/Contents/Resources/THIRD_PARTY_NOTICES.md"
cp "$repo_dir/vendor/scintilla/License.txt" "$candidate/Contents/Resources/licenses/Scintilla.txt"
cp "$repo_dir/vendor/lexilla/License.txt" "$candidate/Contents/Resources/licenses/Lexilla.txt"
cp "$repo_dir/licenses/"*.txt "$candidate/Contents/Resources/licenses/"
cp "$repo_dir/licenses/BUNDLED_COMPONENTS.md" "$candidate/Contents/Resources/licenses/"

# macdeployqt copies Homebrew's dynamic dependencies but not their licences.
# Keep this list in sync with the actual Contents/Frameworks and PlugIns inventory.
while IFS='|' read -r formula licence_file; do
    case "$formula" in ''|'#'*) continue ;; esac
    formula_prefix="$(brew --prefix "$formula")"
    if [ ! -f "$formula_prefix/$licence_file" ]; then
        echo "Missing licence for bundled dependency: $formula/$licence_file" >&2
        exit 1
    fi
    cp "$formula_prefix/$licence_file" "$candidate/Contents/Resources/licenses/$formula-$(basename "$licence_file")"
done <<'LICENCES'
brotli|LICENSE
icu4c@78|LICENSE
openssl@3|LICENSE.txt
pcre2|LICENCE.md
pcre2|COPYING
freetype|LICENSE.TXT
harfbuzz|COPYING
graphite2|LICENSE
graphite2|COPYING
jpeg-turbo|LICENSE.md
libpng|LICENSE
zstd|LICENSE
zstd|COPYING
libb2|COPYING
dbus|COPYING
gettext|COPYING
md4c|LICENSE.md
double-conversion|LICENSE
double-conversion|COPYING
LICENCES
codesign --force --deep --sign - "$candidate"
codesign --verify --deep --strict "$candidate"
echo "$candidate"
