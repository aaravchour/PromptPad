# PromptPad working notes

- Product: a native editor for prompts written **for** coding agents. It does not call or run AI. The current editor-first experience is described in `README.md` and `docs/architecture.md`; the original private design brief is not part of this public source tree.
- Current UI: one focused editor by default, a hidden single-document tab strip, menu-driven dialogs, and lazy Editor / Preview / Split Preview modes. Keep the source Scintilla buffer authoritative.
- Stack: C++20, Qt 6 Widgets, upstream vendored Scintilla Qt integration, Lexilla Markdown lexer, SQLite, CMake/Ninja. Do not replace the editor engine or introduce a web shell.
- Main modules: `src/editor` owns the Scintilla adapter; `src/documents` owns exact file bytes, search and outline ranges; `src/storage` owns app data; `src/launcher` owns local IPC; `src/ui` owns the window. The editor buffer is the authoritative live text.
- Configure/build/test on a Mac with Homebrew Qt: `PATH=/opt/homebrew/bin:$PATH cmake --preset release -DCMAKE_PREFIX_PATH=/opt/homebrew`, then `PATH=/opt/homebrew/bin:$PATH cmake --build --preset release -j 1`, then `PATH=/opt/homebrew/bin:$PATH ctest --test-dir build/release --output-on-failure`.
- IPC tests need permission to create local Unix sockets. Tests use temporary profiles and synthetic prompt text; never use a developer's real documents as fixtures.
- Preserve UTF-8 byte offsets at the Scintilla boundary. Qt strings use UTF-16 indices. Check both directions whenever editing search or section code.
- The draft backup and explicit file save are distinct operations. Never claim a file was saved when only recovery storage succeeded. Preserve atomic writes and external-change checks.
- Release evidence and platform limitations belong in `docs/performance.md`, `docs/architecture.md` and `docs/manual-checklist.md`. Cross-platform CI configuration is not proof of a native manual pass.
