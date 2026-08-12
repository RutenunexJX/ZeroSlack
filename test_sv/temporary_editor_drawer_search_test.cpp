#include "applicationthememanager.h"
#include "editorsearchcandidate.h"
#include "insightvisualstyle.h"
#include "semanticindexsnapshot.h"
#include "tabmanager.h"
#include "temporaryeditordrawer.h"
#include "temporaryeditordrawercontroller.h"
#include "temporaryeditorsearchprovider.h"
#include "workspacemanager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QListWidget>
#include <QLineEdit>
#include <QPalette>
#include <QSettings>
#include <QTabWidget>
#include <QThread>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest/QTest>

#include <cstdio>
#include <algorithm>
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
    ApplicationThemeManager::instance().setMode(ThemeMode::Light);

    QWidget editorRegion;
    editorRegion.resize(1000, 700);
    editorRegion.show();

    TemporaryEditorDrawer drawer(&editorRegion);
    EditorLocation initial;
    initial.filePath = QStringLiteral("/workspace/initial.sv");
    drawer.open(initial);
    drawer.setFloatingGeometry(QRect(80, 60, 760, 520));
    pumpEvents(10);

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

    int providerCalls = 0;
    QObject::connect(
        &drawer,
        &TemporaryEditorDrawer::searchTextChanged,
        &drawer,
        [&drawer, &providerCalls, &duplicateCandidates](
            const QString& query) {
            ++providerCalls;
            drawer.setSearchCandidates(
                query.isEmpty()
                    ? QList<EditorSearchCandidate>{}
                    : duplicateCandidates,
                query);
        });

    QList<EditorSearchCandidate> activated;
    QObject::connect(
        &drawer,
        &TemporaryEditorDrawer::searchCandidateActivated,
        &drawer,
        [&activated](const EditorSearchCandidate& selected) {
            activated.append(selected);
        });

    QLineEdit* field = drawer.searchField();
    field->setFocus();
    field->setText(QStringLiteral("duplicate"));
    pumpEvents(10);

    QListWidget* results = drawer.findChild<QListWidget*>(
        QStringLiteral("temporaryEditorSearchResults"));
    QWidget* popup = drawer.findChild<QWidget*>(
        QStringLiteral("temporaryEditorSearchPopup"));
    expect("typing invokes the candidate provider path",
           providerCalls == 1);
    expect("mixed file/module/package/symbol candidates are all visible",
           results && results->count() == duplicateCandidates.size());
    expect("candidate rows expose type and disambiguation",
           results
               && results->item(0)->data(Qt::UserRole).toInt()
                      == static_cast<int>(EditorSearchCandidateType::File)
               && results->item(2)->data(Qt::UserRole).toInt()
                      == static_cast<int>(EditorSearchCandidateType::Module)
               && results->item(3)->data(Qt::UserRole).toInt()
                      == static_cast<int>(EditorSearchCandidateType::Package)
               && results->item(4)->data(Qt::UserRole).toInt()
                      == static_cast<int>(EditorSearchCandidateType::Symbol)
               && results->item(1)->text().contains(
                      QStringLiteral("b/duplicate.sv")));
    expect("search popup is a drawer-owned child layer, not a top-level window",
           popup
               && drawer.isAncestorOf(popup)
               && !popup->isWindow()
               && drawer.rect().contains(popup->geometry()));

    QTest::keyClick(field, Qt::Key_Return);
    pumpEvents();
    expect("Enter does not silently open the first ambiguous candidate",
           activated.isEmpty());

    QTest::keyClick(field, Qt::Key_Down);
    QTest::keyClick(field, Qt::Key_Down);
    QTest::keyClick(field, Qt::Key_Return);
    pumpEvents();
    expect("keyboard selection opens the explicitly selected duplicate",
           activated.size() == 1
               && activated.last().location.filePath
                      == QStringLiteral("/workspace/b/duplicate.sv"));

    field->setFocus();
    field->setText(QStringLiteral("duplicate"));
    pumpEvents();
    expect("mouse-selection scenario starts with a visible candidate popup",
           popup && popup->isVisible() && results->count() == 5);
    const QRect symbolRect = results->visualItemRect(results->item(4));
    QTest::mouseClick(results->viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      symbolRect.center());
    pumpEvents();
    expect("mouse selection can open a non-first symbol candidate",
           activated.size() == 2
               && activated.last().type
                      == EditorSearchCandidateType::Symbol
               && activated.last().location.line == 41);

    field->setFocus();
    field->setText(QStringLiteral("duplicate"));
    pumpEvents();
    expect("Escape scenario starts with a visible candidate popup",
           popup && popup->isVisible() && results->count() == 5);
    QTest::keyClick(field, Qt::Key_Escape);
    pumpEvents();
    expect("Escape closes the candidate popup without opening a target",
           popup && !popup->isVisible() && activated.size() == 2);

    const EditorSearchCandidate exact = candidate(
        QStringLiteral("unique_module"),
        EditorSearchCandidateType::Module,
        QStringLiteral("/workspace/rtl/unique.sv"),
        QStringLiteral("module in rtl/unique.sv"),
        9);
    field->setText(QStringLiteral("unique_module"));
    drawer.setSearchCandidates({exact}, field->text());
    QTest::keyClick(field, Qt::Key_Return);
    pumpEvents();
    expect("a unique exact match opens directly on Enter",
           activated.size() == 3
               && activated.last().location.filePath == exact.location.filePath);

    field->setText(QStringLiteral("dplct"));
    drawer.setSearchCandidates(duplicateCandidates, field->text());
    pumpEvents();
    expect("candidate popup preserves the provider's ordered fuzzy result",
           results && results->count() == duplicateCandidates.size());

    const auto verifyTheme = [&](ThemeMode mode) {
        ApplicationThemeManager::instance().setMode(mode);
        pumpEvents();
        const InsightTheme& theme = InsightVisualStyle::theme();
        return popup
            && results
            && popup->palette().color(QPalette::Window)
                   == theme.panelBackground
            && results->palette().color(QPalette::Base)
                   == theme.input.background
            && results->palette().color(QPalette::Text)
                   == theme.input.text;
    };
    expect("candidate popup follows Light theme tokens",
           verifyTheme(ThemeMode::Light));
    expect("candidate popup follows Dark theme tokens",
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
    expect("SearchService snapshot catalog is complete beyond 200 entries",
           completeCatalog.size() == semanticMatches.size()
               && completeCatalog.size() > 200);

    indexedProvider.setSemanticCatalog(completeCatalog);
    const EditorSearchCandidates indexedCandidates =
        indexedProvider.query(QStringLiteral("dplct"));
    catalogIndex.clearSemanticState();
    expect("per-keystroke provider query does not revisit SemanticIndex",
           indexedProvider.query(QStringLiteral("dplct"))
               == indexedCandidates);
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
    expect("cached workspace file index returns every same-name file",
           fileCount == 2);
    expect("SearchService result taxonomy maps module/package/symbol candidates",
           moduleCount == 1 && packageCount == 1 && symbolCount == 203);

    TemporaryEditorSearchProvider reorderedProvider;
    reorderedProvider.setWorkspaceFiles(
        {QStringLiteral("/workspace/b/duplicate.sv"),
         QStringLiteral("/workspace/a/duplicate.sv")},
        QStringLiteral("/workspace"));
    QList<SearchResult> reorderedCatalog = completeCatalog;
    std::reverse(reorderedCatalog.begin(), reorderedCatalog.end());
    reorderedProvider.setSemanticCatalog(reorderedCatalog);
    expect("catalog fuzzy search is complete, deduplicated, and input-order independent",
           indexedCandidates.size() == 207
               && indexedCandidates
                      == reorderedProvider.query(QStringLiteral("dplct"))
               && indexedCandidates.first().title
                      == QStringLiteral("duplicate")
               && std::any_of(
                      indexedCandidates.cbegin(),
                      indexedCandidates.cend(),
                      [](const EditorSearchCandidate& value) {
                          return value.disambiguation.contains(
                                     QStringLiteral("owner_204"))
                              && value.location.filePath.endsWith(
                                  QStringLiteral("duplicate_204.sv"))
                              && !value.location.sourceLinkId.isEmpty();
                      }));

    WorkspaceManager refreshWorkspaceManager;
    QObject refreshContext;
    int fileCatalogRefreshCalls = 0;
    const QMetaObject::Connection fileCatalogRefreshConnection =
        connectTemporaryEditorFileCatalogRefresh(
            &refreshWorkspaceManager,
            &refreshContext,
            [&fileCatalogRefreshCalls]() {
                ++fileCatalogRefreshCalls;
            });
    emit refreshWorkspaceManager.filesScanned(
        {QStringLiteral("/workspace/new_file.sv")});
    expect("filesScanned invalidates the temporary-editor file catalog",
           fileCatalogRefreshConnection
               && fileCatalogRefreshCalls == 1);

    QTemporaryDir temporaryDirectory;
    const QString firstPath = temporaryDirectory.filePath(
        QStringLiteral("first.sv"));
    const QString secondPath = temporaryDirectory.filePath(
        QStringLiteral("second.sv"));
    const QString thirdPath = temporaryDirectory.filePath(
        QStringLiteral("third.sv"));
    expect("controller search fixtures are created",
           temporaryDirectory.isValid()
               && writeFile(firstPath, QStringLiteral("module first; endmodule\n"))
               && writeFile(secondPath, QStringLiteral("module second; endmodule\n"))
               && writeFile(thirdPath, QStringLiteral("module third; endmodule\n")));

    QWidget controllerRegion;
    controllerRegion.resize(900, 650);
    auto* controllerLayout = new QVBoxLayout(&controllerRegion);
    controllerLayout->setContentsMargins(0, 0, 0, 0);
    QTabWidget tabs(&controllerRegion);
    controllerLayout->addWidget(&tabs);
    controllerRegion.show();
    TabManager tabManager(&tabs, &controllerRegion);
    auto settings = std::make_unique<QSettings>(
        temporaryDirectory.filePath(QStringLiteral("drawer-search.ini")),
        QSettings::IniFormat);
    TemporaryEditorDrawerController controller(
        &tabManager,
        &controllerRegion,
        std::move(settings),
        &controllerRegion);
    EditorLocation controllerInitial;
    controllerInitial.filePath = firstPath;
    expect("controller opens an initial drawer document",
           controller.openLocation(controllerInitial));
    pumpEvents(10);

    int controllerProviderCalls = 0;
    controller.setSearchProvider(
        [&controllerProviderCalls,
         firstPath,
         secondPath,
         thirdPath](const QString& query) {
            ++controllerProviderCalls;
            if (query != QStringLiteral("target"))
                return EditorSearchCandidates{};
            return EditorSearchCandidates{
                candidate(QStringLiteral("target"),
                          EditorSearchCandidateType::Symbol,
                          firstPath,
                          QStringLiteral("first owner"), 1),
                candidate(QStringLiteral("target"),
                          EditorSearchCandidateType::Symbol,
                          secondPath,
                          QStringLiteral("second owner"), 1),
                candidate(QStringLiteral("target"),
                          EditorSearchCandidateType::Symbol,
                          thirdPath,
                          QStringLiteral("third owner"), 1),
            };
        });
    QLineEdit* controllerSearch = controller.drawer()->searchField();
    controllerSearch->setFocus();
    controllerSearch->setText(QStringLiteral("target"));
    pumpEvents(10);
    QTest::keyClick(controllerSearch, Qt::Key_Return);
    pumpEvents();
    expect("ambiguous controller provider does not open its first result",
           controllerProviderCalls == 1
               && EditorFileIdentity::same(
                      controller.currentLocation().filePath, firstPath));
    QTest::keyClick(controllerSearch, Qt::Key_Down);
    QTest::keyClick(controllerSearch, Qt::Key_Down);
    QTest::keyClick(controllerSearch, Qt::Key_Down);
    QTest::keyClick(controllerSearch, Qt::Key_Return);
    pumpEvents(20);
    expect("explicit non-first selection routes through controller openLocation",
           EditorFileIdentity::same(
               controller.currentLocation().filePath, thirdPath)
               && controller.historyCount() == 2);

    std::printf("temporary editor drawer search: %d checks, %d failed\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
