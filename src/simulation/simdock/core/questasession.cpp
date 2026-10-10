#include "questasession.h"
#include "scoreboard.h"
#include "workspace.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace simdock {
static QString simulatorPath(const QString& path)
{
#ifdef Q_OS_WIN
    // Questa's GUI directory/layout code still expands some path characters.
    // Existing DOS aliases refer to the same files without copying workspace data.
    const QString native = QDir::toNativeSeparators(path);
    const auto* wide = reinterpret_cast<const wchar_t*>(native.utf16());
    const DWORD size = GetShortPathNameW(wide, nullptr, 0);
    if (size) {
        std::wstring buffer(size, L'\0');
        const DWORD written = GetShortPathNameW(wide, buffer.data(), size);
        if (written && written < size)
            return QDir::fromNativeSeparators(QString::fromWCharArray(buffer.data(), int(written)));
    }
#endif
    return QDir::fromNativeSeparators(path);
}

QString tclWord(const QString& value)
{
    QString escaped;
    for (QChar c : value) {
        if (c == QLatin1Char('\n')) escaped += QStringLiteral("\\n");
        else if (c == QLatin1Char('\r')) escaped += QStringLiteral("\\r");
        else {
            if (QStringLiteral("\\\"$[]").contains(c)) escaped += QLatin1Char('\\');
            escaped += c;
        }
    }
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString simulatorError(const QString& path)
{
    const QFileInfo executable(path);
    if (!executable.isFile()) return QStringLiteral("Select a valid Questa vsim executable.");
#ifdef Q_OS_WIN
    if (executable.fileName().compare(QStringLiteral("vsim.exe"), Qt::CaseInsensitive) != 0)
        return QStringLiteral("Select vsim.exe from your Questa installation.");
    if (!QFileInfo(executable.dir().filePath(QStringLiteral("vlog.exe"))).isFile())
#else
    if (!QFileInfo(executable.dir().filePath(QStringLiteral("vlog"))).isFile())
#endif
        return QStringLiteral("vlog was not found in the selected directory. Check your Questa installation.");
    return {};
}

QString scoreboardResultScript(const QString& tbName)
{
    const QString scope = QStringLiteral("sim:/%1/__simdock_scoreboard/").arg(tbName);
    // Questa's decimal radix sign-extends even a one-bit completion flag to -1.
    return QStringLiteral("if {[examine -radix unsigned %1] != 1} {error \"Scoreboard did not finish. Check the run duration and simulation errors.\"}\n"
                          "if {[examine -radix unsigned %2] != 0} {error \"Scoreboard checks failed. See SIMDOCK_CHECK_FAIL entries in the run log.\"}\n"
                          "puts \"Scoreboard checks passed.\"\n")
        .arg(tclWord(scope + QStringLiteral("done")), tclWord(scope + QStringLiteral("errors")));
}
QString runScript(const QString& root, const Project& p, const QString& runDir, const QString& iniPath, WaveTheme waveTheme, bool reuseDesign)
{
    const QString directory = simulatorPath(runDir);
    const QString ini = simulatorPath(iniPath);
    QString s = QStringLiteral("catch {quit -sim}\n::simdock::state compiling\n");
    if (!reuseDesign) s += QStringLiteral("vlib %1\n").arg(tclWord(QDir(directory).filePath(QStringLiteral("work"))));
    else s += QStringLiteral("puts {SimDock: reusing compiled design; compiling stimulus only.}\n");
    QStringList files = p.sources;
    files.removeAll(p.tbFile);
    files << p.tbFile;
    QStringList dirs{simulatorPath(root)};
    for (const auto& f : files) {
        const QString dir = simulatorPath(QFileInfo(QDir(root).filePath(f)).absolutePath());
        if (!dirs.contains(dir)) dirs << dir;
    }
    QString includeArgs;
    for (const auto& dir : dirs) includeArgs += QLatin1Char(' ') + tclWord(QStringLiteral("+incdir+") + dir);
    for (const auto& file : files) {
        if (reuseDesign && file != p.tbFile) continue;
        const QString suffix = QFileInfo(file).suffix().toLower();
        if (suffix == QStringLiteral("vh") || suffix == QStringLiteral("svh")) continue;
        s += QStringLiteral("vlog -modelsimini %1 -work work %2%3 %4\n")
            .arg(tclWord(ini), suffix == QStringLiteral("sv") ? QStringLiteral("-sv") : QString(), includeArgs,
                 tclWord(simulatorPath(QDir(root).filePath(file))));
    }
    s += QStringLiteral("::simdock::state loading\nvsim -modelsimini %1 -onfinish stop -voptargs=+acc -wlf %2 %3 +SIMDOCK_DURATION_NS=%4\n")
        .arg(tclWord(ini), tclWord(QDir(directory).filePath(QStringLiteral("result.wlf"))),
             tclWord(QStringLiteral("work.") + p.tbName)).arg(p.durationNs);
    // Questa can return normally from Tcl's vsim command after elaboration fails.
    s += QStringLiteral("if {[runStatus] eq \"nodesign\"} {\n"
        "    error \"Questa could not load the testbench. Check the compiler and elaboration errors above.\"\n}\n");
    s += QStringLiteral("onbreak {resume}\nview wave\ncatch {delete wave *}\n");
    if (p.waveScope == QStringLiteral("all")) s += QStringLiteral("add wave -r /*\nlog -r /*\n");
    else if (p.waveScope == QStringLiteral("selected")) {
        for (const auto &signal : p.waveSignals) {
            const auto path = tclWord(QStringLiteral("/%1/%2").arg(p.tbName, signal));
            s += QStringLiteral("add wave %1\nlog %1\n").arg(path);
        }
    } else {
        const auto path = tclWord(QStringLiteral("/%1/*").arg(p.tbName));
        s += QStringLiteral("add wave %1\nlog %1\n").arg(path);
    }
    if (scoreboardEnabled(p.stimulus.value(QStringLiteral("scoreboard")).toObject()))
        s += QStringLiteral("log -r %1\n").arg(tclWord(QStringLiteral("/%1/__simdock_scoreboard/*").arg(p.tbName)));
    QString colorSignals = QStringLiteral("[find signals -r /*]");
    if (p.waveScope == QStringLiteral("interface"))
        colorSignals = QStringLiteral("[find signals %1]").arg(tclWord(QStringLiteral("/%1/*").arg(p.tbName)));
    else if (p.waveScope == QStringLiteral("selected")) {
        colorSignals = QStringLiteral("[list");
        for (const auto &signal : p.waveSignals) colorSignals += QLatin1Char(' ') + tclWord(QStringLiteral("/%1/%2").arg(p.tbName, signal));
        colorSignals += QLatin1Char(']');
    }
    s += waveAppearanceScript(waveTheme) + waveSignalColorsScript(waveTheme, colorSignals);
    s += QStringLiteral("::simdock::state running\nrun %1 ns\nwave zoom full\n").arg(p.durationNs);
    if (scoreboardEnabled(p.stimulus.value(QStringLiteral("scoreboard")).toObject()))
        s += scoreboardResultScript(p.tbName);
    return s;
}

static bool writeFile(const QString& path, const QString& text, QString* error)
{
    QSaveFile f(path);
    const QByteArray bytes = text.toUtf8();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        *error = f.errorString(); return false;
    }
    return true;
}

QuestaSession::QuestaSession(QObject* parent) : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_poll.setInterval(150);
    m_deadline.setSingleShot(true);
    connect(&m_poll, &QTimer::timeout, this, &QuestaSession::poll);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        if (!m_busy) return;
        stop();
        emit stateChanged(QStringLiteral("Timed out"));
        emit logText(QStringLiteral("\nThe wall-clock timeout was reached. The Questa session has been stopped.\n"));
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        const auto bytes = m_process.readAllStandardOutput();
        if (!bytes.isEmpty()) emit logText(QString::fromLocal8Bit(bytes));
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(false, m_process.errorString());
    });
    connect(&m_process, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
        poll();
        m_ready = false;
        m_poll.stop();
        if (m_busy && !m_stopping) finish(false, QStringLiteral("Questa exited with code %1.").arg(code));
        else if (!m_stopping) emit stateChanged(QStringLiteral("Session closed"));
    });
#ifdef Q_OS_WIN
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_SUSPENDED | CREATE_NO_WINDOW;
    });
    connect(&m_process, &QProcess::started, this, [this] {
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(m_process.processId()));
        const bool assigned = process && m_job && AssignProcessToJobObject(HANDLE(m_job), process);
        if (process) CloseHandle(process);
        if (!assigned) {
            m_process.kill();
            finish(false, QStringLiteral("Could not manage Questa child processes. Startup was cancelled."));
            return;
        }
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        bool resumed = false;
        if (snapshot != INVALID_HANDLE_VALUE && Thread32First(snapshot, &entry)) {
            do {
                if (entry.th32OwnerProcessID != DWORD(m_process.processId())) continue;
                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
                if (thread) { resumed = ResumeThread(thread) != DWORD(-1) || resumed; CloseHandle(thread); }
            } while (Thread32Next(snapshot, &entry));
        }
        if (snapshot != INVALID_HANDLE_VALUE) CloseHandle(snapshot);
        if (!resumed) { m_process.kill(); finish(false, QStringLiteral("Could not resume the Questa startup process.")); }
    });
#endif
}

QuestaSession::~QuestaSession()
{
    stop();
    m_process.waitForFinished(1500);
#ifdef Q_OS_WIN
    if (m_job) CloseHandle(HANDLE(m_job));
#endif
}

bool QuestaSession::launch(const QString& executable, QString* error)
{
    m_stopping = false;
    m_sessionDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/simdock-session-XXXXXX"));
    if (!m_sessionDir->isValid()) { *error = QStringLiteral("Could not create the Questa session directory."); return false; }
    m_ready = false;
    m_logOffset = 0;
    m_lastState.clear();
    m_executable = executable;
    const QString base = m_sessionDir->path();
    QString script = QStringLiteral("namespace eval ::simdock { variable base %1; variable runId \"\" }\n").arg(tclWord(base));
    script += QStringLiteral(R"tcl(
proc ::simdock::state {value} {
    variable base
    variable runId
    set out [open [file join $base state.tmp] w]
    try {
        fconfigure $out -encoding utf-8 -translation lf
        puts $out "$runId|$value"
    } finally {
        close $out
    }
    # Windows readers can briefly deny replacement. Keep the complete record
    # and retry for at most 975 ms, without reentering the command event loop.
    for {set attempt 0} {$attempt < 40} {incr attempt} {
        if {![catch {file rename -force [file join $base state.tmp] [file join $base state]} message details]} {
            return
        }
        set code [dict get $details -errorcode]
        if {$attempt == 39 || $::tcl_platform(platform) ne "windows"
            || [lindex $code 0] ne "POSIX" || [lindex $code 1] ni {EACCES EPERM EBUSY}} {
            return -options $details "Could not publish Questa state '$value': $message"
        }
        after 25
    }
}
proc ::simdock::fatal {message details} {
    puts "SimDock session error: $message"
    puts [dict get $details -errorinfo]
    quit -force -code 1
}
proc ::simdock::poll {} {
    variable base
    if {[catch {
        if {[file exists [file join $base command.do]]} {
            file rename -force [file join $base command.do] [file join $base active.do]
            if {[catch {uplevel #0 [list source -encoding utf-8 [file join $base active.do]]} message details]} {
                puts "SimDock error: $message"
                puts [dict get $details -errorinfo]
                set result failed
            } else {
                set result completed
            }
            file delete -force [file join $base active.do]
            ::simdock::state $result
        }
    } message details]} {
        # A failed terminal publication must not leave a live but unresponsive
        # session. Process exit reports failure to the host even if state is locked.
        ::simdock::fatal $message $details
        return
    }
    after 150 ::simdock::poll
}
)tcl");
    script += QStringLiteral("transcript file %1\ntranscript on\n"
        "if {[catch {::simdock::state ready} message details]} {\n"
        "    ::simdock::fatal $message $details\n"
        "} else {after 150 ::simdock::poll}\n")
        .arg(tclWord(QDir(base).filePath(QStringLiteral("transcript.log"))));
    const QString bootstrap = QDir(base).filePath(QStringLiteral("bootstrap.do"));
    if (!writeFile(bootstrap, script, error)) return false;
#ifdef Q_OS_WIN
    if (m_job) CloseHandle(HANDLE(m_job));
    m_job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!m_job || !SetInformationJobObject(HANDLE(m_job), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        *error = QStringLiteral("Could not create the Questa process management object."); return false;
    }
#endif
    m_process.setProgram(executable);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("MODELSIM_PREFERENCES"), QDir(base).filePath(QStringLiteral("preferences")));
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(base);
    m_process.setArguments({QStringLiteral("-gui"), QStringLiteral("-do"), QStringLiteral("source -encoding utf-8 ") + tclWord(bootstrap)});
    m_process.start();
    m_poll.start();
    return true;
}

PreparedRun prepareRun(const QString& executable, const QString& root, const Project& p,
                       const std::atomic_bool* cancelled, const QMap<QString, QByteArray>& overrides)
{
    PreparedRun result{executable, root, {}, p, {}};
    result.error = simulatorError(executable);
    if (!result.error.isEmpty()) return result;
    QMap<QString, QByteArray> fingerprints;
    result.error = validateInputs(root, p, &fingerprints, cancelled, overrides);
    if (!result.error.isEmpty()) return result;
    result.inputFingerprints = fingerprints;
    if (p.waveScope == QStringLiteral("selected") && p.waveSignals.isEmpty()) {
        result.error = QStringLiteral("Choose at least one waveform signal, or use Interface / All."); return result;
    }
    const QRegularExpression identifier(QStringLiteral("^[A-Za-z_][A-Za-z0-9_$]*$"));
    for (const auto &signal : p.waveSignals) if (!identifier.match(signal).hasMatch()) {
        result.error = QStringLiteral("Invalid waveform signal: %1").arg(signal); return result;
    }
    // Reuse is limited to SimDock-generated stimulus. An arbitrary user TB can
    // declare packages/design units or compilation-unit state of its own.
    if (p.stimulus.isEmpty()) return result;
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << QStringLiteral("simdock.questa-design/v1") << root << p.sources;
    stream << QStringLiteral("vlog:-work work;-sv by suffix;incdir=root,ordered-source-dirs,tb-dir")
           << QFileInfo(p.tbFile).path() << QProcessEnvironment::systemEnvironment().toStringList();
    fingerprints.remove(QDir::cleanPath(QDir(root).filePath(p.tbFile)));
    stream << fingerprints;
    const QDir toolDir(QFileInfo(executable).absolutePath());
#ifdef Q_OS_WIN
    const auto compiler = toolDir.filePath(QStringLiteral("vlog.exe"));
#else
    const auto compiler = toolDir.filePath(QStringLiteral("vlog"));
#endif
    // Ask the selected compiler for its actual version, including patch level.
    QProcess version;
    version.setProgram(compiler); version.setArguments({QStringLiteral("-version")});
    version.setProcessChannelMode(QProcess::MergedChannels); version.start();
    int elapsed = 0;
    while (!version.waitForFinished(50) && elapsed < 5000 && !(cancelled && cancelled->load())) elapsed += 50;
    if (version.state() != QProcess::NotRunning) { version.kill(); version.waitForFinished(1000); return result; }
    if (version.exitStatus() != QProcess::NormalExit || version.exitCode() != 0) return result;
    const auto versionText = version.readAllStandardOutput();
    if (versionText.isEmpty()) return result;
    stream << executable << versionText;
    for (const auto &path : {executable, compiler, toolDir.absoluteFilePath(QStringLiteral("../modelsim.ini"))}) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!hash.addData(&file)) return result;
            stream << path << hash.result();
        } else stream << path << QByteArray();
    }
    if (!(cancelled && cancelled->load())) result.designFingerprint = QCryptographicHash::hash(key, QCryptographicHash::Sha256);
    return result;
}

bool QuestaSession::start(const QString& executable, const QString& root, const Project& p, QString* error, WaveTheme waveTheme)
{
    return start(prepareRun(executable, root, p), error, waveTheme);
}
bool QuestaSession::start(const PreparedRun& prepared, QString* error, WaveTheme waveTheme)
{
    *error = prepared.error;
    if (!error->isEmpty()) return false;
    const auto& executable = prepared.executable;
    const auto& root = prepared.root;
    const auto& p = prepared.project;
    if (m_busy) { *error = QStringLiteral("A simulation is already running."); return false; }
    if (alive() && !m_ready) { *error = QStringLiteral("The Questa session is starting or closing. Try again shortly."); return false; }
    if (alive() && QFileInfo(executable).canonicalFilePath() != QFileInfo(m_executable).canonicalFilePath()) {
        *error = QStringLiteral("Close the current Questa session before changing the simulator path."); return false;
    }
    m_runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_runDir = QDir(root).filePath(QStringLiteral(".simdock/runs/") + m_runId);
    if (!insideWorkspace(root, m_runDir) || !QDir().mkpath(m_runDir)) {
        *error = QStringLiteral("Could not create a run directory inside the workspace."); return false;
    }
    const QString ini = QDir(m_runDir).filePath(QStringLiteral("modelsim.ini"));
    const QString installedIni = QDir(QFileInfo(executable).absolutePath()).absoluteFilePath(QStringLiteral("../modelsim.ini"));
    const bool reuse = alive() && !prepared.designFingerprint.isEmpty() && prepared.designFingerprint == m_cachedDesign
        && QFileInfo(QDir(m_cachedWork).filePath(QStringLiteral("_info"))).isFile();
    const auto work = reuse ? m_cachedWork : QDir(m_runDir).filePath(QStringLiteral("work"));
    m_pendingDesign = prepared.designFingerprint; m_pendingWork = work;
    const auto toolDirectory = simulatorPath(m_runDir);
    // The new library does not exist yet, so shorten its existing parent.
    const auto mappedWork = reuse ? simulatorPath(work) : QDir(toolDirectory).filePath(QStringLiteral("work"));
    QString iniText = QStringLiteral("[Library]\nwork = %1\n").arg(mappedWork);
    if (QFileInfo(installedIni).isFile()) iniText += QStringLiteral("others = %1\n").arg(QDir::cleanPath(installedIni));
    if (!writeFile(ini, iniText, error)) return false;
    const QString script = runScript(root, p, m_runDir, ini, waveTheme, reuse);
    m_pending = QStringLiteral("set ::simdock::runId %1\n").arg(tclWord(m_runId)) + script;
    if (!writeFile(QDir(m_runDir).filePath(QStringLiteral("run.do")), script, error)) return false;
    m_busy = true;
    if (!alive() && !launch(executable, error)) { m_busy = false; return false; }
    m_deadline.start(m_wallTimeout);
    emit stateChanged(m_ready ? QStringLiteral("Preparing compilation") : QStringLiteral("Starting Questa"));
    emit logText(QStringLiteral("\n--- %1 | %2 ns ---\nWorkspace: %3\nRun directory: %4\n").arg(p.name).arg(p.durationNs).arg(root, m_runDir));
    if (m_ready && !sendPending(error)) { finish(false, *error); return false; }
    return true;
}

bool QuestaSession::sendPending(QString* error)
{
    if (m_pending.isEmpty()) return true;
    if (!writeFile(QDir(m_sessionDir->path()).filePath(QStringLiteral("command.do")), m_pending, error)) return false;
    m_pending.clear();
    return true;
}

void QuestaSession::poll()
{
    if (!m_sessionDir) return;
    const QDir dir(m_sessionDir->path());
    QFile log(dir.filePath(QStringLiteral("transcript.log")));
    if (log.open(QIODevice::ReadOnly)) {
        if (log.size() < m_logOffset) m_logOffset = 0;
        log.seek(m_logOffset);
        const auto bytes = log.read(256 * 1024);
        m_logOffset += bytes.size();
        if (!bytes.isEmpty()) {
            emit logText(QString::fromLocal8Bit(bytes));
            if (!m_runDir.isEmpty()) {
                QFile saved(QDir(m_runDir).filePath(QStringLiteral("transcript.log")));
                if (saved.open(QIODevice::WriteOnly | QIODevice::Append)) saved.write(bytes);
            }
        }
    }
    QByteArray record;
    {
        QFile stateFile(dir.filePath(QStringLiteral("state")));
        if (!stateFile.open(QIODevice::ReadOnly)) return;
        record = stateFile.read(513);
        if (stateFile.error() != QFileDevice::NoError) return;
    }
    // Release the read handle before sending commands or notifying receivers;
    // either can give Questa time to replace the state file on Windows.
    if (record.size() > 512 || !record.endsWith('\n') || record.count('\n') != 1) return;
    const QString state = QString::fromUtf8(record).trimmed();
    if (state.count(QLatin1Char('|')) != 1) return;
    if (state == m_lastState) return;
    m_lastState = state;
    const QString id = state.section(QLatin1Char('|'), 0, 0);
    const QString value = state.section(QLatin1Char('|'), 1, 1);
    if (id.isEmpty() && value == QStringLiteral("ready") && m_busy && !m_ready && !m_stopping) {
        m_ready = true;
        QString error;
        if (!sendPending(&error)) finish(false, error);
        return;
    }
    if (id != m_runId || !m_busy) return;
    if (value == QStringLiteral("compiling")) emit stateChanged(QStringLiteral("Compiling"));
    else if (value == QStringLiteral("loading")) emit stateChanged(QStringLiteral("Loading TB"));
    else if (value == QStringLiteral("running")) emit stateChanged(QStringLiteral("Running"));
    else if (value == QStringLiteral("completed")) finish(true, QStringLiteral("Run completed. Questa remains open for waveform inspection."));
    else if (value == QStringLiteral("failed")) finish(false, QStringLiteral("Compilation or simulation failed. Check the log."));
}

void QuestaSession::finish(bool success, const QString& message)
{
    if (success) { m_cachedDesign = m_pendingDesign; m_cachedWork = m_pendingWork; }
    else { m_cachedDesign.clear(); m_cachedWork.clear(); }
    m_pendingDesign.clear(); m_pendingWork.clear();
    m_busy = false;
    m_deadline.stop();
    m_pending.clear();
    emit stateChanged(success ? QStringLiteral("Completed") : QStringLiteral("Failed"));
    emit logText(message + QLatin1Char('\n'));
    emit finished(success, message);
}

void QuestaSession::stop()
{
    m_cachedDesign.clear(); m_cachedWork.clear();
    m_pendingDesign.clear(); m_pendingWork.clear();
    m_stopping = true;
    m_deadline.stop();
    m_pending.clear();
#ifdef Q_OS_WIN
    if (m_job) TerminateJobObject(HANDLE(m_job), 1);
#endif
    if (alive()) m_process.kill();
    m_ready = false;
    m_poll.stop();
    if (m_busy) finish(false, QStringLiteral("Simulation stopped and the SimDock-managed Questa session closed."));
}
}
