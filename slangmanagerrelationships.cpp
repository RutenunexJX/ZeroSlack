#include "slangmanager.h"
#include "slangparseoptions.h"
#include "slangrelationshiphelpers.h"

#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/expressions/AssignmentExpressions.h>
#include <slang/ast/expressions/CallExpression.h>
#include <slang/ast/statements/ConditionalStatements.h>
#include <slang/ast/statements/LoopStatements.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace slang::ast;
using namespace slang_relationship::detail;

namespace {

AssignmentInfo assignmentInfoFromExpression(const AssignmentExpression& assignment,
                                            const slang::SourceManager* sm)
{
    AssignmentInfo info;
    info.leftName = assignmentRootName(assignment.left());
    info.leftAccessPath = expressionAccessPath(assignment.left());
    if (info.leftAccessPath.isEmpty())
        info.leftAccessPath = info.leftName;
    info.rightNames = collectValueNames(assignment.right());
    info.rightAccessPaths = collectValueAccessPaths(assignment.right());
    size_t line = sm ? sm->getLineNumber(assignment.sourceRange.start()) : 0;
    info.lineNumber = (line == 0) ? 1 : static_cast<int>(line);
    info.sourceRange = relationshipEvidenceRange(sm, assignment.sourceRange);
    return info;
}
QString rootNameForAccessPathText(const QString& accessPath)
{
    const int dotIndex = accessPath.indexOf(QLatin1Char('.'));
    return dotIndex < 0 ? accessPath : accessPath.left(dotIndex);
}

bool isIgnoredAccessRoot(const QString& root)
{
    static const QSet<QString> ignored = {
        QStringLiteral("assign"),
        QStringLiteral("begin"),
        QStringLiteral("end"),
        QStringLiteral("if"),
        QStringLiteral("else"),
        QStringLiteral("case"),
        QStringLiteral("endcase"),
        QStringLiteral("for"),
        QStringLiteral("while"),
        QStringLiteral("posedge"),
        QStringLiteral("negedge"),
        QStringLiteral("or"),
        QStringLiteral("and")
    };
    return root.isEmpty() || ignored.contains(root);
}

bool isIdentifierStart(QChar ch)
{
    return ch == QLatin1Char('$') || ch == QLatin1Char('_')
        || ch.isLetter();
}

bool isIdentifierPart(QChar ch)
{
    return isIdentifierStart(ch) || ch.isDigit();
}

void skipSpaces(const QString& text, int* pos)
{
    while (pos && *pos < text.size() && text.at(*pos).isSpace())
        ++(*pos);
}

QString readIdentifier(const QString& text, int* pos)
{
    if (!pos || *pos >= text.size())
        return QString();

    if (text.at(*pos) == QLatin1Char('\\')) {
        const int start = *pos;
        ++(*pos);
        while (*pos < text.size() && !text.at(*pos).isSpace())
            ++(*pos);
        return text.mid(start, *pos - start).trimmed();
    }

    if (!isIdentifierStart(text.at(*pos)))
        return QString();

    const int start = *pos;
    ++(*pos);
    while (*pos < text.size() && isIdentifierPart(text.at(*pos)))
        ++(*pos);
    return text.mid(start, *pos - start);
}

void skipSelectSuffixes(const QString& text, int* pos)
{
    if (!pos)
        return;

    bool skipped = true;
    while (skipped) {
        skipped = false;
        skipSpaces(text, pos);
        if (*pos >= text.size() || text.at(*pos) != QLatin1Char('['))
            continue;

        int depth = 0;
        while (*pos < text.size()) {
            const QChar ch = text.at(*pos);
            if (ch == QLatin1Char('['))
                ++depth;
            else if (ch == QLatin1Char(']')) {
                --depth;
                ++(*pos);
                if (depth <= 0)
                    break;
                continue;
            }
            ++(*pos);
        }
        skipped = true;
    }
}

QString readAccessPath(const QString& text, int* pos)
{
    if (!pos)
        return QString();

    skipSpaces(text, pos);
    QString root = readIdentifier(text, pos);
    if (root.isEmpty())
        return QString();

    QString accessPath = root;
    while (*pos < text.size()) {
        skipSelectSuffixes(text, pos);
        skipSpaces(text, pos);
        if (*pos >= text.size() || text.at(*pos) != QLatin1Char('.'))
            break;

        ++(*pos);
        skipSpaces(text, pos);
        QString member = readIdentifier(text, pos);
        if (member.isEmpty())
            break;
        accessPath += QLatin1Char('.');
        accessPath += member;
    }
    return accessPath;
}

QStringList collectAccessPathsFromText(const QString& text)
{
    QStringList paths;
    QSet<QString> seen;
    int pos = 0;
    while (pos < text.size()) {
        if (!isIdentifierStart(text.at(pos)) && text.at(pos) != QLatin1Char('\\')) {
            ++pos;
            continue;
        }
        if (pos > 0
            && (text.at(pos - 1) == QLatin1Char('\'')
                || text.at(pos - 1) == QLatin1Char('.')
                || isIdentifierPart(text.at(pos - 1)))) {
            ++pos;
            continue;
        }

        int end = pos;
        const QString accessPath = readAccessPath(text, &end);
        if (end <= pos) {
            ++pos;
            continue;
        }
        pos = end;

        const QString root = rootNameForAccessPathText(accessPath);
        if (isIgnoredAccessRoot(root) || seen.contains(accessPath))
            continue;

        seen.insert(accessPath);
        paths.append(accessPath);
    }
    return paths;
}

QStringList rootNamesForAccessPaths(const QStringList& accessPaths)
{
    QStringList names;
    QSet<QString> seen;
    for (const QString& accessPath : accessPaths) {
        const QString root = rootNameForAccessPathText(accessPath);
        if (isIgnoredAccessRoot(root) || seen.contains(root))
            continue;
        seen.insert(root);
        names.append(root);
    }
    return names;
}

bool appendAssignmentInfo(QVector<AssignmentInfo>& result,
                          const AssignmentExpression& assignment,
                          const slang::SourceManager* sm)
{
    AssignmentInfo info = assignmentInfoFromExpression(assignment, sm);
    if (info.leftName.isEmpty() || info.rightNames.isEmpty())
        return false;

    result.append(info);
    return true;
}

bool appendAssignmentInfoFromSyntax(QVector<AssignmentInfo>& result,
                                    const slang::syntax::SyntaxNode* syntax,
                                    const SemanticSourceRange& sourceRange)
{
    if (!syntax
        || (syntax->kind != slang::syntax::SyntaxKind::AssignmentExpression
            && syntax->kind
                != slang::syntax::SyntaxKind::NonblockingAssignmentExpression)) {
        return false;
    }

    const auto& binary = syntax->as<slang::syntax::BinaryExpressionSyntax>();
    const QString leftText = QString::fromStdString(binary.left->toString());
    const QString rightText = QString::fromStdString(binary.right->toString());
    const QStringList leftPaths = collectAccessPathsFromText(leftText);
    const QStringList rightPaths = collectAccessPathsFromText(rightText);
    if (leftPaths.isEmpty() || rightPaths.isEmpty())
        return false;

    AssignmentInfo info;
    info.leftAccessPath = leftPaths.first();
    info.leftName = rootNameForAccessPathText(info.leftAccessPath);
    info.rightAccessPaths = rightPaths;
    info.rightNames = rootNamesForAccessPaths(rightPaths);
    info.lineNumber = sourceRange.line <= 0 ? 1 : sourceRange.line;
    info.sourceRange = sourceRange;
    if (info.leftName.isEmpty() || info.rightNames.isEmpty())
        return false;

    result.append(info);
    return true;
}

bool appendContinuousAssignmentInfo(QVector<AssignmentInfo>& result,
                                    const ContinuousAssignSymbol& continuousAssign,
                                    const slang::SourceManager* sm)
{
    const int before = result.size();
    const Expression& expression = continuousAssign.getAssignment();
    if (const auto* assignment = expression.as_if<AssignmentExpression>())
        appendAssignmentInfo(result, *assignment, sm);
    if (result.size() > before)
        return true;

    const auto* syntax = continuousAssign.getSyntax();
    if (!syntax)
        return false;

    return appendAssignmentInfoFromSyntax(
        result,
        syntax,
        relationshipEvidenceRange(sm, syntax->sourceRange()));
}

QString normalizedSourceFileName(const QString& path)
{
    if (path.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

void mergeRelationshipInfo(RelationshipExtractionInfo& target,
                           const RelationshipExtractionInfo& source)
{
    target.moduleInstantiations += source.moduleInstantiations;
    target.subroutineCalls += source.subroutineCalls;
    target.assignments += source.assignments;
    target.conditionReferences += source.conditionReferences;
    target.timingSignals += source.timingSignals;
}

void appendRelationshipInfoForFile(
    QHash<QString, RelationshipExtractionInfo>& grouped,
    const QString& fileName,
    const RelationshipExtractionInfo& source)
{
    const QString key = normalizedSourceFileName(fileName);
    if (key.isEmpty())
        return;
    mergeRelationshipInfo(grouped[key], source);
}

void clearRelationshipInfo(RelationshipExtractionInfo* info)
{
    if (!info)
        return;
    info->moduleInstantiations.clear();
    info->subroutineCalls.clear();
    info->assignments.clear();
    info->conditionReferences.clear();
    info->timingSignals.clear();
}

void appendModuleInstantiation(RelationshipExtractionInfo& result,
                               const slang::SourceManager* sm,
                               const InstanceSymbol& inst)
{
    const SemanticSourceRange sourceRange =
        relationshipEvidenceRange(sm, slang::SourceRange(inst.location, inst.location));
    if (sourceRange.line <= 0)
        return;

    ModuleInstantiationInfo info;
    info.instanceName = QString::fromStdString(std::string(inst.name));
    info.moduleName = QString::fromStdString(std::string(inst.getDefinition().name));
    info.lineNumber = sourceRange.line;
    info.sourceRange = sourceRange;
    result.moduleInstantiations.append(info);
}

void appendSubroutineCall(RelationshipExtractionInfo& result,
                          const slang::SourceManager* sm,
                          const CallExpression& call)
{
    if (call.isSystemCall())
        return;

    SubroutineCallInfo info;
    info.subroutineName = QString::fromStdString(std::string(call.getSubroutineName()));
    size_t line = sm ? sm->getLineNumber(call.sourceRange.start()) : 0;
    info.lineNumber = (line == 0) ? 1 : static_cast<int>(line);
    info.sourceRange = relationshipEvidenceRange(sm, call.sourceRange);
    result.subroutineCalls.append(info);
}

auto makeRelationshipVisitor(RelationshipExtractionInfo& result,
                             const slang::SourceManager* sm,
                             std::function<bool()> isCancelled = nullptr,
                             bool* cancellationReached = nullptr)
{
    auto cancelled = [isCancelled = std::move(isCancelled),
                      cancellationReached]() {
        if (isCancelled && isCancelled()) {
            if (cancellationReached)
                *cancellationReached = true;
            return true;
        }
        return false;
    };
    return makeVisitor(
        [&, cancelled](auto& v, const InstanceSymbol& inst) {
            if (cancelled())
                return;
            if (inst.instanceDepth == 0) {
                inst.body.visit(v);
                return;
            }
            appendModuleInstantiation(result, sm, inst);
            if (cancelled())
                return;
            v.visitDefault(inst);
        },
        [&, cancelled](auto& v, const CallExpression& call) {
            if (cancelled())
                return;
            appendSubroutineCall(result, sm, call);
            if (cancelled())
                return;
            v.visitDefault(call);
        },
        [&, cancelled](auto& v, const AssignmentExpression& assignment) {
            if (cancelled())
                return;
            appendAssignmentInfo(result.assignments, assignment, sm);
            if (cancelled())
                return;
            v.visitDefault(assignment);
        },
        [&, cancelled](auto& v, const ContinuousAssignSymbol& continuousAssign) {
            if (cancelled())
                return;
            if (appendContinuousAssignmentInfo(result.assignments,
                                               continuousAssign,
                                               sm)) {
                return;
            }
            if (cancelled())
                return;
            v.visitDefault(continuousAssign);
        },
        [&, cancelled](auto& v, const ConditionalStatement& stmt) {
            if (cancelled())
                return;
            for (const auto& condition : stmt.conditions)
                appendConditionReference(result.conditionReferences, sm, *condition.expr);
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const CaseStatement& stmt) {
            if (cancelled())
                return;
            appendConditionReference(result.conditionReferences, sm, stmt.expr);
            for (const auto& item : stmt.items) {
                if (cancelled())
                    return;
                for (const Expression* expr : item.expressions)
                    appendConditionReference(result.conditionReferences, sm, *expr);
            }
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const PatternCaseStatement& stmt) {
            if (cancelled())
                return;
            appendConditionReference(result.conditionReferences, sm, stmt.expr);
            for (const auto& item : stmt.items) {
                if (cancelled())
                    return;
                if (item.filter)
                    appendConditionReference(result.conditionReferences, sm, *item.filter);
            }
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const ForLoopStatement& stmt) {
            if (cancelled())
                return;
            if (stmt.stopExpr)
                appendConditionReference(result.conditionReferences, sm, *stmt.stopExpr);
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const RepeatLoopStatement& stmt) {
            if (cancelled())
                return;
            appendConditionReference(result.conditionReferences, sm, stmt.count);
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const ForeachLoopStatement& stmt) {
            if (cancelled())
                return;
            appendConditionReference(result.conditionReferences, sm, stmt.arrayRef);
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const WhileLoopStatement& stmt) {
            if (cancelled())
                return;
            appendConditionReference(result.conditionReferences, sm, stmt.cond);
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const DoWhileLoopStatement& stmt) {
            if (cancelled())
                return;
            appendConditionReference(result.conditionReferences, sm, stmt.cond);
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const SignalEventControl& event) {
            if (cancelled())
                return;
            appendTimingSignal(result.timingSignals, sm, event);
            if (cancelled())
                return;
            v.visitDefault(event);
        });
}

bool collectWorkspaceRelationships(
    const std::shared_ptr<slang::syntax::SyntaxTree>& tree,
    QHash<QString, RelationshipExtractionInfo>* grouped,
    const std::function<bool()>& isCancelled)
{
    if (!tree || !grouped)
        return false;
    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };
    if (cancelled())
        return false;

    slang::Bag compilationOptions =
        slang_parse_options::makeCompilationOptions();
    Compilation compilation(compilationOptions);
    compilation.addSyntaxTree(tree);
    if (cancelled())
        return false;

    RelationshipExtractionInfo allRelationships;
    const RootSymbol& root = compilation.getRoot();
    const slang::SourceManager* sm = compilation.getSourceManager();
    if (!sm || cancelled())
        return false;

    bool cancellationReached = false;
    auto visitor = makeRelationshipVisitor(allRelationships,
                                           sm,
                                           isCancelled,
                                           &cancellationReached);
    root.visit(visitor);
    if (cancellationReached || cancelled()) {
        clearRelationshipInfo(&allRelationships);
        return false;
    }

    // Stage the complete grouping separately so cancellation never publishes
    // a mixture of relationship facts from different files.
    QHash<QString, RelationshipExtractionInfo> staged = *grouped;
    for (const ModuleInstantiationInfo& item
         : std::as_const(allRelationships.moduleInstantiations)) {
        if (cancelled())
            return false;
        RelationshipExtractionInfo one;
        one.moduleInstantiations.append(item);
        appendRelationshipInfoForFile(staged, item.sourceRange.fileName, one);
    }
    for (const SubroutineCallInfo& item
         : std::as_const(allRelationships.subroutineCalls)) {
        if (cancelled())
            return false;
        RelationshipExtractionInfo one;
        one.subroutineCalls.append(item);
        appendRelationshipInfoForFile(staged, item.sourceRange.fileName, one);
    }
    for (const AssignmentInfo& item
         : std::as_const(allRelationships.assignments)) {
        if (cancelled())
            return false;
        RelationshipExtractionInfo one;
        one.assignments.append(item);
        appendRelationshipInfoForFile(staged, item.sourceRange.fileName, one);
    }
    for (const ConditionReferenceInfo& item
         : std::as_const(allRelationships.conditionReferences)) {
        if (cancelled())
            return false;
        RelationshipExtractionInfo one;
        one.conditionReferences.append(item);
        appendRelationshipInfoForFile(staged, item.sourceRange.fileName, one);
    }
    for (const TimingSignalInfo& item
         : std::as_const(allRelationships.timingSignals)) {
        if (cancelled())
            return false;
        RelationshipExtractionInfo one;
        one.timingSignals.append(item);
        appendRelationshipInfoForFile(staged, item.sourceRange.fileName, one);
    }
    if (cancelled())
        return false;

    *grouped = std::move(staged);
    return true;
}

QString relationshipSourceLookupKey(const QString& path)
{
    QString key = normalizedSourceFileName(path);
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

} // namespace

QHash<QString, RelationshipExtractionInfo> SlangManager::extractWorkspaceRelationshipInfo(
    const QStringList& filePaths,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines,
    std::function<bool()> isCancelled)
{
    QHash<QString, RelationshipExtractionInfo> grouped;
    for (const QString& filePath : filePaths)
        grouped.insert(normalizedSourceFileName(filePath), RelationshipExtractionInfo{});
    if (filePaths.isEmpty())
        return grouped;
    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };
    if (cancelled())
        return grouped;

    {
        std::vector<std::string> pathStrs;
        pathStrs.reserve(filePaths.size());
        for (const QString& path : filePaths) {
            if (cancelled())
                return grouped;
            pathStrs.push_back(path.toStdString());
        }

        std::vector<std::string_view> pathViews;
        pathViews.reserve(pathStrs.size());
        for (const std::string& path : pathStrs) {
            if (cancelled())
                return grouped;
            pathViews.push_back(path);
        }

        const QStringList effectiveIncludeDirs =
            slang_parse_options::effectiveIncludeDirsForFiles(filePaths, includeDirs);
        if (cancelled())
            return grouped;

        slang::SourceManager sourceManager;
        slang::syntax::SyntaxTree::TreeOrError treeOrErr =
            effectiveIncludeDirs.isEmpty() && defines.isEmpty()
                ? slang::syntax::SyntaxTree::fromFiles(pathViews, sourceManager)
                : slang::syntax::SyntaxTree::fromFiles(
                    pathViews,
                    sourceManager,
                    slang_parse_options::makeSyntaxOptions(effectiveIncludeDirs, defines));
        if (cancelled())
            return grouped;
        if (!treeOrErr)
            return grouped;

        std::shared_ptr<slang::syntax::SyntaxTree> tree = std::move(*treeOrErr);
        if (!tree)
            return grouped;
        collectWorkspaceRelationships(tree, &grouped, isCancelled);
    }
    return grouped;
}

QHash<QString, RelationshipExtractionInfo>
SlangManager::extractOverlayWorkspaceRelationshipInfo(
    const QHash<QString, QString>& fileContents,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines,
    std::function<bool()> isCancelled,
    const QStringList& orderedFilePaths)
{
    QHash<QString, QString> contentsByKey;
    QHash<QString, QString> pathsByKey;
    for (auto it = fileContents.constBegin();
         it != fileContents.constEnd();
         ++it) {
        const QString path = normalizedSourceFileName(it.key());
        const QString key = relationshipSourceLookupKey(path);
        if (key.isEmpty())
            continue;
        pathsByKey.insert(key, path);
        contentsByKey.insert(key, it.value());
    }

    QStringList fileNames;
    QSet<QString> added;
    for (const QString& requestedPath : orderedFilePaths) {
        const QString key = relationshipSourceLookupKey(requestedPath);
        if (key.isEmpty() || added.contains(key)
            || !contentsByKey.contains(key)) {
            continue;
        }
        added.insert(key);
        fileNames.append(normalizedSourceFileName(requestedPath));
    }
    QStringList remainingKeys = contentsByKey.keys();
    remainingKeys.sort(Qt::CaseInsensitive);
    for (const QString& key : std::as_const(remainingKeys)) {
        if (added.contains(key))
            continue;
        added.insert(key);
        fileNames.append(pathsByKey.value(key));
    }

    QHash<QString, RelationshipExtractionInfo> grouped;
    for (const QString& fileName : std::as_const(fileNames))
        grouped.insert(normalizedSourceFileName(fileName),
                       RelationshipExtractionInfo{});
    if (fileNames.isEmpty())
        return grouped;

    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };
    if (cancelled())
        return grouped;

    const QStringList effectiveIncludeDirs =
        slang_parse_options::effectiveIncludeDirsForFiles(fileNames,
                                                          includeDirs);
    const slang::Bag syntaxOptions =
        slang_parse_options::makeSyntaxOptions(effectiveIncludeDirs,
                                               defines);
    slang::SourceManager sourceManager;
    sourceManager.setDisableProximatePaths(true);
    std::vector<std::string> pathStrings;
    std::vector<slang::SourceBuffer> sourceBuffers;
    pathStrings.reserve(static_cast<std::size_t>(fileNames.size()));
    sourceBuffers.reserve(static_cast<std::size_t>(fileNames.size()));
    for (const QString& fileName : std::as_const(fileNames)) {
        if (cancelled())
            return grouped;
        const QString key = relationshipSourceLookupKey(fileName);
        const QByteArray sourceBytes = contentsByKey.value(key).toUtf8();
        pathStrings.push_back(fileName.toUtf8().toStdString());
        slang::SourceBuffer sourceBuffer = sourceManager.assignText(
            std::string_view(pathStrings.back()),
            std::string_view(sourceBytes.constData(),
                             static_cast<std::size_t>(sourceBytes.size())));
        // Bind facts back to the exact absolute workspace identity instead of
        // the temporary in-memory buffer's basename.
        sourceManager.addLineDirective(
            slang::SourceLocation(sourceBuffer.id, 0),
            2,
            std::string_view(pathStrings.back()),
            0);
        sourceBuffers.push_back(sourceBuffer);
    }
    if (sourceBuffers.empty() || cancelled())
        return grouped;

    std::shared_ptr<slang::syntax::SyntaxTree> tree =
        slang::syntax::SyntaxTree::fromBuffers(sourceBuffers,
                                               sourceManager,
                                               syntaxOptions);
    if (!tree || cancelled())
        return grouped;

    collectWorkspaceRelationships(tree, &grouped, isCancelled);
    return grouped;
}
