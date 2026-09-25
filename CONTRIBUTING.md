# Contributing to PromptPad

Thanks for helping make long-form prompt writing more dependable. PromptPad is a local editor for prompts written **for** coding tools; it does not use AI itself. Keep changes focused on editing, preservation, performance and a calm native interface.

## Before starting

Read [AGENTS.md](AGENTS.md) and [architecture](docs/architecture.md). Discuss a large feature or a change to storage/file semantics in an issue before implementing it. Small bug fixes and documentation corrections can go directly to a pull request.

The application uses C++20, Qt 6 Widgets, upstream vendored Scintilla with its Qt integration, Lexilla, SQLite and CMake/Ninja. Do not replace the editor engine with a web view or add a network service, AI client, telemetry or account system.

## Build and test

On the tested Apple Silicon setup:

```sh
brew install qtbase qt5compat qtsvg ninja
PATH=/opt/homebrew/bin:$PATH cmake --preset release -DCMAKE_PREFIX_PATH=/opt/homebrew
PATH=/opt/homebrew/bin:$PATH cmake --build --preset release -j 1
PATH=/opt/homebrew/bin:$PATH ctest --test-dir build/release --output-on-failure
```

IPC tests create temporary local Unix sockets and need permission to do so. Tests must use synthetic text and temporary profiles, never real user documents. On another platform, install the equivalent Qt modules, pass its Qt root with `-DCMAKE_PREFIX_PATH`, and report which native checks were actually run.

## Code and tests

- Scintilla's UTF-8 buffer is the live source. Qt strings use UTF-16 indices; check both directions when changing search, selection or section code.
- Treat draft backup and explicit file save as distinct outcomes. Preserve atomic replacement, external-change checks, BOM and newline handling.
- Keep the library and preview lazy. Avoid extra permanent panels or controls that lack a working action.
- Add a focused regression test for bugs in editing, recovery, saving or command-line hand-off. Use [the manual checklist](docs/manual-checklist.md) for native input and accessibility cases that automated tests cannot establish.
- Measure performance changes using the existing `promptpad_perf` harness, and record the hardware, platform plugin, methodology and limitations in [performance notes](docs/performance.md).

For a pull request, explain the user-visible change, how it was tested and any remaining risk. Screenshots should use synthetic prompts and temporary profiles. Do not commit credentials, user drafts, build output, the private design brief or screenshots containing personal data.

## Licences

New original contributions are submitted under the project's [MIT licence](LICENSE). Third-party source or assets need a compatible redistribution basis and an entry in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). A CI build is not a release package: bundled Qt and transitive dependencies require their own distribution review.
