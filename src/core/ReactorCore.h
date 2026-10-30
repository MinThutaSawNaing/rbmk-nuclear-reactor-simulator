#pragma once

#include "core/ReactorTypes.h"

#include <QObject>
#include <QString>

class QTimer;

namespace rbmk {

/*! Starting configurations. These are only initial conditions - the simulator
 *  has no objectives and no scripted events. Everything that happens afterwards
 *  is a consequence of the operator's actions. */
enum class CorePreset {
    NominalHot,      //!< 1000 MWt, hot, equilibrium xenon, bank at ~43 %
    LowPower,        //!< 200 MWt, poisoned core, bank at ~47 %
    ColdShutdown     //!< cold, atmospheric, all rods fully inserted
};

enum class ReactorModel { Rbmk1000, Rbmkp2400 };

QString presetName(CorePreset preset);

/*! State of the automatic power controller. The controller drives the rod bank
 *  through the normal drive speed, so it is an operator aid, not a cheat: it
 *  cannot insert reactivity faster than the drives can move. */
enum class AutoHoldState {
    Off = 0,             //!< released
    Holding,             //!< within the deadband of the target power
    Raising,             //!< driving power up
    Lowering,            //!< driving power down
    RodWorthLimit,       //!< target above what the installed drives can hold
    ReactivityLimit,     //!< target below what the core can reach (poison/void)
    NoDrives             //!< no drive is attached to the bank
};

/*!
 * The physics engine. Deliberately free of UI code so it can be unit tested.
 *
 * Model summary (all game-tuned but physically motivated):
 *  - Reactivity in dollars, from fuel inventory, radial control-rod worth
 *    (S-curve), xenon, fuel-temperature feedback, void and burnup.
 *  - Point kinetics reduced to the stable reactor period, with a prompt
 *    critical branch when reactivity exceeds one dollar.
 *  - Six-group kinetics are approximated by the delayed neutron period;
 *    prompt criticality bypasses it (reactor period = Lambda / (rho - 1)).
 *  - Iodine / xenon dynamics with a post-shutdown xenon peak.
 *  - Positive void coefficient: losing flow raises reactivity.
 *  - Two-node thermal hydraulics (fuel + coolant) with eight switchable MCPs
 *    and a governed steam turbine driving the generator.
 *  - Core damage accumulates above 900 degC; above 1500 degC the core fails.
 */
class ReactorCore : public QObject
{
    Q_OBJECT
public:
    explicit ReactorCore(QObject *parent = nullptr);
    ~ReactorCore() override;

    void initialize(CorePreset preset = CorePreset::NominalHot);
    void setModel(ReactorModel model) { m_model = model; }
    ReactorModel model() const { return m_model; }
    int gridSize() const { return m_model == ReactorModel::Rbmkp2400 ? 24 : 18; }
    double powerScale() const { return m_model == ReactorModel::Rbmkp2400 ? 7.5 : 1.0; }

    const QVector<Channel> &channels() const { return m_channels; }
    int channelCount() const { return m_channels.size(); }
    const Channel &channel(int index) const { return m_channels.at(index); }
    bool isValidChannel(int index) const;

    const Telemetry &telemetry() const { return m_telemetry; }
    Telemetry &mutableTelemetry() { return m_telemetry; } // tests only

    //! Number of main circulation pumps on this core (eight for a 2400 MW-class
    //! machine - a four-pump train cannot carry that heat load).
    int pumpCount() const { return m_pumpCount; }
    double turbineValve() const { return m_turbineValve; }
    bool turbineTripped() const { return m_turbineTripped; }

    CorePreset preset() const { return m_preset; }
    double bankTarget() const { return m_bankTarget; }
    bool isPaused() const { return m_paused; }
    double timeScale() const { return m_timeScale; }

    //! Static helper shared with the UI and tests.
    static double rodWorthCurve(double insertionPct);
    static double reactorPeriod(double reactivityDollars);
    //! Dollar worth of a drive with this radial weight at this insertion.
    double rodWorthDollars(double radialWeight, double insertionPct) const;

    //! Deterministic stepping (no timer) - used by the unit tests and by the
    //! headless self-test. Advances the physics by \a seconds.
    void advance(double seconds);

public slots:
    void start();
    void stop();
    void setPaused(bool paused);
    void setTimeScale(double scale);
    void reset();                                  //!< re-apply the active preset
    void loadPreset(rbmk::CorePreset preset);

    //! Move the whole rod bank (0..100 %). Every installed drive follows.
    //! \a announce writes an event-log line; slider dragging suppresses it.
    void setBankTarget(double pct, bool announce = true);
    void nudgeBank(double deltaPct);
    //! Make every installed drive follow the bank set point again.
    void syncAllRodsToBank();

    //! Independently position a single control rod drive.
    void setRodTarget(int channelIndex, double pct, bool detachFromBank = true);
    void nudgeRod(int channelIndex, double deltaPct);

    //! Assembly exchange: every socket can be loaded, emptied or swapped.
    void insertAssembly(int channelIndex, rbmk::AssemblyType type);
    void extractAssembly(int channelIndex);
    void swapAssembly(int channelIndex, rbmk::AssemblyType type);

    void setPump(int index, bool running);
    void setAllPumps(bool running);

    //! Governor valve of the turbine train (0 = shut, 100 = full admission).
    void setTurbineValve(double pct, bool announce = true);
    //! Trip the turbine: the governor valve slams shut and the shaft coasts down.
    void tripTurbine();
    //! Acknowledge the trip, re-open the governor and roll the shaft back up.
    void resetTurbine();
    void scram();

    // ---- automatic power hold ------------------------------------------------
    //! Engage the power controller with \a mwt as the set point. It trims the rod
    //! bank (through the normal drive speed) until the core sits critical at that
    //! power, and keeps re-solving as xenon, void and burnup drift.
    void setPowerTarget(double mwt);
    //! Release the controller; the bank stays where the controller left it.
    void clearPowerTarget();
    bool autoHoldEngaged() const { return m_autoHold; }
    double powerTarget() const { return m_powerTarget * powerScale(); }

signals:
    void stateUpdated();
    void eventLogged(const rbmk::LogEntry &entry);
    void alarmRaised(int level, const QString &text);
    void coreDestroyed();
    void coreRebuilt();
    void channelChanged(int index);

private slots:
    void tick();

private:
    void buildGrid();
    void applyPreset(CorePreset preset);
    void step(double dt);
    void updateTurbine(double dt);
    void updateSpatialReadout();
    void evaluateAlarm();
    void updateInventoryCounts();
    void recalcRodWorthSum();
    double computeReactivity() const;
    double computeRodWorth() const;
    double computeOrm() const;
    double voidFractionTarget() const;
    void pushLog(const QString &text, int severity);
    void emitLog(const QString &text, int severity);
    void refreshTelemetry();

    // power-hold controller internals
    void updateAutoHold();
    void applyBankSilently(double pct);
    double rodWorthWithBank(double bankPct) const;
    bool solveBankForWorth(double wantedWorth, double *bankPct) const;
    void releaseAutoHold(const QString &reason, int severity);

    QVector<Channel> m_channels;
    ReactorModel m_model = ReactorModel::Rbmk1000;
    Telemetry m_telemetry;

    QTimer *m_timer = nullptr;

    CorePreset m_preset = CorePreset::NominalHot;
    bool m_paused = false;
    double m_timeScale = 1.0;
    double m_simSeconds = 0.0;
    double m_uiAccumulator = 0.0;

    // reactor state
    double m_power = 1000.0;          // MWt
    double m_fuelTemp = 617.0;        // degC
    double m_coolantTemp = 284.0;     // degC
    double m_pressure = 68.0;         // atm
    double m_flow = 100.0;            // %
    double m_inletTemp = 270.0;       // degC, depends on how the core was started
    double m_xenon = 1.0;             // normalised, 1.0 = equilibrium at 1000 MWt
    double m_iodine = 1.0;
    double m_void = 0.0;              // 0..1
    double m_burnup = 0.0;
    double m_integrity = 1.0;
    double m_energyMWh = 0.0;
    double m_bankTarget = 43.0;
    double m_lastBankLog = -1000.0;
    double m_scramSpike = 0.0;        // dollars, positive scram transient
    double m_reactivity = 0.0;
    double m_period = 1.0e9;

    bool m_scrammed = false;
    bool m_destroyed = false;
    bool m_pipeRupture = false;
    bool m_reliefOpen = false;

    //! MCP train. Each running pump contributes 100 / m_pumpCount % of flow.
    int m_pumpCount = 8;
    QVector<bool> m_pumpCommand;

    //! Steam turbine / generator train.
    double m_turbineValve = 100.0;   // % governor opening
    double m_turbineRpm = 3000.0;    // shaft speed
    double m_turbineMW = 0.0;        // electrical export
    bool m_turbineTripped = false;

    int m_fuelCount = 0;
    int m_controlCount = 0;
    int m_reflectorCount = 0;
    int m_emptyCount = 0;
    double m_rodWorthSum = 0.0;       // sum of radial weights of installed drives
    double m_nominalFuelCount = 180.0;
    double m_nominalReflectorCount = 38.0;
    double m_nominalRodWeightSum = 21.094;

    int m_alarmLevel = 0;
    QString m_lastStatus;

    // power-hold autopilot
    bool m_autoHold = false;
    double m_powerTarget = 1000.0;     // MWt
    AutoHoldState m_autoState = AutoHoldState::Off;
    double m_autoBank = 43.0;          // last bank position the controller asked for
    int m_autoReattach = 0;            // log throttling for "drives detached" notes
};

} // namespace rbmk
