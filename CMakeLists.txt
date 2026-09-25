cmake_minimum_required(VERSION 3.24)
project(PromptPad VERSION 0.1.0 LANGUAGES CXX)
set(PROMPTPAD_DISPLAY_NAME "${PROJECT_NAME}")
configure_file(src/app/AppIdentity.h.in generated/AppIdentity.h @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Widgets Network Sql Test Concurrent Core5Compat Svg)

file(GLOB SCINTILLA_CORE CONFIGURE_DEPENDS vendor/scintilla/src/*.cxx)
add_library(scintilla_qt STATIC
  vendor/scintilla/qt/ScintillaEditBase/PlatQt.cpp
  vendor/scintilla/qt/ScintillaEditBase/ScintillaQt.cpp
  vendor/scintilla/qt/ScintillaEditBase/ScintillaEditBase.cpp
  vendor/scintilla/qt/ScintillaEditBase/ScintillaEditBase.h
  ${SCINTILLA_CORE})
target_include_directories(scintilla_qt PUBLIC
  vendor/scintilla/qt/ScintillaEditBase vendor/scintilla/include vendor/scintilla/src)
target_compile_definitions(scintilla_qt PRIVATE SCINTILLA_QT=1 MAKING_LIBRARY=1)
target_link_libraries(scintilla_qt PUBLIC Qt6::Widgets Qt6::Core5Compat)

file(GLOB LEXILLA_SUPPORT CONFIGURE_DEPENDS vendor/lexilla/lexlib/*.cxx)
add_library(lexilla_markdown STATIC ${LEXILLA_SUPPORT} vendor/lexilla/lexers/LexMarkdown.cxx)
target_include_directories(lexilla_markdown PUBLIC
  vendor/scintilla/include vendor/lexilla/include vendor/lexilla/lexlib)

add_library(promptpad_core STATIC
  src/Appearance.h
  src/editor/PromptEditor.cpp src/editor/PromptEditor.h
  src/documents/FileIO.cpp src/documents/FileIO.h
  src/documents/Outline.cpp src/documents/Outline.h
  src/documents/FindReplace.cpp src/documents/FindReplace.h
  src/storage/Store.cpp src/storage/Store.h
  src/launcher/Ipc.cpp src/launcher/Ipc.h
  src/ui/MainWindow.cpp src/ui/MainWindow.h
  src/ui/LibraryPanel.cpp src/ui/LibraryPanel.h src/ui/assets.qrc)
target_link_libraries(promptpad_core PUBLIC scintilla_qt lexilla_markdown Qt6::Widgets Qt6::Network Qt6::Sql Qt6::Concurrent Qt6::Svg)
target_include_directories(promptpad_core PUBLIC src ${CMAKE_CURRENT_BINARY_DIR}/generated)

add_executable(promptpad MACOSX_BUNDLE src/app/main.cpp)
target_link_libraries(promptpad PRIVATE promptpad_core)
set_target_properties(promptpad PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "${PROMPTPAD_DISPLAY_NAME}")

enable_testing()
add_executable(editor_smoke tests/editor_smoke.cpp)
target_link_libraries(editor_smoke PRIVATE promptpad_core Qt6::Test)
add_test(NAME editor_smoke COMMAND editor_smoke)
set_tests_properties(editor_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_executable(preservation_tests tests/preservation_tests.cpp)
target_link_libraries(preservation_tests PRIVATE promptpad_core Qt6::Test)
target_compile_definitions(preservation_tests PRIVATE RECOVERY_WORKER_EXE="$<TARGET_FILE:recovery_worker>")
add_test(NAME preservation_tests COMMAND preservation_tests)
set_tests_properties(preservation_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_executable(outline_tests tests/outline_tests.cpp)
target_link_libraries(outline_tests PRIVATE promptpad_core Qt6::Test)
add_test(NAME outline_tests COMMAND outline_tests)
set_tests_properties(outline_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_executable(find_tests tests/find_tests.cpp)
target_link_libraries(find_tests PRIVATE promptpad_core Qt6::Test)
add_test(NAME find_tests COMMAND find_tests)
set_tests_properties(find_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_executable(ui_capture tests/ui_capture.cpp)
target_link_libraries(ui_capture PRIVATE promptpad_core)
add_executable(ipc_tests tests/ipc_tests.cpp)
target_link_libraries(ipc_tests PRIVATE promptpad_core Qt6::Test)
target_compile_definitions(ipc_tests PRIVATE PROMPTPAD_EXE="$<TARGET_FILE:promptpad>")
add_test(NAME ipc_tests COMMAND ipc_tests)
set_tests_properties(ipc_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 25)
add_executable(recovery_worker tests/recovery_worker.cpp)
target_link_libraries(recovery_worker PRIVATE promptpad_core)
add_executable(promptpad_perf benchmarks/perf.cpp)
target_link_libraries(promptpad_perf PRIVATE promptpad_core)
