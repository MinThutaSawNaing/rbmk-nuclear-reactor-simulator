#include "ui/MainWindow.h"

#include "ui/CoreView.h"
#include "ui/GaugeCard.h"
#include "ui/Theme.h"
#include "ui/TrendChart.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

using namespace rbmk;

namespace {

QLabel *legendChip(const QString &text, const QColor &color)
{
    auto *label = new QLabel(QStringLiteral("<span style='color:%1'>&#9632;</span> %2")
                                 .arg(color.name(), text));
    label->setTextFormat(Qt::RichText);
    label->setObjectName(QStringLiteral("Hint"));
    return label;
}

QLabel *caption(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("Hint"));
    return label;
}

QWidget *statBlock(const QString &name, QLabel **valueOut, const QString &initial,
                   const QString &color = QStringLiteral("#e2e8f0"))
{
    auto *box = new QWidget;
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *nameLabel = new QLabel(name);
    nameLabel->setObjectName(QStringLiteral("Hint"));
    auto *value = new QLabel(initial);
    value->setStyleSheet(QStringLiteral("color:%1; font-family:'Consolas'; font-size:13px; "
                                        "font-weight:bold;")
                             .arg(color));
    layout->addWidget(nameLabel);
    layout->addWidget(value);
    *valueOut = value;
    return box;
}

QFrame *separatorLine()
{
    auto *line = new QFrame;
    line->setFrameShape(QFrame::VLine);
    line->setStyleSheet(QStringLiteral("background:#1e293b;"));
    line->setFixedWidth(1);
    return line;
}

//! Nominal thermal power of a starting state, used as the default power target.
double presetPower(CorePreset preset)
{
    switch (preset) {
    case CorePreset::NominalHot:   return 1000.0;
    case CorePreset::LowPower:     return 200.0;
    case CorePreset::ColdShutdown: return 100.0;
    }
    return 1000.0;
}

} // namespace

// -----------------------------------------------------------------------------
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_core = new ReactorCore(this);

    setWindowTitle(QStringLiteral("RBMK-1000 Control Room  —  Core Physics Sandbox"));
    resize(1680, 1000);
    setMinimumSize(1360, 780);

    buildUi();
    buildMenus();

    connect(m_core, &ReactorCore::stateUpdated, this, &MainWindow::refreshUi);
    connect(m_core, &ReactorCore::eventLogged, this, &MainWindow::onEventLogged);
    connect(m_core, &ReactorCore::alarmRaised, this, &MainWindow::onAlarmRaised);
    connect(m_core, &ReactorCore::coreDestroyed, this, &MainWindow::onCoreDestroyed);
    connect(m_core, &ReactorCore::coreRebuilt, this, [this] {
        m_selectedIndex = -1;
        m_view->setSelectedIndex(-1);
        m_chart->clear();
        refreshUi();
    });

    connect(m_view, &CoreView::channelSelected, this, &MainWindow::onChannelSelected);
    connect(m_view, &CoreView::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_view, &CoreView::contextMenuRequested, this, &MainWindow::onContextMenu);
    connect(m_view, &CoreView::statusMessage, this, &MainWindow::setHint);
    connect(m_view, &CoreView::rodInsertionRequested, this, [this](int index, double pct) {
        m_core->setRodTarget(index, pct, true);
    });

    m_chartTimer = new QTimer(this);
    m_chartTimer->setInterval(250);
    connect(m_chartTimer, &QTimer::timeout, this, &MainWindow::sampleTrend);
    m_chartTimer->start();
}

MainWindow::~MainWindow() = default;

void MainWindow::startSimulation(CorePreset preset)
{
    m_core->stop();
    m_core->initialize(preset);
    m_selectedIndex = -1;
    m_view->setSelectedIndex(-1);
    m_chart->clear();
    m_lastAlarmLevel = 0;

    // Open with the automatic power hold engaged on a running reactor, so the
    // operator only has to pick the megawatt. A cold shutdown core is left alone
    // until they ask for one.
    {
        const QSignalBlocker blocker(m_powerTargetSpin);
        m_powerTargetSpin->setValue(presetPower(preset));
    }
    if (preset == CorePreset::ColdShutdown)
        m_core->clearPowerTarget();
    else
        m_core->setPowerTarget(presetPower(preset));

    refreshUi();
    if (preset == CorePreset::ColdShutdown) {
        setHint(QStringLiteral("Cold core loaded. AUTO HOLD is off - set a power target "
                               "and press it to let the controller take the bank."));
    } else {
        setHint(QStringLiteral("Reactor at %1 MWt with AUTO HOLD engaged. Type the megawatt "
                               "you want in POWER TARGET - the bank follows automatically.")
                    .arg(presetPower(preset), 0, 'f', 0));
    }
}

// ---------------------------------------------------------------- UI assembly
void MainWindow::buildUi()
{
    auto *central = new QWidget;
    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    outer->addWidget(buildHeader());

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(buildCorePanel());

    auto *controls = new QWidget;
    auto *controlLayout = new QVBoxLayout(controls);
    controlLayout->setContentsMargins(10, 10, 6, 10);
    controlLayout->setSpacing(8);
    controlLayout->addWidget(buildBankCard());
    controlLayout->addWidget(buildPumpCard());
    controlLayout->addWidget(buildChannelCard());
    controlLayout->addStretch(1);

    auto *controlScroll = new QScrollArea;
    controlScroll->setWidgetResizable(true);
    controlScroll->setWidget(controls);
    controlScroll->setMinimumWidth(356);
    controlScroll->setFrameShape(QFrame::NoFrame);
    controlScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    splitter->addWidget(controlScroll);

    auto *telemetry = new QWidget;
    auto *telemetryLayout = new QVBoxLayout(telemetry);
    telemetryLayout->setContentsMargins(6, 10, 10, 10);
    telemetryLayout->setSpacing(8);
    telemetryLayout->addWidget(buildTelemetryCard());
    telemetryLayout->addStretch(1);

    auto *telemetryScroll = new QScrollArea;
    telemetryScroll->setWidgetResizable(true);
    telemetryScroll->setWidget(telemetry);
    telemetryScroll->setMinimumWidth(372);
    telemetryScroll->setFrameShape(QFrame::NoFrame);
    telemetryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    splitter->addWidget(telemetryScroll);

    splitter->setStretchFactor(0, 6);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 4);
    splitter->setSizes({ 780, 380, 460 });

    outer->addWidget(splitter, 1);
    setCentralWidget(central);

    setStatusBar(new QStatusBar);
    statusBar()->showMessage(QStringLiteral("Ready."));

    auto *dock = new QDockWidget(QStringLiteral("EVENT LOG"), this);
    dock->setObjectName(QStringLiteral("EventLogDock"));
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    dock->setWidget(buildLogDock());
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    resizeDocks({ dock }, { 142 }, Qt::Vertical);
}

QWidget *MainWindow::buildHeader()
{
    auto *bar = new QFrame;
    bar->setObjectName(QStringLiteral("HeaderBar"));
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(14, 8, 14, 8);
    layout->setSpacing(14);

    auto *titleBlock = new QWidget;
    auto *titleLayout = new QVBoxLayout(titleBlock);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(1);
    auto *title = new QLabel(QStringLiteral("RBMK-1000  ELENA"));
    title->setObjectName(QStringLiteral("AppTitle"));
    auto *subtitle = new QLabel(QStringLiteral("upper biological shield IZ-250  //  "
                                               "every channel is insertable and removable"));
    subtitle->setObjectName(QStringLiteral("AppSubtitle"));
    titleLayout->addWidget(title);
    titleLayout->addWidget(subtitle);
    layout->addWidget(titleBlock);

    layout->addWidget(separatorLine());

    auto *statusBlock = new QWidget;
    auto *statusLayout = new QVBoxLayout(statusBlock);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(2);
    m_statusBadge = new QLabel(QStringLiteral("NORMAL OPERATION"));
    m_statusBadge->setObjectName(QStringLiteral("StatusBadge"));
    m_statusDetail = new QLabel(QStringLiteral("core stable"));
    m_statusDetail->setObjectName(QStringLiteral("Hint"));
    statusLayout->addWidget(m_statusBadge, 0, Qt::AlignLeft);
    statusLayout->addWidget(m_statusDetail);
    layout->addWidget(statusBlock, 1);

    layout->addWidget(separatorLine());
    layout->addWidget(statBlock(QStringLiteral("SIM TIME"), &m_clockLabel,
                                QStringLiteral("00:00:00")));
    layout->addWidget(statBlock(QStringLiteral("XENON"), &m_headerXenon,
                                QStringLiteral("1.00"), QStringLiteral("#a855f7")));
    layout->addWidget(separatorLine());

    auto *presetBox = new QWidget;
    auto *presetLayout = new QVBoxLayout(presetBox);
    presetLayout->setContentsMargins(0, 0, 0, 0);
    presetLayout->setSpacing(2);
    presetLayout->addWidget(caption(QStringLiteral("CORE STATE")));
    auto *presetRow = new QHBoxLayout;
    presetRow->setSpacing(4);
    m_presetCombo = new QComboBox;
    m_presetCombo->addItem(QStringLiteral("Nominal 1000 MWt"), int(CorePreset::NominalHot));
    m_presetCombo->addItem(QStringLiteral("Low power 200 MWt"), int(CorePreset::LowPower));
    m_presetCombo->addItem(QStringLiteral("Cold shutdown"), int(CorePreset::ColdShutdown));
    m_presetCombo->setMinimumWidth(150);
    auto *loadButton = new QPushButton(QStringLiteral("Load"));
    loadButton->setToolTip(QStringLiteral("Rebuild the core in the selected state"));
    connect(loadButton, &QPushButton::clicked, this, &MainWindow::onLoadPreset);
    presetRow->addWidget(m_presetCombo);
    presetRow->addWidget(loadButton);
    presetLayout->addLayout(presetRow);
    layout->addWidget(presetBox);

    layout->addWidget(separatorLine());

    auto *targetBox = new QWidget;
    auto *targetLayout = new QVBoxLayout(targetBox);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    targetLayout->setSpacing(2);
    targetLayout->addWidget(caption(QStringLiteral("POWER TARGET")));
    auto *targetRow = new QHBoxLayout;
    targetRow->setSpacing(4);
    m_powerTargetSpin = new QDoubleSpinBox;
    m_powerTargetSpin->setRange(10.0, 1450.0);
    m_powerTargetSpin->setDecimals(0);
    m_powerTargetSpin->setSingleStep(25.0);
    m_powerTargetSpin->setSuffix(QStringLiteral(" MWt"));
    m_powerTargetSpin->setValue(1000.0);
    m_powerTargetSpin->setKeyboardTracking(false);
    m_powerTargetSpin->setMinimumWidth(104);
    m_powerTargetSpin->setToolTip(QStringLiteral(
        "Thermal power the reactor should hold. Type a value and the automatic "
        "controller trims the rod bank to reach and keep it."));
    m_autoHoldButton = new QPushButton(QStringLiteral("AUTO HOLD"));
    m_autoHoldButton->setCheckable(true);
    m_autoHoldButton->setObjectName(QStringLiteral("AutoHoldButton"));
    // Reserve room for the "AUTO HOLD ON" caption so the header does not twitch
    // every time the controller engages or releases.
    m_autoHoldButton->setMinimumWidth(
        m_autoHoldButton->fontMetrics().horizontalAdvance(QStringLiteral("AUTO HOLD ON")) + 26);
    m_autoHoldButton->setToolTip(QStringLiteral(
        "Automatic power hold (Ctrl+H): the controller trims the rod bank until the "
        "core is critical at the target power, and keeps correcting as xenon, void "
        "and burnup drift. Moving the bank by hand releases it."));
    targetRow->addWidget(m_powerTargetSpin);
    targetRow->addWidget(m_autoHoldButton);
    connect(m_autoHoldButton, &QPushButton::toggled, this, &MainWindow::onAutoHoldToggled);
    connect(m_powerTargetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MainWindow::onPowerTargetEdited);
    targetLayout->addLayout(targetRow);
    layout->addWidget(targetBox);

    auto *speedBox = new QWidget;
    auto *speedLayout = new QVBoxLayout(speedBox);
    speedLayout->setContentsMargins(0, 0, 0, 0);
    speedLayout->setSpacing(2);
    speedLayout->addWidget(caption(QStringLiteral("TIME SCALE")));
    m_timeScaleCombo = new QComboBox;
    m_timeScaleCombo->addItem(QStringLiteral("0.5x"), 0.5);
    m_timeScaleCombo->addItem(QStringLiteral("1x"), 1.0);
    m_timeScaleCombo->addItem(QStringLiteral("2x"), 2.0);
    m_timeScaleCombo->addItem(QStringLiteral("5x"), 5.0);
    m_timeScaleCombo->addItem(QStringLiteral("10x"), 10.0);
    m_timeScaleCombo->setCurrentIndex(1);
    m_timeScaleCombo->setFixedWidth(70);
    connect(m_timeScaleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onTimeScaleChanged);
    speedLayout->addWidget(m_timeScaleCombo);
    layout->addWidget(speedBox);

    m_pauseButton = new QPushButton(QStringLiteral("Pause"));
    m_pauseButton->setToolTip(QStringLiteral("Freeze the simulation (Space)"));
    connect(m_pauseButton, &QPushButton::clicked, this, &MainWindow::onPauseToggled);
    layout->addWidget(m_pauseButton);

    m_soundButton = new QPushButton(QStringLiteral("Sound ON"));
    m_soundButton->setToolTip(QStringLiteral("Audible alarm when the alarm level escalates"));
    connect(m_soundButton, &QPushButton::clicked, this, &MainWindow::onSoundToggled);
    layout->addWidget(m_soundButton);

    auto *manual = new QPushButton(QStringLiteral("Manual"));
    connect(manual, &QPushButton::clicked, this, &MainWindow::showManual);
    layout->addWidget(manual);

    return bar;
}

QWidget *MainWindow::buildCorePanel()
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("Card"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *headerRow = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("REACTOR CORE  —  TOP-DOWN VIEW"));
    title->setObjectName(QStringLiteral("SectionTitle"));
    headerRow->addWidget(title);
    headerRow->addStretch(1);
    headerRow->addWidget(legendChip(QStringLiteral("FUEL"), theme::fuel()));
    headerRow->addWidget(legendChip(QStringLiteral("CONTROL ROD"), theme::controlRod()));
    headerRow->addWidget(legendChip(QStringLiteral("REFLECTOR"), theme::reflector()));
    headerRow->addWidget(legendChip(QStringLiteral("EMPTY"), theme::borderLight()));
    headerRow->addWidget(legendChip(QStringLiteral("HOT"), theme::danger()));
    layout->addLayout(headerRow);

    m_view = new CoreView(m_core);
    layout->addWidget(m_view, 1);

    auto *footerRow = new QHBoxLayout;
    m_selectedLabel = new QLabel(QStringLiteral("Selected: none (core aggregate view)"));
    m_selectedLabel->setObjectName(QStringLiteral("SelectedLabel"));
    footerRow->addWidget(m_selectedLabel, 1);

    auto *resetView = new QPushButton(QStringLiteral("Reset view"));
    resetView->setToolTip(QStringLiteral("Fit the shield to the window (or double-click "
                                         "empty space)"));
    connect(resetView, &QPushButton::clicked, m_view, &CoreView::resetZoom);
    footerRow->addWidget(resetView);

    auto *zoomOut = new QPushButton(QStringLiteral("-"));
    zoomOut->setFixedWidth(30);
    connect(zoomOut, &QPushButton::clicked, m_view, &CoreView::zoomOut);
    auto *zoomIn = new QPushButton(QStringLiteral("+"));
    zoomIn->setFixedWidth(30);
    connect(zoomIn, &QPushButton::clicked, m_view, &CoreView::zoomIn);
    footerRow->addWidget(zoomOut);
    footerRow->addWidget(zoomIn);

    layout->addLayout(footerRow);

    auto *hint = new QLabel(QStringLiteral("Left click = inspect   ·   ctrl+click = add to a "
                                           "group   ·   shift+click = rectangle   ·   drag a rod "
                                           "= trim it (or the whole group)   ·   wheel = trim   ·   "
                                           "ctrl+wheel = zoom   ·   right click = insert / extract"));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    return card;
}

QWidget *MainWindow::buildTelemetryCard()
{
    auto *group = new QGroupBox(QStringLiteral("CORE TELEMETRY AND INSTRUMENTATION"));
    auto *layout = new QVBoxLayout(group);
    layout->setSpacing(8);

    auto *grid = new QGridLayout;
    grid->setSpacing(6);
    grid->setContentsMargins(0, 0, 0, 0);

    m_powerGauge = new GaugeCard(QStringLiteral("Thermal power"), QStringLiteral("MWt"));
    m_fuelGauge = new GaugeCard(QStringLiteral("Fuel temperature"), QStringLiteral("degC"));
    m_coolantGauge = new GaugeCard(QStringLiteral("Coolant temperature"), QStringLiteral("degC"));
    m_pressureGauge = new GaugeCard(QStringLiteral("Primary pressure"), QStringLiteral("atm"));
    m_flowGauge = new GaugeCard(QStringLiteral("Coolant flow"), QStringLiteral("%"));
    m_reactivityGauge = new GaugeCard(QStringLiteral("Reactivity"), QStringLiteral("$"));
    m_fluxGauge = new GaugeCard(QStringLiteral("Neutron flux"), QStringLiteral("n/cm2s"));
    m_xenonGauge = new GaugeCard(QStringLiteral("Xenon poison"), QStringLiteral("rel"));
    m_ormGauge = new GaugeCard(QStringLiteral("Shutdown margin"), QStringLiteral("eq rods"));
    m_periodGauge = new GaugeCard(QStringLiteral("Reactor period"), QStringLiteral("s"));

    QWidget *cards[10] = { m_powerGauge, m_fuelGauge, m_coolantGauge, m_pressureGauge,
                           m_flowGauge, m_reactivityGauge, m_fluxGauge, m_xenonGauge,
                           m_ormGauge, m_periodGauge };
    for (int i = 0; i < 10; ++i)
        grid->addWidget(cards[i], i / 2, i % 2);

    layout->addLayout(grid);

    m_chart = new TrendChart;
    layout->addWidget(m_chart);

    // CORE INVENTORY AND SESSION
    auto *inventory = new QFrame;
    inventory->setObjectName(QStringLiteral("SubCard"));
    auto *inventoryLayout = new QVBoxLayout(inventory);
    inventoryLayout->setContentsMargins(9, 7, 9, 7);
    inventoryLayout->setSpacing(3);

    m_inventoryLabel = new QLabel(QStringLiteral("FUEL 180   ·   CONTROL 30   ·   "
                                                 "REFLECTOR 38   ·   EMPTY 0"));
    m_inventoryLabel->setObjectName(QStringLiteral("Hint"));
    m_inventoryLabel->setWordWrap(true);
    m_sessionLabel = new QLabel(QStringLiteral("ENERGY 0.00 MWh   ·   BURNUP 0.000 %   ·   "
                                               "INTEGRITY 100.0 %"));
    m_sessionLabel->setObjectName(QStringLiteral("Hint"));
    m_sessionLabel->setWordWrap(true);
    inventoryLayout->addWidget(m_inventoryLabel);
    inventoryLayout->addWidget(m_sessionLabel);
    layout->addWidget(inventory);

    return group;
}

QWidget *MainWindow::buildBankCard()
{
    auto *group = new QGroupBox(QStringLiteral("REACTIVITY CONTROL  —  ROD BANK AND DRIVES"));
    auto *layout = new QVBoxLayout(group);
    layout->setSpacing(7);

    auto *bankRow = new QHBoxLayout;
    bankRow->addWidget(caption(QStringLiteral("BANK SET POINT")));
    bankRow->addStretch(1);
    m_bankValue = new QLabel(QStringLiteral("43.0 %"));
    m_bankValue->setStyleSheet(QStringLiteral("color:#38bdf8; font-weight:bold;"));
    bankRow->addWidget(m_bankValue);
    layout->addLayout(bankRow);

    m_bankSlider = new QSlider(Qt::Horizontal);
    m_bankSlider->setRange(0, 1000);
    m_bankSlider->setValue(430);
    m_bankSlider->setToolTip(QStringLiteral("0 % = all rods withdrawn, 100 % = fully inserted"));
    connect(m_bankSlider, &QSlider::valueChanged, this, &MainWindow::onBankSliderChanged);
    layout->addWidget(m_bankSlider);

    m_bankAverage = new QLabel(QStringLiteral("30 drives  ·  avg 43.0 %  ·  ORM 12.9"));
    m_bankAverage->setObjectName(QStringLiteral("Hint"));
    m_bankAverage->setWordWrap(true);
    layout->addWidget(m_bankAverage);

    auto *bankButtons = new QHBoxLayout;
    bankButtons->setSpacing(4);
    const struct
    {
        QString text;
        double delta;
    } steps[4] = { { QStringLiteral("-5 %"), -5.0 }, { QStringLiteral("-1 %"), -1.0 },
                   { QStringLiteral("+1 %"), 1.0 }, { QStringLiteral("+5 %"), 5.0 } };
    for (const auto &step : steps) {
        auto *button = new QPushButton(step.text);
        connect(button, &QPushButton::clicked, this, [this, step] {
            m_core->nudgeBank(step.delta);
        });
        bankButtons->addWidget(button);
    }
    auto *sync = new QPushButton(QStringLiteral("Sync"));
    sync->setToolTip(QStringLiteral("Re-attach every independently trimmed drive to the bank"));
    connect(sync, &QPushButton::clicked, m_core, &ReactorCore::syncAllRodsToBank);
    bankButtons->addWidget(sync, 1);
    layout->addLayout(bankButtons);

    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QStringLiteral("background:#1e293b;"));
    line->setFixedHeight(1);
    layout->addWidget(line);

    auto *rodRow = new QHBoxLayout;
    rodRow->addWidget(caption(QStringLiteral("INDIVIDUAL DRIVE")));
    rodRow->addStretch(1);
    m_rodValue = new QLabel(QStringLiteral("select a control rod"));
    m_rodValue->setStyleSheet(QStringLiteral("color:#f59e0b; font-weight:bold;"));
    rodRow->addWidget(m_rodValue);
    layout->addLayout(rodRow);

    m_rodSlider = new QSlider(Qt::Horizontal);
    m_rodSlider->setRange(0, 1000);
    m_rodSlider->setEnabled(false);
    m_rodSlider->setToolTip(QStringLiteral("Trim the selected rod drive - or every drive in a "
                                           "Ctrl+click group. They detach from the bank until the "
                                           "bank is moved."));
    connect(m_rodSlider, &QSlider::valueChanged, this, &MainWindow::onRodSliderChanged);
    layout->addWidget(m_rodSlider);

    auto *rodFooter = new QHBoxLayout;
    m_rodActual = new QLabel(QStringLiteral("no drive selected"));
    m_rodActual->setObjectName(QStringLiteral("Hint"));
    m_rodActual->setWordWrap(true);
    rodFooter->addWidget(m_rodActual, 1);
    m_syncRodButton = new QPushButton(QStringLiteral("Attach to bank"));
    m_syncRodButton->setEnabled(false);
    connect(m_syncRodButton, &QPushButton::clicked, this, &MainWindow::onSyncRodToBank);
    rodFooter->addWidget(m_syncRodButton);
    layout->addLayout(rodFooter);

    auto *emergencyRow = new QHBoxLayout;
    emergencyRow->setSpacing(6);
    auto *scram = new QPushButton(QStringLiteral("AZ-5  SCRAM"));
    scram->setObjectName(QStringLiteral("DangerButton"));
    scram->setToolTip(QStringLiteral("Drop every installed rod to 100 % (F2)"));
    connect(scram, &QPushButton::clicked, m_core, &ReactorCore::scram);
    emergencyRow->addWidget(scram, 2);

    auto *rebuild = new QPushButton(QStringLiteral("Rebuild core"));
    rebuild->setToolTip(QStringLiteral("Restore the as-built assembly layout and state (Ctrl+R)"));
    connect(rebuild, &QPushButton::clicked, m_core, &ReactorCore::reset);
    emergencyRow->addWidget(rebuild, 1);
    layout->addLayout(emergencyRow);

    m_autoStatusLabel = new QLabel(QStringLiteral("AUTO HOLD off  ·  manual rod control"));
    m_autoStatusLabel->setWordWrap(true);
    m_autoStatusLabel->setStyleSheet(QStringLiteral("color:#64748b;"));
    layout->addWidget(m_autoStatusLabel);

    auto *hint = new QLabel(QStringLiteral("Bank moves every drive; an individual trim detaches "
                                           "one drive until the bank moves again."));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    return group;
}
QWidget *MainWindow::buildPumpCard()
{
    auto *group = new QGroupBox(QStringLiteral("MAIN CIRCULATION PUMPS  —  MCP 1-4"));
    auto *layout = new QVBoxLayout(group);
    layout->setSpacing(7);

    auto *grid = new QGridLayout;
    grid->setSpacing(5);
    for (int i = 0; i < 4; ++i) {
        auto *button = new QPushButton(QStringLiteral("MCP-%1\nONLINE").arg(i + 1));
        button->setObjectName(QStringLiteral("PumpButton"));
        button->setCheckable(true);
        button->setChecked(true);
        button->setMinimumHeight(38);
        connect(button, &QPushButton::toggled, this, [this, i](bool on) {
            if (!m_updatingUi)
                m_core->setPump(i, on);
        });
        m_pumpButtons[i] = button;
        grid->addWidget(button, i / 2, i % 2);
    }
    layout->addLayout(grid);

    auto *row = new QHBoxLayout;
    auto *allOn = new QPushButton(QStringLiteral("Start all"));
    connect(allOn, &QPushButton::clicked, this, [this] { m_core->setAllPumps(true); });
    auto *allOff = new QPushButton(QStringLiteral("Stop all"));
    connect(allOff, &QPushButton::clicked, this, [this] { m_core->setAllPumps(false); });
    row->addWidget(allOn);
    row->addWidget(allOff);
    layout->addLayout(row);

    m_pumpStatus = new QLabel(QStringLiteral("4/4 pumps  ·  flow 100 %"));
    m_pumpStatus->setObjectName(QStringLiteral("Hint"));
    m_pumpStatus->setWordWrap(true);
    layout->addWidget(m_pumpStatus);

    auto *hint = new QLabel(QStringLiteral("Each pump = 25 % flow. Losing flow raises void - "
                                           "which adds positive reactivity here."));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    return group;
}

QWidget *MainWindow::buildChannelCard()
{
    auto *group = new QGroupBox(QStringLiteral("CHANNEL INSPECTOR AND ASSEMBLY BAY"));
    auto *layout = new QVBoxLayout(group);
    layout->setSpacing(7);

    auto *head = new QHBoxLayout;
    m_inspCoord = new QLabel(QStringLiteral("--"));
    m_inspCoord->setStyleSheet(QStringLiteral("color:#38bdf8; font-size:16px; font-weight:bold;"));
    m_inspType = new QLabel(QStringLiteral("no channel selected"));
    m_inspType->setStyleSheet(QStringLiteral("color:#cbd5e1; font-weight:bold;"));
    head->addWidget(m_inspCoord);
    head->addWidget(m_inspType);
    head->addStretch(1);
    layout->addLayout(head);

    auto *grid = new QGridLayout;
    grid->setSpacing(5);
    m_inspTemp = new QLabel(QStringLiteral("temp   --"));
    m_inspFlux = new QLabel(QStringLiteral("flux   --"));
    m_inspDetail = new QLabel(QStringLiteral("select a socket on the core map"));
    for (QLabel *label : { m_inspTemp, m_inspFlux, m_inspDetail })
        label->setObjectName(QStringLiteral("Hint"));
    m_inspDetail->setWordWrap(true);
    grid->addWidget(m_inspTemp, 0, 0);
    grid->addWidget(m_inspFlux, 0, 1);
    grid->addWidget(m_inspDetail, 1, 0, 1, 2);
    layout->addLayout(grid);

    auto *installRow = new QHBoxLayout;
    installRow->setSpacing(4);
    m_installFuelButton = new QPushButton(QStringLiteral("Fuel"));
    m_installRodButton = new QPushButton(QStringLiteral("Control rod"));
    m_installReflectorButton = new QPushButton(QStringLiteral("Reflector"));
    const struct
    {
        QPushButton *button;
        AssemblyType type;
        QString tip;
    } installs[3] = {
        { m_installFuelButton, AssemblyType::Fuel,
          QStringLiteral("Install a fuel assembly in the selected socket") },
        { m_installRodButton, AssemblyType::Control,
          QStringLiteral("Install a control rod drive in the selected socket") },
        { m_installReflectorButton, AssemblyType::Reflector,
          QStringLiteral("Install a water reflector column in the selected socket") },
    };
    for (const auto &item : installs) {
        item.button->setToolTip(item.tip);
        connect(item.button, &QPushButton::clicked, this,
                [this, type = item.type] { installAssembly(type); });
        installRow->addWidget(item.button);
    }
    layout->addLayout(installRow);

    m_extractButton = new QPushButton(QStringLiteral("Extract selected assembly"));
    connect(m_extractButton, &QPushButton::clicked, this, &MainWindow::onExtractClicked);
    layout->addWidget(m_extractButton);

    for (QPushButton *button : { m_installFuelButton, m_installRodButton,
                                 m_installReflectorButton, m_extractButton })
        button->setEnabled(false);

    return group;
}

QWidget *MainWindow::buildLogDock()
{
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 4, 6, 6);
    layout->setSpacing(4);

    auto *row = new QHBoxLayout;
    auto *hint = new QLabel(QStringLiteral("Every operator action, protection response and "
                                           "reactivity step is recorded here."));
    hint->setObjectName(QStringLiteral("Hint"));
    row->addWidget(hint, 1);

    auto *clear = new QPushButton(QStringLiteral("Clear"));
    connect(clear, &QPushButton::clicked, this, [this] {
        if (m_log)
            m_log->clear();
    });
    auto *save = new QPushButton(QStringLiteral("Save log..."));
    connect(save, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save event log"),
            QStringLiteral("rbmk-session.log"),
            QStringLiteral("Log files (*.log *.txt);;All files (*)"));
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, QStringLiteral("Save failed"),
                                 QStringLiteral("Could not write %1").arg(path));
            return;
        }
        QTextStream stream(&file);
        stream << m_log->toPlainText();
        setHint(QStringLiteral("Event log saved to %1").arg(path));
    });
    row->addWidget(clear);
    row->addWidget(save);
    layout->addLayout(row);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont font(theme::monoFamily());
    font.setPointSizeF(8.5);
    m_log->setFont(font);
    m_log->setPlaceholderText(QStringLiteral("Event log will appear here..."));
    layout->addWidget(m_log, 1);

    return panel;
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));

    auto *rebuild = fileMenu->addAction(QStringLiteral("Rebuild core / reset state"));
    rebuild->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    connect(rebuild, &QAction::triggered, m_core, &ReactorCore::reset);

    auto *presetMenu = fileMenu->addMenu(QStringLiteral("Load core state"));
    const struct
    {
        QString text;
        CorePreset preset;
    } presets[3] = {
        { QStringLiteral("Nominal 1000 MWt"), CorePreset::NominalHot },
        { QStringLiteral("Low power 200 MWt"), CorePreset::LowPower },
        { QStringLiteral("Cold shutdown"), CorePreset::ColdShutdown },
    };
    for (const auto &item : presets) {
        auto *action = presetMenu->addAction(item.text);
        connect(action, &QAction::triggered, this, [this, preset = item.preset] {
            m_core->loadPreset(preset);
        });
    }

    fileMenu->addSeparator();
    auto *quit = fileMenu->addAction(QStringLiteral("Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    auto *reactorMenu = menuBar()->addMenu(QStringLiteral("&Reactor"));
    m_pauseAction = reactorMenu->addAction(QStringLiteral("Pause simulation"));
    m_pauseAction->setShortcut(QKeySequence(QStringLiteral("P")));
    m_pauseAction->setCheckable(true);
    connect(m_pauseAction, &QAction::toggled, this, [this](bool paused) {
        m_core->setPaused(paused);
        if (m_pauseButton) {
            const QSignalBlocker blocker(m_pauseButton);
            m_pauseButton->setText(paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
        }
    });

    reactorMenu->addSeparator();
    auto *withdraw = reactorMenu->addAction(QStringLiteral("Withdraw bank 5 %"));
    withdraw->setShortcut(QKeySequence(QStringLiteral("Ctrl+Up")));
    connect(withdraw, &QAction::triggered, this, [this] { m_core->nudgeBank(-5.0); });

    auto *insert = reactorMenu->addAction(QStringLiteral("Insert bank 5 %"));
    insert->setShortcut(QKeySequence(QStringLiteral("Ctrl+Down")));
    connect(insert, &QAction::triggered, this, [this] { m_core->nudgeBank(5.0); });

    auto *syncAll = reactorMenu->addAction(QStringLiteral("Attach all drives to bank"));
    syncAll->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    connect(syncAll, &QAction::triggered, m_core, &ReactorCore::syncAllRodsToBank);

    reactorMenu->addSeparator();
    m_autoHoldAction = reactorMenu->addAction(QStringLiteral("Automatic power hold"));
    m_autoHoldAction->setCheckable(true);
    m_autoHoldAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
    connect(m_autoHoldAction, &QAction::toggled, this, [this](bool on) {
        if (m_updatingUi || !m_autoHoldButton)
            return;
        m_autoHoldButton->setChecked(on); // the button drives the core
    });

    auto *holdNow = reactorMenu->addAction(QStringLiteral("Hold the power the reactor is at now"));
    holdNow->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+H")));
    connect(holdNow, &QAction::triggered, this, [this] {
        const int current = int(std::lround(m_core->telemetry().powerMWt / 5.0) * 5);
        {
            const QSignalBlocker blocker(m_powerTargetSpin);
            m_powerTargetSpin->setValue(qBound(10, current, 1450));
        }
        m_autoHoldButton->setChecked(true);
        m_core->setPowerTarget(m_powerTargetSpin->value());
        setHint(QStringLiteral("AUTO HOLD: holding the present %1 MWt.")
                    .arg(m_powerTargetSpin->value(), 0, 'f', 0));
    });

    reactorMenu->addSeparator();
    auto *scram = reactorMenu->addAction(QStringLiteral("AZ-5  SCRAM"));
    scram->setShortcut(QKeySequence(Qt::Key_F2));
    connect(scram, &QAction::triggered, m_core, &ReactorCore::scram);

    reactorMenu->addSeparator();
    auto *pumpsOn = reactorMenu->addAction(QStringLiteral("Start all pumps"));
    connect(pumpsOn, &QAction::triggered, this, [this] { m_core->setAllPumps(true); });
    auto *pumpsOff = reactorMenu->addAction(QStringLiteral("Stop all pumps"));
    connect(pumpsOff, &QAction::triggered, this, [this] { m_core->setAllPumps(false); });

    auto *channelMenu = menuBar()->addMenu(QStringLiteral("&Channel"));
    m_installFuelAction = channelMenu->addAction(QStringLiteral("Install fuel assembly"));
    m_installFuelAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    connect(m_installFuelAction, &QAction::triggered, this,
            [this] { installAssembly(AssemblyType::Fuel); });

    m_installRodAction = channelMenu->addAction(QStringLiteral("Install control rod drive"));
    m_installRodAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    connect(m_installRodAction, &QAction::triggered, this,
            [this] { installAssembly(AssemblyType::Control); });

    m_installReflectorAction = channelMenu->addAction(QStringLiteral("Install water reflector"));
    m_installReflectorAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+3")));
    connect(m_installReflectorAction, &QAction::triggered, this,
            [this] { installAssembly(AssemblyType::Reflector); });

    channelMenu->addSeparator();
    m_extractAction = channelMenu->addAction(QStringLiteral("Extract assembly"));
    m_extractAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(m_extractAction, &QAction::triggered, this, &MainWindow::onExtractClicked);

    m_syncRodAction = channelMenu->addAction(QStringLiteral("Attach selected drive to bank"));
    m_syncRodAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    connect(m_syncRodAction, &QAction::triggered, this, &MainWindow::onSyncRodToBank);

    channelMenu->addSeparator();
    auto *selectAll = channelMenu->addAction(QStringLiteral("Select all control rods"));
    selectAll->setShortcut(QKeySequence(QStringLiteral("Ctrl+A")));
    selectAll->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(selectAll, &QAction::triggered, this, &MainWindow::selectAllControlRods);
    m_view->addAction(selectAll);

    auto *clearSel = channelMenu->addAction(QStringLiteral("Clear multi-selection"));
    clearSel->setShortcut(QKeySequence(Qt::Key_Escape));
    clearSel->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(clearSel, &QAction::triggered, this, &MainWindow::clearSelection);
    m_view->addAction(clearSel);

    auto *copySel = channelMenu->addAction(QStringLiteral("Copy selection to clipboard"));
    copySel->setShortcut(QKeySequence(QStringLiteral("Ctrl+C")));
    copySel->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(copySel, &QAction::triggered, this, &MainWindow::copySelection);
    m_view->addAction(copySel);

    auto *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    auto *resetZoom = viewMenu->addAction(QStringLiteral("Reset zoom"));
    resetZoom->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    connect(resetZoom, &QAction::triggered, m_view, &CoreView::resetZoom);
    auto *coords = viewMenu->addAction(QStringLiteral("Show channel coordinates"));
    coords->setCheckable(true);
    connect(coords, &QAction::toggled, m_view, &CoreView::setShowCoordinates);

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    auto *manual = helpMenu->addAction(QStringLiteral("Operating manual"));
    manual->setShortcut(QKeySequence(Qt::Key_F1));
    connect(manual, &QAction::triggered, this, &MainWindow::showManual);
    auto *about = helpMenu->addAction(QStringLiteral("About"));
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);
}

// ------------------------------------------------------------------- channels
void MainWindow::selectChannel(int index)
{
    m_selectedIndex = m_core->isValidChannel(index) ? index : -1;
    m_view->setSelectedIndex(m_selectedIndex);
    updateInspector();
    refreshUi();
}

void MainWindow::onChannelSelected(int index)
{
    m_selectedIndex = m_core->isValidChannel(index) ? index : -1;
    updateInspector();
    if (m_selectedIndex >= 0) {
        const Channel &ch = m_core->channel(m_selectedIndex);
        if (m_view->selectionCount() > 1) {
            setHint(QStringLiteral("%1 sockets selected (%2 is primary). Drag, wheel or use the "
                                   "trim slider to move every selected drive together.")
                        .arg(m_view->selectionCount())
                        .arg(ch.coord));
        } else {
            setHint(QStringLiteral("Inspecting %1 (%2). Drag or wheel over the socket to trim a rod.")
                        .arg(ch.coord, assemblyNameQ(ch.type)));
        }
    }
}

void MainWindow::onSelectionChanged()
{
    m_selectedIndex = m_view->selectedIndex();
    updateInspector();
}

QVector<int> MainWindow::selectedChannels() const
{
    return m_view ? m_view->selection() : QVector<int>();
}

QVector<int> MainWindow::selectedRodChannels() const
{
    QVector<int> rods;
    for (int index : selectedChannels()) {
        if (m_core->isValidChannel(index) && m_core->channel(index).isControlRod())
            rods.append(index);
    }
    return rods;
}

void MainWindow::selectAllControlRods()
{
    m_view->setFocus(Qt::ShortcutFocusReason);
    m_view->selectAllControlRods();
}

void MainWindow::clearSelection()
{
    if (m_view->selectionCount() == 0) {
        setHint(QStringLiteral("Nothing is selected."));
        return;
    }
    m_view->clearMultiSelection();
    m_view->setSelectedIndex(-1);
    m_selectedIndex = -1;
    updateInspector();
    setHint(QStringLiteral("Selection cleared."));
}

void MainWindow::copySelection()
{
    const QVector<int> selection = selectedChannels();
    if (selection.isEmpty()) {
        setHint(QStringLiteral("Nothing selected - click a socket (Ctrl+click to group several)."));
        return;
    }

    QStringList rows;
    rows << QStringLiteral("coord\ttype\tinsertion %\ttarget %\ttemp degC\tflux e13\tbank");
    for (int index : selection) {
        const Channel &ch = m_core->channel(index);
        rows << QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7")
                    .arg(ch.coord, assemblyNameQ(ch.type))
                    .arg(ch.rodPosition, 0, 'f', 1)
                    .arg(ch.rodTarget, 0, 'f', 1)
                    .arg(ch.localTemp, 0, 'f', 0)
                    .arg(ch.localFlux, 0, 'f', 2)
                    .arg(ch.isControlRod() ? (ch.followBank ? QStringLiteral("bank")
                                                            : QStringLiteral("independent"))
                                           : QStringLiteral("-"));
    }
    if (QClipboard *clipboard = QApplication::clipboard())
        clipboard->setText(rows.join(QLatin1Char('\n')));
    setHint(QStringLiteral("Copied %1 channels to the clipboard.").arg(selection.size()));
}

void MainWindow::updateInspector()
{
    const QVector<int> selection = selectedChannels();
    const int count = selection.size();
    const bool valid = count > 0;

    int rodCount = 0;
    int fuelCount = 0;
    int reflectorCount = 0;
    int emptyCount = 0;
    int independentRods = 0;
    int withAssembly = 0;
    double maxTemp = 0.0;
    double maxFlux = 0.0;
    double targetSum = 0.0;
    double positionSum = 0.0;
    for (int index : selection) {
        if (!m_core->isValidChannel(index))
            continue;
        const Channel &ch = m_core->channel(index);
        if (ch.hasAssembly())
            ++withAssembly;
        switch (ch.type) {
        case AssemblyType::Control:
            ++rodCount;
            targetSum += ch.rodTarget;
            positionSum += ch.rodPosition;
            if (!ch.followBank)
                ++independentRods;
            break;
        case AssemblyType::Fuel:      ++fuelCount; break;
        case AssemblyType::Reflector: ++reflectorCount; break;
        case AssemblyType::Empty:     ++emptyCount; break;
        }
        maxTemp = qMax(maxTemp, ch.localTemp);
        maxFlux = qMax(maxFlux, ch.localFlux);
    }

    const bool hasAssembly = withAssembly > 0;
    const bool isRod = rodCount > 0;

    for (QPushButton *button : { m_installFuelButton, m_installRodButton,
                                 m_installReflectorButton })
        button->setEnabled(valid);
    m_extractButton->setEnabled(hasAssembly);
    m_extractButton->setText(count > 1
                                 ? QStringLiteral("Extract %1 selected assemblies").arg(count)
                                 : QStringLiteral("Extract selected assembly"));
    if (m_extractAction)
        m_extractAction->setEnabled(hasAssembly);
    if (m_installFuelAction)
        m_installFuelAction->setEnabled(valid);
    if (m_installRodAction)
        m_installRodAction->setEnabled(valid);
    if (m_installReflectorAction)
        m_installReflectorAction->setEnabled(valid);
    if (m_syncRodAction)
        m_syncRodAction->setEnabled(isRod);

    if (!valid) {
        m_inspCoord->setText(QStringLiteral("--"));
        m_inspType->setText(QStringLiteral("no channel selected"));
        m_inspTemp->setText(QStringLiteral("temp   --"));
        m_inspFlux->setText(QStringLiteral("flux   --"));
        m_inspDetail->setText(QStringLiteral("select a socket on the core map"));
        m_selectedLabel->setText(QStringLiteral("Selected: none (core aggregate view)"));
        m_rodSlider->setEnabled(false);
        m_syncRodButton->setEnabled(false);
        m_rodValue->setText(QStringLiteral("select a control rod"));
        m_rodActual->setText(QStringLiteral("no drive selected"));
        return;
    }

    const int primary = m_core->isValidChannel(m_view->selectedIndex()) ? m_view->selectedIndex()
                                                                       : selection.first();

    if (count > 1) {
        // -- group summary: what the batch actions below will touch
        QString types;
        const auto addType = [&types](int n, const QString &label) {
            if (n <= 0)
                return;
            if (!types.isEmpty())
                types += QStringLiteral("  ·  ");
            types += QStringLiteral("%1 %2").arg(n).arg(label);
        };
        addType(rodCount, QStringLiteral("rods"));
        addType(fuelCount, QStringLiteral("fuel"));
        addType(reflectorCount, QStringLiteral("reflector"));
        addType(emptyCount, QStringLiteral("empty"));

        m_inspCoord->setText(QStringLiteral("%1 sockets").arg(count));
        m_inspType->setText(types.isEmpty() ? QStringLiteral("empty sockets") : types);
        m_inspTemp->setText(QStringLiteral("temp   %1 degC max").arg(maxTemp, 0, 'f', 0));
        m_inspFlux->setText(QStringLiteral("flux   %1e13 max").arg(maxFlux, 0, 'f', 2));
        m_selectedLabel->setText(QStringLiteral("Selected: %1 sockets   ·   group actions apply to all")
                                     .arg(count));

        QString detail = QStringLiteral("%1 channels selected").arg(count);
        if (rodCount > 0) {
            detail += QStringLiteral("   ·   %1 drives   avg trim %2 %")
                          .arg(rodCount)
                          .arg(targetSum / rodCount, 0, 'f', 1);
        }
        m_inspDetail->setText(detail);
    } else {
        const Channel &ch = m_core->channel(primary);
        m_inspCoord->setText(ch.coord);
        m_inspType->setText(assemblyNameQ(ch.type));
        m_inspTemp->setText(QStringLiteral("temp   %1 degC").arg(ch.localTemp, 0, 'f', 0));
        m_inspFlux->setText(QStringLiteral("flux   %1e13").arg(ch.localFlux, 0, 'f', 2));
        m_selectedLabel->setText(QStringLiteral("Selected: %1  ·  %2")
                                     .arg(ch.coord, assemblyNameQ(ch.type)));

        QString detail;
        switch (ch.type) {
        case AssemblyType::Control:
            detail = QStringLiteral("insertion %1 %   ·   worth %2 $   ·   %3")
                         .arg(ch.rodPosition, 0, 'f', 1)
                         .arg(ReactorCore::rodWorthDollars(ch.rodWorth, ch.rodPosition), 0, 'f', 3)
                         .arg(ch.followBank ? QStringLiteral("follows the bank")
                                            : QStringLiteral("independent trim"));
            break;
        case AssemblyType::Fuel:
            detail = QStringLiteral("burnup %1 %   ·   local xenon %2 rel")
                         .arg(ch.burnup * 100.0, 0, 'f', 3)
                         .arg(ch.localXenon, 0, 'f', 2);
            break;
        case AssemblyType::Reflector:
            detail = QStringLiteral("water column - reflects neutrons back into the core");
            break;
        case AssemblyType::Empty:
            detail = QStringLiteral("empty socket - install fuel, a control rod or a reflector");
            break;
        }
        m_inspDetail->setText(detail);
    }

    if (isRod) {
        const double avgTarget = targetSum / rodCount;
        const double avgPosition = positionSum / rodCount;
        m_rodSlider->setEnabled(true);
        m_syncRodButton->setEnabled(independentRods > 0);
        const QSignalBlocker blocker(m_rodSlider);
        m_rodSlider->setValue(int(std::lround((count > 1 ? avgTarget
                                                        : m_core->channel(primary).rodTarget) * 10.0)));
        if (rodCount > 1) {
            m_rodValue->setText(QStringLiteral("%1 rods").arg(rodCount));
            m_rodActual->setText(QStringLiteral("group trim   ·   avg target %1 %   ·   avg actual %2 %")
                                     .arg(avgTarget, 0, 'f', 1)
                                     .arg(avgPosition, 0, 'f', 1));
        } else {
            const Channel &rod = m_core->channel(primary);
            m_rodValue->setText(QStringLiteral("%1 %").arg(rod.rodPosition, 0, 'f', 1));
            m_rodActual->setText(QStringLiteral("actual %1 %   ·   target %2 %   ·   %3")
                                     .arg(rod.rodPosition, 0, 'f', 1)
                                     .arg(rod.rodTarget, 0, 'f', 1)
                                     .arg(rod.followBank ? QStringLiteral("attached to bank")
                                                         : QStringLiteral("independent")));
        }
    } else {
        m_rodSlider->setEnabled(false);
        m_syncRodButton->setEnabled(false);
        m_rodValue->setText(QStringLiteral("select a control rod"));
        m_rodActual->setText(QStringLiteral("no drive selected"));
    }
}

void MainWindow::installAssembly(AssemblyType type)
{
    const QVector<int> targets = selectedChannels();
    if (targets.isEmpty()) {
        setHint(QStringLiteral("Select a channel inside the shield first (Ctrl+click groups them)."));
        return;
    }
    int done = 0;
    for (int index : targets) {
        if (!m_core->isValidChannel(index))
            continue;
        m_core->insertAssembly(index, type);
        ++done;
    }
    if (done == 1) {
        setHint(QStringLiteral("%1 loaded into %2.")
                    .arg(assemblyNameQ(type), m_core->channel(targets.first()).coord));
    } else {
        setHint(QStringLiteral("%1 loaded into %2 sockets.").arg(assemblyNameQ(type)).arg(done));
    }
}

void MainWindow::onExtractClicked()
{
    const QVector<int> targets = selectedChannels();
    if (targets.isEmpty()) {
        setHint(QStringLiteral("Select a channel inside the shield first (Ctrl+click groups them)."));
        return;
    }
    QStringList coords;
    int done = 0;
    for (int index : targets) {
        if (!m_core->isValidChannel(index) || !m_core->channel(index).hasAssembly())
            continue;
        if (coords.size() < 6)
            coords << m_core->channel(index).coord;
        m_core->extractAssembly(index);
        ++done;
    }
    if (done == 0) {
        setHint(QStringLiteral("Nothing to extract - the selected sockets are already empty."));
        return;
    }
    if (done == 1) {
        setHint(QStringLiteral("Assembly extracted from %1.").arg(coords.first()));
    } else {
        setHint(QStringLiteral("%1 assemblies extracted (%2).")
                    .arg(done)
                    .arg(coords.join(QStringLiteral(", "))));
    }
}

void MainWindow::onSyncRodToBank()
{
    const QVector<int> rods = selectedRodChannels();
    if (rods.isEmpty()) {
        setHint(QStringLiteral("No control rod drive is selected."));
        return;
    }
    for (int index : rods)
        m_core->setRodTarget(index, m_core->bankTarget(), false);
    if (rods.size() == 1) {
        setHint(QStringLiteral("%1: drive re-attached to the bank set point.")
                    .arg(m_core->channel(rods.first()).coord));
    } else {
        setHint(QStringLiteral("%1 drives re-attached to the bank set point (%2 %).")
                    .arg(rods.size())
                    .arg(m_core->bankTarget(), 0, 'f', 0));
    }
    updateInspector();
}

// -------------------------------------------------------------------- actions
void MainWindow::onBankSliderChanged(int value)
{
    if (m_updatingUi)
        return;
    m_core->setBankTarget(value / 10.0, false);
}

void MainWindow::onRodSliderChanged(int value)
{
    if (m_updatingUi)
        return;
    const QVector<int> rods = selectedRodChannels();
    if (rods.isEmpty())
        return;
    const double target = value / 10.0;
    for (int index : rods)
        m_core->setRodTarget(index, target, true);
}

void MainWindow::onAutoHoldToggled()
{
    if (m_updatingUi || !m_autoHoldButton || !m_core)
        return;
    if (m_autoHoldButton->isChecked()) {
        m_core->setPowerTarget(m_powerTargetSpin->value());
        setHint(QStringLiteral("AUTO HOLD engaged: the bank is being trimmed towards %1 MWt.")
                    .arg(m_powerTargetSpin->value(), 0, 'f', 0));
    } else {
        m_core->clearPowerTarget();
        setHint(QStringLiteral("AUTO HOLD released: the rod bank is yours again."));
    }
}

void MainWindow::onPowerTargetEdited(double value)
{
    if (m_updatingUi || !m_core)
        return;
    if (!m_core->autoHoldEngaged())
        return; // value is staged; the AUTO HOLD button engages it
    m_core->setPowerTarget(value);
    setHint(QStringLiteral("AUTO HOLD: new target %1 MWt - recalculating the rod bank.")
                .arg(value, 0, 'f', 0));
}

void MainWindow::onPauseToggled()
{
    const bool next = !m_core->isPaused();
    m_core->setPaused(next);
    m_pauseButton->setText(next ? QStringLiteral("Resume") : QStringLiteral("Pause"));
    if (m_pauseAction) {
        const QSignalBlocker blocker(m_pauseAction);
        m_pauseAction->setChecked(next);
    }
}

void MainWindow::onSoundToggled()
{
    m_soundEnabled = !m_soundEnabled;
    m_soundButton->setText(m_soundEnabled ? QStringLiteral("Sound ON")
                                          : QStringLiteral("Sound OFF"));
    if (m_soundEnabled)
        beep(1);
}

void MainWindow::onLoadPreset()
{
    const CorePreset preset = static_cast<CorePreset>(m_presetCombo->currentData().toInt());
    m_core->loadPreset(preset); // this releases the controller, see below
    {
        const QSignalBlocker blocker(m_powerTargetSpin);
        m_powerTargetSpin->setValue(presetPower(preset));
    }
    if (preset == CorePreset::ColdShutdown) {
        m_core->clearPowerTarget();
        setHint(QStringLiteral("Core rebuilt: %1. AUTO HOLD is off on a cold core.")
                    .arg(presetName(preset)));
    } else {
        m_core->setPowerTarget(presetPower(preset));
        setHint(QStringLiteral("Core rebuilt: %1. AUTO HOLD engaged at %2 MWt.")
                    .arg(presetName(preset))
                    .arg(presetPower(preset), 0, 'f', 0));
    }
}

void MainWindow::onTimeScaleChanged(int index)
{
    const double scale = m_timeScaleCombo->itemData(index).toDouble();
    m_core->setTimeScale(scale);
}

void MainWindow::sampleTrend()
{
    if (!m_core)
        return;
    const Telemetry &t = m_core->telemetry();
    m_chart->addSample(t.simSeconds, t.powerMWt, t.fuelTempC, t.xenon);
}

void MainWindow::setHint(const QString &text)
{
    statusBar()->showMessage(text, 8000);
}

void MainWindow::beep(int times)
{
    if (!m_soundEnabled || times <= 0)
        return;
    QApplication::beep();
    for (int i = 1; i < times; ++i)
        QTimer::singleShot(190 * i, this, [] { QApplication::beep(); });
}

// ------------------------------------------------------------------ event log
void MainWindow::onEventLogged(const LogEntry &entry)
{
    appendLog(entry);
}

void MainWindow::appendLog(const LogEntry &entry)
{
    if (!m_log)
        return;
    QColor color;
    switch (entry.severity) {
    case 2: color = theme::danger(); break;
    case 1: color = theme::warning(); break;
    case 3: color = theme::accent(); break;
    default: color = theme::dim(); break;
    }
    const QString html = QStringLiteral("<span style='color:#475569'>[%1]</span> "
                                        "<span style='color:%2'>%3</span>")
                             .arg(entry.timestamp, color.name(), entry.text.toHtmlEscaped());
    m_log->appendHtml(html);
    m_log->moveCursor(QTextCursor::End);
}

void MainWindow::onAlarmRaised(int level, const QString &text)
{
    Q_UNUSED(text);
    if (level > m_lastAlarmLevel)
        beep(level >= 2 ? 3 : 1);
    m_lastAlarmLevel = level;
    updateStatusBadge();
}

void MainWindow::onCoreDestroyed()
{
    beep(4);
    setHint(QStringLiteral("CORE DESTROYED — rebuild the core with Ctrl+R or the Rebuild button."));
    updateStatusBadge();
}

void MainWindow::updateStatusBadge()
{
    const Telemetry &t = m_core->telemetry();
    const QColor color = theme::alarmColor(t.alarmLevel);
    m_statusBadge->setText(t.statusText.toUpper());
    m_statusBadge->setStyleSheet(
        QStringLiteral("QLabel#StatusBadge { background-color: rgba(%1,%2,%3,0.18); "
                       "color: %4; border: 1px solid %4; }")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(color.name()));
    m_statusDetail->setText(t.statusDetail);
    m_statusDetail->setToolTip(t.statusDetail);
}

// --------------------------------------------------------------- context menu
void MainWindow::onContextMenu(int index, const QPoint &globalPos)
{
    showContextMenu(index, globalPos);
}

void MainWindow::showContextMenu(int index, const QPoint &globalPos)
{
    if (!m_core->isValidChannel(index))
        return;
    selectChannel(index);
    const Channel &ch = m_core->channel(index);

    // A right-click inside a multi-selection acts on the whole group.
    const bool group = m_view->selectionCount() > 1 && m_view->isSelected(index);
    QVector<int> targets = group ? selectedChannels() : QVector<int>{ index };
    int rods = 0;
    int loaded = 0;
    for (int target : targets) {
        if (!m_core->isValidChannel(target))
            continue;
        if (m_core->channel(target).isControlRod())
            ++rods;
        if (m_core->channel(target).hasAssembly())
            ++loaded;
    }

    QMenu menu(this);

    auto *header = menu.addAction(group ? QStringLiteral("%1 sockets   ·   %2 rods")
                                              .arg(targets.size())
                                              .arg(rods)
                                        : QStringLiteral("%1  ·  %2")
                                              .arg(ch.coord, assemblyNameQ(ch.type)));
    header->setEnabled(false);
    menu.addSeparator();

    if (loaded > 0) {
        auto *extract = menu.addAction(group ? QStringLiteral("Extract %1 assemblies").arg(loaded)
                                             : QStringLiteral("Extract assembly"));
        connect(extract, &QAction::triggered, this, [this, targets] {
            for (int target : targets) {
                if (m_core->isValidChannel(target) && m_core->channel(target).hasAssembly())
                    m_core->extractAssembly(target);
            }
        });
        menu.addSeparator();
    }

    auto *install = menu.addMenu(group ? QStringLiteral("Install into %1 sockets").arg(targets.size())
                                       : QStringLiteral("Install / replace with"));
    const struct
    {
        QString text;
        AssemblyType type;
    } items[3] = {
        { QStringLiteral("Fuel assembly"), AssemblyType::Fuel },
        { QStringLiteral("Control rod drive"), AssemblyType::Control },
        { QStringLiteral("Water reflector"), AssemblyType::Reflector },
    };
    for (const auto &item : items) {
        auto *action = install->addAction(item.text);
        connect(action, &QAction::triggered, this, [this, targets, type = item.type] {
            for (int target : targets) {
                if (m_core->isValidChannel(target))
                    m_core->insertAssembly(target, type);
            }
        });
    }

    QVector<int> rodTargets;
    for (int target : targets) {
        if (m_core->isValidChannel(target) && m_core->channel(target).isControlRod())
            rodTargets.append(target);
    }

    if (!rodTargets.isEmpty()) {
        menu.addSeparator();
        auto *rodMenu = menu.addMenu(rodTargets.size() > 1
                                         ? QStringLiteral("Rod drives (%1 selected)")
                                               .arg(rodTargets.size())
                                         : QStringLiteral("Rod drive %1 %")
                                               .arg(ch.rodPosition, 0, 'f', 0));
        const struct
        {
            QString text;
            double pct;
        } positions[5] = {
            { QStringLiteral("Withdraw fully (0 %)"), 0.0 },
            { QStringLiteral("Set 25 %"), 25.0 },
            { QStringLiteral("Set 50 %"), 50.0 },
            { QStringLiteral("Set 75 %"), 75.0 },
            { QStringLiteral("Insert fully (100 %)"), 100.0 },
        };
        for (const auto &pos : positions) {
            auto *action = rodMenu->addAction(pos.text);
            connect(action, &QAction::triggered, this, [this, rodTargets, pct = pos.pct] {
                for (int target : rodTargets)
                    m_core->setRodTarget(target, pct, true);
            });
        }
        rodMenu->addSeparator();
        auto *trimOut = rodMenu->addAction(QStringLiteral("Trim -5 % (withdraw)"));
        connect(trimOut, &QAction::triggered, this, [this, rodTargets] {
            for (int target : rodTargets)
                m_core->nudgeRod(target, -5.0);
        });
        auto *trimIn = rodMenu->addAction(QStringLiteral("Trim +5 % (insert)"));
        connect(trimIn, &QAction::triggered, this, [this, rodTargets] {
            for (int target : rodTargets)
                m_core->nudgeRod(target, 5.0);
        });
        rodMenu->addSeparator();
        auto *attach = rodMenu->addAction(QStringLiteral("Attach to bank (%1 %)")
                                              .arg(m_core->bankTarget(), 0, 'f', 0));
        connect(attach, &QAction::triggered, this, [this, rodTargets] {
            for (int target : rodTargets)
                m_core->setRodTarget(target, m_core->bankTarget(), false);
        });
    }

    menu.exec(globalPos);
}

// ------------------------------------------------------------------ UI refresh
void MainWindow::refreshUi()
{
    if (!m_core || !m_chart || !m_statusBadge)
        return;

    const Telemetry &t = m_core->telemetry();
    m_updatingUi = true;

    // --- header -------------------------------------------------------------
    const int total = static_cast<int>(t.simSeconds);
    m_clockLabel->setText(QStringLiteral("%1:%2:%3")
                              .arg(total / 3600, 2, 10, QLatin1Char('0'))
                              .arg((total % 3600) / 60, 2, 10, QLatin1Char('0'))
                              .arg(total % 60, 2, 10, QLatin1Char('0')));
    m_headerXenon->setText(QString::number(t.xenon, 'f', 2));

    if (m_core->preset() != m_shownPreset) {
        m_shownPreset = m_core->preset();
        const QSignalBlocker blocker(m_presetCombo);
        const int index = m_presetCombo->findData(int(m_shownPreset));
        if (index >= 0)
            m_presetCombo->setCurrentIndex(index);
    }

    if (!qFuzzyCompare(m_core->timeScale(), m_shownTimeScale)) {
        m_shownTimeScale = m_core->timeScale();
        const QSignalBlocker blocker(m_timeScaleCombo);
        const int index = m_timeScaleCombo->findData(m_shownTimeScale);
        if (index >= 0)
            m_timeScaleCombo->setCurrentIndex(index);
    }

    // --- power hold controller ---------------------------------------------
    {
        const bool engaged = t.autoHold;
        if (m_autoHoldButton->isChecked() != engaged) {
            const QSignalBlocker blocker(m_autoHoldButton);
            m_autoHoldButton->setChecked(engaged);
        }
        m_autoHoldButton->setText(engaged ? QStringLiteral("AUTO HOLD ON")
                                          : QStringLiteral("AUTO HOLD"));
        if (engaged && !m_powerTargetSpin->hasFocus()) {
            const QSignalBlocker blocker(m_powerTargetSpin);
            m_powerTargetSpin->setValue(m_core->powerTarget());
        }
        if (m_autoHoldAction && m_autoHoldAction->isChecked() != engaged) {
            const QSignalBlocker blocker(m_autoHoldAction);
            m_autoHoldAction->setChecked(engaged);
        }
        if (m_autoStatusLabel) {
            m_autoStatusLabel->setText(t.autoHoldStatus);
            QColor color = theme::dim();
            switch (t.autoHoldState) {
            case int(AutoHoldState::Holding):    color = theme::success(); break;
            case int(AutoHoldState::Raising):
            case int(AutoHoldState::Lowering):   color = theme::accent(); break;
            case int(AutoHoldState::RodWorthLimit):
            case int(AutoHoldState::ReactivityLimit):
            case int(AutoHoldState::NoDrives):   color = theme::warning(); break;
            default:                             break;
            }
            m_autoStatusLabel->setStyleSheet(QStringLiteral("color:%1; font-weight:bold;")
                                                 .arg(color.name()));
        }
    }

    // --- gauges -------------------------------------------------------------
    m_powerGauge->setReading(QString::number(t.powerMWt, 'f', 0),
                             t.powerMWt / 3200.0,
                             theme::accent(),
                             QStringLiteral("%1 MWe").arg(t.electricMW, 0, 'f', 0));

    m_fuelGauge->setReading(QString::number(t.fuelTempC, 'f', 0),
                            t.fuelTempC / 1500.0,
                            theme::temperatureColor(t.fuelTempC),
                            QStringLiteral("integrity %1 %").arg(t.integrity * 100.0, 0, 'f', 1));

    const QColor coolantColor = t.coolantTempC > 340.0 ? theme::warning() : theme::cyan();
    m_coolantGauge->setReading(QString::number(t.coolantTempC, 'f', 0),
                               t.coolantTempC / 500.0,
                               coolantColor,
                               t.reliefOpen ? QStringLiteral("relief open")
                                            : QStringLiteral("inlet %1 degC")
                                                  .arg(t.inletTempC, 0, 'f', 0));

    const QColor pressureColor = t.pressureAtm > 112.0 ? theme::danger()
                                                      : (t.pressureAtm > 100.0 ? theme::warning()
                                                                               : theme::accent());
    m_pressureGauge->setReading(QString::number(t.pressureAtm, 'f', 0),
                                t.pressureAtm / 160.0,
                                pressureColor,
                                QStringLiteral("relief 118 atm"));

    int runningPumps = 0;
    for (int i = 0; i < 4; ++i)
        runningPumps += t.pumps[i] ? 1 : 0;
    const QColor flowColor = t.flowPct < 50.0 ? theme::danger()
                                             : (t.flowPct < 90.0 ? theme::warning()
                                                                 : theme::success());
    m_flowGauge->setReading(QString::number(t.flowPct, 'f', 0),
                            t.flowPct / 100.0,
                            flowColor,
                            QStringLiteral("%1/4 pumps").arg(runningPumps));

    const double rho = t.reactivityDollars;
    const QColor rhoColor = rho > 0.5 ? theme::danger()
                                      : (rho > 0.05 ? theme::warning()
                                                    : (rho < -0.05 ? theme::success()
                                                                   : theme::accent()));
    m_reactivityGauge->setReading(QStringLiteral("%1%2")
                                      .arg(rho >= 0.0 ? QStringLiteral("+") : QString())
                                      .arg(rho, 0, 'f', 3),
                                  (rho + 4.0) / 8.0,
                                  rhoColor,
                                  QStringLiteral("%1 pcm").arg(t.reactivityPcm, 0, 'f', 0));

    m_fluxGauge->setReading(QStringLiteral("%1e13").arg(t.flux / 1.0e13, 0, 'f', 2),
                            t.flux / 5.0e13,
                            theme::purple(),
                            QStringLiteral("nominal 1.2e13"));

    m_xenonGauge->setReading(QString::number(t.xenon, 'f', 2),
                             t.xenon / 3.0,
                             theme::purple(),
                             QStringLiteral("%1 $").arg(t.xenonDollars, 0, 'f', 2));

    const QColor ormColor = t.orm < 6.0 ? theme::danger()
                                        : (t.orm < 10.0 ? theme::warning() : theme::success());
    m_ormGauge->setReading(QString::number(t.orm, 'f', 1),
                           t.orm / 30.0,
                           ormColor,
                           QStringLiteral("limit 10 rods"));

    QString periodText;
    QColor periodColor = theme::success();
    if (!std::isfinite(t.periodSeconds) || std::fabs(t.periodSeconds) > 1.0e6) {
        periodText = QStringLiteral("stable");
        periodColor = theme::accent();
    } else {
        periodText = QStringLiteral("%1%2")
                         .arg(t.periodSeconds >= 0.0 ? QStringLiteral("+") : QString())
                         .arg(t.periodSeconds, 0, 'f', 1);
        periodColor = t.periodSeconds > 0.0
                          ? (t.periodSeconds < 20.0 ? theme::danger() : theme::warning())
                          : theme::success();
    }
    m_periodGauge->setReading(periodText,
                              std::fabs(t.periodSeconds) > 1.0e6
                                  ? 0.0
                                  : 30.0 / (30.0 + std::fabs(t.periodSeconds)),
                              periodColor,
                              QStringLiteral("positive = rising"));

    // --- rod bank -----------------------------------------------------------
    m_bankValue->setText(QStringLiteral("%1 %").arg(t.bankTarget, 0, 'f', 1));
    {
        const QSignalBlocker blocker(m_bankSlider);
        m_bankSlider->setValue(int(std::lround(t.bankTarget * 10.0)));
    }
    int independent = 0;
    for (const Channel &ch : m_core->channels()) {
        if (ch.isControlRod() && !ch.followBank)
            ++independent;
    }
    m_bankAverage->setText(QStringLiteral("%1 drives  ·  avg %2 %  ·  %3 indep  ·  ORM %4")
                               .arg(t.controlChannels)
                               .arg(t.bankAverage, 0, 'f', 1)
                               .arg(independent)
                               .arg(t.orm, 0, 'f', 1));

    // --- pumps --------------------------------------------------------------
    m_pumpStatus->setText(QStringLiteral("%1/4 pumps  ·  flow %2 %")
                              .arg(runningPumps)
                              .arg(t.flowPct, 0, 'f', 0));
    for (int i = 0; i < 4; ++i) {
        const QSignalBlocker blocker(m_pumpButtons[i]);
        m_pumpButtons[i]->setChecked(t.pumpCommand[i]);
        m_pumpButtons[i]->setText(QStringLiteral("MCP-%1\n%2")
                                      .arg(i + 1)
                                      .arg(t.pumpCommand[i] ? QStringLiteral("ONLINE")
                                                            : QStringLiteral("OFFLINE")));
    }

    m_pauseButton->setText(t.paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));

    m_inventoryLabel->setText(QStringLiteral("FUEL %1   ·   CONTROL %2   ·   REFLECTOR %3"
                                             "   ·   EMPTY %4")
                                  .arg(t.fuelChannels)
                                  .arg(t.controlChannels)
                                  .arg(t.reflectorChannels)
                                  .arg(t.emptyChannels));
    m_sessionLabel->setText(QStringLiteral("ENERGY %1 MWh   ·   BURNUP %2 %   ·   "
                                           "INTEGRITY %3 %   ·   CHANNELS %4")
                                .arg(t.energyMWh, 0, 'f', 2)
                                .arg(t.burnup * 100.0, 0, 'f', 3)
                                .arg(t.integrity * 100.0, 0, 'f', 1)
                                .arg(t.totalChannels));

    updateStatusBadge();
    updateInspector();
    m_view->update();

    m_updatingUi = false;
}

// --------------------------------------------------------------------- help
void MainWindow::showManual()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Operating manual — RBMK-1000 ELENA sandbox"));
    dialog.resize(860, 700);

    auto *layout = new QVBoxLayout(&dialog);
    auto *browser = new QTextBrowser(&dialog);
    browser->setOpenExternalLinks(false);

    QString html;
    html += QStringLiteral(R"HTML(
<style>
  body { color:#cbd5e1; font-family:'Consolas','Courier New',monospace; font-size:12px; }
  h2 { color:#38bdf8; font-size:15px; letter-spacing:1px; margin-bottom:2px; }
  h3 { color:#f59e0b; font-size:12px; letter-spacing:1px; margin-top:14px; margin-bottom:2px; }
  code { color:#67e8f9; }
  li { margin-bottom:4px; }
  .warn { color:#ef4444; }
  .note { color:#64748b; }
</style>
<h2>RBMK-1000 ELENA — CORE PHYSICS SANDBOX</h2>
<p>There are no missions, objectives or scripted faults. The reactor is a physical
system: whatever happens is the consequence of what you do to it. Every one of the
248 channels can be emptied or reloaded, and every control rod drive can be moved
on its own.</p>

<h3>THE CORE MAP</h3>
<ul>
<li><b>Left click</b> a socket to inspect it.</li>
<li><b>Ctrl + click</b> adds or removes sockets from a <b>multi-selection</b>; the cyan ring marks
    every member and the amber ring the primary. <b>Shift + click</b> selects the whole rectangle
    between the anchor and that socket, <b>Ctrl+A</b> selects every installed rod drive and
    <b>Esc</b> clears the group.</li>
<li>With several drives selected, <b>drag</b>, the <b>mouse wheel</b>, <b>+ / -</b>, the
    <b>trim slider</b> and the <b>right-click rod menu</b> move the whole group together: each
    drive keeps its own starting point and every one of them detaches from the bank.</li>
<li>Drag a control rod socket up/down to withdraw/insert that single drive.</li>
<li><b>Mouse wheel over a control rod</b> trims it by 3 %. Over empty space or fuel it zooms.</li>
<li><b>Ctrl + wheel</b> zooms anywhere; double-click empty space resets the view.</li>
<li><b>Right click</b> a socket to install or extract assemblies and to preset a rod drive; inside
    a group the menu acts on all selected sockets.</li>
<li><b>Arrow keys</b> walk the selection, <b>+ / -</b> trim the selected rods.</li>
</ul>

<h3>ASSEMBLIES (all insertable and removable)</h3>
<ul>
<li><b>Fuel bundle</b> — contributes +7 $ of excess reactivity across the as-built load
    (180 channels). Removing fuel lowers reactivity, adding fuel raises it.</li>
<li><b>Control rod drive</b> — 8.5 $ total worth across the as-built 30 drives, weighted
    radially (centre rods are worth more). Worth follows an S-curve, so a rod at 50 %
    is worth half of its full stroke. Each drive travels at 14 %/s.</li>
<li><b>Water reflector</b> — small positive worth (+0.008 $ per column versus as-built).</li>
<li><b>Empty channel</b> — no absorption and no fuel. Legal, and sometimes fatal.</li>
</ul>
)HTML");

    html += QStringLiteral(R"HTML(
<h3>REACTIVITY BALANCE</h3>
<p>The displayed reactivity is in dollars (1 $ = beta_eff = 0.0065):</p>
<ul>
<li>fuel inventory (+7 $ at the as-built count)</li>
<li>control rod worth (up to -8.5 $)</li>
<li>xenon poisoning (-3 $ at equilibrium)</li>
<li>fuel temperature feedback (negative, -0.0022 $/degC above 300 degC)</li>
<li>void fraction (positive, +1.4 $ when fully voided) — losing flow adds reactivity</li>
<li>burnup (small negative term)</li>
</ul>
<p class="warn">At or above +1.00 $ the core is prompt critical: the neutron population
grows on the prompt lifetime and the power excursion is essentially instantaneous.</p>

<h3>ROD BANK vs. INDIVIDUAL DRIVES</h3>
<p>The bank slider re-attaches and moves every installed drive together. Moving an
individual rod (drag, wheel, slider, context menu) detaches that drive from the bank
until you move the bank again or press <b>Attach to bank</b>. The <b>ORM</b> read-out is
the operating reactivity margin in equivalent fully-inserted rods; below 10 is a
warning and below 6 is critical. Pulling rods out physically reduces it.</p>

<h3>AUTOMATIC POWER HOLD</h3>
<p>Set the megawatt you want in <b>POWER TARGET</b> and press <b>AUTO HOLD</b> (Ctrl+H).
Every physics step the controller adds a small reactivity bias proportional to the
logarithm of the power error, then solves the rod-worth curve for the bank position
that produces exactly that bias on the core as it is measured right now — xenon, void,
fuel temperature, burnup and pump state included. Because the bias goes to zero as the
power reaches the target, the core settles genuinely critical on the requested
megawatt instead of hunting around it, and it keeps re-solving as xenon drifts, so the
requested power is <i>held</i>, not just reached.</p>
<ul>
<li>The controller only moves drives that are attached to the bank, through the normal
    14 %/s drive speed, so it cannot insert worth faster than a human could. Drives you
    trimmed by hand stay where you put them and are accounted for in the solve.</li>
<li>Moving the bank by hand releases the controller at once. Loading a core state
    re-targets it at that state's nominal power; a cold core loads with AUTO HOLD off.</li>
<li>AZ-5 also releases it, and the controller refuses to engage while a scram is still
    dropping the rods: a scram always wins.</li>
<li>If the target is out of reach the status line says so — either every drive is
    inserted and the core is still too reactive, or every drive is withdrawn and xenon
    and void hold the core down. The bank stays parked at the limit until things change.</li>
<li><b>Ctrl+Shift+H</b> holds whatever power the reactor is at right now.</li>
</ul>

<h3>COOLING AND VOID</h3>
<p>Four main circulation pumps each supply 25 % flow (about a 2.5 s ramp). Flow
removes heat; losing flow raises the void fraction, which adds positive reactivity on
this core, so a pump trip is not just a thermal event. Pressure follows coolant
temperature and flow: a relief valve opens at 118 atm, and above 140 atm the primary
circuit can rupture (coolant inventory lost, residual flow only).</p>

<h3>XENON</h3>
<p>Iodine and xenon are integrated continuously. After a power reduction xenon keeps
building (the iodine inventory is still decaying) and poisons the core for a while —
you must withdraw rods to hold power, which costs shutdown margin. Accelerate time to
watch it happen.</p>
)HTML");

    html += QStringLiteral(R"HTML(
<h3>PROTECTION AND DAMAGE</h3>
<ul>
<li><b>AZ-5</b> drops all installed drives to 100 % at 42 %/s.</li>
<li>With ORM below 10 the AZ-5 graphite displacers add a positive reactivity spike
    (+0.35 to +0.9 $) before the absorber bites — the historical positive scram effect.</li>
<li>Above 900 degC fuel damage accumulates; above 1500 degC the core is destroyed.
    A destroyed core cannot produce power until you rebuild it (Ctrl+R).</li>
</ul>

<h3>KEYBOARD</h3>
<ul>
<li><code>F2</code> AZ-5 scram &nbsp;·&nbsp; <code>Ctrl+R</code> rebuild core</li>
<li><code>P</code> pause &nbsp;·&nbsp; <code>Ctrl+Up/Down</code> bank -5/+5 %</li>
<li><code>Ctrl+H</code> automatic power hold &nbsp;·&nbsp; <code>Ctrl+Shift+H</code> hold the
    present power</li>
<li><code>Ctrl+A</code> select all rod drives &nbsp;·&nbsp; <code>Esc</code> clear the group
    &nbsp;·&nbsp; <code>Ctrl+C</code> copy the selection to the clipboard</li>
<li><code>Ctrl+1/2/3</code> install fuel / rod / reflector in the selected socket(s)</li>
<li><code>Ctrl+E</code> extract the selected assembly &nbsp;·&nbsp; <code>Ctrl+B</code> attach rod to bank</li>
<li><code>F1</code> this manual</li>
</ul>

<h3>MODEL NOTES</h3>
<p class="note">This is an educational simulation, not an operating procedure or a
validated plant model. Values are game-tuned around plausible RBMK behaviour:
point kinetics reduced to the one-group inhour root, lumped two-node thermal
hydraulics, simplified iodine/xenon dynamics, and a radially weighted rod lattice.
Numbers are internally consistent and stable, but they are not a substitute for a
real reactor physics code.</p>
)HTML");

    browser->setHtml(html);
    layout->addWidget(browser, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, QStringLiteral("About RBMK-1000 ELENA"),
                       QStringLiteral("<h3>RBMK-1000 ELENA — core physics sandbox</h3>"
                                      "<p>A Qt 6 / C++17 control-room simulator with "
                                      "individually removable assemblies and independently "
                                      "driven control rods.</p>"
                                      "<p>Built with Qt %1 and a hand-written reactor physics "
                                      "engine (point kinetics, xenon dynamics, void feedback, "
                                      "two-node thermal hydraulics).</p>"
                                      "<p style='color:#64748b'>Educational use only — not a "
                                      "real plant model.</p>")
                           .arg(QString::fromLatin1(qVersion())));
}
