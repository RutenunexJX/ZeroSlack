#include "commodecommandregistry.h"

#include <QStringList>

namespace {
constexpr int kVisibleHintCommandLimit = 8;

bool isUnsignedIntegerText(const QString& text)
{
    if (text.isEmpty())
        return false;
    for (const QChar ch : text) {
        if (!ch.isDigit())
            return false;
    }
    return true;
}

bool isFixedCommand(const ComModeCommandMetadata& command)
{
    return command.inputKind == ComModeCommandInputKind::Fixed;
}

void setReason(QString* reason, const QString& message)
{
    if (reason)
        *reason = message;
}

bool fixedRegistryHasChild(const QList<ComModeCommandMetadata>& registry,
                           const ComModeCommandMetadata& prefix)
{
    for (const ComModeCommandMetadata& command : registry) {
        if (&command == &prefix || !isFixedCommand(command))
            continue;
        if (command.command.size() > prefix.command.size()
            && command.command.startsWith(prefix.command)) {
            return true;
        }
    }
    return false;
}

QString commandLabel(const ComModeCommandMetadata& command)
{
    return command.command.isEmpty()
        ? QStringLiteral("<empty>")
        : command.command;
}

const ComModeCommandMetadata* moduleRelativeLineCommand()
{
    for (const ComModeCommandMetadata& command : comModeCommandRegistry()) {
        if (command.inputKind == ComModeCommandInputKind::ModuleRelativeLine)
            return &command;
    }
    return nullptr;
}

QStringList directChildCommands(const QString& prefix)
{
    QStringList children;
    for (const ComModeCommandMetadata& command : comModeCommandRegistry()) {
        if (!isFixedCommand(command)
            || command.command.size() <= prefix.size()
            || !command.command.startsWith(prefix)) {
            continue;
        }

        const QChar next = command.command.at(prefix.size());
        int existingIndex = -1;
        for (int i = 0; i < children.size(); ++i) {
            if (children.at(i).at(prefix.size()) == next) {
                existingIndex = i;
                break;
            }
        }
        if (existingIndex < 0) {
            children.append(command.command);
        } else if (command.command.size()
                   < children.at(existingIndex).size()) {
            children[existingIndex] = command.command;
        }
    }

    children.sort(Qt::CaseSensitive);
    return children;
}

QString childCommandHint(const QString& prefix)
{
    QStringList children = directChildCommands(prefix);
    if (children.isEmpty())
        return QString();

    QStringList visibleChildren;
    for (int i = 0;
         i < children.size() && i < kVisibleHintCommandLimit;
         ++i) {
        visibleChildren.append(children.at(i));
    }
    if (children.size() > kVisibleHintCommandLimit)
        visibleChildren.append(QStringLiteral("..."));

    return QStringLiteral("next: %1")
        .arg(visibleChildren.join(QStringLiteral(", ")));
}
} // namespace

const QList<ComModeCommandMetadata>& comModeCommandRegistry()
{
    static const QList<ComModeCommandMetadata> registry = {
        {QStringLiteral("cr"),
         true,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("clear"),
         QStringLiteral("Clear assignment RHS"),
         QStringLiteral("Clear assignment RHS and create fill slots.")},
        {QStringLiteral("g<num>"),
         true,
         ComModeCommandInputKind::ModuleRelativeLine,
         QStringLiteral("navigation"),
         QStringLiteral("Go module line"),
         QStringLiteral("Jump to a 1-based line inside the current module.")},
        {QStringLiteral("gm"),
         true,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("navigation"),
         QStringLiteral("Go module"),
         QStringLiteral("Open the module picker.")},
        {QStringLiteral("ga"),
         false,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("assign"),
         QStringLiteral("Assign prefix"),
         QStringLiteral("Prefix for assign-family commands.")},
        {QStringLiteral("gac"),
         true,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("assign"),
         QStringLiteral("Go assign continuous"),
         QStringLiteral("Move to the continuous assign insert point.")},
        {QStringLiteral("ge"),
         false,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("end"),
         QStringLiteral("End prefix"),
         QStringLiteral("Prefix for end-family commands.")},
        {QStringLiteral("gef"),
         true,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("end"),
         QStringLiteral("Go end final"),
         QStringLiteral("Move before the final endmodule.")},
        {QStringLiteral("gp"),
         false,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("package-parameter-port"),
         QStringLiteral("P-family prefix"),
         QStringLiteral("Prefix for package, parameter, port, and related commands.")},
        {QStringLiteral("gi"),
         false,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("instance"),
         QStringLiteral("Instance prefix"),
         QStringLiteral("Prefix for instance-family commands.")},
        {QStringLiteral("gii"),
         true,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("instance"),
         QStringLiteral("Go instance insert"),
         QStringLiteral("Move to the instance insert point.")},
        {QStringLiteral("gpi"),
         true,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("package-parameter-port"),
         QStringLiteral("Go parameter insert"),
         QStringLiteral("Move to the parameter insert point.")},
        {QStringLiteral("gpk"),
         true,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("package-parameter-port"),
         QStringLiteral("Go package"),
         QStringLiteral("Open the package picker.")},
        {QStringLiteral("gpa"),
         true,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("package-parameter-port"),
         QStringLiteral("Go parameter"),
         QStringLiteral("Open the current-scope parameter picker.")},
        {QStringLiteral("gpo"),
         true,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("package-parameter-port"),
         QStringLiteral("Go port append"),
         QStringLiteral("Append a row in a multiline ANSI port list.")},
        {QStringLiteral("gs"),
         false,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("signal"),
         QStringLiteral("Signal prefix"),
         QStringLiteral("Prefix for signal-family commands.")},
        {QStringLiteral("gsd"),
         true,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("signal"),
         QStringLiteral("Go signal declaration"),
         QStringLiteral("Open the current-module signal declaration picker.")},
        {QStringLiteral("gsi"),
         true,
         ComModeCommandInputKind::Fixed,
         QStringLiteral("signal"),
         QStringLiteral("Go signal insert"),
         QStringLiteral("Move to the internal signal declaration insert point.")},
    };
    return registry;
}

const ComModeCommandMetadata* findComModeCommandMetadata(
    const QString& command)
{
    for (const ComModeCommandMetadata& metadata : comModeCommandRegistry()) {
        if (metadata.command == command)
            return &metadata;
    }
    return nullptr;
}

bool validateComModeCommandRegistry(
    const QList<ComModeCommandMetadata>& registry,
    QString* reason)
{
    for (int i = 0; i < registry.size(); ++i) {
        const ComModeCommandMetadata& left = registry.at(i);
        if (left.command.isEmpty()) {
            setReason(reason, QStringLiteral("empty COM command"));
            return false;
        }
        if (!left.executable && left.inputKind != ComModeCommandInputKind::Fixed) {
            setReason(reason,
                      QStringLiteral("COM prefix must be fixed: %1")
                          .arg(commandLabel(left)));
            return false;
        }
        if (!left.executable && !fixedRegistryHasChild(registry, left)) {
            setReason(reason,
                      QStringLiteral("COM prefix has no registered child: %1")
                          .arg(left.command));
            return false;
        }
        for (int j = i + 1; j < registry.size(); ++j) {
            const ComModeCommandMetadata& right = registry.at(j);
            if (left.command == right.command) {
                setReason(reason,
                          QStringLiteral("duplicate COM command: %1")
                              .arg(left.command));
                return false;
            }
            if (isFixedCommand(left)
                && isFixedCommand(right)
                && left.executable
                && right.executable
                && (left.command.startsWith(right.command)
                    || right.command.startsWith(left.command))) {
                setReason(
                    reason,
                    QStringLiteral(
                        "executable COM command prefix conflict: %1 / %2")
                        .arg(left.command, right.command));
                return false;
            }
        }
    }
    if (reason)
        reason->clear();
    return true;
}

bool comModeCommandRegistryIsValid(QString* reason)
{
    return validateComModeCommandRegistry(comModeCommandRegistry(), reason);
}

QString comModeCommandHint(const QString& buffer)
{
    if (buffer.isEmpty() || !comModeCommandRegistryIsValid())
        return QString();

    if (isComModeLineBuffer(buffer)) {
        const ComModeCommandMetadata* metadata = moduleRelativeLineCommand();
        if (!metadata)
            return QStringLiteral("Enter: go module line");
        return QStringLiteral("Enter: %1").arg(metadata->title);
    }

    if (const ComModeCommandMetadata* metadata =
            findComModeCommandMetadata(buffer)) {
        if (!metadata->executable) {
            const QString children = childCommandHint(buffer);
            return children.isEmpty() ? metadata->description : children;
        }
        return metadata->description;
    }

    return childCommandHint(buffer);
}

QString comModeCommandFailureMessage(const QString& buffer)
{
    QString registryError;
    if (!comModeCommandRegistryIsValid(&registryError)) {
        return registryError.isEmpty()
            ? QStringLiteral("COM command registry is invalid")
            : QStringLiteral("COM command registry is invalid: %1")
                  .arg(registryError);
    }

    if (buffer.isEmpty())
        return QStringLiteral("Unknown COM command");

    if (const ComModeCommandMetadata* metadata =
            findComModeCommandMetadata(buffer)) {
        if (!metadata->executable) {
            const QString hint = childCommandHint(buffer);
            if (!hint.isEmpty()) {
                return QStringLiteral("Incomplete COM command: %1 (%2)")
                    .arg(buffer, hint);
            }
            return QStringLiteral("Incomplete COM command: %1")
                .arg(buffer);
        }
    }

    return QStringLiteral("Unknown COM command: %1").arg(buffer);
}

QString executableComModeCommand(const QString& buffer)
{
    if (!comModeCommandRegistryIsValid())
        return QString();
    for (const ComModeCommandMetadata& command : comModeCommandRegistry()) {
        if (command.executable
            && command.inputKind == ComModeCommandInputKind::Fixed
            && command.command == buffer) {
            return command.command;
        }
    }
    return QString();
}

bool isComModeLineBuffer(const QString& buffer)
{
    if (buffer.size() < 2 || !buffer.startsWith(QLatin1Char('g')))
        return false;
    return isUnsignedIntegerText(buffer.mid(1));
}

bool isComModeBufferPrefix(const QString& buffer)
{
    if (buffer.isEmpty())
        return true;
    if (isComModeLineBuffer(buffer))
        return true;
    if (!comModeCommandRegistryIsValid())
        return false;
    for (const ComModeCommandMetadata& command : comModeCommandRegistry()) {
        if (command.inputKind == ComModeCommandInputKind::Fixed
            && command.command.startsWith(buffer)) {
            return true;
        }
    }
    return false;
}
