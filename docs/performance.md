# Release-build measurements

## Window management and Preview return, 25 September 2026

The same Release preset, Apple M5 Mac, Qt 6.11.2 and `promptpad_perf` harness were used after the window-management change. These are one warm-cache sample per workload, so small differences from the earlier Figma-refactor section are not evidence of a speedup or regression. The offscreen platform includes Qt layout and Scintilla paint events but cannot validate native dragging or display latency; the Cocoa samples run a real native window. Physical footprint uses macOS `task_vm_info.phys_footprint` in decimal MB.

| Workload | Offscreen launch to editable | Offscreen steady physical footprint | Native Cocoa launch to editable | Native Cocoa steady physical footprint |
| --- | ---: | ---: | ---: | ---: |
| Empty editor | 75.3 ms | 12.60 MB | 156.1 ms | 23.71 MB |
| 100,000-character editor | 109.9 ms | 18.20 MB | 185.3 ms | 36.23 MB |

The 100,000-character offscreen input-to-paint p95 was **0.96 ms** for 100 natural events; the native Cocoa p95 was **1.32 ms**. First opening the offscreen library took **26.0 ms** and reached **22.38 MB** physical footprint. First opening Preview took **135.1 ms** and reached **32.08 MB**. Native drag gestures were exercised by Qt event tests, and the Cocoa captures rendered each placement, but a physical pointer pass remains open in the manual checklist.

Reproduce with `QT_QPA_PLATFORM=offscreen build/release/promptpad_perf empty`, `one`, `library` and `preview`; omit `QT_QPA_PLATFORM=offscreen` for Cocoa. The Cocoa results are not directly comparable to the offscreen results.

## Figma native-window refactor, 25 September 2026

Measured on the same Apple M5 Mac, Qt 6.11.2 and Release preset with `promptpad_perf` and synthetic Markdown/Unicode text. The paired **before/after** samples both use `QT_QPA_PLATFORM=offscreen`, the same 1.8-second settling interval, macOS `task_vm_info.phys_footprint`, and one warm-cache run per workload. The previous **12.3 MB** report measured a short-lived packaged forwarding launcher, not the persistent editor, so it is not the before value here. These runs show observations, not a guaranteed speedup.

| Workload | Before | After |
| --- | ---: | ---: |
| Empty editor, launch to editable | 113.1 ms | 86.8 ms |
| Empty editor, steady physical footprint | 14.89 MB | 12.60 MB |
| 100,000 Unicode characters, launch to editable | 120.6 ms | 104.2 ms |
| 100,000 characters, steady physical footprint | 21.09 MB | 18.12 MB |
| 100 natural input-to-paint events, p95 | 1.63 ms | 0.77 ms |

In the final offscreen build, first opening the real library beside a 100,000-character prompt took **21.4 ms** and raised physical footprint from **18.02 MB** to **22.35 MB**. First opening Markdown Preview took **105.1 ms** and raised it from **17.94 MB** to **32.00 MB**. Idle process CPU for the editor-only 100,000-character case was **0.000141 seconds over 2 seconds**. A million-character single wrapped line took **1,195.9 ms** to become editable and occupied **34.06 MB**; that extreme wrap case still misses the one-second target.

The same final harness on the **native Cocoa** Qt platform measured a blank editor at **167.9 ms** to editable and **23.77 MB** steady physical footprint. One 100,000-character prompt measured **179.4 ms**, **33.26 MB** steady footprint and **1.24 ms** input-to-paint p95. Opening its library took **28.9 ms** and reached **44.76 MB** physical footprint. Cocoa and offscreen numbers are different rendering environments and must not be compared as if they were paired before/after runs. All startup and paint timings include Qt and local OS scheduling variance; none measure display scan-out or physical key latency.

The [raw final offscreen output](measurements/figma-after-macos-m5.jsonl) is retained. Reproduce with:

```sh
for scenario in empty one preview library longline; do
  QT_QPA_PLATFORM=offscreen build/release/promptpad_perf "$scenario"
done
```

## Writing surface prototype, 24 September 2026

The before and after builds were measured on the same Apple M5 Mac with the same Release preset, Qt 6.11.2, synthetic prompt, and `QT_QPA_PLATFORM=offscreen`. These are single warm-cache runs; startup differences should not be treated as a reliable speedup or regression. Physical footprint is macOS `task_vm_info.phys_footprint`, in decimal MB. The paint number is a Qt/Scintilla offscreen event proxy, not display latency.

| Workload | Before | Prototype |
| --- | ---: | ---: |
| Empty draft, launch to editable | 62.8 ms | 68.5 ms |
| Empty draft, steady physical footprint | 14.86 MB | 14.86 MB |
| 100,000-character draft, launch to editable | 124.4 ms | 90.7 ms |
| 100,000-character draft, steady physical footprint | 20.96 MB | 21.27 MB |
| 100 edit events, input to paint p95 | 1.475 ms | 1.475 ms |
| 100,000-character first Preview open | Prior minimalist build: 89.0 ms | 155.3 ms |
| 100,000-character Preview footprint | Prior minimalist build: 33.75 MB | 34.36 MB |

The Preview comparison uses the previous release measurement, not a paired run on 24 September. The prototype applies one document-wide block-format operation for more readable preview spacing, and it appears to cost some preview-open time. Preview remains lazy; editor startup and typing do not perform that formatting. The editor's long-document paint proxy and physical footprint stayed close to the prior build in this sample.

Additional prototype stress runs: one million mixed Unicode characters became editable in **109.8 ms** with **26.76 MB** steady physical footprint. One million characters on a single wrapped line took **1,047.4 ms** and **37.65 MB**. That extreme line still exceeds the one-second startup target and remains a known Scintilla wrapping stress case.

Reproduce the current measurements after configuring and building:

```sh
for scenario in empty one preview million longline; do
  QT_QPA_PLATFORM=offscreen build/release/promptpad_perf "$scenario"
done
```

The actual Cocoa widget also launched and rendered the light and dark synthetic prompt in `ui_capture`. Native-window startup, physical display latency, and a long-session typing study are still unmeasured for this prototype.

## Minimal editor refactor, before and after

The before and after runs on this same Apple M5 Mac used the same CMake Release build preset, Qt 6.11.2, synthetic 100,000-character Markdown/Unicode document, `QT_QPA_PLATFORM=offscreen`, and `promptpad_perf` harness. `launch_to_editable_ms` ends after the window is shown and initial events are processed. Memory is macOS `task_vm_info.phys_footprint`, reported below in decimal MB (1 MB = 1,000,000 bytes). Idle CPU is process CPU seconds over a two-second idle interval after pending work settled. Each row is one warm-cache run, not a confidence interval.

| Workload | Before startup | After startup | Before steady footprint | After steady footprint | Before idle CPU / 2 s | After idle CPU / 2 s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| One empty draft | 152.2 ms | 66.7 ms | 20.74 MB | 14.81 MB | 0.00175 s | 0.00011 s |
| One 100,000-character draft (103,304 UTF-8 bytes) | 170.1 ms | 100.7 ms | 26.95 MB | 21.01 MB | 0.00317 s | 0.00013 s |

For the 100,000-character document, 100 natural Scintilla input-to-paint proxy samples had p95 **3.04 ms before** and **1.51 ms after**. This is event timing under the offscreen plugin, not physical display latency. The faster startup and lower memory are observed results of this run; the app's stack alone does not guarantee them.

Preview is lazy. In a separate after run, the 100,000-character editor occupied **21.01 MB** before Preview; first Preview took **89.0 ms** and raised footprint to **33.75 MB** (about **12.74 MB** more). Split Preview took **87.6 ms** and reached **33.49 MB** from **21.14 MB**. Twenty Editor→Preview→Editor round trips took **1.24 s** total; after destroying the hidden preview the process still occupied **35.39 MB**, versus **20.79 MB** before its first preview, with a sampled peak of **35.39 MB**. Qt and allocator retention may explain this; the run does not establish a leak or prove memory returns to baseline. Preview did no work during the final two-second idle interval (0.00011 CPU seconds).

Reproduce the refactor measurements after configuring and building this repository:

```sh
for scenario in empty one preview split switch; do
  QT_QPA_PLATFORM=offscreen build/release/promptpad_perf "$scenario"
done
```

Raw measurements: [before](measurements/minimal-before-macos-m5.jsonl) and [after](measurements/minimal-after-macos-m5.jsonl). The previously reported **12.3 MB** is a transient *packaged forwarding launcher* peak, not the persistent app's blank-editor footprint. These offscreen runs do not replace a physical-window cold-start or long-term leak study.

The rebuilt, ad-hoc signed `dist/PromptPad.app` also started successfully using the native Cocoa platform and an isolated temporary profile. Two seconds after launch, `/usr/bin/footprint --noCategories` reported **51 MB** physical footprint for one blank draft (`ps` RSS: **135,776 KiB**) and **57 MB** for one 100,000-character prompt of 103,304 bytes (sampled peak **58 MB**; RSS: **149,104 KiB**). The earlier packaged native 100,000-character sample below was **89 MB**; these samples were taken on the same Mac but at different times, so they are supporting evidence rather than a controlled paired benchmark. Native startup-to-editable and physical-display input latency remain unmeasured.

Measured 23 September 2026 on a MacBook Pro `Mac17,2`, Apple M5 (10 cores), 24 GB RAM, arm64, macOS 27.0, Apple clang 21.0.0, Homebrew Qt 6.11.2, Ninja, CMake Release build. The benchmark starts a real `MainWindow` with the upstream Scintilla Qt widget and synthetic Markdown/JSON/Unicode content, using Qt's **offscreen** platform plugin. This tests widget layout and paint events but not physical display latency, native IME or window-server composition. Runs were warm-cache; a cold launch was not measured.

Reproduce from the repository root:

```sh
PATH=/opt/homebrew/bin:$PATH cmake --preset release -DCMAKE_PREFIX_PATH=/opt/homebrew
PATH=/opt/homebrew/bin:$PATH cmake --build --preset release -j 1
for scenario in one ten million longline unicode cycles prolonged; do
  QT_QPA_PLATFORM=offscreen build/release/promptpad_perf "$scenario"
done
```

The table uses **physical footprint** for the macOS total-app memory target. RSS is shown separately and is not directly comparable. Values are from a release run after building the final font fallback; they vary with OS cache and compression. Ordinary documents contain exactly 100,000 Unicode code points (103,304 UTF-8 bytes); the million-character mixed and one-line cases contain 1,033,056 and 1,063,493 bytes respectively.

| Scenario | Launch to editable | Steady physical footprint | Steady RSS | Final physical footprint | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| One 100,000-character mixed prompt | 193 ms | 25.6 MiB | 65.2 MiB | 26.0 MiB | Below 100 MB memory target |
| Ten 100,000-character prompts | 189 ms | 31.5 MiB | 71.1 MiB | 31.5 MiB | Below 150 MB memory target |
| One 1,000,000-character prompt | 149 ms | 35.1 MiB | 74.6 MiB | 35.1 MiB | Stress case |
| One 1,000,000-character wrapped line | **1,179 ms** | 40.3 MiB | 79.8 MiB | 40.3 MiB | Misses 1 s startup target |
| Mixed Unicode, 100,000 characters | 142 ms | 25.7 MiB | 65.3 MiB | 26.0 MiB | Stress case |
| 20 repeated open/close cycles | 136 ms initial | 25.6 MiB | 65.2 MiB | **37.5 MiB** | ~11.9 MiB retained growth |
| 1,000 timed edits to 100,000-character prompt | 136 ms initial | 25.6 MiB | 65.2 MiB | 26.3 MiB | Recovery timers active |

For 100 short edits, the input-to-Scintilla-paint-event proxy had p95 1.84 ms, max 6.09 ms, with 100 natural paint events and no forced paints. The Unicode run had p95 2.70 ms. The 1,000-event prolonged run had p95 **5.25 ms**, max **10.24 ms**, and no forced paints. It inserted 1,000 bytes, processed events between keystrokes, exercised the 5-second backup maximum and completed undo/redo cycles. These numbers are well below the 30 ms proxy target, but they do **not** measure screen scan-out or a physical keyboard. Timer callbacks between sampled key events can cause pauses this proxy misses.

Idle CPU after pending work completed was 0.0021 CPU seconds over a 2-second window for one prompt, 0.0020 seconds for ten prompts, and 0.0195 seconds for the long wrapped line. The app has no continuous polling.

An additional **native Cocoa-window** sample used the ad-hoc signed `dist/PromptPad.app`, a temporary profile and the same 100,000-character synthetic files. After two seconds, `/usr/bin/footprint --noCategories` reported **89 MB physical footprint** and `ps` reported **184,128 KiB RSS** for one open file. After forwarding nine more files to the same instance and waiting two seconds, it reported **95 MB physical footprint** and **183,440 KiB RSS**. Both native physical-footprint samples meet the initial one- and ten-document memory budgets, but the one-document result is close to 100 MB. The bundle's Cocoa/Qt framework loading is materially heavier than the offscreen harness; do not treat the two measurements as interchangeable. These are sampled steady values, with `footprint` showing equal sampled peaks of 89 and 95 MB. Native startup-to-editable and native input-to-paint latency were not instrumented.

The **packaged native forwarding launcher** for `--new`, with a running instance, took **0.12 s real time**, peaked at **61.4 MB RSS** and **12.3 MB physical footprint** as a separate, short-lived process. The offscreen/unbundled forwarder took 0.03 s, 19.7 MB peak RSS and 4.4 MB peak physical footprint. These are per-process transient peaks, not a permanent helper. Do not add launcher peak RSS to the steady app physical-footprint numbers; those are different metrics and sampling windows.

The long-line startup miss is primarily a wrapped layout stress case; making over 1 MB a single visual line causes Scintilla's initial wrap calculation and paint work to dominate. The 20-cycle growth may include Qt and Scintilla allocator retention; it has not been established as a leak. Whole-document search, applying fold levels and backup snapshots currently run on the GUI thread, so still larger real prompts or slow disks may pause. Outline parsing already runs in a worker with a revision check. Next profiling steps are Instruments allocation and time traces during repeated close/open and long-line startup, then moving expensive backup/search work off the GUI thread with revision checks. A native-window, cold-start and ten-minute physical typing run remain to be measured.
