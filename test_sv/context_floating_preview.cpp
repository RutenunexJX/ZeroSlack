#include "applicationthememanager.h"
#include "contextfloatingwindow.h"
#include "insightviewsurface.h"
#include "signal_trace_test_fixture.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QScreen>
#include <QSlider>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QPlainTextEdit>
#include <QVBoxLayout>

// Native desktop fixture for DWM composition, which offscreen tests cannot render.
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    ApplicationThemeManager::instance().applyToApplication();
    QMainWindow main;
    main.setWindowTitle(QStringLiteral("ZeroSlack Acrylic verification"));
    auto* background = new QWidget(&main);
    background->setObjectName(QStringLiteral("acrylicTestBackground"));
    background->setStyleSheet(QStringLiteral(
        "#acrylicTestBackground { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, "
        "stop:0 #76c7c0, stop:0.4 #c8a5e5, stop:0.7 #edb37a, stop:1 #79a7e0); }"));
    main.setCentralWidget(background);
    auto* layout = new QVBoxLayout(background);
    auto* caption = new QLabel(QStringLiteral("BACKGROUND  /  0123456789"), background);
    caption->setStyleSheet(QStringLiteral("font-size: 40px; font-weight: bold; color: #203047;"));
    layout->addWidget(caption, 0, Qt::AlignCenter);
    layout->addStretch();
    auto* floatAgain = new QPushButton(QStringLiteral("Float content"), background);
    layout->addWidget(floatAgain);
    main.resize(1160, 850);
    main.move(main.screen()->availableGeometry().center() - main.rect().center());
    main.show();

    ContextFloatingWindow floating(&main, background);
    auto* content = new QWidget;
    auto* controls = new QVBoxLayout(content);
    auto* theme = new QPushButton(QStringLiteral("Switch light / dark"), content);
    controls->addWidget(theme);
    auto* slider = new QSlider(Qt::Horizontal, content);
    slider->setRange(60, 100);
    slider->setValue(floating.backgroundOpacity());
    controls->addWidget(slider);
    auto* status = new QLabel(content);
    controls->addWidget(status);
    auto* tabs = new QTabWidget(content);
    controls->addWidget(tabs, 1);
    SignalTraceTestFixture fixture;
    if(!fixture.build("module top(input logic [7:0] rx_data,output logic [7:0] tx_data);\nlogic [7:0] byte_data;\nassign byte_data=rx_data;\nassign tx_data=byte_data;\nendmodule\n"))return 2;
    auto* heatmap=new SignalTracePanel(tabs);heatmap->setContext(fixture.context);heatmap->showForSymbol("byte_data",fixture.file,"top");heatmap->setHeatmapMode(true);
    tabs->addTab(heatmap,QStringLiteral("Heatmap"));
    auto* trace=new SignalTracePanel(tabs);trace->setContext(fixture.context);trace->showForSymbol("byte_data",fixture.file,"top");
    tabs->addTab(trace,QStringLiteral("Signal Trace"));
    auto* documents = new QWidget(tabs);
    auto* documentLayout = new QVBoxLayout(documents);
    auto* tree = new QTreeWidget(documents);
    tree->setHeaderLabel(QStringLiteral("Signals"));
    new QTreeWidgetItem(tree, {QStringLiteral("byte_data")});
    new QTreeWidgetItem(tree, {QStringLiteral("rx_data")});
    documentLayout->addWidget(tree);
    auto* table = new QTableWidget(1, 2, documents);
    table->setHorizontalHeaderLabels({QStringLiteral("Signal"), QStringLiteral("Width")});
    table->setItem(0, 0, new QTableWidgetItem(QStringLiteral("byte_data")));
    table->setItem(0, 1, new QTableWidgetItem(QStringLiteral("8 bits")));
    documentLayout->addWidget(table);
    documentLayout->addWidget(new QPlainTextEdit(QStringLiteral("Readable text over the shared material."), documents));
    tabs->addTab(documents, QStringLiteral("Documents"));
    QObject::connect(slider, &QSlider::valueChanged, &floating, &ContextFloatingWindow::setBackgroundOpacity);
    QObject::connect(theme, &QPushButton::clicked, &floating, [] {
        auto& manager = ApplicationThemeManager::instance();
        manager.setMode(isDarkTheme(manager.mode()) ? ThemeMode::Light : ThemeMode::Dark);
    });
    QObject::connect(&floating, &ContextFloatingWindow::closeRequested, &app, &QApplication::quit);
    ContextResource resource;
    resource.providerId = QStringLiteral("preview");
    resource.resourceId = QStringLiteral("native-acrylic");
    resource.uri = QUrl(QStringLiteral("preview:/acrylic"));
    resource.title = QStringLiteral("ZeroSlack Acrylic floating preview");
    QObject::connect(&floating, &ContextFloatingWindow::titleDragFinished, &main, [&](const QPoint& position, bool cancelled) {
        if (cancelled || !main.rect().contains(main.mapFromGlobal(position)) || !floating.hasResource()) return;
        layout->insertWidget(1, floating.takeView(), 1);
        content->show();
    });
    QObject::connect(floatAgain, &QPushButton::clicked, &floating, [&] {
        if (floating.hasResource()) return;
        layout->removeWidget(content);
        floating.setView(resource, content);
    });
    floating.setActionsAvailable(true, false);
    floating.setInitialSize(QSize(920, 690));
    floating.setView(resource, content);
    status->setText(floating.hasAcrylicBackdrop()
        ? QStringLiteral("Desktop Acrylic enabled · text opacity 100%")
        : QStringLiteral("Solid fallback · text opacity 100%"));
    return app.exec();
}
