#include "slangparseoptions.h"
#include "stimulus.h"

#include <slang/ast/Compilation.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/types/AllTypes.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>

#include <QCryptographicHash>
#include <QFile>
#include <slang/ZeroSlackCancellation.h>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <stdexcept>

namespace simdock
{
namespace
{
QString name(std::string_view text) { return QString::fromUtf8(text.data(), qsizetype(text.size())); }
[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toUtf8().constData()); }
} // namespace

StimulusSemantics resolveStimulus(const Module &module, const Scan &scan,
                                  const std::function<bool()> &cancelled)
{
    using namespace slang;
    using namespace slang::ast;
    StimulusSemantics result;
    try
    {
        // Same in-memory compilation route as ZeroSlack's SlangManager:
        // preload the source snapshot and parse the ordered buffers together.
        // Slang owns package lookup, preprocessing, typedef resolution, constant
        // evaluation and elaboration. SimDock only maps its results to lanes.
        const slang::zeroslack::CancellationScope cancellation(cancelled);
        if (cancelled && cancelled()) fail(QStringLiteral("Preparation cancelled."));
        SourceManager sourceManager;
        // Capture all compiler include attempts, including missing candidates.
        // A new higher-priority header must invalidate reuse just like an edit.
        sourceManager.setSourceLoader([&](const std::filesystem::path &path, slang::SmallVector<char> &buffer) {
            const auto utf8 = path.generic_u8string();
            const auto fileName = QString::fromUtf8(reinterpret_cast<const char *>(utf8.data()), qsizetype(utf8.size()));
            QFile file(fileName);
            if (!file.open(QIODevice::ReadOnly)) {
                result.includeFingerprints.insert(fileName, {});
                return std::make_error_code(std::errc::no_such_file_or_directory);
            }
            if (file.size() > 8 * 1024 * 1024) fail(QStringLiteral("Include exceeds the 8 MiB analysis limit: %1").arg(fileName));
            const auto bytes = file.readAll();
            if (file.error() != QFileDevice::NoError) fail(file.errorString());
            result.includeFingerprints.insert(fileName, QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
            buffer.insert(buffer.end(), bytes.cbegin(), bytes.cend());
            buffer.push_back('\0');
            return std::error_code{};
        });
        sourceManager.setDisableProximatePaths(true);
        QStringList fileNames;
        std::vector<SourceBuffer> buffers;
        for (const auto &file : scan.files)
        {
            const auto path = QFileInfo(QDir(scan.root).filePath(file.path)).absoluteFilePath();
            fileNames << path;
            const auto pathBytes = path.toUtf8();
            const auto buffer = sourceManager.assignText(
                std::string_view(pathBytes.constData(), size_t(pathBytes.size())),
                std::string_view(file.content.constData(), size_t(file.content.size())));
            sourceManager.addLineDirective(SourceLocation(buffer.id, 0), 2,
                                           std::string_view(pathBytes.constData(), size_t(pathBytes.size())),
                                           0);
            buffers.push_back(buffer);
        }
        if (buffers.empty())
            fail(QStringLiteral("No simulation sources are available for semantic analysis."));
        const auto includeDirs = slang_parse_options::effectiveIncludeDirsForFiles(fileNames, {});
        const auto syntaxOptions = slang_parse_options::makeSyntaxOptions(includeDirs, {});
        auto options = slang_parse_options::makeCompilationOptions();
        const auto top = module.name.toStdString();
        auto &compilationOptions = options.insertOrGet<CompilationOptions>();
        compilationOptions.topModules.emplace(top);
        // Match the generated TB's timebase for design elements without an
        // explicit timescale; packages often omit it even when the DUT has one.
        compilationOptions.defaultTimeScale = TimeScale::fromString("1ns/1ps");
        Compilation compilation(options);
        compilation.addSyntaxTree(syntax::SyntaxTree::fromBuffers(buffers, sourceManager, syntaxOptions));
        const auto &root = compilation.getRoot();
        Diagnostics errors;
        for (const auto &diagnostic : compilation.getAllDiagnostics())
            if (diagnostic.isError())
                errors.push_back(diagnostic);
        if (!errors.empty())
            fail(QStringLiteral("SystemVerilog semantic analysis failed:\n%1")
                     .arg(QString::fromStdString(DiagnosticEngine::reportAll(sourceManager, errors))));
        const InstanceSymbol *instance = nullptr;
        for (const auto *candidate : root.topInstances)
            if (candidate->name == top)
                instance = candidate;
        if (!instance)
            fail(QStringLiteral("Slang could not elaborate DUT %1.").arg(module.name));
        // These are restrictions of the current TB writer / waveform editor,
        // not an alternative type resolver or a fallback for compiler errors.
        if (!module.limitations.isEmpty())
            fail(module.limitations.join(QLatin1Char('\n')));
        const auto ports = instance->body.getPortList();
        if (ports.size() != size_t(module.ports.size()))
            fail(QStringLiteral("The DUT port list changed. Rescan the workspace."));
        for (const auto *symbol : ports)
        {
            if (symbol->kind != SymbolKind::Port)
                fail(QStringLiteral("Port %1 is not a scalar or packed data port.").arg(name(symbol->name)));
            const auto &port = symbol->as<PortSymbol>();
            if (port.direction == ArgumentDirection::InOut || port.direction == ArgumentDirection::Ref)
                fail(QStringLiteral(
                    "Graphical stimulus supports input ports; bidirectional ports are not supported yet."));
            const bool input = port.direction == ArgumentDirection::In;
            const auto portName = name(port.name);
            const auto declaration = std::find_if(module.ports.begin(), module.ports.end(),
                                                  [&](const auto &item) { return item.name == portName; });
            if (declaration == module.ports.end())
                fail(QStringLiteral("The DUT port list changed. Rescan the workspace."));
            const auto &type = port.getType().getCanonicalType();
            if (!input && (!type.isIntegral() || type.getBitWidth() < 1 || type.getBitWidth() > 64))
                continue;
            if (!type.isIntegral())
                fail(QStringLiteral("Port %1 is not a scalar or packed integral type.").arg(portName));
            const auto width = type.getBitWidth();
            if (width < 1 || width > 64)
                fail(QStringLiteral("Graphical stimulus supports packed values up to 64 bits: %1.")
                         .arg(portName));
            StimulusSignal signal{portName, declaration->type,     int(width), type.isSigned(),
                                  {},       declaration->namedType};
            signal.direction = input ? QStringLiteral("input") : QStringLiteral("output");
            if (type.kind == SymbolKind::EnumType)
                for (const auto &value : type.as<EnumType>().values())
                {
                    const auto &constant = value.getValue();
                    if (!constant.isInteger() || constant.integer().hasUnknown()) {
                        const auto issue = QStringLiteral("Enum value %1 cannot be represented by the waveform editor.")
                                               .arg(name(value.name));
                        if (input) fail(issue);
                        result.checksError = issue;
                        break;
                    }
                    signal.enumValues.insert(
                        name(value.name),
                        QString::fromStdString(constant.integer().toString(LiteralBase::Decimal, false)));
                }
            result.ports << signal;
            if (input) result.inputs << signal;
        }
        if (result.ports.isEmpty())
            fail(QStringLiteral("The DUT has no editable inputs."));
        return result;
    }
    catch (const slang::zeroslack::CompilationCancelled &)
    {
        result.error = QStringLiteral("Preparation cancelled.");
        result.inputs.clear(); result.ports.clear();
        return result;
    }
    catch (const std::exception &exception)
    {
        result.error = QString::fromUtf8(exception.what());
        result.inputs.clear(); result.ports.clear();
        return result;
    }
}
QList<StimulusSignal> stimulusSignals(const Module &module, const Scan &scan, QString *error)
{
    const auto result = resolveStimulus(module, scan);
    *error = result.error;
    if (error->isEmpty() && result.inputs.isEmpty()) *error = QStringLiteral("The DUT has no editable inputs.");
    return result.inputs;
}
QList<StimulusSignal> scoreboardSignals(const Module &module, const Scan &scan, QString *error)
{
    const auto result = resolveStimulus(module, scan);
    *error = result.error.isEmpty() ? result.checksError : result.error;
    return result.ports;
}
bool stimulusIncludesCurrent(const StimulusSemantics &semantics, const std::function<bool()> &cancelled)
{
    for (auto it = semantics.includeFingerprints.cbegin(); it != semantics.includeFingerprints.cend(); ++it) {
        if (cancelled && cancelled()) return false;
        QFile file(it.key());
        QByteArray fingerprint;
        if (file.open(QIODevice::ReadOnly)) {
            if (file.size() > 8 * 1024 * 1024) return false;
            fingerprint = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
            if (file.error() != QFileDevice::NoError) return false;
        }
        if (fingerprint != it.value()) return false;
    }
    return true;
}
} // namespace simdock
