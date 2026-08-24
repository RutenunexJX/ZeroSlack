#include "pinloomcodelinkstore.h"

#include "editorfileidentity.h"
#include "semanticindex.h"
#include "tsdocument.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <iterator>
#include <limits>

namespace {
constexpr int kContextLength = 96;
const QString kSchema = QStringLiteral("ZeroSlack.PinloomCodeLinks");

QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
}

QString textHash(const QString& text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256)
            .toHex());
}

QString normalizedStructure(QString text)
{
    static const QRegularExpression blockComment(
        QStringLiteral("/\\*.*?\\*/"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression lineComment(
        QStringLiteral("//[^\\r\\n]*"));
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    text.remove(blockComment);
    text.remove(lineComment);
    text.remove(spaces);
    return text;
}

QStringList collectSemanticTokens(const QString& text)
{
    static const QRegularExpression identifier(
        QStringLiteral("[A-Za-z_][A-Za-z0-9_$]*"));
    static const QSet<QString> ignored{
        QStringLiteral("always"), QStringLiteral("always_comb"),
        QStringLiteral("always_ff"), QStringLiteral("always_latch"),
        QStringLiteral("assign"), QStringLiteral("begin"),
        QStringLiteral("end"), QStringLiteral("if"),
        QStringLiteral("else"), QStringLiteral("case"),
        QStringLiteral("endcase"), QStringLiteral("posedge"),
        QStringLiteral("negedge"), QStringLiteral("or"),
        QStringLiteral("logic"), QStringLiteral("wire"),
        QStringLiteral("reg")};
    QSet<QString> unique;
    auto matches = identifier.globalMatch(text);
    while (matches.hasNext()) {
        const QString token = matches.next().captured().toLower();
        if (!ignored.contains(token))
            unique.insert(token);
    }
    QStringList result = unique.values();
    std::sort(result.begin(), result.end());
    return result;
}

double tokenSimilarity(const QStringList& left, const QStringList& right)
{
    const QSet<QString> a(left.cbegin(), left.cend());
    const QSet<QString> b(right.cbegin(), right.cend());
    if (a.isEmpty() && b.isEmpty())
        return 1.0;
    QSet<QString> intersection = a;
    intersection.intersect(b);
    QSet<QString> unionSet = a;
    unionSet.unite(b);
    return unionSet.isEmpty()
        ? 0.0
        : static_cast<double>(intersection.size()) / unionSet.size();
}

QPair<int, int> lineColumnAt(const QString& text, int position)
{
    const int bounded = qBound(0, position, text.size());
    int line = 1;
    int lineStart = 0;
    for (int i = 0; i < bounded; ++i) {
        if (text.at(i) == QLatin1Char('\n')) {
            ++line;
            lineStart = i + 1;
        }
    }
    return {line, bounded - lineStart + 1};
}

int positionAtLineColumn(const QString& text, int line, int column)
{
    if (line < 1 || column < 1)
        return -1;
    int currentLine = 1;
    int position = 0;
    while (currentLine < line && position < text.size()) {
        const int newline = text.indexOf(QLatin1Char('\n'), position);
        if (newline < 0)
            return -1;
        position = newline + 1;
        ++currentLine;
    }
    const int lineEnd = text.indexOf(QLatin1Char('\n'), position);
    const int maximum = lineEnd < 0 ? text.size() : lineEnd;
    const int result = position + column - 1;
    return result <= maximum ? result : -1;
}

int matchingPrefixLength(const QString& expected, const QString& actual)
{
    const int maximum = qMin(expected.size(), actual.size());
    int count = 0;
    while (count < maximum && expected.at(count) == actual.at(count))
        ++count;
    return count;
}

int matchingSuffixLength(const QString& expected, const QString& actual)
{
    const int maximum = qMin(expected.size(), actual.size());
    int count = 0;
    while (count < maximum
           && expected.at(expected.size() - 1 - count)
                  == actual.at(actual.size() - 1 - count)) {
        ++count;
    }
    return count;
}

int sourceContextScore(const PinloomSourceSelection& expected,
                       const PinloomSourceSelection& candidate)
{
    const int matchingCharacters =
        matchingSuffixLength(expected.prefixContext,
                             candidate.prefixContext)
        + matchingPrefixLength(expected.suffixContext,
                               candidate.suffixContext);
    return qMin(90, matchingCharacters / 2);
}

QString anchorKindText(PinloomCodeAnchorKind kind)
{
    switch (kind) {
    case PinloomCodeAnchorKind::Symbol:
        return QStringLiteral("symbol");
    case PinloomCodeAnchorKind::AlwaysBlock:
        return QStringLiteral("always");
    case PinloomCodeAnchorKind::ContinuousAssign:
        return QStringLiteral("assign");
    case PinloomCodeAnchorKind::LegacySelection:
        return QStringLiteral("legacy-selection");
    }
    return QStringLiteral("legacy-selection");
}

PinloomCodeAnchorKind anchorKindFromText(const QString& text)
{
    if (text == QStringLiteral("symbol"))
        return PinloomCodeAnchorKind::Symbol;
    if (text == QStringLiteral("always"))
        return PinloomCodeAnchorKind::AlwaysBlock;
    if (text == QStringLiteral("assign"))
        return PinloomCodeAnchorKind::ContinuousAssign;
    return PinloomCodeAnchorKind::LegacySelection;
}

QString symbolLogicalKey(const SemanticSymbolRecord& symbol)
{
    return QStringLiteral("symbol|%1|%2|%3|%4|%5|%6")
        .arg(static_cast<int>(symbol.declarationKind))
        .arg(static_cast<int>(symbol.collectorKind))
        .arg(static_cast<int>(symbol.owner.kind))
        .arg(symbol.owner.name.trimmed())
        .arg(symbol.name.trimmed())
        .arg(static_cast<int>(symbol.visibility));
}

QString symbolFingerprint(const SemanticSymbolRecord& symbol)
{
    QString declaration = symbol.presentation.declarationText;
    if (declaration.isEmpty())
        declaration = symbol.type.rawTypeText;
    if (!symbol.name.isEmpty())
        declaration.replace(symbol.name, QStringLiteral("$symbol"));
    return textHash(QStringLiteral("%1|%2|%3|%4")
                        .arg(static_cast<int>(symbol.declarationKind))
                        .arg(static_cast<int>(symbol.collectorKind))
                        .arg(static_cast<int>(symbol.owner.kind))
                        .arg(normalizedStructure(declaration)));
}

QString syntaxLogicalKey(const QString& moduleName,
                         const QString& syntaxKind,
                         const QString& identityText,
                         PinloomCodeAnchorKind kind)
{
    return QStringLiteral("syntax|%1|%2|%3|%4")
        .arg(anchorKindText(kind),
             moduleName.trimmed(),
             syntaxKind.trimmed(),
             normalizedStructure(identityText));
}

bool sourceMatchesFile(const PinloomSourceSelection& source,
                       const QString& workspaceRoot,
                       const QString& filePath)
{
    const QString normalizedFile = normalizedPath(filePath);
    if (!workspaceRoot.isEmpty()) {
        const QString relative = normalizedPath(
            QDir(workspaceRoot).relativeFilePath(normalizedFile));
        if (!relative.startsWith(QStringLiteral("../"))
            && relative != QStringLiteral("..")) {
            return relative.compare(source.relativeFilePath,
                                    Qt::CaseInsensitive) == 0;
        }
    }
    return EditorFileIdentity::same(normalizedFile, source.absoluteFilePath);
}

void setResolvedRange(ResolvedPinloomCodeLink* result,
                      const QString& text,
                      int start,
                      int end,
                      PinloomCodeLinkResolution resolution)
{
    if (!result || start < 0 || end <= start || end > text.size())
        return;
    result->resolution = resolution;
    result->startPosition = start;
    result->endPosition = end;
    result->firstLine = lineColumnAt(text, start).first - 1;
    result->lastLine = lineColumnAt(text, end).first - 1;
}

ResolvedPinloomCodeLink baseResolution(
    const PinloomCodeLinkAnchorRecord& anchor)
{
    ResolvedPinloomCodeLink result;
    result.anchor = anchor;
    if (!anchor.links.isEmpty()) {
        result.record = anchor.links.constFirst();
        result.record.source = anchor.source;
    }
    return result;
}

ResolvedPinloomCodeLink resolveLegacy(
    const PinloomCodeLinkAnchorRecord& anchor,
    const QString& text)
{
    ResolvedPinloomCodeLink result = baseResolution(anchor);
    const QString& needle = anchor.source.selectedText;
    if (needle.isEmpty())
        return result;

    const int storedStart = anchor.source.startPosition >= 0
            && anchor.source.startPosition < text.size()
        ? anchor.source.startPosition
        : -1;
    if (storedStart >= 0 && text.mid(storedStart, needle.size()) == needle) {
        setResolvedRange(&result, text, storedStart,
                         storedStart + needle.size(),
                         PinloomCodeLinkResolution::Exact);
        return result;
    }

    struct Candidate {
        int position = -1;
        int contextScore = 0;
        int distance = 0;
    };
    QList<Candidate> candidates;
    int from = 0;
    while (from <= text.size()) {
        const int position = text.indexOf(needle, from);
        if (position < 0)
            break;
        const QString prefix = text.mid(
            qMax(0, position - kContextLength),
            qMin(kContextLength, position));
        const int suffixStart = position + needle.size();
        Candidate candidate;
        candidate.position = position;
        candidate.contextScore =
            matchingSuffixLength(anchor.source.prefixContext, prefix)
            + matchingPrefixLength(
                anchor.source.suffixContext,
                text.mid(suffixStart, kContextLength));
        candidate.distance = storedStart < 0
            ? std::numeric_limits<int>::max()
            : qAbs(position - storedStart);
        candidates.append(candidate);
        from = position + qMax(1, needle.size());
    }
    if (candidates.isEmpty())
        return result;
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& left, const Candidate& right) {
                  if (left.contextScore != right.contextScore)
                      return left.contextScore > right.contextScore;
                  return left.distance < right.distance;
              });
    if (candidates.size() > 1
        && candidates.at(0).contextScore == candidates.at(1).contextScore
        && candidates.at(0).distance == candidates.at(1).distance) {
        result.resolution = PinloomCodeLinkResolution::Ambiguous;
        return result;
    }
    setResolvedRange(&result, text, candidates.first().position,
                     candidates.first().position + needle.size(),
                     PinloomCodeLinkResolution::Moved);
    return result;
}

ResolvedPinloomCodeLink resolveSyntax(
    const PinloomCodeLinkAnchorRecord& anchor,
    const QString& root,
    const QString& filePath,
    const QString& text,
    const QList<TSBindableCodeAnchor>& syntaxAnchors)
{
    ResolvedPinloomCodeLink result = baseResolution(anchor);
    const int storedStart = anchor.source.startPosition >= 0
            && anchor.source.startPosition < text.size()
        ? anchor.source.startPosition
        : -1;
    if (storedStart >= 0
        && text.mid(storedStart, anchor.source.selectedText.size())
               == anchor.source.selectedText) {
        setResolvedRange(&result, text, storedStart,
                         storedStart + anchor.source.selectedText.size(),
                         PinloomCodeLinkResolution::Exact);
        return result;
    }

    struct Candidate {
        TSBindableCodeAnchor syntax;
        PinloomSourceSelection source;
        int score = 0;
        int distance = 0;
    };
    QList<Candidate> candidates;
    for (const TSBindableCodeAnchor& syntax : syntaxAnchors) {
        const PinloomCodeAnchorKind expected =
            syntax.kind == TSBindableCodeAnchorKind::AlwaysBlock
            ? PinloomCodeAnchorKind::AlwaysBlock
            : PinloomCodeAnchorKind::ContinuousAssign;
        if (expected != anchor.source.anchorKind)
            continue;
        Candidate candidate;
        candidate.syntax = syntax;
        candidate.source = PinloomSourceSelection::fromSyntaxAnchor(
            root, filePath, text, syntax);
        if (candidate.source.logicalKey == anchor.source.logicalKey)
            candidate.score += 140;
        if (candidate.source.structuralFingerprint
            == anchor.source.structuralFingerprint) {
            candidate.score += 110;
        }
        if (candidate.source.moduleName == anchor.source.moduleName)
            candidate.score += 20;
        if (candidate.source.syntaxKind == anchor.source.syntaxKind)
            candidate.score += 20;
        candidate.score += sourceContextScore(anchor.source,
                                              candidate.source);
        candidate.score += qRound(
            tokenSimilarity(candidate.source.semanticTokens,
                            anchor.source.semanticTokens) * 80.0);
        candidate.distance = anchor.source.startPosition < 0
            ? std::numeric_limits<int>::max()
            : qAbs(syntax.startChar - anchor.source.startPosition);
        candidate.score += qMax(0, 20 - candidate.distance / 200);
        candidates.append(candidate);
    }
    if (candidates.isEmpty())
        return result;
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& left, const Candidate& right) {
                  if (left.score != right.score)
                      return left.score > right.score;
                  return left.distance < right.distance;
              });
    const Candidate& best = candidates.constFirst();
    if (best.score < 75)
        return result;
    if (candidates.size() > 1
        && best.score - candidates.at(1).score < 15) {
        result.resolution = PinloomCodeLinkResolution::Ambiguous;
        return result;
    }
    setResolvedRange(&result, text,
                     best.syntax.startChar, best.syntax.endChar,
                     PinloomCodeLinkResolution::Moved);
    return result;
}

ResolvedPinloomCodeLink resolveSymbol(
    const PinloomCodeLinkAnchorRecord& anchor,
    const QString& root,
    const QString& filePath,
    const QString& text,
    const QList<SemanticSymbolRecord>& symbols)
{
    ResolvedPinloomCodeLink result = baseResolution(anchor);
    struct Candidate {
        PinloomSourceSelection source;
        int score = 0;
        int distance = 0;
    };
    QList<Candidate> candidates;
    for (const SemanticSymbolRecord& symbol : symbols) {
        if (!symbol.isValid()
            || !EditorFileIdentity::same(symbol.location.fileName,
                                         filePath)) {
            continue;
        }
        Candidate candidate;
        candidate.source = PinloomSourceSelection::fromSemanticSymbol(
            root, text, symbol);
        if (!candidate.source.isValid())
            continue;
        if (candidate.source.logicalKey == anchor.source.logicalKey)
            candidate.score += 150;
        if (candidate.source.structuralFingerprint
            == anchor.source.structuralFingerprint) {
            candidate.score += 115;
        }
        if (candidate.source.ownerScope == anchor.source.ownerScope)
            candidate.score += 25;
        if (candidate.source.syntaxKind == anchor.source.syntaxKind)
            candidate.score += 20;
        candidate.score += sourceContextScore(anchor.source,
                                              candidate.source);
        candidate.score += qRound(
            tokenSimilarity(candidate.source.semanticTokens,
                            anchor.source.semanticTokens) * 70.0);
        candidate.distance = anchor.source.startPosition < 0
            ? std::numeric_limits<int>::max()
            : qAbs(candidate.source.startPosition
                   - anchor.source.startPosition);
        candidate.score += qMax(0, 20 - candidate.distance / 200);
        candidates.append(candidate);
    }
    if (candidates.isEmpty())
        return resolveLegacy(anchor, text);
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& left, const Candidate& right) {
                  if (left.score != right.score)
                      return left.score > right.score;
                  return left.distance < right.distance;
              });
    const Candidate& best = candidates.constFirst();
    if (best.score < 90)
        return result;
    if (candidates.size() > 1
        && best.score - candidates.at(1).score < 15) {
        result.resolution = PinloomCodeLinkResolution::Ambiguous;
        return result;
    }
    const bool exact = best.source.startPosition
            == anchor.source.startPosition
        && best.source.selectedText == anchor.source.selectedText;
    setResolvedRange(&result, text,
                     best.source.startPosition,
                     best.source.endPosition,
                     exact ? PinloomCodeLinkResolution::Exact
                           : PinloomCodeLinkResolution::Moved);
    return result;
}

QJsonObject targetToJson(const PinloomCodeLinkRecord& record)
{
    return {
        {QStringLiteral("id"), record.id},
        {QStringLiteral("title"), record.title},
        {QStringLiteral("uri"), record.uri.toString(QUrl::FullyEncoded)},
        {QStringLiteral("identity"),
         QJsonObject::fromVariantMap(record.identity)},
        {QStringLiteral("createdAtUtc"), record.createdAtUtc},
    };
}

PinloomCodeLinkRecord targetFromJson(const QJsonObject& object)
{
    PinloomCodeLinkRecord record;
    record.id = object.value(QStringLiteral("id")).toString().trimmed();
    record.title = object.value(QStringLiteral("title")).toString();
    record.uri = QUrl(object.value(QStringLiteral("uri")).toString());
    record.identity = object.value(QStringLiteral("identity"))
                          .toObject().toVariantMap();
    record.createdAtUtc =
        object.value(QStringLiteral("createdAtUtc")).toString();
    return record;
}

QJsonObject anchorToJson(const PinloomCodeLinkAnchorRecord& anchor)
{
    QJsonArray links;
    for (const PinloomCodeLinkRecord& link : anchor.links)
        links.append(targetToJson(link));
    return {
        {QStringLiteral("id"), anchor.id},
        {QStringLiteral("source"),
         QJsonObject::fromVariantMap(anchor.source.toVariantMap())},
        {QStringLiteral("links"), links},
        {QStringLiteral("createdAtUtc"), anchor.createdAtUtc},
    };
}

PinloomCodeLinkAnchorRecord anchorFromJson(const QJsonObject& object)
{
    PinloomCodeLinkAnchorRecord anchor;
    anchor.id = object.value(QStringLiteral("id")).toString().trimmed();
    anchor.source = PinloomSourceSelection::fromVariantMap(
        object.value(QStringLiteral("source")).toObject().toVariantMap());
    anchor.source.anchorId = anchor.id;
    anchor.createdAtUtc =
        object.value(QStringLiteral("createdAtUtc")).toString();
    for (const QJsonValue& value :
         object.value(QStringLiteral("links")).toArray()) {
        if (!value.isObject())
            continue;
        PinloomCodeLinkRecord target = targetFromJson(value.toObject());
        target.source = anchor.source;
        if (target.isValid())
            anchor.links.append(target);
    }
    return anchor;
}
}

bool PinloomSourceSelection::isValid() const
{
    const QString relative = normalizedPath(relativeFilePath);
    const bool structuralIdentity =
        anchorKind == PinloomCodeAnchorKind::LegacySelection
        || !logicalKey.trimmed().isEmpty()
        || !structuralFingerprint.trimmed().isEmpty();
    return structuralIdentity
        && !workspaceRoot.trimmed().isEmpty()
        && !relative.isEmpty()
        && relative != QStringLiteral("..")
        && !relative.startsWith(QStringLiteral("../"))
        && !QDir::isAbsolutePath(relative)
        && !absoluteFilePath.trimmed().isEmpty()
        && !selectedText.isEmpty()
        && startPosition >= 0
        && endPosition > startPosition
        && startLine > 0
        && startColumn > 0
        && endLine >= startLine
        && endColumn > 0;
}

QString PinloomSourceSelection::suggestedTitle() const
{
    if (anchorKind == PinloomCodeAnchorKind::Symbol
        && !symbolName.trimmed().isEmpty()) {
        return symbolName.trimmed();
    }
    const QStringList rows = selectedText.split(QLatin1Char('\n'));
    for (const QString& row : rows) {
        const QString compact = row.simplified();
        if (!compact.isEmpty())
            return compact.left(72);
    }
    return QFileInfo(absoluteFilePath).fileName()
        + QStringLiteral(":%1").arg(startLine);
}

QVariantMap PinloomSourceSelection::toVariantMap() const
{
    return {
        {QStringLiteral("anchorId"), anchorId},
        {QStringLiteral("anchorKind"), anchorKindText(anchorKind)},
        {QStringLiteral("workspaceRoot"), workspaceRoot},
        {QStringLiteral("relativeFilePath"), relativeFilePath},
        {QStringLiteral("absoluteFilePath"), absoluteFilePath},
        {QStringLiteral("moduleName"), moduleName},
        {QStringLiteral("selectedText"), selectedText},
        {QStringLiteral("selectedTextHash"), selectedTextHash},
        {QStringLiteral("prefixContext"), prefixContext},
        {QStringLiteral("suffixContext"), suffixContext},
        {QStringLiteral("logicalKey"), logicalKey},
        {QStringLiteral("structuralFingerprint"), structuralFingerprint},
        {QStringLiteral("syntaxKind"), syntaxKind},
        {QStringLiteral("ownerScope"), ownerScope},
        {QStringLiteral("symbolName"), symbolName},
        {QStringLiteral("semanticTokens"), semanticTokens},
        {QStringLiteral("startPosition"), startPosition},
        {QStringLiteral("endPosition"), endPosition},
        {QStringLiteral("startLine"), startLine},
        {QStringLiteral("startColumn"), startColumn},
        {QStringLiteral("endLine"), endLine},
        {QStringLiteral("endColumn"), endColumn},
    };
}

PinloomSourceSelection PinloomSourceSelection::fromVariantMap(
    const QVariantMap& map)
{
    PinloomSourceSelection source;
    source.anchorId = map.value(QStringLiteral("anchorId")).toString();
    source.anchorKind = anchorKindFromText(
        map.value(QStringLiteral("anchorKind")).toString());
    source.workspaceRoot = map.value(QStringLiteral("workspaceRoot")).toString();
    source.relativeFilePath =
        map.value(QStringLiteral("relativeFilePath")).toString();
    source.absoluteFilePath =
        map.value(QStringLiteral("absoluteFilePath")).toString();
    source.moduleName = map.value(QStringLiteral("moduleName")).toString();
    source.selectedText = map.value(QStringLiteral("selectedText")).toString();
    source.selectedTextHash =
        map.value(QStringLiteral("selectedTextHash")).toString();
    source.prefixContext =
        map.value(QStringLiteral("prefixContext")).toString();
    source.suffixContext =
        map.value(QStringLiteral("suffixContext")).toString();
    source.logicalKey = map.value(QStringLiteral("logicalKey")).toString();
    source.structuralFingerprint =
        map.value(QStringLiteral("structuralFingerprint")).toString();
    source.syntaxKind = map.value(QStringLiteral("syntaxKind")).toString();
    source.ownerScope = map.value(QStringLiteral("ownerScope")).toString();
    source.symbolName = map.value(QStringLiteral("symbolName")).toString();
    source.semanticTokens =
        map.value(QStringLiteral("semanticTokens")).toStringList();
    source.startPosition = map.value(QStringLiteral("startPosition"), -1).toInt();
    source.endPosition = map.value(QStringLiteral("endPosition"), -1).toInt();
    source.startLine = map.value(QStringLiteral("startLine")).toInt();
    source.startColumn = map.value(QStringLiteral("startColumn")).toInt();
    source.endLine = map.value(QStringLiteral("endLine")).toInt();
    source.endColumn = map.value(QStringLiteral("endColumn")).toInt();
    return source;
}

PinloomSourceSelection PinloomSourceSelection::fromDocumentSelection(
    const QString& workspaceRootValue,
    const QString& filePath,
    const QString& moduleNameValue,
    const QString& documentText,
    int selectionStart,
    int selectionEnd)
{
    PinloomSourceSelection source;
    source.workspaceRoot = normalizedPath(workspaceRootValue);
    source.absoluteFilePath = normalizedPath(filePath);
    source.moduleName = moduleNameValue.trimmed();
    source.startPosition = qBound(0, selectionStart, documentText.size());
    source.endPosition = qBound(
        source.startPosition, selectionEnd, documentText.size());
    if (!source.workspaceRoot.isEmpty()) {
        source.relativeFilePath = normalizedPath(
            QDir(source.workspaceRoot).relativeFilePath(
                source.absoluteFilePath));
    }
    source.selectedText = documentText.mid(
        source.startPosition,
        source.endPosition - source.startPosition);
    source.selectedTextHash = textHash(source.selectedText);
    source.prefixContext = documentText.mid(
        qMax(0, source.startPosition - kContextLength),
        qMin(kContextLength, source.startPosition));
    source.suffixContext = documentText.mid(
        source.endPosition, kContextLength);
    const QPair<int, int> start =
        lineColumnAt(documentText, source.startPosition);
    const QPair<int, int> end =
        lineColumnAt(documentText, source.endPosition);
    source.startLine = start.first;
    source.startColumn = start.second;
    source.endLine = end.first;
    source.endColumn = end.second;
    return source;
}

PinloomSourceSelection PinloomSourceSelection::fromSemanticSymbol(
    const QString& workspaceRootValue,
    const QString& documentText,
    const SemanticSymbolRecord& symbol)
{
    int start = symbol.location.position;
    if (start < 0 || start >= documentText.size()) {
        start = positionAtLineColumn(documentText,
                                     symbol.location.startLine,
                                     symbol.location.startColumn);
    }
    int length = symbol.location.length;
    if (length <= 0)
        length = symbol.name.size();
    PinloomSourceSelection source = fromDocumentSelection(
        workspaceRootValue,
        symbol.location.fileName,
        symbol.owner.name,
        documentText,
        start,
        qMin(documentText.size(), start + length));
    source.anchorKind = PinloomCodeAnchorKind::Symbol;
    source.logicalKey = symbolLogicalKey(symbol);
    source.structuralFingerprint = symbolFingerprint(symbol);
    source.syntaxKind = QString::number(
        static_cast<int>(symbol.declarationKind));
    source.ownerScope = QStringLiteral("%1:%2")
        .arg(static_cast<int>(symbol.owner.kind))
        .arg(symbol.owner.name);
    source.symbolName = symbol.name;
    source.semanticTokens = collectSemanticTokens(
        symbol.presentation.declarationText
        + QLatin1Char(' ') + symbol.type.rawTypeText);
    return source;
}

PinloomSourceSelection PinloomSourceSelection::fromSyntaxAnchor(
    const QString& workspaceRootValue,
    const QString& filePath,
    const QString& documentText,
    const TSBindableCodeAnchor& anchor)
{
    PinloomSourceSelection source = fromDocumentSelection(
        workspaceRootValue,
        filePath,
        anchor.moduleName,
        documentText,
        anchor.startChar,
        anchor.endChar);
    source.anchorKind =
        anchor.kind == TSBindableCodeAnchorKind::AlwaysBlock
        ? PinloomCodeAnchorKind::AlwaysBlock
        : PinloomCodeAnchorKind::ContinuousAssign;
    source.syntaxKind = anchor.syntaxKind;
    source.logicalKey = syntaxLogicalKey(
        anchor.moduleName, anchor.syntaxKind,
        anchor.identityText, source.anchorKind);
    source.structuralFingerprint = textHash(
        normalizedStructure(source.selectedText));
    source.semanticTokens = collectSemanticTokens(source.selectedText);
    return source;
}

bool PinloomCodeLinkRecord::isValid() const
{
    return !id.trimmed().isEmpty()
        && uri.isValid()
        && uri.scheme() == QStringLiteral("pinloom")
        && source.isValid();
}

bool PinloomCodeLinkAnchorRecord::isValid() const
{
    if (id.trimmed().isEmpty() || !source.isValid() || links.isEmpty())
        return false;
    for (const PinloomCodeLinkRecord& link : links) {
        if (!link.isValid())
            return false;
    }
    return true;
}

void PinloomCodeLinkStore::setWorkspaceRoot(
    const QString& workspaceRootValue)
{
    const QString normalized = normalizedPath(workspaceRootValue);
    if (root == normalized)
        return;
    root = normalized;
    anchorRecords.clear();
    load();
    if (changedHandler)
        changedHandler();
}

QString PinloomCodeLinkStore::workspaceRoot() const
{
    return root;
}

QString PinloomCodeLinkStore::storagePath() const
{
    return root.isEmpty()
        ? QString()
        : QDir(root).filePath(
              QStringLiteral(".zeroslack/pinloom-links.json"));
}

bool PinloomCodeLinkStore::addLink(
    const PinloomSourceSelection& sourceValue,
    const QUrl& uri,
    const QString& title,
    const QVariantMap& identity,
    QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (!sourceValue.isValid()
        || sourceValue.anchorKind
               == PinloomCodeAnchorKind::LegacySelection
        || !uri.isValid()
        || uri.scheme() != QStringLiteral("pinloom")) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The semantic source anchor or Pinloom link is invalid.");
        }
        return false;
    }
    if (root.isEmpty()
        || normalizedPath(sourceValue.workspaceRoot)
               .compare(root, Qt::CaseInsensitive) != 0) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The source anchor is outside the active workspace.");
        }
        return false;
    }

    auto anchorIt = anchorRecords.end();
    if (!sourceValue.anchorId.trimmed().isEmpty()) {
        anchorIt = std::find_if(
            anchorRecords.begin(), anchorRecords.end(),
            [&sourceValue](const PinloomCodeLinkAnchorRecord& anchor) {
                return anchor.id == sourceValue.anchorId;
            });
        if (anchorIt != anchorRecords.end()
            && (anchorIt->source.relativeFilePath.compare(
                    sourceValue.relativeFilePath,
                    Qt::CaseInsensitive) != 0
                || anchorIt->source.anchorKind
                       != sourceValue.anchorKind)) {
            if (failureReason) {
                *failureReason = QStringLiteral(
                    "The source anchor identity does not match the selected code unit.");
            }
            return false;
        }
    }
    if (anchorIt == anchorRecords.end()) {
        anchorIt = std::find_if(
            anchorRecords.begin(), anchorRecords.end(),
            [&sourceValue](const PinloomCodeLinkAnchorRecord& anchor) {
                const bool sameFile =
                    anchor.source.relativeFilePath.compare(
                        sourceValue.relativeFilePath,
                        Qt::CaseInsensitive) == 0;
                if (!sameFile
                    || anchor.source.anchorKind != sourceValue.anchorKind) {
                    return false;
                }
                if (!sourceValue.logicalKey.isEmpty()) {
                    return anchor.source.logicalKey
                               == sourceValue.logicalKey
                        && anchor.source.structuralFingerprint
                               == sourceValue.structuralFingerprint
                        && anchor.source.startPosition
                               == sourceValue.startPosition;
                }
                return anchor.source.selectedTextHash
                           == sourceValue.selectedTextHash
                    && anchor.source.startLine == sourceValue.startLine
                    && anchor.source.startColumn == sourceValue.startColumn;
            });
    }

    const bool createdAnchor = anchorIt == anchorRecords.end();
    if (createdAnchor) {
        PinloomCodeLinkAnchorRecord anchor;
        anchor.id = sourceValue.anchorId.trimmed().isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : sourceValue.anchorId.trimmed();
        anchor.source = sourceValue;
        anchor.source.anchorId = anchor.id;
        anchor.createdAtUtc =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        anchorRecords.append(anchor);
        anchorIt = std::prev(anchorRecords.end());
    }

    const auto duplicate = std::find_if(
        anchorIt->links.cbegin(), anchorIt->links.cend(),
        [&uri](const PinloomCodeLinkRecord& link) {
            return link.uri == uri;
        });
    if (duplicate != anchorIt->links.cend())
        return true;

    PinloomCodeLinkRecord target;
    target.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    target.title = title.trimmed().isEmpty()
        ? anchorIt->source.suggestedTitle()
        : title.trimmed();
    target.uri = uri;
    target.identity = identity;
    target.source = anchorIt->source;
    target.createdAtUtc =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    anchorIt->links.append(target);
    if (!save(failureReason)) {
        anchorIt->links.removeLast();
        if (createdAnchor && anchorIt->links.isEmpty())
            anchorRecords.erase(anchorIt);
        return false;
    }
    if (changedHandler)
        changedHandler();
    return true;
}

QList<ResolvedPinloomCodeLink> PinloomCodeLinkStore::linksForDocument(
    const QString& filePath,
    const QString& documentText,
    const TSDocument* syntaxDocument) const
{
    QList<ResolvedPinloomCodeLink> result;
    QList<SemanticSymbolRecord> symbols;
    const bool needsSymbols = std::any_of(
        anchorRecords.cbegin(), anchorRecords.cend(),
        [this, &filePath](const PinloomCodeLinkAnchorRecord& anchor) {
            return anchor.source.anchorKind
                       == PinloomCodeAnchorKind::Symbol
                && sourceMatchesFile(anchor.source, root, filePath);
        });
    if (needsSymbols) {
        SemanticIndex* index = SemanticIndex::getInstance();
        if (index->getCachedFileContent(filePath) == documentText)
            symbols = index->getSymbolRecords(filePath);
    }
    const bool needsSyntax = std::any_of(
        anchorRecords.cbegin(), anchorRecords.cend(),
        [this, &filePath](const PinloomCodeLinkAnchorRecord& anchor) {
            return (anchor.source.anchorKind
                        == PinloomCodeAnchorKind::AlwaysBlock
                    || anchor.source.anchorKind
                        == PinloomCodeAnchorKind::ContinuousAssign)
                && sourceMatchesFile(anchor.source, root, filePath);
        });
    TSDocument parsedDocument;
    const TSDocument* currentSyntax = syntaxDocument;
    if (needsSyntax && !currentSyntax) {
        parsedDocument.setText(documentText);
        currentSyntax = &parsedDocument;
    }
    const QList<TSBindableCodeAnchor> syntaxAnchors =
        needsSyntax && currentSyntax
        ? currentSyntax->bindableCodeAnchors()
        : QList<TSBindableCodeAnchor>{};
    for (const PinloomCodeLinkAnchorRecord& anchor : anchorRecords) {
        if (!sourceMatchesFile(anchor.source, root, filePath))
            continue;
        switch (anchor.source.anchorKind) {
        case PinloomCodeAnchorKind::Symbol:
            result.append(resolveSymbol(
                anchor, root, filePath, documentText, symbols));
            break;
        case PinloomCodeAnchorKind::AlwaysBlock:
        case PinloomCodeAnchorKind::ContinuousAssign:
            result.append(resolveSyntax(
                anchor, root, filePath, documentText,
                syntaxAnchors));
            break;
        case PinloomCodeAnchorKind::LegacySelection:
            result.append(resolveLegacy(anchor, documentText));
            break;
        }
    }
    return result;
}

QList<ResolvedPinloomCodeLink> PinloomCodeLinkStore::linksAtPosition(
    const QString& filePath,
    const QString& documentText,
    int position,
    const TSDocument* syntaxDocument) const
{
    QList<ResolvedPinloomCodeLink> result;
    for (const ResolvedPinloomCodeLink& link :
         linksForDocument(filePath, documentText, syntaxDocument)) {
        if (link.available()
            && position >= link.startPosition
            && position < link.endPosition) {
            result.append(link);
        }
    }
    return result;
}

ResolvedPinloomCodeLink PinloomCodeLinkStore::anchorByIdForDocument(
    const QString& anchorId,
    const QString& filePath,
    const QString& documentText,
    const TSDocument* syntaxDocument) const
{
    for (const ResolvedPinloomCodeLink& link :
         linksForDocument(filePath, documentText, syntaxDocument)) {
        if (link.anchor.id == anchorId)
            return link;
    }
    return {};
}

QList<PinloomCodeLinkAnchorRecord> PinloomCodeLinkStore::anchors() const
{
    return anchorRecords;
}

QList<PinloomCodeLinkRecord> PinloomCodeLinkStore::records() const
{
    QList<PinloomCodeLinkRecord> result;
    for (const PinloomCodeLinkAnchorRecord& anchor : anchorRecords) {
        for (PinloomCodeLinkRecord link : anchor.links) {
            link.source = anchor.source;
            result.append(link);
        }
    }
    return result;
}

void PinloomCodeLinkStore::setChangedHandler(
    std::function<void()> handler)
{
    changedHandler = std::move(handler);
}

bool PinloomCodeLinkStore::load(QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    anchorRecords.clear();
    const QString path = storagePath();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return true;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (failureReason)
            *failureReason = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Pinloom code-link file is invalid JSON.");
        }
        return false;
    }
    const QJsonObject rootObject = document.object();
    if (rootObject.value(QStringLiteral("schema")).toString() != kSchema) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Pinloom code-link schema is unsupported.");
        }
        return false;
    }
    const int version = rootObject.value(QStringLiteral("version")).toInt();
    if (version == kVersion) {
        for (const QJsonValue& value :
             rootObject.value(QStringLiteral("anchors")).toArray()) {
            if (!value.isObject())
                continue;
            const PinloomCodeLinkAnchorRecord anchor =
                anchorFromJson(value.toObject());
            if (anchor.isValid())
                anchorRecords.append(anchor);
        }
        return true;
    }
    if (version != 1) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Pinloom code-link schema is unsupported.");
        }
        return false;
    }

    // v1 records are migrated in memory without rewriting the workspace file.
    // The next explicit link change writes v2, preserving every legacy URI.
    for (const QJsonValue& value :
         rootObject.value(QStringLiteral("links")).toArray()) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        PinloomCodeLinkRecord target = targetFromJson(object);
        target.source = PinloomSourceSelection::fromVariantMap(
            object.value(QStringLiteral("source"))
                .toObject().toVariantMap());
        target.source.anchorKind = PinloomCodeAnchorKind::LegacySelection;
        if (!target.isValid())
            continue;
        auto existing = std::find_if(
            anchorRecords.begin(), anchorRecords.end(),
            [&target](const PinloomCodeLinkAnchorRecord& anchor) {
                return anchor.source.relativeFilePath.compare(
                           target.source.relativeFilePath,
                           Qt::CaseInsensitive) == 0
                    && anchor.source.selectedTextHash
                           == target.source.selectedTextHash
                    && anchor.source.startLine == target.source.startLine
                    && anchor.source.startColumn
                           == target.source.startColumn;
            });
        if (existing == anchorRecords.end()) {
            PinloomCodeLinkAnchorRecord anchor;
            anchor.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            anchor.source = target.source;
            anchor.source.anchorId = anchor.id;
            anchor.createdAtUtc = target.createdAtUtc;
            target.source = anchor.source;
            anchor.links.append(target);
            anchorRecords.append(anchor);
        } else {
            target.source = existing->source;
            existing->links.append(target);
        }
    }
    return true;
}

bool PinloomCodeLinkStore::save(QString* failureReason) const
{
    if (failureReason)
        failureReason->clear();
    const QString path = storagePath();
    if (path.isEmpty()) {
        if (failureReason)
            *failureReason = QStringLiteral("No workspace is active.");
        return false;
    }
    const QFileInfo fileInfo(path);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Unable to create the ZeroSlack workspace metadata directory.");
        }
        return false;
    }
    QJsonArray anchorsValue;
    for (const PinloomCodeLinkAnchorRecord& anchor : anchorRecords)
        anchorsValue.append(anchorToJson(anchor));
    const QJsonObject rootObject{
        {QStringLiteral("schema"), kSchema},
        {QStringLiteral("version"), kVersion},
        {QStringLiteral("anchors"), anchorsValue},
    };
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (failureReason)
            *failureReason = file.errorString();
        return false;
    }
    if (file.write(QJsonDocument(rootObject).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        if (failureReason)
            *failureReason = file.errorString();
        return false;
    }
    return true;
}
