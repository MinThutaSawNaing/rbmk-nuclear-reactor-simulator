#include "core/ReactorCore.h"

#include <QTimer>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace rbmk {

namespace {

// ---------------------------------------------------------------- grid layout
constexpr double kLayoutCentre    = 300.0;
constexpr double kLayoutRadius    = 250.0;
constexpr double kInsideRadius    = 245.0;
const char      *kRowLetters      = "ABCDEFGHJKLMNOPRSTUVWXYZ"; // I and Q skipped

// ------------------------------------------------------------ neutron kinetics
constexpr double kBeta            = 0.0065;   // delayed neutron fraction
constexpr double kLambdaPrompt    = 1.0e-4;   // prompt neutron lifetime [s]
constexpr double kLambdaDelayed   = 0.0784;   // one-group delayed precursor decay [1/s]
constexpr double kPeriodCap       = 1.0e7;    // reported period when essentially stable

// --------------------------------------------------------------- reactivities
constexpr double kRhoExcess       = 7.0;      // dollars with the as-built fuel load, rods out
constexpr double kRodWorthFull    = 8.5;      // dollars for the as-built 30 drives, full stroke
constexpr double kRodWeightSum    = 21.094;   // sum of radial weights of the as-built lattice
constexpr double kXenonWorth      = 3.0;      // dollars at equilibrium xenon
constexpr double kTempCoeff       = -0.0022;  // dollars per degC of fuel temperature
constexpr double kTempReference   = 300.0;    // degC
constexpr double kVoidWorth       = 1.4;      // dollars at fully voided core
constexpr double kBurnupWorth     = 0.35;     // dollars at end of life
constexpr double kReflectorWorth  = 0.008;    // dollars per reflector column vs. as-built
constexpr double kOrmLimit        = 10.0;     // operating reactivity margin, equivalent rods
constexpr double kOrmDanger       = 6.0;

// ------------------------------------------------------- thermal hydraulics
constexpr double kFuelHeatCap     = 30.0;     // MW*s/degC
constexpr double kCoolHeatCap     = 45.0;     // MW*s/degC
constexpr double kFuelToCoolant   = 3.0;      // MW/degC
constexpr double kFlowHeatRemoval = 71.0;     // MW/degC at 100 % flow
constexpr double kInletTemp       = 270.0;    // degC

// ------------------------------------------------------------- drive dynamics
constexpr double kRodDriveSpeed   = 14.0;     // %/s normal rod drive
constexpr double kScramRodSpeed   = 42.0;     // %/s AZ-5 rod drop
constexpr int    kMainPumpCount   = 8;        // MCPs on a 2400 MW-class core
constexpr double kTickSeconds     = 0.05;

// --------------------------------------------------- turbine / generator train
constexpr double kTurbineRatedRpm = 3000.0;   // synchronous speed
constexpr double kTurbineEff      = 0.32;     // net thermal -> electrical
constexpr double kTurbineSpool    = 4.0;      // s, run-up / load ramp inertia
constexpr double kTurbineCoast    = 12.0;     // s, trip coast-down inertia

// ------------------------------------------------------ power hold controller
/*!
 * The controller adds a small reactivity bias proportional to the logarithm of
 * the power error and then solves for the bank position that produces exactly
 * that bias on the *measured* core state. Consequences of this choice:
 *  - the bias vanishes as P -> P_target, so the core settles genuinely critical
 *    on the requested power instead of hunting around it;
 *  - the solve is re-done every physics step, so xenon, void, burnup and pump
 *    trips are corrected without a separate integral term;
 *  - the loop gain is a reactivity, not a rod speed, so the achievable ramp is
 *    bounded no matter how large the error is.
 */
constexpr double kAutoGain        = 0.18;     // dollars of bias per e-fold of error
constexpr double kAutoRhoMax      = 0.30;     // dollars, hard cap on that bias
constexpr double kAutoDeadband    = 0.005;    // relative power error ignored (0.5 %)
constexpr double kAutoBankEpsilon = 0.005;    // % of bank travel treated as "settled"

double clampd(double v, double lo, double hi)
{
    return std::max(lo, std::min(hi, v));
}

/*! Saturation pressure of water in atm, fitted through 100 degC / 1 atm and
 *  284 degC / 68 atm. Used so a cold circuit is not magically pressurised. */
double saturationPressure(double celsius)
{
    const double kelvin = celsius + 273.15;
    return std::exp(12.78 - 4768.0 / kelvin);
}

QString simStamp(double seconds)
{
    const int total = static_cast<int>(seconds);
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;
    return QStringLiteral("T+%1:%2:%3")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

} // namespace

QString presetName(CorePreset preset)
{
    switch (preset) {
    case CorePreset::NominalHot:   return QStringLiteral("Nominal hot (equilibrium xenon)");
    case CorePreset::LowPower:     return QStringLiteral("Low power (xenon poisoned)");
    case CorePreset::ColdShutdown: return QStringLiteral("Cold shutdown (all rods inserted)");
    }
    return QString();
}

// -----------------------------------------------------------------------------
ReactorCore::ReactorCore(QObject *parent)
    : QObject(parent)
{
    m_pumpCount = kMainPumpCount;
    m_pumpCommand = QVector<bool>(m_pumpCount, true);
    buildGrid();
    updateInventoryCounts();
    recalcRodWorthSum();
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(static_cast<int>(kTickSeconds * 1000.0));
    connect(m_timer, &QTimer::timeout, this, &ReactorCore::tick);
    refreshTelemetry();
}

ReactorCore::~ReactorCore() = default;

void ReactorCore::initialize(CorePreset preset)
{
    m_channels.clear();
    buildGrid();
    applyPreset(preset);
    updateInventoryCounts();
    recalcRodWorthSum();
    updateSpatialReadout();
    refreshTelemetry();
    evaluateAlarm();
    emitLog(QStringLiteral("Simulator initialised: %1").arg(presetName(preset)), 3);
    start();
}

// ------------------------------------------------------------------ grid build
void ReactorCore::buildGrid()
{
    m_channels.clear();
    const int size = gridSize();
    const double channelStep = (kLayoutRadius * 2.0) / size;
    m_channels.reserve(size * size);

    for (int r = 0; r < size; ++r) {
        for (int c = 0; c < size; ++c) {
            Channel ch;
            ch.row = r;
            ch.col = c;
            ch.index = m_channels.size();
            ch.coord = QStringLiteral("%1-%2")
                           .arg(QLatin1Char(kRowLetters[r]))
                           .arg(c + 1, 2, 10, QLatin1Char('0'));

            ch.x = kLayoutCentre - kLayoutRadius + c * channelStep + channelStep / 2.0;
            ch.y = kLayoutCentre - kLayoutRadius + r * channelStep + channelStep / 2.0;
            ch.distance = std::hypot(ch.x - kLayoutCentre, ch.y - kLayoutCentre);
            ch.inside = ch.distance <= kInsideRadius;

            // As-built RBMK-style lattice: staggered control rod grid, water
            // reflector ring on the periphery, fuel everywhere else.
            const bool control = (r % 4 == 1 && c % 4 == 2) || (r % 4 == 3 && c % 4 == 0);
            if (!ch.inside) {
                ch.type = AssemblyType::Empty;
            } else if (control) {
                ch.type = AssemblyType::Control;
            } else if (ch.distance > 220.0) {
                ch.type = AssemblyType::Reflector;
            } else {
                ch.type = AssemblyType::Fuel;
            }

            ch.rodWorth = 0.55 + 0.45 * (1.0 - clampd(ch.distance / kInsideRadius, 0.0, 1.0));
            ch.rodPosition = 0.0;
            ch.rodTarget = 0.0;
            ch.followBank = true;
            ch.localTemp = kInletTemp;
            m_channels.push_back(ch);
        }
    }
    if (m_model == ReactorModel::Rbmkp2400) {
        updateInventoryCounts();
        recalcRodWorthSum();
        m_nominalFuelCount = m_fuelCount;
        m_nominalReflectorCount = m_reflectorCount;
        m_nominalRodWeightSum = m_rodWorthSum;
    } else {
        m_nominalFuelCount = 180.0;
        m_nominalReflectorCount = 38.0;
        m_nominalRodWeightSum = kRodWeightSum;
    }
}

void ReactorCore::updateInventoryCounts()
{
    m_fuelCount = 0;
    m_controlCount = 0;
    m_reflectorCount = 0;
    m_emptyCount = 0;
    for (const Channel &ch : m_channels) {
        if (!ch.inside)
            continue;
        switch (ch.type) {
        case AssemblyType::Fuel:      ++m_fuelCount; break;
        case AssemblyType::Control:   ++m_controlCount; break;
        case AssemblyType::Reflector: ++m_reflectorCount; break;
        case AssemblyType::Empty:     ++m_emptyCount; break;
        }
    }
}

void ReactorCore::recalcRodWorthSum()
{
    m_rodWorthSum = 0.0;
    for (const Channel &ch : m_channels) {
        if (ch.type == AssemblyType::Control)
            m_rodWorthSum += ch.rodWorth;
    }
}

// -------------------------------------------------------------------- presets
void ReactorCore::applyPreset(CorePreset preset)
{
    m_preset = preset;
    m_simSeconds = 0.0;
    m_destroyed = false;
    m_pipeRupture = false;
    m_reliefOpen = false;
    m_scrammed = false;
    m_scramSpike = 0.0;
    m_burnup = 0.0;
    m_integrity = 1.0;
    m_energyMWh = 0.0;
    m_alarmLevel = 0;
    m_lastStatus.clear();

    for (int i = 0; i < m_pumpCount; ++i)
        m_pumpCommand[i] = true;

    // The turbine train resets with the core: hot preset arrives on the grid at
    // rated speed, a cold core has the machine shut down and ready to roll.
    m_turbineTripped = false;
    m_turbineValve = (preset == CorePreset::ColdShutdown) ? 0.0 : 100.0;
    m_turbineRpm = (preset == CorePreset::ColdShutdown) ? 0.0 : kTurbineRatedRpm;
    m_turbineMW = 0.0;

    switch (preset) {
    case CorePreset::NominalHot:
        m_power = 1000.0;
        m_fuelTemp = 617.0;
        m_coolantTemp = 284.0;
        m_pressure = 68.0;
        m_flow = 100.0;
        m_inletTemp = 270.0;
        m_xenon = 1.0;
        m_iodine = 1.0;
        m_void = 0.0;
        m_bankTarget = 43.0;
        break;
    case CorePreset::LowPower:
        m_power = 200.0;
        m_fuelTemp = 341.0;
        m_coolantTemp = 273.0;
        m_pressure = 56.0;
        m_flow = 100.0;
        m_inletTemp = 270.0;
        m_xenon = 1.0;
        m_iodine = 1.0;
        m_void = 0.0;
        m_bankTarget = 47.0;
        break;
    case CorePreset::ColdShutdown:
        m_power = 0.5;
        m_fuelTemp = 45.0;
        m_coolantTemp = 42.0;
        m_pressure = 6.0;
        m_flow = 100.0;
        m_inletTemp = 40.0;
        m_xenon = 0.0;
        m_iodine = 0.0;
        m_void = 0.0;
        m_bankTarget = 100.0;
        break;
    }

    const double rodSet = (preset == CorePreset::ColdShutdown) ? 100.0 : m_bankTarget;
    for (Channel &ch : m_channels) {
        if (ch.type == AssemblyType::Control) {
            ch.followBank = true;
            ch.rodPosition = rodSet;
            ch.rodTarget = rodSet;
        } else {
            ch.rodPosition = 0.0;
            ch.rodTarget = 0.0;
        }
        ch.burnup = m_burnup;
        ch.localXenon = m_xenon;
    }
    // The machine comes onto the bar with a hot core, so the initial export is
    // consistent with the preset instead of reading 0 MWe until the first tick.
    m_turbineRpm = (preset == CorePreset::ColdShutdown) ? 0.0 : kTurbineRatedRpm;
    updateTurbine(0.0);

    m_reactivity = computeReactivity();
    m_period = reactorPeriod(m_reactivity);
}

void ReactorCore::reset()
{
    const CorePreset p = m_preset;
    buildGrid();
    applyPreset(p);
    updateInventoryCounts();
    recalcRodWorthSum();
    updateSpatialReadout();
    refreshTelemetry();
    evaluateAlarm();
    emitLog(QStringLiteral("CORE REBUILT: all assemblies restored to the as-built %1 layout.")
                .arg(presetName(p)),
            3);
    emit coreRebuilt();
    emit stateUpdated();
}

void ReactorCore::loadPreset(CorePreset preset)
{
    buildGrid();
    applyPreset(preset);
    updateInventoryCounts();
    recalcRodWorthSum();
    updateSpatialReadout();
    refreshTelemetry();
    evaluateAlarm();
    emitLog(QStringLiteral("Core reloaded: %1.").arg(presetName(preset)), 3);
    if (m_autoHold)
        releaseAutoHold(QStringLiteral("AUTO HOLD released: a new core state was loaded."), 1);
    emit coreRebuilt();
    emit stateUpdated();
}

// -------------------------------------------------------------------- control
bool ReactorCore::isValidChannel(int index) const
{
    return index >= 0 && index < m_channels.size() && m_channels.at(index).inside;
}

void ReactorCore::start()
{
    if (m_timer && !m_timer->isActive())
        m_timer->start();
}

void ReactorCore::stop()
{
    if (m_timer)
        m_timer->stop();
}

void ReactorCore::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    m_telemetry.paused = paused;
    emitLog(paused ? QStringLiteral("Simulation PAUSED by operator.")
                   : QStringLiteral("Simulation resumed."),
            3);
    emit stateUpdated();
}

void ReactorCore::setTimeScale(double scale)
{
    m_timeScale = clampd(scale, 0.25, 20.0);
    emitLog(QStringLiteral("Simulation time scale set to %1x.").arg(m_timeScale, 0, 'f', 1), 3);
    emit stateUpdated();
}

void ReactorCore::setBankTarget(double pct, bool announce)
{
    pct = clampd(pct, 0.0, 100.0);
    if (std::fabs(pct - m_bankTarget) < 1e-6)
        return;

    if (m_autoHold) {
        // The operator grabbed the bank: hand control back immediately.
        releaseAutoHold(QStringLiteral("AUTO HOLD released: the rod bank was moved by hand."), 1);
    }

    if (m_scrammed) {
        m_scrammed = false;
        emitLog(QStringLiteral("Operator override: AZ-5 rod drop cancelled, bank control restored."), 1);
    }

    m_bankTarget = pct;
    for (Channel &ch : m_channels) {
        if (ch.type == AssemblyType::Control && ch.inside) {
            ch.followBank = true;
            ch.rodTarget = pct;
        }
    }
    if (announce || std::fabs(pct - m_lastBankLog) >= 2.0) {
        m_lastBankLog = pct;
        emitLog(QStringLiteral("Rod bank set point %1 % (all %2 drives following).")
                    .arg(pct, 0, 'f', 1)
                    .arg(m_controlCount),
                3);
    }
    refreshTelemetry();
    emit stateUpdated();
}

void ReactorCore::nudgeBank(double deltaPct)
{
    setBankTarget(m_bankTarget + deltaPct);
}

void ReactorCore::syncAllRodsToBank()
{
    bool any = false;
    for (Channel &ch : m_channels) {
        if (ch.type == AssemblyType::Control) {
            if (!ch.followBank || std::fabs(ch.rodTarget - m_bankTarget) > 1e-6) {
                any = true;
                ch.followBank = true;
                ch.rodTarget = m_bankTarget;
            }
        }
    }
    if (any) {
        emitLog(QStringLiteral("All rod drives synchronised to the bank set point (%1 %).")
                    .arg(m_bankTarget, 0, 'f', 1),
                3);
        emit stateUpdated();
    }
}

// ------------------------------------------------------- automatic power hold
void ReactorCore::releaseAutoHold(const QString &reason, int severity)
{
    if (!m_autoHold)
        return;
    m_autoHold = false;
    m_autoState = AutoHoldState::Off;
    if (!reason.isEmpty())
        pushLog(reason, severity);
    refreshTelemetry();
}

void ReactorCore::setPowerTarget(double mwt)
{
    if (m_scrammed) {
        // A scram always wins: the controller would have no authority over the
        // drives, so engaging here would only produce a misleading readout.
        emitLog(QStringLiteral("AUTO HOLD refused: AZ-5 still commands every drive to 100 %. "
                               "Move the bank to cancel the scram first."),
                2);
        refreshTelemetry();
        emit stateUpdated();
        return;
    }

    m_powerTarget = clampd(mwt / powerScale(), 5.0, 1450.0);
    const bool wasEngaged = m_autoHold;
    m_autoHold = true;

    // Label the state from the power *now*, so the readout is already correct when
    // the actor engages on a paused simulation (no physics step will follow).
    const double lnError = std::log(std::max(1e-3, m_powerTarget / std::max(0.05, m_power)));
    m_autoState = (std::fabs(lnError) <= kAutoDeadband)
                      ? AutoHoldState::Holding
                      : (lnError > 0.0 ? AutoHoldState::Raising : AutoHoldState::Lowering);
    emitLog(wasEngaged
                ? QStringLiteral("AUTO HOLD: power target changed to %1 MWt.")
                      .arg(powerTarget(), 0, 'f', 0)
                : QStringLiteral("AUTO HOLD engaged: the bank will be trimmed until the core holds "
                                 "%1 MWt.").arg(powerTarget(), 0, 'f', 0),
            3);
    refreshTelemetry();
    emit stateUpdated();
}

void ReactorCore::clearPowerTarget()
{
    if (!m_autoHold)
        return;
    m_autoHold = false;
    m_autoState = AutoHoldState::Off;
    emitLog(QStringLiteral("AUTO HOLD released: the rod bank is back under manual control."), 3);
    refreshTelemetry();
    emit stateUpdated();
}

/*! Total dollar worth of the installed drives if the bank sat at \a bankPct.
 *  Drives that were trimmed independently keep their own position - that is what
 *  makes the controller respect an operator's individual rod trims. */
double ReactorCore::rodWorthWithBank(double bankPct) const
{
    double worth = 0.0;
    for (const Channel &ch : m_channels) {
        if (ch.type != AssemblyType::Control)
            continue;
        const double position = ch.followBank ? bankPct : ch.rodPosition;
        worth += ch.rodWorth * (kRodWorthFull / m_nominalRodWeightSum) * rodWorthCurve(position);
    }
    return worth;
}

/*! Invert rodWorthWithBank(). The S-curve is monotonic, so bisection is exact
 *  and branch-free; 60 iterations over 0..100 % is far below one display pixel. */
bool ReactorCore::solveBankForWorth(double wantedWorth, double *bankPct) const
{
    const double lowWorth = rodWorthWithBank(0.0);
    const double highWorth = rodWorthWithBank(100.0);
    if (highWorth - lowWorth < 1e-9)
        return false; // nothing follows the bank
    if (wantedWorth < lowWorth || wantedWorth > highWorth)
        return false;

    double lo = 0.0;
    double hi = 100.0;
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (rodWorthWithBank(mid) < wantedWorth)
            lo = mid;
        else
            hi = mid;
    }
    *bankPct = 0.5 * (lo + hi);
    return true;
}

/*! Command the bank without re-attaching detached drives, without logging and
 *  without emitting stateUpdated (this runs every physics sub-step). */
void ReactorCore::applyBankSilently(double pct)
{
    pct = clampd(pct, 0.0, 100.0);
    if (std::fabs(pct - m_bankTarget) < kAutoBankEpsilon)
        return;
    m_bankTarget = pct;
    for (Channel &ch : m_channels) {
        if (ch.type == AssemblyType::Control && ch.inside && ch.followBank)
            ch.rodTarget = pct;
    }
}

void ReactorCore::updateAutoHold()
{
    if (!m_autoHold)
        return;
    if (m_destroyed) {
        releaseAutoHold(QStringLiteral("AUTO HOLD released: the core is destroyed."), 2);
        return;
    }

    int followers = 0;
    for (const Channel &ch : m_channels) {
        if (ch.type == AssemblyType::Control && ch.followBank)
            ++followers;
    }
    if (followers == 0) {
        if (m_autoState != AutoHoldState::NoDrives) {
            m_autoState = AutoHoldState::NoDrives;
            pushLog(QStringLiteral("AUTO HOLD: no rod drive is attached to the bank - "
                                   "attach a drive or press Sync."),
                    1);
        }
        return;
    }

    const double power = std::max(0.05, m_power);
    const double lnError = clampd(std::log(std::max(1e-3, m_powerTarget / power)), -3.0, 3.0);

    // Proportional-in-log bias. It is applied even inside the deadband, so the
    // controller has no steady-state offset; the deadband only labels the state
    // and keeps the readout from flickering between "raising" and "lowering".
    const double bias = clampd(kAutoGain * lnError, -kAutoRhoMax, kAutoRhoMax);

    // Reactivity right now is m_reactivity; moving the bank from its present
    // position to p changes the rod term by (worth(now) - worth(p)).
    const double worthNow = computeRodWorth();
    const double worthWanted = worthNow + m_reactivity - bias;

    double bankPct = m_bankTarget;
    if (solveBankForWorth(worthWanted, &bankPct)) {
        if (std::fabs(lnError) <= kAutoDeadband)
            m_autoState = AutoHoldState::Holding;
        else
            m_autoState = (lnError > 0.0) ? AutoHoldState::Raising : AutoHoldState::Lowering;
    } else if (worthWanted > rodWorthWithBank(100.0)) {
        bankPct = 100.0;
        m_autoState = AutoHoldState::RodWorthLimit;
    } else {
        bankPct = 0.0;
        m_autoState = AutoHoldState::ReactivityLimit;
    }

    applyBankSilently(bankPct);
}

void ReactorCore::setRodTarget(int channelIndex, double pct, bool detachFromBank)
{
    if (!isValidChannel(channelIndex))
        return;
    Channel &ch = m_channels[channelIndex];
    if (ch.type != AssemblyType::Control)
        return;

    pct = clampd(pct, 0.0, 100.0);
    const bool wasFollowing = ch.followBank;
    ch.rodTarget = pct;
    ch.followBank = !detachFromBank;

    if (detachFromBank) {
        if (m_scrammed) {
            m_scrammed = false;
            emitLog(QStringLiteral("Operator override: AZ-5 cancelled by manual rod movement."), 1);
        }
        if (wasFollowing) {
            emitLog(QStringLiteral("Drive %1 trimmed independently to %2 % (detached from bank).")
                        .arg(ch.coord)
                        .arg(pct, 0, 'f', 1),
                        3);
        }
    }
    emit channelChanged(channelIndex);
    emit stateUpdated();
}

void ReactorCore::nudgeRod(int channelIndex, double deltaPct)
{
    if (!isValidChannel(channelIndex))
        return;
    const Channel &ch = m_channels.at(channelIndex);
    if (ch.type != AssemblyType::Control)
        return;
    setRodTarget(channelIndex, ch.rodTarget + deltaPct, true);
}

// -------------------------------------------------------- assembly exchange
void ReactorCore::insertAssembly(int channelIndex, AssemblyType type)
{
    if (!isValidChannel(channelIndex)) {
        emitLog(QStringLiteral("Cannot insert: socket is outside the reactor shield."), 1);
        return;
    }
    if (type == AssemblyType::Empty) {
        extractAssembly(channelIndex);
        return;
    }

    Channel &ch = m_channels[channelIndex];
    const double rhoBefore = (m_fuelCount > 0) ? computeReactivity() : -99.0;

    if (ch.hasAssembly())
        ch.type = AssemblyType::Empty; // direct swap, inventory recounted below

    ch.type = type;
    ch.localXenon = m_xenon;
    ch.localTemp = m_coolantTemp;

    if (type == AssemblyType::Control) {
        ch.followBank = true;
        ch.rodTarget = m_scrammed ? 100.0 : m_bankTarget;
        ch.rodPosition = ch.rodTarget;
        emitLog(QStringLiteral("CONTROL ROD %1 loaded - drive attached to the %2 % bank position.")
                    .arg(ch.coord)
                    .arg(ch.rodTarget, 0, 'f', 0),
                3);
    } else if (type == AssemblyType::Fuel) {
        emitLog(QStringLiteral("FUEL ASSEMBLY %1 loaded.").arg(ch.coord), 3);
    } else {
        emitLog(QStringLiteral("WATER REFLECTOR %1 loaded.").arg(ch.coord), 3);
    }

    updateInventoryCounts();
    recalcRodWorthSum();

    const double rhoAfter = computeReactivity();
    m_reactivity = rhoAfter;
    m_period = reactorPeriod(rhoAfter);
    if (rhoBefore > -90.0 && std::fabs(rhoAfter - rhoBefore) > 0.02) {
        pushLog(QStringLiteral("Reactivity step from assembly change: %1 $ -> %2 $ (%3 pcm).")
                    .arg(rhoBefore, 0, 'f', 3)
                    .arg(rhoAfter, 0, 'f', 3)
                    .arg((rhoAfter - rhoBefore) * kBeta * 1.0e5, 0, 'f', 0),
                rhoAfter > rhoBefore + 0.25 ? 1 : 0);
    }

    refreshTelemetry();
    emit channelChanged(channelIndex);
    emit stateUpdated();
}

void ReactorCore::extractAssembly(int channelIndex)
{
    if (!isValidChannel(channelIndex)) {
        emitLog(QStringLiteral("Cannot extract: socket is outside the reactor shield."), 1);
        return;
    }
    Channel &ch = m_channels[channelIndex];
    if (!ch.hasAssembly()) {
        emitLog(QStringLiteral("Socket %1 is already empty.").arg(ch.coord), 0);
        return;
    }

    const double rhoBefore = (m_fuelCount > 0) ? computeReactivity() : -99.0;
    const AssemblyType removed = ch.type;
    ch.type = AssemblyType::Empty;
    ch.rodPosition = 0.0;
    ch.rodTarget = 0.0;
    ch.followBank = true;
    ch.localFlux = 0.0;

    updateInventoryCounts();
    recalcRodWorthSum();

    const double rhoAfter = computeReactivity();
    m_reactivity = rhoAfter;
    m_period = reactorPeriod(rhoAfter);
    const bool destabilising = rhoAfter > rhoBefore;
    emitLog(QStringLiteral("%1 %2 EXTRACTED - channel now empty%3.")
                .arg(assemblyNameQ(removed), ch.coord,
                     destabilising ? QStringLiteral("; reactivity INCREASED") : QString()),
            (destabilising && removed == AssemblyType::Control) ? 1 : 3);

    if (rhoBefore > -90.0 && std::fabs(rhoAfter - rhoBefore) > 0.02) {
        pushLog(QStringLiteral("Reactivity step: %1 $ -> %2 $ (%3 pcm).")
                    .arg(rhoBefore, 0, 'f', 3)
                    .arg(rhoAfter, 0, 'f', 3)
                    .arg((rhoAfter - rhoBefore) * kBeta * 1.0e5, 0, 'f', 0),
                destabilising ? 1 : 0);
    }

    refreshTelemetry();
    emit channelChanged(channelIndex);
    emit stateUpdated();
}

void ReactorCore::swapAssembly(int channelIndex, AssemblyType type)
{
    if (!isValidChannel(channelIndex))
        return;
    if (m_channels.at(channelIndex).type == type)
        return;
    extractAssembly(channelIndex);
    insertAssembly(channelIndex, type);
}

// ---------------------------------------------------------------- pumps / AZ5
void ReactorCore::setPump(int index, bool running)
{
    if (index < 0 || index >= m_pumpCount)
        return;
    if (m_pumpCommand[index] == running)
        return;
    m_pumpCommand[index] = running;
    emitLog(QStringLiteral("MCP-%1 %2.")
                .arg(index + 1)
                .arg(running ? QStringLiteral("STARTED - coolant flow rising")
                             : QStringLiteral("TRIPPED - coolant flow falling")),
            running ? 3 : 1);
    refreshTelemetry();
    emit stateUpdated();
}

void ReactorCore::setAllPumps(bool running)
{
    for (int i = 0; i < m_pumpCount; ++i)
        m_pumpCommand[i] = running;
    emitLog(running ? QStringLiteral("All %1 main circulation pumps started.")
                          .arg(m_pumpCount)
                    : QStringLiteral("All %1 main circulation pumps stopped - "
                                     "flow coasting down.")
                          .arg(m_pumpCount),
            running ? 3 : 1);
    refreshTelemetry();
    emit stateUpdated();
}

// ------------------------------------------------------------ turbine controls
void ReactorCore::setTurbineValve(double pct, bool announce)
{
    const double next = clampd(pct, 0.0, 100.0);
    if (qFuzzyCompare(next + 1.0, m_turbineValve + 1.0))
        return;
    m_turbineValve = next;
    if (m_turbineValve > 0.0 && m_turbineTripped) {
        // opening the governor after a trip is how the machine is re-armed
        m_turbineTripped = false;
        if (announce)
            emitLog(QStringLiteral("Turbine trip reset - governor valve opening to %1 %.")
                        .arg(m_turbineValve, 0, 'f', 0),
                    3);
    } else if (announce) {
        emitLog(QStringLiteral("Turbine governor valve set to %1 %.")
                    .arg(m_turbineValve, 0, 'f', 0),
                3);
    }
    refreshTelemetry();
    emit stateUpdated();
}

void ReactorCore::tripTurbine()
{
    if (m_turbineTripped)
        return;
    m_turbineTripped = true;
    m_turbineValve = 0.0;
    emitLog(QStringLiteral("TURBINE TRIP - governor valve shut, generator breaker open, "
                           "shaft coasting down from %1 rpm.")
                .arg(m_turbineRpm, 0, 'f', 0),
            2);
    refreshTelemetry();
    emit stateUpdated();
}

void ReactorCore::resetTurbine()
{
    m_turbineTripped = false;
    m_turbineValve = 100.0;
    emitLog(QStringLiteral("Turbine re-armed - governor to 100 %, rolling the shaft back to "
                           "%1 rpm.")
                .arg(kTurbineRatedRpm, 0, 'f', 0),
            3);
    refreshTelemetry();
    emit stateUpdated();
}

void ReactorCore::scram()
{
    const double orm = computeOrm();
    m_scrammed = true;
    m_bankTarget = 100.0;
    for (Channel &ch : m_channels) {
        if (ch.type == AssemblyType::Control) {
            ch.followBank = true;
            ch.rodTarget = 100.0;
        }
    }

    emitLog(QStringLiteral("AZ-5 ACTUATED: all %1 rod drives dropping to 100 %.").arg(m_controlCount), 2);
    // A scram takes the machine off the grid with the reactor.
    tripTurbine();
    releaseAutoHold(QStringLiteral("AUTO HOLD released: AZ-5 overrides the power controller."), 1);

    // The infamous positive scram effect: with a very small operating reactivity
    // margin the graphite displacers displace water on the way in, adding
    // positive reactivity before the absorber takes effect.
    if (orm < kOrmLimit && m_controlCount > 0) {
        const double spike = clampd(0.35 + 0.06 * (kOrmLimit - orm), 0.35, 0.9);
        m_scramSpike = spike;
        pushLog(QStringLiteral("WARNING: only %1 equivalent rods inserted (limit %2). "
                               "AZ-5 positive scram transient +%3 $ injected.")
                    .arg(orm, 0, 'f', 1)
                    .arg(kOrmLimit, 0, 'f', 0)
                    .arg(spike, 0, 'f', 2),
                2);
    }
    refreshTelemetry();
    emit stateUpdated();
}

// ------------------------------------------------------------------- physics
double ReactorCore::rodWorthCurve(double insertionPct)
{
    const double x = clampd(insertionPct / 100.0, 0.0, 1.0);
    return 0.5 * (1.0 - std::cos(M_PI * x)); // S-curve: little worth near either end
}

double ReactorCore::rodWorthDollars(double radialWeight, double insertionPct) const
{
    return radialWeight * (kRodWorthFull / m_nominalRodWeightSum) * rodWorthCurve(insertionPct);
}

double ReactorCore::reactorPeriod(double reactivityDollars)
{
    // Solve the one-group inhour equation  rho = Lambda*s + beta*s/(s+lambda)
    // for the stable root s.  f(s) is monotonically increasing, so bisection is
    // unconditionally safe and cheap enough to run once per physics tick.
    const double rho = reactivityDollars * kBeta;
    if (std::fabs(rho) < 1e-12)
        return kPeriodCap;

    const auto f = [rho](double s) {
        return kLambdaPrompt * s + kBeta * s / (s + kLambdaDelayed) - rho;
    };

    double lo = -kLambdaDelayed + 1e-9;
    double hi = 10.0;
    int guard = 0;
    while (f(hi) < 0.0 && guard++ < 40)
        hi *= 2.0;

    for (int i = 0; i < 80; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (f(mid) > 0.0)
            hi = mid;
        else
            lo = mid;
    }

    const double s = 0.5 * (lo + hi);
    if (std::fabs(s) < 1e-9)
        return (s >= 0.0) ? kPeriodCap : -kPeriodCap;
    return 1.0 / s;
}

double ReactorCore::computeRodWorth() const
{
    double worth = 0.0;
    for (const Channel &ch : m_channels) {
        if (ch.type != AssemblyType::Control)
            continue;
        worth += ch.rodWorth * (kRodWorthFull / m_nominalRodWeightSum) * rodWorthCurve(ch.rodPosition);
    }
    return worth;
}

double ReactorCore::computeOrm() const
{
    // Operating reactivity margin expressed as equivalent fully-inserted drives,
    // normalised by the mean radial weight of the as-built lattice.
    constexpr double kMeanWeight = kRodWeightSum / 30.0;
    double orm = 0.0;
    for (const Channel &ch : m_channels) {
        if (ch.type != AssemblyType::Control)
            continue;
        orm += (ch.rodPosition / 100.0) * (ch.rodWorth / kMeanWeight);
    }
    return orm;
}

double ReactorCore::computeReactivity() const
{
    if (m_fuelCount == 0)
        return -99.0; // no fuel, no chain reaction

    const double fuelTerm      = kRhoExcess * (static_cast<double>(m_fuelCount) / m_nominalFuelCount);
    const double reflectorTerm = kReflectorWorth * (static_cast<double>(m_reflectorCount) - m_nominalReflectorCount);
    const double rodTerm       = -computeRodWorth();
    const double xenonTerm     = -kXenonWorth * m_xenon;
    const double tempTerm      = kTempCoeff * (m_fuelTemp - kTempReference);
    const double voidTerm      = kVoidWorth * m_void;
    const double burnTerm      = -kBurnupWorth * m_burnup;

    return fuelTerm + reflectorTerm + rodTerm + xenonTerm + tempTerm + voidTerm + burnTerm
           + m_scramSpike;
}

double ReactorCore::voidFractionTarget() const
{
    const double flowFrac = clampd(m_flow / 100.0, 0.0, 1.0);
    const double powerFrac = clampd(m_power / 3200.0, 0.0, 1.0);

    // Flow deficit: pumps down while the core is still making power.
    double target = (1.0 - flowFrac) * clampd(0.35 + 0.65 * powerFrac, 0.0, 1.0);

    // Bulk boiling once the coolant is far above the saturation temperature
    // for the circuit pressure.
    if (m_coolantTemp > 340.0) {
        target += clampd((m_coolantTemp - 340.0) / 600.0, 0.0, 0.35)
                  * (0.20 + 0.80 * powerFrac);
    }
    if (m_reliefOpen)
        target += 0.05;
    if (m_pipeRupture)
        target += 0.35;

    return clampd(target, 0.0, 1.0);
}

void ReactorCore::step(double dt)
{
    if (dt <= 0.0)
        return;

    // --- automatic power hold ----------------------------------------------
    // Runs before the drives so a bank command issued here is executed in the
    // same sub-step; the drives themselves still limit how fast it can happen.
    updateAutoHold();

    // --- rod drives ---------------------------------------------------------
    for (Channel &ch : m_channels) {
        if (ch.type != AssemblyType::Control)
            continue;
        const double target = m_scrammed ? 100.0 : (ch.followBank ? m_bankTarget : ch.rodTarget);
        const double speed = m_scrammed ? kScramRodSpeed : kRodDriveSpeed;
        const double delta = target - ch.rodPosition;
        const double maxStep = speed * dt;
        if (std::fabs(delta) <= maxStep)
            ch.rodPosition = target;
        else
            ch.rodPosition += (delta > 0.0 ? maxStep : -maxStep);
    }

    // --- coolant flow -------------------------------------------------------
    int runningPumps = 0;
    for (int i = 0; i < m_pumpCount; ++i)
        runningPumps += m_pumpCommand[i] ? 1 : 0;

    const double pumpFlow = 100.0 / static_cast<double>(m_pumpCount);
    double flowTarget = runningPumps * pumpFlow;
    if (m_pipeRupture)
        flowTarget = std::min<double>(flowTarget, 8.0);
    m_flow += (flowTarget - m_flow) * std::min(1.0, dt / 2.5);
    m_flow = clampd(m_flow, 0.0, 100.0);

    // --- turbine / generator -------------------------------------------------
    updateTurbine(dt);

    // --- void fraction ------------------------------------------------------
    m_void += (voidFractionTarget() - m_void) * std::min(1.0, dt / 3.0);

    if (m_destroyed) {
        m_power = 0.0;
        m_reactivity = 0.0;
        m_period = kPeriodCap;
        // A destroyed core cannot make steam: the machine trips and coasts down.
        m_turbineTripped = true;
        m_turbineValve = 0.0;
        m_fuelTemp += (m_coolantTemp - m_fuelTemp) * std::min(1.0, dt / 120.0);
        m_coolantTemp += (m_inletTemp + 20.0 - m_coolantTemp) * std::min(1.0, dt / 300.0);
        m_pressure += (std::max(1.0, m_pressure * 0.995) - m_pressure) * std::min(1.0, dt / 5.0);
        m_energyMWh += m_power * dt / 3600.0;
        m_simSeconds += dt;
        return;
    }

    // --- reactivity ---------------------------------------------------------
    m_scramSpike *= std::exp(-dt / 5.0);
    m_reactivity = computeReactivity();
    m_period = reactorPeriod(m_reactivity);

    // --- neutron power ------------------------------------------------------
    if (m_fuelCount == 0) {
        m_power = 0.0;
    } else {
        double rate = (std::fabs(m_period) > 1e-9) ? 1.0 / m_period : 0.0;
        rate = clampd(rate, -6.0, 8.0); // keep the integrator sane at prompt criticality
        m_power *= std::exp(rate * dt);
        m_power = clampd(m_power, 0.01, 4500.0);
    }

    // --- thermal hydraulics ------------------------------------------------
    const double fuelScale = std::max(0.05, static_cast<double>(m_fuelCount) / m_nominalFuelCount);
    const double flowFrac = clampd(m_flow / 100.0, 0.0, 1.0);
    const double hArea = kFuelToCoolant * fuelScale;
    const double heatToCoolant = hArea * (m_fuelTemp - m_coolantTemp);   // MW
    const double heatRemoved = kFlowHeatRemoval * flowFrac * (m_coolantTemp - m_inletTemp);

    m_fuelTemp += (m_power - heatToCoolant) * dt / (kFuelHeatCap * fuelScale);
    m_coolantTemp += (heatToCoolant - heatRemoved) * dt / kCoolHeatCap;
    m_fuelTemp = clampd(m_fuelTemp, 5.0, 3000.0);
    m_coolantTemp = clampd(m_coolantTemp, 5.0, 1200.0);

    // --- pressure -----------------------------------------------------------
    // The circuit sits at the saturation pressure for its bulk temperature
    // (plus the pump head when flow is lost), so a cold core stays at a few
    // atmospheres instead of jumping to 68 atm.
    const double rawTarget = std::max(6.0, saturationPressure(m_coolantTemp))
                             + (1.0 - flowFrac) * 42.0;
    if (m_pressure > 118.0 && !m_pipeRupture) {
        if (!m_reliefOpen) {
            m_reliefOpen = true;
            pushLog(QStringLiteral("Primary circuit relief valve OPEN - steam venting at %1 atm.")
                        .arg(m_pressure, 0, 'f', 0),
                    1);
        }
    } else if (m_pressure < 105.0) {
        m_reliefOpen = false;
    }

    double pressTarget = rawTarget;
    if (m_reliefOpen) {
        // Relief capacity is finite: if the driving pressure is far above the
        // set point the valve cannot keep up and the circuit is at risk.
        pressTarget = (rawTarget > 150.0) ? rawTarget - 10.0 : std::min(rawTarget, 126.0);
    }
    if (m_pipeRupture)
        pressTarget = 6.0;

    m_pressure += (pressTarget - m_pressure) * std::min(1.0, dt / 2.5);
    m_pressure = clampd(m_pressure, 1.0, 200.0);

    if (!m_pipeRupture && m_pressure > 140.0) {
        m_pipeRupture = true;
        m_flow = std::min(m_flow, 12.0);
        pushLog(QStringLiteral("CRITICAL: primary circuit pipe rupture at %1 atm! Coolant inventory lost.")
                    .arg(m_pressure, 0, 'f', 0),
                2);
    }

    // --- iodine / xenon -----------------------------------------------------
    const double pn = m_power / 1000.0;
    m_iodine += (pn - m_iodine) * std::min(1.0, dt / 90.0);
    const double xeProduction = 0.94 * m_iodine + 0.06 * pn;
    const double xeLoss = m_xenon * (0.35 + 0.65 * pn);
    m_xenon += (xeProduction - xeLoss) * std::min(1.0, dt / 120.0);
    m_xenon = clampd(m_xenon, 0.0, 3.0);
    m_iodine = clampd(m_iodine, 0.0, 4.0);

    // --- burnup and integrity ----------------------------------------------
    m_burnup = clampd(m_burnup + (m_power / 3200.0) * (dt / 3600.0) * 0.06, 0.0, 1.0);

    if (m_fuelTemp > 900.0)
        m_integrity -= ((m_fuelTemp - 900.0) / 2500.0) * dt; // fraction per second
    if (m_fuelTemp > 1500.0)
        m_integrity = 0.0;
    m_integrity = clampd(m_integrity, 0.0, 1.0);

    if (m_integrity <= 0.0 && !m_destroyed) {
        m_destroyed = true;
        m_power = 0.0;
        pushLog(QStringLiteral("CORE DESTROYED: fuel melted at %1 degC. The reactor is lost.")
                    .arg(m_fuelTemp, 0, 'f', 0),
                2);
        emit coreDestroyed();
    }

    m_energyMWh += m_power * dt / 3600.0;
    m_simSeconds += dt;
}

void ReactorCore::tick()
{
    if (m_paused)
        return;

    const double total = kTickSeconds * m_timeScale;
    // Sub-step so the explicit integrator stays stable at high time acceleration.
    const int subSteps = std::max(1, std::min(40, static_cast<int>(std::ceil(total / 0.05))));
    const double h = total / subSteps;
    for (int i = 0; i < subSteps; ++i)
        step(h);

    m_uiAccumulator += total;
    if (m_uiAccumulator >= 0.1) {
        m_uiAccumulator = 0.0;
        updateSpatialReadout();
        refreshTelemetry();
        evaluateAlarm();
        emit stateUpdated();
    }
}

void ReactorCore::advance(double seconds)
{
    double remaining = std::max(0.0, seconds);
    while (remaining > 1e-9) {
        const double dt = std::min(0.05, remaining);
        step(dt);
        remaining -= dt;
    }
    updateSpatialReadout();
    refreshTelemetry();
    evaluateAlarm();
    emit stateUpdated();
}

// -------------------------------------------------------------- spatial model
void ReactorCore::updateSpatialReadout()
{
    struct RodSample
    {
        double x;
        double y;
        double insertion;
    };
    QVector<RodSample> rods;
    rods.reserve(m_controlCount);
    for (const Channel &ch : m_channels) {
        if (ch.type == AssemblyType::Control)
            rods.push_back({ ch.x, ch.y, ch.rodPosition / 100.0 });
    }

    const double powerRel = m_power / 1000.0;
    constexpr double kShadowSigma = 46.0; // layout units

    for (Channel &ch : m_channels) {
        if (!ch.inside) {
            ch.localTemp = m_coolantTemp;
            ch.localFlux = 0.0;
            continue;
        }
        const double radial =
            1.0 - 0.55 * std::pow(clampd(ch.distance / kInsideRadius, 0.0, 1.0), 2.0);

        if (ch.type == AssemblyType::Fuel) {
            double shadow = 0.0;
            for (const RodSample &rod : rods) {
                const double dx = ch.x - rod.x;
                const double dy = ch.y - rod.y;
                const double d2 = dx * dx + dy * dy;
                shadow = std::max(shadow,
                                  rod.insertion * std::exp(-d2 / (2.0 * kShadowSigma * kShadowSigma)));
            }
            const double localFactor = radial * (1.0 - 0.65 * shadow);
            ch.localFlux = std::max(0.0, powerRel * localFactor);
            ch.localTemp = m_coolantTemp + (m_fuelTemp - m_coolantTemp) * std::max(0.15, localFactor);
            ch.localXenon = m_xenon;
            ch.burnup = m_burnup;
        } else if (ch.type == AssemblyType::Control) {
            ch.localFlux = std::max(0.0, powerRel * radial * (1.0 - 0.8 * ch.rodPosition / 100.0));
            ch.localTemp = m_coolantTemp + 35.0 + 40.0 * powerRel * radial;
        } else if (ch.type == AssemblyType::Reflector) {
            ch.localFlux = std::max(0.0, powerRel * 0.25 * radial);
            ch.localTemp = m_coolantTemp + 12.0;
        } else {
            ch.localFlux = 0.0;
            ch.localTemp = m_coolantTemp;
        }
    }
}

// ------------------------------------------------------------------ turbine
/*!
 * Game-tuned steam turbine / generator train.
 *
 *  - The governor holds the shaft at synchronous speed for any valve opening
 *    above zero, but only if the primary circuit can still make steam: below
 *    roughly 20 atm the machine cannot hold speed and starts to bog down.
 *  - The shaft does not follow the governor instantly (k turbine spool inertia),
 *    and a trip coasts it down over tens of seconds.
 *  - Electrical export is thermal power x net efficiency x valve opening x the
 *    fraction of synchronous speed actually reached.
 */
void ReactorCore::updateTurbine(double dt)
{
    const double valveFrac = clampd(m_turbineValve / 100.0, 0.0, 1.0);
    // Enough steam to hold the machine at speed (rated pressure is ~68 atm).
    const double steamGate = clampd((m_pressure - 12.0) / 45.0, 0.0, 1.0);

    double rpmTarget = kTurbineRatedRpm * valveFrac * steamGate;
    if (m_turbineTripped)
        rpmTarget = 0.0;

    const double inertia = m_turbineTripped ? kTurbineCoast : kTurbineSpool;
    m_turbineRpm += (rpmTarget - m_turbineRpm) * std::min(1.0, dt / inertia);
    m_turbineRpm = clampd(m_turbineRpm, 0.0, 3300.0);

    const double spin = clampd(m_turbineRpm / kTurbineRatedRpm, 0.0, 1.0);
    const double steam = clampd(m_power / 1000.0, 0.0, 4.5) * steamGate;
    m_turbineMW = steam * kTurbineEff * 1000.0 * valveFrac * spin;
    if (m_turbineTripped)
        m_turbineMW = 0.0;
}

// ------------------------------------------------------------------ telemetry
void ReactorCore::refreshTelemetry()
{
    Telemetry &t = m_telemetry;
    t.powerMWt = m_power * powerScale();
    t.fuelTempC = m_fuelTemp;
    t.coolantTempC = m_coolantTemp;
    t.pressureAtm = m_pressure;
    t.flowPct = m_flow;
    t.inletTempC = m_inletTemp;
    t.reactivityDollars = m_reactivity;
    t.reactivityPcm = m_reactivity * kBeta * 1.0e5;
    t.flux = (m_power / 1000.0) * 1.2e13;
    t.xenon = m_xenon;
    t.iodine = m_iodine;
    t.xenonDollars = -kXenonWorth * m_xenon;
    t.voidFraction = m_void;
    t.periodSeconds = m_period;
    t.orm = computeOrm();
    t.integrity = m_integrity;
    t.burnup = m_burnup;
    t.energyMWh = m_energyMWh * powerScale();
    t.simSeconds = m_simSeconds;
    t.bankTarget = m_bankTarget;

    double sum = 0.0;
    int belowHalf = 0;
    for (const Channel &ch : m_channels) {
        if (ch.type != AssemblyType::Control)
            continue;
        sum += ch.rodPosition;
        if (ch.rodPosition < 50.0)
            ++belowHalf;
    }
    t.bankAverage = (m_controlCount > 0) ? sum / m_controlCount : 0.0;
    t.rodsBelowHalf = belowHalf;

    t.fuelChannels = m_fuelCount;
    t.controlChannels = m_controlCount;
    t.reflectorChannels = m_reflectorCount;
    t.emptyChannels = m_emptyCount;
    t.totalChannels = m_fuelCount + m_controlCount + m_reflectorCount + m_emptyCount;

    t.pumpCount = m_pumpCount;
    t.pumps = m_pumpCommand;

    t.turbineRpm = m_turbineRpm;
    t.turbineValvePct = m_turbineValve;
    // Export follows the turbine, not the core: a cold core or a tripped machine
    // exports nothing. The reading carries the model's power scale so the
    // RBMKP-2400 approximation reports megawatts rather than core units.
    t.turbineMW = m_turbineMW * powerScale();
    t.turbineTripped = m_turbineTripped;
    t.turbineSynced = !m_turbineTripped && m_turbineRpm >= 2950.0;
    t.scrammed = m_scrammed;
    t.destroyed = m_destroyed;
    t.pipeRupture = m_pipeRupture;
    t.reliefOpen = m_reliefOpen;
    t.paused = m_paused;
    t.alarmLevel = m_alarmLevel;
    t.statusText = m_lastStatus;

    // --- power hold controller readout --------------------------------------
    t.autoHold = m_autoHold;
    t.powerTargetMWt = m_autoHold ? powerTarget() : 0.0;
    t.autoHoldState = static_cast<int>(m_autoHold ? m_autoState : AutoHoldState::Off);
    const QString targetText = QStringLiteral("%1 MWt").arg(powerTarget(), 0, 'f', 0);
    const QString bankText = QStringLiteral("bank %1 %").arg(m_bankTarget, 0, 'f', 1);
    switch (m_autoHold ? m_autoState : AutoHoldState::Off) {
    case AutoHoldState::Holding:
        t.autoHoldStatus = QStringLiteral("AUTO HOLD  ·  holding %1  ·  %2")
                               .arg(targetText, bankText);
        break;
    case AutoHoldState::Raising:
        t.autoHoldStatus = QStringLiteral("AUTO HOLD  ·  raising to %1  ·  %2")
                               .arg(targetText, bankText);
        break;
    case AutoHoldState::Lowering:
        t.autoHoldStatus = QStringLiteral("AUTO HOLD  ·  lowering to %1  ·  %2")
                               .arg(targetText, bankText);
        break;
    case AutoHoldState::RodWorthLimit:
        t.autoHoldStatus = QStringLiteral("AUTO HOLD  ·  %1 unreachable  ·  every drive inserted, "
                                          "the core is still too reactive").arg(targetText);
        break;
    case AutoHoldState::ReactivityLimit:
        t.autoHoldStatus = QStringLiteral("AUTO HOLD  ·  %1 unreachable  ·  every drive withdrawn, "
                                          "xenon and void hold the core down").arg(targetText);
        break;
    case AutoHoldState::NoDrives:
        t.autoHoldStatus = QStringLiteral("AUTO HOLD  ·  no rod drive is attached to the bank  ·  %1")
                               .arg(targetText);
        break;
    case AutoHoldState::Off:
        t.autoHoldStatus = QStringLiteral("AUTO HOLD off  ·  manual rod control");
        break;
    }
}

void ReactorCore::evaluateAlarm()
{
    const double orm = computeOrm();
    int level = 0;
    QString text;
    QString detail;

    if (m_destroyed) {
        level = 2;
        text = QStringLiteral("CORE DESTROYED — MELTDOWN");
        detail = QStringLiteral("Fuel integrity lost. Rebuild the core (Ctrl+R) to continue.");
    } else if (m_pipeRupture) {
        level = 2;
        text = QStringLiteral("PRIMARY CIRCUIT RUPTURE");
        detail = QStringLiteral("Coolant inventory lost at %1 atm. Flow limited to residual.")
                     .arg(m_pressure, 0, 'f', 0);
    } else if (m_integrity < 0.995) {
        level = 2;
        text = QStringLiteral("FUEL DAMAGE IN PROGRESS");
        detail = QStringLiteral("Integrity %1 %%, fuel %2 degC. Insert rods or restore cooling.")
                     .arg(m_integrity * 100.0, 0, 'f', 1)
                     .arg(m_fuelTemp, 0, 'f', 0);
    } else if (m_fuelTemp > 900.0) {
        level = 2;
        text = QStringLiteral("CORE OVERHEAT — DAMAGE IMMINENT");
        detail = QStringLiteral("Fuel at %1 degC (900 degC damage threshold).")
                     .arg(m_fuelTemp, 0, 'f', 0);
    } else if (m_fuelCount == 0) {
        level = 1;
        text = QStringLiteral("NO FUEL IN THE CORE");
        detail = QStringLiteral("The core cannot sustain a chain reaction. "
                                "Install fuel assemblies in the empty channels.");
    } else if (m_controlCount == 0) {
        level = 1;
        text = QStringLiteral("NO CONTROL RODS INSTALLED");
        detail = QStringLiteral("There is no shutdown mechanism in the core. "
                                "Install control rod drives to regain control.");
    } else if (orm < kOrmDanger && m_controlCount > 0) {
        level = 2;
        text = QStringLiteral("SHUTDOWN MARGIN CRITICAL");
        detail = QStringLiteral("ORM %1 equivalent rods (limit %2). Further rod extraction risks prompt criticality.")
                     .arg(orm, 0, 'f', 1)
                     .arg(kOrmLimit, 0, 'f', 0);
    } else if (m_fuelTemp > 760.0 || m_pressure > 112.0 || orm < kOrmLimit) {
        level = 1;
        if (orm < kOrmLimit) {
            text = QStringLiteral("LOW OPERATING REACTIVITY MARGIN");
            detail = QStringLiteral("ORM %1 of %2 equivalent rods. Drive the rods in to restore margin.")
                         .arg(orm, 0, 'f', 1)
                         .arg(kOrmLimit, 0, 'f', 0);
        } else if (m_pressure > 112.0) {
            text = QStringLiteral("PRIMARY PRESSURE HIGH");
            detail = QStringLiteral("%1 atm (relief at 118 atm). Check pump status and core power.")
                         .arg(m_pressure, 0, 'f', 0);
        } else {
            text = QStringLiteral("CORE TEMPERATURE HIGH");
            detail = QStringLiteral("Fuel at %1 degC. Increase flow or insert the rod bank.")
                         .arg(m_fuelTemp, 0, 'f', 0);
        }
    } else if (m_scrammed && m_power > 30.0) {
        level = 1;
        text = QStringLiteral("AZ-5 SCRAM IN PROGRESS");
        detail = QStringLiteral("Rods at %1 %, power %2 MWt and falling.")
                     .arg(m_telemetry.bankAverage, 0, 'f', 0)
                     .arg(m_power, 0, 'f', 0);
    } else if (m_power < 1.0) {
        level = 0;
        text = QStringLiteral("REACTOR SUBCRITICAL");
        detail = QStringLiteral("No measurable power. Withdraw the rod bank to restart.");
    } else if (m_scrammed) {
        level = 0;
        text = QStringLiteral("REACTOR SHUT DOWN (AZ-5)");
        detail = QStringLiteral("All rods inserted, power %1 MWt.").arg(m_power, 0, 'f', 1);
    } else {
        level = 0;
        text = QStringLiteral("NORMAL OPERATION");
        detail = QStringLiteral("Power %1 MWt, fuel %2 degC, reactivity %3 $.")
                     .arg(m_power, 0, 'f', 0)
                     .arg(m_fuelTemp, 0, 'f', 0)
                     .arg(m_reactivity, 0, 'f', 2);
    }

    const bool changed = (level != m_alarmLevel) || (text != m_lastStatus);
    m_telemetry.statusText = text;
    m_telemetry.statusDetail = detail;
    m_telemetry.alarmLevel = level;

    if (changed) {
        const bool escalating = level > m_alarmLevel;
        m_alarmLevel = level;
        m_lastStatus = text;
        emit alarmRaised(level, text);
        if (escalating || level == 2)
            pushLog(QStringLiteral("ALARM: %1 — %2").arg(text, detail), level == 2 ? 2 : 1);
    }
}

// --------------------------------------------------------------------- logging
void ReactorCore::pushLog(const QString &text, int severity)
{
    emitLog(text, severity);
}

void ReactorCore::emitLog(const QString &text, int severity)
{
    LogEntry entry;
    entry.timestamp = simStamp(m_simSeconds);
    entry.text = text;
    entry.severity = severity;
    emit eventLogged(entry);
}

} // namespace rbmk
