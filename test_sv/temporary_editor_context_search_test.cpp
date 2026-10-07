#include "applicationthememanager.h"
#include "testuistyle.h"
#include "editorsearchcandidate.h"
#include "insightvisualstyle.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "searchservice.h"
#include "tabmanager.h"
#include "temporaryeditorcontextview.h"
#include "temporaryeditorsearchpopup.h"
#include "temporaryeditorsearchprovider.h"
#include "workspacemanager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLineEdit>
#include <QListView>
#include <QSignalSpy>
#include <atomic>
#include <QPalette>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest/QTest>

#include <algorithm>
#include <cstdio>
#include <memory>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* label, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
}

void pumpEvents(int milliseconds = 0)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    } while (timer.elapsed() < milliseconds);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

EditorSearchCandidate candidate(
    const QString& title,
    EditorSearchCandidateType type,
    const QString& path,
    const QString& disambiguation,
    int line = 1)
{
    EditorSearchCandidate value;
    value.location.filePath = path;
    value.location.line = line;
    value.title = title;
    value.type = type;
    value.disambiguation = disambiguation;
    return value;
}

bool writeFile(const QString& path, const QString& text)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Text)
        && file.write(text.toUtf8()) == text.toUtf8().size();
}

SearchResult symbolResult(
    const QString& title,
    SymbolTaxonomy::DeclarationKind kind,
    const QString& filePath,
    int line,
    const QString& owner = QString())
{
    SearchResult result;
    result.symbolDisplayName = title;
    result.symbolRecord.name = title;
    result.symbolRecord.declarationKind = kind;
    result.symbolRecord.owner.name = owner;
    result.symbolRecord.location.fileName = filePath;
    result.symbolRecord.location.startLine = line;
    result.symbolRecord.location.startColumn = 1;
    result.symbolRecord.location.endLine = line;
    result.symbolRecord.location.endColumn = 2;
    result.symbolStableKey.fileName = filePath;
    result.symbolStableKey.symbolName = title;
    result.symbolStableKey.declarationKind = kind;
    result.symbolStableKey.ownerScope = owner;
    result.symbolStableKey.sourcePosition = line * 10;
    result.symbolRecord.stableKey = result.symbolStableKey;
    result.codeLink = RtlInsightLink::fromFileLine(filePath, line, 1);
    result.score = 100;
    return result;
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    if (!initializeUiStyleForTest()) return 2;
    ApplicationThemeManager::instance().setMode(ThemeMode::Light);

    QWidget popupHost;
    popupHost.resize(800, 500);
    auto* popupLayout = new QVBoxLayout(&popupHost);
    auto* field = new QLineEdit(&popupHost);
    popupLayout->addWidget(field);
    popupLayout->addStretch(1);
    popupHost.show();
    TemporaryEditorSearchPopup popup(&popupHost);
    popup.attachSearchField(field);

    const QList<EditorSearchCandidate> duplicateCandidates = {
        candidate(QStringLiteral("duplicate.sv"),
                  EditorSearchCandidateType::File,
                  QStringLiteral("/workspace/a/duplicate.sv"),
                  QStringLiteral("a/duplicate.sv")),
        candidate(QStringLiteral("duplicate.sv"),
                  EditorSearchCandidateType::File,
                  QStringLiteral("/workspace/b/duplicate.sv"),
                  QStringLiteral("b/duplicate.sv")),
        candidate(QStringLiteral("duplicate"),
                  EditorSearchCandidateType::Module,
                  QStringLiteral("/workspace/rtl/top.sv"),
                  QStringLiteral("module in rtl/top.sv"), 12),
        candidate(QStringLiteral("duplicate"),
                  EditorSearchCandidateType::Package,
                  QStringLiteral("/workspace/pkg/types.sv"),
                  QStringLiteral("package in pkg/types.sv"), 7),
        candidate(QStringLiteral("duplicate"),
                  EditorSearchCandidateType::Symbol,
                  QStringLiteral("/workspace/rtl/consumer.sv"),
                  QStringLiteral("consumer in rtl/consumer.sv"), 41),
    };
    QList<EditorSearchCandidate> activated;
    popup.setActivationHandler(
        [&activated](const EditorSearchCandidate& selected) {
            activated.append(selected);
        });
    field->setText(QStringLiteral("duplicate"));
    popup.setCandidates(duplicateCandidates, field->text());
    pumpEvents();

    QListView* results = popup.resultsList();
    expect("mixed candidate taxonomy remains visible",
           results && results->model()->rowCount() == duplicateCandidates.size()
               && results->model()->index(2, 0).data(Qt::UserRole).toInt()
                      == static_cast<int>(EditorSearchCandidateType::Module)
               && results->model()->index(3, 0).data(Qt::UserRole).toInt()
                      == static_cast<int>(EditorSearchCandidateType::Package)
               && results->model()->index(4, 0).data(Qt::UserRole).toInt()
                      == static_cast<int>(EditorSearchCandidateType::Symbol));
    expect("search popup is a child layer rather than a top-level window",
           popupHost.isAncestorOf(&popup) && !popup.isWindow()
               && popupHost.rect().contains(popup.geometry()));
    QTest::keyClick(field, Qt::Key_Return);
    expect("ambiguous Enter does not select the first candidate",
           activated.isEmpty());
    QTest::keyClick(field, Qt::Key_Down);
    QTest::keyClick(field, Qt::Key_Down);
    QTest::keyClick(field, Qt::Key_Return);
    expect("keyboard selection activates the explicit duplicate",
           activated.size() == 1
               && activated.last().location.filePath
                      == QStringLiteral("/workspace/b/duplicate.sv"));

    field->setText(QStringLiteral("duplicate"));
    popup.setCandidates(duplicateCandidates, field->text());
    const QRect symbolRect = results->visualRect(results->model()->index(4, 0));
    QTest::mouseClick(results->viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      symbolRect.center());
    expect("mouse selection activates a non-first symbol candidate",
           activated.size() == 2
               && activated.last().type
                      == EditorSearchCandidateType::Symbol);

    field->setText(QStringLiteral("duplicate"));
    popup.setCandidates(duplicateCandidates, field->text());
    QTest::keyClick(field, Qt::Key_Escape);
    expect("Escape closes the popup without activation",
           !popup.isVisible() && activated.size() == 2);

    const EditorSearchCandidate exact = candidate(
        QStringLiteral("unique_module"),
        EditorSearchCandidateType::Module,
        QStringLiteral("/workspace/rtl/unique.sv"),
        QStringLiteral("module in rtl/unique.sv"), 9);
    field->setText(QStringLiteral("unique_module"));
    popup.setCandidates({exact}, field->text());
    QTest::keyClick(field, Qt::Key_Return);
    expect("unique exact match activates directly",
           activated.size() == 3 && activated.last() == exact);

    const auto verifyTheme = [&](ThemeMode mode) {
        ApplicationThemeManager::instance().setMode(mode);
        pumpEvents();
        const InsightTheme& theme = InsightVisualStyle::theme();
        return popup.palette().color(QPalette::Window)
                   == theme.panelBackground
            && results->palette().color(QPalette::Base)
                   == theme.input.background
            && results->palette().color(QPalette::Text)
                   == theme.input.text;
    };
    expect("context search popup follows Light tokens",
           verifyTheme(ThemeMode::Light));
    expect("context search popup follows Dark tokens",
           verifyTheme(ThemeMode::Dark));

    TemporaryEditorSearchProvider indexedProvider;
    indexedProvider.setWorkspaceFiles(
        {QStringLiteral("/workspace/a/duplicate.sv"),
         QStringLiteral("/workspace/b/duplicate.sv"),
         QStringLiteral("/workspace/unrelated.sv")},
        QStringLiteral("/workspace"));
    QList<SearchResult> semanticMatches;
    semanticMatches.reserve(209);
    for (int index = 0; index < 205; ++index) {
        const SymbolTaxonomy::DeclarationKind kind = index == 0
            ? SymbolTaxonomy::DeclarationKind::Module
            : (index == 1
                   ? SymbolTaxonomy::DeclarationKind::Package
                   : SymbolTaxonomy::DeclarationKind::Signal);
        semanticMatches.append(symbolResult(
            QStringLiteral("duplicate"),
            kind,
            QStringLiteral("/workspace/rtl/duplicate_%1.sv").arg(index),
            index + 1,
            QStringLiteral("owner_%1").arg(index)));
    }
    semanticMatches.append(semanticMatches.at(17));
    semanticMatches.append(semanticMatches.at(104));
    semanticMatches.append(semanticMatches.at(204));
    std::reverse(semanticMatches.begin(), semanticMatches.end());

    QList<SemanticSymbolRecord> semanticRecords;
    semanticRecords.reserve(semanticMatches.size());
    for (const SearchResult& match : semanticMatches)
        semanticRecords.append(match.symbolRecord);
    SemanticIndex catalogIndex;
    catalogIndex.setSnapshot(
        std::make_shared<SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(semanticRecords)));
    SearchService catalogService(&catalogIndex);
    const QList<SearchResult> completeCatalog =
        catalogService.symbolCatalog();
    expect("semantic catalog remains complete beyond 200 entries",
           completeCatalog.size() == semanticMatches.size()
               && completeCatalog.size() > 200);

    indexedProvider.setSemanticCatalog(completeCatalog);
    const EditorSearchCandidates indexedCandidates =
        indexedProvider.query(QStringLiteral("dplct"));
    TemporaryEditorSearchProvider compactProvider;
    compactProvider.setWorkspaceFiles(
        {QStringLiteral("/workspace/a/duplicate.sv"),
         QStringLiteral("/workspace/b/duplicate.sv"),
         QStringLiteral("/workspace/unrelated.sv")},
        QStringLiteral("/workspace"));
    compactProvider.setSemanticRecords(catalogIndex.getSymbolRecords());
    expect("compact semantic records preserve complete search results and navigation",
           compactProvider.query(QStringLiteral("dplct")) == indexedCandidates);
    TemporaryEditorSearchProvider publishedProvider;
    publishedProvider.setWorkspaceFiles(
        {QStringLiteral("/workspace/a/duplicate.sv"),
         QStringLiteral("/workspace/b/duplicate.sv"),
         QStringLiteral("/workspace/unrelated.sv")}, QStringLiteral("/workspace"));
    const auto publishedCatalog = std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(catalogIndex.getSymbolRecords()));
    publishedProvider.setSemanticSnapshot(publishedCatalog);
    QElapsedTimer catalogWait;
    catalogWait.start();
    while (!publishedProvider.semanticCatalogReady() && catalogWait.elapsed() < 5000)
        pumpEvents(2);
    auto expectedPublished = indexedCandidates;
    for (auto& candidate : expectedPublished)
        if (candidate.type != EditorSearchCandidateType::File)
            candidate.sourceTextSha256 = QByteArray{};
    expect("snapshot catalog preserves metadata and marks unavailable source evidence",
        publishedProvider.semanticCatalogReady()
            && publishedProvider.query(QStringLiteral("dplct")) == expectedPublished);
    publishedProvider.setSemanticSnapshot(publishedCatalog);
    publishedProvider.setSemanticSnapshot(std::make_shared<const SemanticIndexSnapshot>());
    catalogWait.restart();
    while (!publishedProvider.semanticCatalogReady() && catalogWait.elapsed() < 5000)
        pumpEvents(2);
    expect("latest empty publication discards obsolete symbol catalog",
        publishedProvider.semanticCatalogReady()
            && publishedProvider.query(QStringLiteral("dplct")).size() < indexedCandidates.size());
    compactProvider.setWorkspaceFiles({}, QStringLiteral("/workspace/rtl"));
    TemporaryEditorSearchProvider rebasedCatalogProvider;
    rebasedCatalogProvider.setWorkspaceFiles({}, QStringLiteral("/workspace/rtl"));
    rebasedCatalogProvider.setSemanticCatalog(completeCatalog);
    expect("compact semantic catalog preserves rebasing and metadata ordering",
           compactProvider.query(QStringLiteral("dplct"))
               == rebasedCatalogProvider.query(QStringLiteral("dplct")));
    catalogIndex.clearSemanticState();
    expect("per-keystroke search uses its cached catalog",
           indexedProvider.query(QStringLiteral("dplct"))
               == indexedCandidates);
    expect("compact catalog remains independent of the released source index",
           compactProvider.query(QStringLiteral("dplct"))
               == rebasedCatalogProvider.query(QStringLiteral("dplct")));

    SemanticSymbolRecord unknownRecord;
    unknownRecord.location.fileName = QStringLiteral("/workspace/unknown.sv");
    unknownRecord.location.startLine = 0;
    unknownRecord.location.startColumn = -1;
    unknownRecord.owner.name = QStringLiteral(" owner ");
    catalogIndex.setSnapshot(std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords({unknownRecord})));
    const auto unknownRecords = catalogIndex.getSymbolRecords();
    compactProvider.setSemanticRecords(unknownRecords);
    rebasedCatalogProvider.setSemanticCatalog(catalogService.symbolCatalog());
    const auto unknownCandidates = compactProvider.query(QStringLiteral("unknown"));
    expect("compact catalog keeps unknown names, invalid-key fallback and clamped locations",
           unknownCandidates == rebasedCatalogProvider.query(QStringLiteral("unknown"))
               && unknownCandidates.size() == 1
               && unknownCandidates.first().title == QStringLiteral("<unknown>")
               && unknownCandidates.first().location.line == 1
               && unknownCandidates.first().location.column == 1);
    compactProvider.setSemanticRecords({});
    expect("clearing compact semantic records removes old candidates",
           compactProvider.query(QStringLiteral("unknown")).isEmpty());
    catalogIndex.clearSemanticState();
    int fileCount = 0;
    int moduleCount = 0;
    int packageCount = 0;
    int symbolCount = 0;
    for (const EditorSearchCandidate& value : indexedCandidates) {
        switch (value.type) {
        case EditorSearchCandidateType::File:
            ++fileCount;
            break;
        case EditorSearchCandidateType::Module:
            ++moduleCount;
            break;
        case EditorSearchCandidateType::Package:
            ++packageCount;
            break;
        case EditorSearchCandidateType::Symbol:
            ++symbolCount;
            break;
        }
    }
    expect("cached catalog retains all files and symbol taxonomies",
           fileCount == 2 && moduleCount == 1
               && packageCount == 1 && symbolCount == 203);

    TemporaryEditorSearchProvider reorderedProvider;
    reorderedProvider.setWorkspaceFiles(
        {QStringLiteral("/workspace/b/duplicate.sv"),
         QStringLiteral("/workspace/a/duplicate.sv")},
        QStringLiteral("/workspace"));
    QList<SearchResult> reorderedCatalog = completeCatalog;
    std::reverse(reorderedCatalog.begin(), reorderedCatalog.end());
    reorderedProvider.setSemanticCatalog(reorderedCatalog);
    expect("fuzzy catalog results are deduplicated and order independent",
           indexedCandidates.size() == 207
               && indexedCandidates
                      == reorderedProvider.query(QStringLiteral("dplct")));

    QTemporaryDir identityFixture;
    const QString identityFile = identityFixture.filePath(QStringLiteral("catalog.sv"));
    expect("catalog identity fixture exists",
           identityFixture.isValid()
               && writeFile(identityFile, QStringLiteral("module catalog; endmodule\n")));
    QList<SearchResult> sharedFileCatalog;
    for (int i = 0; i < 512; ++i) {
        sharedFileCatalog.append(symbolResult(
            QStringLiteral("cached_symbol_%1").arg(i),
            SymbolTaxonomy::DeclarationKind::Signal, identityFile, i + 1));
    }
    // Same source link reached through another lexical path must still dedup.
    SearchResult alias = sharedFileCatalog.first();
    alias.codeLink.fileName = identityFixture.path() + QStringLiteral("/./catalog.sv");
    sharedFileCatalog.append(alias);
    sharedFileCatalog.append(sharedFileCatalog.last());
    TemporaryEditorSearchProvider identityProvider;
    identityProvider.setSemanticCatalog(sharedFileCatalog);
    const auto originalIdentityCandidates = identityProvider.query(QStringLiteral("cached_symbol"));
    expect("repeated file identities retain all distinct symbols and dedup path aliases",
           originalIdentityCandidates.size() == 512);
    std::reverse(sharedFileCatalog.begin(), sharedFileCatalog.end());
    identityProvider.setSemanticCatalog(sharedFileCatalog);
    expect("per-rebuild identity reuse preserves candidate ordering and locations",
           identityProvider.query(QStringLiteral("cached_symbol")) == originalIdentityCandidates);
    identityProvider.setWorkspaceFiles({identityFile}, identityFixture.path());
    const auto relativeCandidates = identityProvider.query(QStringLiteral("cached_symbol"));
    expect("workspace root refresh rebases every symbol sharing a file",
           relativeCandidates.size() == originalIdentityCandidates.size()
               && std::all_of(relativeCandidates.cbegin(), relativeCandidates.cend(),
                   [&](const EditorSearchCandidate& candidate) {
                       return candidate.disambiguation.contains(QStringLiteral("in catalog.sv"))
                           && QDir::cleanPath(candidate.location.filePath) == QDir::cleanPath(identityFile)
                           && candidate.location.line > 0
                           && candidate.location.column > 0;
                   }));
    identityProvider.setWorkspaceFiles({}, QString());
    expect("clearing workspace root restores the original semantic search catalog",
           identityProvider.query(QStringLiteral("cached_symbol")) == originalIdentityCandidates);
    sharedFileCatalog = {symbolResult(QStringLiteral("replacement_symbol"),
                                     SymbolTaxonomy::DeclarationKind::Module,
                                     identityFixture.filePath(QStringLiteral("not_created.sv")), 7)};
    identityProvider.setSemanticCatalog(sharedFileCatalog);
    expect("catalog refresh replaces old symbols and retains unresolved file targets",
           identityProvider.query(QStringLiteral("cached_symbol")).isEmpty()
               && identityProvider.query(QStringLiteral("replacement_symbol")).size() == 1);
    identityProvider.setSemanticCatalog({});
    expect("catalog clear removes candidates after identity reuse",
           identityProvider.query(QStringLiteral("replacement_symbol")).isEmpty());

    WorkspaceManager refreshWorkspaceManager;
    QObject refreshContext;
    int fileCatalogRefreshCalls = 0;
    const QMetaObject::Connection refreshConnection =
        connectTemporaryEditorFileCatalogRefresh(
            &refreshWorkspaceManager,
            &refreshContext,
            [&fileCatalogRefreshCalls]() {
                ++fileCatalogRefreshCalls;
            });
    emit refreshWorkspaceManager.filesScanned(
        {QStringLiteral("/workspace/new_file.sv")});
    expect("workspace scan invalidates the context file catalog",
           refreshConnection && fileCatalogRefreshCalls == 1);

    QTemporaryDir temporaryDirectory;
    const QString firstPath = temporaryDirectory.filePath(
        QStringLiteral("first.sv"));
    const QString secondPath = temporaryDirectory.filePath(
        QStringLiteral("second.sv"));
    const QString thirdPath = temporaryDirectory.filePath(
        QStringLiteral("third.sv"));
    expect("context search fixtures are writable",
           temporaryDirectory.isValid()
               && writeFile(firstPath, QStringLiteral("module first; endmodule\n"))
               && writeFile(secondPath, QStringLiteral("module second; endmodule\n"))
               && writeFile(thirdPath, QStringLiteral("module third; endmodule\n")));

    QWidget contextHost;
    auto* contextLayout = new QVBoxLayout(&contextHost);
    auto* tabs = new QTabWidget(&contextHost);
    contextLayout->addWidget(tabs);
    TabManager tabManager(tabs);
    expect("primary search fixture opens",
           tabManager.openFileInTab(firstPath));
    auto* contextView = new TemporaryEditorContextView(
        &tabManager, &contextHost);
    contextLayout->addWidget(contextView);
    contextHost.resize(900, 650);
    contextHost.show();
    EditorLocation initial;
    initial.filePath = firstPath;
    expect("context view opens an initial document",
           contextView->openLocation(initial));
    int providerCalls = 0;
    contextView->setSearchProvider(
        [&providerCalls, firstPath, secondPath, thirdPath](
            const QString& query) {
            ++providerCalls;
            return [query, firstPath, secondPath, thirdPath](const EditorSearchCancellation&) {
            if (query != QStringLiteral("target"))
                return EditorSearchCandidates{};
            return EditorSearchCandidates{
                candidate(QStringLiteral("target"),
                          EditorSearchCandidateType::Symbol,
                          firstPath,
                          QStringLiteral("first owner")),
                candidate(QStringLiteral("target"),
                          EditorSearchCandidateType::Symbol,
                          secondPath,
                          QStringLiteral("second owner")),
                candidate(QStringLiteral("target"),
                          EditorSearchCandidateType::Symbol,
                          thirdPath,
                          QStringLiteral("third owner")),
            };
            };
        });
    QLineEdit* contextSearch = contextView->searchField();
    contextSearch->setFocus();
    contextSearch->setText(QStringLiteral("target"));
    pumpEvents();
    expect("async search publishes candidates", QTest::qWaitFor([&] {
        const auto* list = contextView->findChild<QListView*>("temporaryEditorSearchResults");
        return list && list->model()->rowCount() == 3;
    }, 1500));
    QTest::keyClick(contextSearch, Qt::Key_Return);
    expect("ambiguous context search preserves its current document",
           providerCalls == 1
               && EditorFileIdentity::same(
                   contextView->currentLocation().filePath, firstPath));
    QTest::keyClick(contextSearch, Qt::Key_Down);
    QTest::keyClick(contextSearch, Qt::Key_Down);
    QTest::keyClick(contextSearch, Qt::Key_Down);
    QTest::keyClick(contextSearch, Qt::Key_Return);
    pumpEvents();
    expect("explicit context result opens through shared session history",
           EditorFileIdentity::same(
               contextView->currentLocation().filePath, thirdPath)
               && contextView->historyCount() == 2);

    TemporaryEditorSearchProvider largeProvider;
    QList<SearchResult> largeCatalog;
    for (int i = 0; i < 25000; ++i)
        largeCatalog.append(symbolResult(QStringLiteral("logic_item_%1").arg(i),
            SymbolTaxonomy::DeclarationKind::Signal, firstPath, i + 1, QStringLiteral("owner")));
    largeProvider.setSemanticCatalog(largeCatalog);
    auto captured = largeProvider.queryTask("logic_item_");
    int cancellationChecks = 0;
    expect("candidate matching observes internal cancellation",
        captured([&] { return ++cancellationChecks > 100; }).isEmpty() && cancellationChecks == 101);
    int sortChecks = 0;
    expect("candidate sorting observes internal cancellation",
        captured([&] { return ++sortChecks > 25020; }).isEmpty());
    std::atomic_int started{0};
    contextView->setSearchProvider([&](const QString& query) {
        auto task = largeProvider.queryTask(query);
        return [task, &started](const EditorSearchCancellation& cancelled) {
            ++started;
            return task(cancelled);
        };
    });
    QSignalSpy published(contextView, &TemporaryEditorContextView::searchResultsReady);
    const auto* focusBefore = QApplication::focusWidget();
    for (int i = 0; i < 200; ++i) contextSearch->setText(QStringLiteral("logic_item_%1").arg(i));
    contextSearch->setText("logic_item_");
    expect("rapid search completes only the latest query", QTest::qWaitFor([&] { return !published.isEmpty(); }, 5000)
        && published.size() == 1 && published.first()[0].toString() == "logic_item_" && started <= 2);
    auto* largeList = contextView->findChild<QListView*>("temporaryEditorSearchResults");
    expect("all large-catalog matches remain directly accessible without focus loss",
        largeList && largeList->model()->rowCount() == 25000
        && largeList->model()->index(24999, 0).data().isValid() && QApplication::focusWidget() == focusBefore);
    const auto complete = captured({});
    largeProvider.setSemanticCatalog({});
    expect("query task owns an immutable catalog across host replacement", captured({}) == complete
        && largeProvider.query("logic_item_").isEmpty());
    popup.setCandidates(complete, "logic_item_");
    QTest::keyClick(field, Qt::Key_Up);
    expect("Up reaches the last model row", results->currentIndex().row() == 24999);
    popup.clearCandidates();
    contextSearch->setText("closing");
    contextView->hide();
    const auto publishedBeforeHide = published.size();
    QTest::qWait(50);
    expect("hidden host rejects in-flight results", published.size() == publishedBeforeHide);
    delete contextView;
    pumpEvents();

    std::printf("temporary editor context search: %d checks, %d failed\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
