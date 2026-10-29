#pragma once

#include "core/ReactorCore.h"

#include <QMainWindow>
#include <QVector>

class QAction;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QTimer;
class CoreView;
class GaugeCard;
class TrendChart;

/*! The control room: core map, telemetry, rod bank, pumps, channel bay, log. */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void startSimulation(rbmk::CorePreset preset = rbmk::CorePreset::NominalHot);

    rbmk::ReactorCore *core() const { return m_core; }
    CoreView *coreView() const { return m_view; }
    void selectChannel(int index);
    void selectAllControlRods();
    void clearSelection();
    void copySelection();

private slots:
    void refreshUi();
    void onChannelSelected(int index);
    void onSelectionChanged();
    void onContextMenu(int index, const QPoint &globalPos);
    void onEventLogged(const rbmk::LogEntry &entry);
    void onAlarmRaised(int level, const QString &text);
    void onCoreDestroyed();
    void onPauseToggled();
    void onSoundToggled();
    void onLoadPreset();
    void onTimeScaleChanged(int index);
    void onBankSliderChanged(int value);
    void onRodSliderChanged(int value);
    void onAutoHoldToggled();
    void onPowerTargetEdited(double value);
    void onExtractClicked();
    void onSyncRodToBank();
    void sampleTrend();
    void showManual();
    void showAbout();

private:
    void buildUi();
    void buildMenus();
    QWidget *buildHeader();
    QWidget *buildCorePanel();
    QWidget *buildTelemetryCard();
    QWidget *buildBankCard();
    QWidget *buildPumpCard();
    QWidget *buildChannelCard();
    QWidget *buildLogDock();
    void installAssembly(rbmk::AssemblyType type);
    QVector<int> selectedChannels() const;
    QVector<int> selectedRodChannels() const;
    void setHint(const QString &text);
    void appendLog(const rbmk::LogEntry &entry);
    void beep(int times = 1);
    void updateStatusBadge();
    void updateInspector();
    void showContextMenu(int index, const QPoint &globalPos);

    rbmk::ReactorCore *m_core = nullptr;
    CoreView *m_view = nullptr;
    TrendChart *m_chart = nullptr;

    GaugeCard *m_powerGauge = nullptr;
    GaugeCard *m_fuelGauge = nullptr;
    GaugeCard *m_coolantGauge = nullptr;
    GaugeCard *m_pressureGauge = nullptr;
    GaugeCard *m_flowGauge = nullptr;
    GaugeCard *m_reactivityGauge = nullptr;
    GaugeCard *m_fluxGauge = nullptr;
    GaugeCard *m_xenonGauge = nullptr;
    GaugeCard *m_ormGauge = nullptr;
    GaugeCard *m_periodGauge = nullptr;

    QLabel *m_statusBadge = nullptr;
    QLabel *m_statusDetail = nullptr;
    QLabel *m_clockLabel = nullptr;
    QLabel *m_headerXenon = nullptr;
    QLabel *m_selectedLabel = nullptr;
    QLabel *m_bankValue = nullptr;
    QLabel *m_bankAverage = nullptr;
    QLabel *m_pumpStatus = nullptr;

    QLabel *m_inspCoord = nullptr;
    QLabel *m_inspType = nullptr;
    QLabel *m_inspTemp = nullptr;
    QLabel *m_inspFlux = nullptr;
    QLabel *m_inspDetail = nullptr;
    QLabel *m_rodValue = nullptr;
    QLabel *m_rodActual = nullptr;
    QLabel *m_inventoryLabel = nullptr;
    QLabel *m_sessionLabel = nullptr;

    QSlider *m_bankSlider = nullptr;
    QSlider *m_rodSlider = nullptr;
    QPushButton *m_pumpButtons[4] = { nullptr, nullptr, nullptr, nullptr };
    QPushButton *m_pauseButton = nullptr;
    QPushButton *m_soundButton = nullptr;
    QPushButton *m_extractButton = nullptr;
    QPushButton *m_installFuelButton = nullptr;
    QPushButton *m_installRodButton = nullptr;
    QPushButton *m_installReflectorButton = nullptr;
    QPushButton *m_syncRodButton = nullptr;

    QComboBox *m_presetCombo = nullptr;
    QComboBox *m_timeScaleCombo = nullptr;
    QDoubleSpinBox *m_powerTargetSpin = nullptr;
    QPushButton *m_autoHoldButton = nullptr;
    QLabel *m_autoStatusLabel = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QTimer *m_chartTimer = nullptr;

    QAction *m_pauseAction = nullptr;
    QAction *m_extractAction = nullptr;
    QAction *m_installFuelAction = nullptr;
    QAction *m_installRodAction = nullptr;
    QAction *m_installReflectorAction = nullptr;
    QAction *m_syncRodAction = nullptr;
    QAction *m_autoHoldAction = nullptr;

    int m_selectedIndex = -1;
    int m_lastAlarmLevel = 0;
    bool m_soundEnabled = true;
    bool m_updatingUi = false;
    rbmk::CorePreset m_shownPreset = rbmk::CorePreset::NominalHot;
    double m_shownTimeScale = 1.0;
};
