#include "editor/PromptEditor.h"
#include "storage/Store.h"
#include "ui/MainWindow.h"
#include "Scintilla.h"

#include <QApplication>
#include <QAction>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cstdio>

#ifdef Q_OS_MACOS
#include <mach/mach.h>
#include <mach/task_info.h>
#endif
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif

namespace {
QByteArray workload(qsizetype targetCharacters, bool oneLine = false) {
    QByteArray result;
    result.reserve(targetCharacters * 2);
    const QByteArray line = oneLine ? QByteArray("αb/{{braces}} `path/with spaces` command --flag JSON {\"x\":1} 🧭 ")
        : QByteArray("## Step\n\nDescribe the coding change precisely.\n\n```json\n{\"path\":\"src/α.cpp\",\"ok\":true}\n```\n\n- Keep {{braces}} literal 🧭\n\n");
    const auto codepoints = QString::fromUtf8(line).toUcs4();
    qsizetype characters = 0;
    while (characters + codepoints.size() <= targetCharacters) {
        result.append(line);
        characters += codepoints.size();
    }
    for (qsizetype index = 0; characters < targetCharacters; ++index, ++characters) {
        const char32_t point = static_cast<char32_t>(codepoints[index]);
        result.append(QString::fromUcs4(&point, 1).toUtf8());
    }
    return result;
}

struct Memory { qint64 rss = -1; qint64 footprint = -1; qint64 peak = -1; };
Memory memory() {
    Memory result;
#ifdef Q_OS_MACOS
    mach_task_basic_info_data_t basic{};
    mach_msg_type_number_t basicCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&basic), &basicCount) == KERN_SUCCESS)
        result.rss = basic.resident_size;
    task_vm_info_data_t vm{};
    mach_msg_type_number_t vmCount = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &vmCount) == KERN_SUCCESS)
        result.footprint = vm.phys_footprint;
#endif
#ifdef Q_OS_UNIX
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) result.peak = usage.ru_maxrss;
#endif
    return result;
}

double cpuSeconds() {
#ifdef Q_OS_UNIX
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 + usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
#endif
    return -1;
}

void pauseWithEvents(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
}

int main(int argc, char **argv) {
    const auto start = std::chrono::steady_clock::now();
    QApplication app(argc, argv);
    if (argc != 2) return 2;
    const QString scenario = QString::fromLocal8Bit(argv[1]);
    if (!QStringList{QStringLiteral("empty"), QStringLiteral("preview"), QStringLiteral("split"), QStringLiteral("switch"), QStringLiteral("library"),
                     QStringLiteral("one"), QStringLiteral("ten"), QStringLiteral("million"),
                     QStringLiteral("longline"), QStringLiteral("unicode"), QStringLiteral("cycles"),
                     QStringLiteral("prolonged")}.contains(scenario)) return 2;
    QCoreApplication::setOrganizationName(QStringLiteral("PromptPadBenchmark"));
    QCoreApplication::setApplicationName(QStringLiteral("PromptPadBenchmark"));
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 3;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    Store store;
    QString error;
    if (!store.open(temporary.path(), &error)) return 4;
    const bool empty = scenario == QStringLiteral("empty");
    MainWindow window(&store, empty);
    window.show();
    const int files = empty ? 0 : scenario == QStringLiteral("ten") ? 10 : 1;
    const qsizetype size = empty ? 0 : (scenario == QStringLiteral("million") || scenario == QStringLiteral("longline")) ? 1000000 : 100000;
    const QByteArray source = empty ? QByteArray{} : workload(size, scenario == QStringLiteral("longline"));
    for (int i = 0; i < files; ++i) {
        const QString path = temporary.filePath(QStringLiteral("synthetic-%1.md").arg(i));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(source) != source.size()) return 5;
        file.close();
        if (window.openPath(path).isEmpty()) return 6;
    }
    app.processEvents();
    const double readyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    pauseWithEvents(1800);
    const Memory steady = memory();
    double previewOpenMs = -1;
    double libraryOpenMs = -1;
    double switchTotalMs = -1;
    Memory previewMemory;
    Memory libraryMemory;
    Memory afterSwitchMemory;
    qint64 switchPeakFootprint = -1;
    if (scenario == QStringLiteral("preview") || scenario == QStringLiteral("split") || scenario == QStringLiteral("switch")) {
        const auto beforePreview = std::chrono::steady_clock::now();
        window.setViewMode(scenario == QStringLiteral("split") ? MainWindow::ViewMode::SplitPreview : MainWindow::ViewMode::Preview);
        app.processEvents();
        previewOpenMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - beforePreview).count();
        pauseWithEvents(300);
        previewMemory = memory();
        switchPeakFootprint = previewMemory.footprint;
        if (scenario == QStringLiteral("switch")) {
            const auto beforeSwitch = std::chrono::steady_clock::now();
            for (int i = 0; i < 20; ++i) {
                window.setViewMode(MainWindow::ViewMode::Editor);
                app.processEvents();
                window.setViewMode(MainWindow::ViewMode::Preview);
                app.processEvents();
                switchPeakFootprint = qMax(switchPeakFootprint, memory().footprint);
            }
            window.setViewMode(MainWindow::ViewMode::Editor);
            app.processEvents();
            switchTotalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - beforeSwitch).count();
            pauseWithEvents(300);
            afterSwitchMemory = memory();
        }
    }
    if (scenario == QStringLiteral("library")) {
        const auto beforeLibrary = std::chrono::steady_clock::now();
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == QStringLiteral("Open library")) { action->trigger(); break; }
        app.processEvents();
        libraryOpenMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - beforeLibrary).count();
        pauseWithEvents(300);
        libraryMemory = memory();
    }
    auto *editor = window.findChild<PromptEditor *>();
    auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("documentTabs"));
    if (tabs) editor = tabs->currentWidget()->findChild<PromptEditor *>();
    QVector<double> timings;
    int naturalPaints = 0;
    int forcedPaints = 0;
    qint64 bytesBeforeTyping = editor ? editor->send(SCI_GETLENGTH) : -1;
    qint64 bytesAfterTyping = bytesBeforeTyping;
    if (editor && (scenario == QStringLiteral("one") || scenario == QStringLiteral("unicode") ||
                   scenario == QStringLiteral("prolonged"))) {
        editor->send(SCI_GOTOPOS, editor->send(SCI_GETLENGTH));
        editor->setFocus();
        qint64 paintNanoseconds = -1;
        QElapsedTimer timer;
        bool measuring = false;
        QObject::connect(editor, &ScintillaEditBase::painted, &app, [&] {
            if (measuring) paintNanoseconds = timer.nsecsElapsed();
        });
        const int inputCount = scenario == QStringLiteral("prolonged") ? 1000 : 100;
        for (int i = 0; i < inputCount; ++i) {
            paintNanoseconds = -1;
            measuring = true;
            timer.start();
            QKeyEvent press(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_X, Qt::NoModifier);
            QApplication::sendEvent(editor, &press);
            QApplication::sendEvent(editor, &release);
            for (int spin = 0; spin < 100 && paintNanoseconds < 0; ++spin) app.processEvents();
            if (paintNanoseconds >= 0) ++naturalPaints;
            else {
                editor->viewport()->repaint();
                if (paintNanoseconds >= 0) ++forcedPaints;
            }
            measuring = false;
            if (paintNanoseconds >= 0) timings.append(paintNanoseconds / 1e6);
            if (scenario == QStringLiteral("prolonged")) pauseWithEvents(8);
        }
        for (int i = 0; i < 100; ++i) { editor->send(SCI_UNDO); editor->send(SCI_REDO); }
        bytesAfterTyping = editor->send(SCI_GETLENGTH);
    }
    if (scenario == QStringLiteral("cycles") && tabs) {
        for (int i = 0; i < 20; ++i) {
            const QString path = temporary.filePath(QStringLiteral("synthetic-%1.md").arg(i));
            if (!QFile::exists(path)) {
                QFile file(path); if (!file.open(QIODevice::WriteOnly)) return 7;
                file.write(source); file.close();
            }
            window.openPath(path);
            app.processEvents();
            for (auto *action : window.findChildren<QAction *>())
                if (action->text() == QStringLiteral("Close tab")) { action->trigger(); break; }
            app.processEvents();
        }
    }
    pauseWithEvents(1800);
    const Memory finalMemory = memory();
    const double cpuBeforeIdle = cpuSeconds();
    pauseWithEvents(2000);
    const double idleCpu = cpuSeconds() - cpuBeforeIdle;
    std::sort(timings.begin(), timings.end());
    const double p95 = timings.isEmpty() ? -1 : timings[qMin(timings.size() - 1, static_cast<int>(timings.size() * 0.95))];
    const QJsonObject report{
        {QStringLiteral("scenario"), scenario},
        {QStringLiteral("document_count"), empty ? 1 : files},
        {QStringLiteral("bytes_each"), static_cast<qint64>(source.size())},
        {QStringLiteral("characters_each"), static_cast<qint64>(size)},
        {QStringLiteral("launch_to_editable_ms"), readyMs},
        {QStringLiteral("steady_rss_bytes"), steady.rss},
        {QStringLiteral("steady_physical_footprint_bytes"), steady.footprint},
        {QStringLiteral("final_rss_bytes"), finalMemory.rss},
        {QStringLiteral("final_physical_footprint_bytes"), finalMemory.footprint},
        {QStringLiteral("peak_rss_bytes"), finalMemory.peak},
        {QStringLiteral("input_to_paint_proxy_samples"), timings.size()},
        {QStringLiteral("natural_paint_samples"), naturalPaints},
        {QStringLiteral("forced_paint_samples"), forcedPaints},
        {QStringLiteral("bytes_before_typing"), bytesBeforeTyping},
        {QStringLiteral("bytes_after_typing"), bytesAfterTyping},
        {QStringLiteral("input_to_paint_proxy_p95_ms"), p95},
        {QStringLiteral("input_to_paint_proxy_max_ms"), timings.isEmpty() ? -1 : timings.back()},
        {QStringLiteral("idle_cpu_seconds_over_2s"), idleCpu}
        ,{QStringLiteral("preview_first_open_ms"), previewOpenMs}
        ,{QStringLiteral("library_first_open_ms"), libraryOpenMs}
        ,{QStringLiteral("library_physical_footprint_bytes"), libraryMemory.footprint}
        ,{QStringLiteral("preview_physical_footprint_bytes"), previewMemory.footprint}
        ,{QStringLiteral("preview_rss_bytes"), previewMemory.rss}
        ,{QStringLiteral("switch_20_roundtrips_ms"), switchTotalMs}
        ,{QStringLiteral("switch_peak_physical_footprint_bytes"), switchPeakFootprint}
        ,{QStringLiteral("switch_final_physical_footprint_bytes"), afterSwitchMemory.footprint}
    };
    std::fputs(QJsonDocument(report).toJson(QJsonDocument::Compact).constData(), stdout);
    std::fputc('\n', stdout);
    return 0;
}
