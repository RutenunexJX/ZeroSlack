#include "applicationthememanager.h"
#include "contextfloatingwindow.h"
#include "insightgraphview.h"
#include "insightvisualstyle.h"
#include "testuistyle.h"

#include <QApplication>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGridLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QTableWidget>
#include <QTest>
#include <QTreeWidget>

namespace {
// A deterministic two-color backdrop detects opaque children without claiming
// that an offscreen test can render the Windows compositor's Acrylic material.
class BackdropProbe final : public QWidget {
public:
    QColor colorAt(const QPoint& point) const
    {
        return point.x() < width() / 2 ? QColor(96, 142, 166) : QColor(173, 126, 149);
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), colorAt(QPoint()));
        painter.fillRect(QRect(width() / 2, 0, width(), height()), colorAt(rect().topRight()));
    }
};

QImage render(QWidget& widget)
{
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget.render(&image);
    return image;
}
}

class ContextFloatingBackgroundTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY(initializeUiStyleForTest()); }
    void contentSurfacesFollowPlacementAndTheme();
};

void ContextFloatingBackgroundTest::contentSurfacesFollowPlacementAndTheme()
{
    ApplicationThemeManager::instance().applyToApplication();
    ContextFloatingWindow nativeHost(nullptr, nullptr);
    BackdropProbe floating;
    floating.setObjectName(nativeHost.objectName());
    floating.setStyleSheet(nativeHost.styleSheet());
    floating.resize(1000, 700);
    QWidget dock;
    dock.resize(floating.size());
    auto* content = new QWidget(&dock);
    content->setObjectName(QStringLiteral("backgroundTestPanel"));
    content->setGeometry(dock.rect());
    InsightVisualStyle::applyPanel(content);
    auto* layout = new QGridLayout(content);
    auto* title = new QLabel(QStringLiteral("Content title"), content);
    title->setObjectName(QStringLiteral("backgroundTestTitle"));
    InsightVisualStyle::applyTitleLabel(title);
    layout->addWidget(title, 0, 0, 1, 3);
    auto* search = new QLineEdit(content);
    search->setObjectName(QStringLiteral("backgroundTestSearch"));
    InsightVisualStyle::applySearchField(search);
    layout->addWidget(search, 1, 0, 1, 3);
    auto* graph = new InsightGraphView(content);
    auto* scene = new QGraphicsScene(graph);
    scene->setSceneRect(0, 0, 300, 220);
    auto* node = scene->addRect(125, 85, 50, 50, Qt::NoPen, QColor(12, 47, 93));
    graph->setScene(scene);
    layout->addWidget(graph, 2, 0);
    auto* tree = new QTreeWidget(content);
    tree->setHeaderLabel(QStringLiteral("Signals"));
    auto* row = new QTreeWidgetItem(tree, {QStringLiteral("byte_data")});
    row->setSelected(true);
    layout->addWidget(tree, 2, 1);
    auto* table = new QTableWidget(1, 1, content);
    table->setItem(0, 0, new QTableWidgetItem(QStringLiteral("8 bits")));
    layout->addWidget(table, 2, 2);
    auto* text = new QPlainTextEdit(QStringLiteral("Pinloom document preview"), content);
    layout->addWidget(text, 3, 0);
    auto* scroll = new QScrollArea(content);
    auto* scrollContent = new QWidget;
    scroll->setWidget(scrollContent);
    scroll->setWidgetResizable(true);
    layout->addWidget(scroll, 3, 1);
    auto* panel = new QWidget(content);
    panel->setObjectName(QStringLiteral("nestedBackgroundPanel"));
    InsightVisualStyle::applyPanel(panel);
    layout->addWidget(panel, 3, 2);
    InsightGraphView* lazyGraph = nullptr;
    dock.show();
    floating.show();

    for (ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark, ThemeMode::CatppuccinMocha}) {
        // Change theme while floating, then verify the same content docked.
        content->setParent(&floating);
        content->setGeometry(floating.rect());
        content->show();
        if (!lazyGraph) {
            lazyGraph = new InsightGraphView(panel);
            lazyGraph->setGeometry(8, 8, 180, 65);
            lazyGraph->setScene(new QGraphicsScene(lazyGraph));
            lazyGraph->show();
        }
        ApplicationThemeManager::instance().setMode(mode);
        QApplication::processEvents();
        const QImage image = render(floating);
        const QList<QWidget*> blankSurfaces{title, search, graph->viewport(), tree->viewport(),
            table->viewport(), text->viewport(), scrollContent, panel, lazyGraph->viewport()};
        for (QWidget* surface : blankSurfaces) {
            const QPoint sample = surface->mapTo(&floating, surface->rect().bottomRight() - QPoint(15, 15));
            const QString failure = QStringLiteral("surface %1: %2 / %3 covers the shared backdrop: %4 instead of %5")
                .arg(blankSurfaces.indexOf(surface))
                .arg(QString::fromLatin1(surface->metaObject()->className()), surface->objectName(),
                     image.pixelColor(sample).name(), floating.colorAt(sample).name());
            QVERIFY2(image.pixelColor(sample) == floating.colorAt(sample), qPrintable(failure));
        }
        const QPoint nodePoint = graph->viewport()->mapTo(&floating, graph->mapFromScene(node->rect().center()));
        QCOMPARE(image.pixelColor(nodePoint), node->brush().color());
        const QPoint selectionPoint = tree->viewport()->mapTo(&floating,
            tree->visualItemRect(row).topRight() + QPoint(-15, 8));
        QVERIFY(image.pixelColor(selectionPoint) != floating.colorAt(selectionPoint));

        content->setParent(&dock);
        content->setGeometry(dock.rect());
        content->show();
        QApplication::processEvents();
        const QImage docked = render(dock);
        const QPoint panelPoint = panel->mapTo(&dock, panel->rect().center());
        QCOMPARE(docked.pixelColor(panelPoint), InsightVisualStyle::theme().panelBackground);
        const QPoint canvasPoint = graph->viewport()->mapTo(&dock, graph->viewport()->rect().bottomRight() - QPoint(15, 15));
        QCOMPARE(docked.pixelColor(canvasPoint), InsightVisualStyle::theme().canvasBackground);
        QVERIFY(row->isSelected());
        QCOMPARE(scene->items().size(), 1);
    }
}

QTEST_MAIN(ContextFloatingBackgroundTest)
#include "context_floating_background_test.moc"
