#include "core/ReactorCore.h"

#include <QtTest>

using namespace rbmk;

class CorePhysicsTest : public QObject
{
    Q_OBJECT

private slots:
    void nominalCoreIsCritical();
    void scaledModelAndIndependentUnits();
    void nominalHoldsPowerAndPressure();
    void coldPresetStaysColdAndDepressurised();
    void rodWorthCurveIsMonotonic();
    void reactorPeriodSigns();
    void extractingControlRodAddsReactivity();
    void controlRodCanBeInsertedAndExtracted();
    void individualRodFollowsAndDetaches();
    void scramDrivesEveryRodIn();
    void pumpTripHeatsTheCore();
    void xenonBuildsAfterShutdown();
    void swappingAssembliesChangesInventory();
    void removingEveryRodDestroysTheCore();
    void rebuildingRestoresTheCore();
    void autoHoldDrivesPowerToTarget();
    void autoHoldHoldsThroughXenonTransient();
    void autoHoldStartsAColdCore();
    void autoHoldReleasesOnManualBankMove();
    void autoHoldReportsWhenNoDriveFollowsTheBank();
    void autoHoldRefusesWhileScrammed();

private:
    //! Fresh nominal core, timer stopped so advance() is deterministic.
    static void boot(ReactorCore &core)
    {
        core.initialize(CorePreset::NominalHot);
        core.stop();
    }

    static int firstControlRod(const ReactorCore &core)
    {
        for (int i = 0; i < core.channelCount(); ++i) {
            if (core.channel(i).type == AssemblyType::Control)
                return i;
        }
        return -1;
    }

    static int firstFuel(const ReactorCore &core)
    {
        for (int i = 0; i < core.channelCount(); ++i) {
            if (core.channel(i).type == AssemblyType::Fuel)
                return i;
        }
        return -1;
    }
};

void CorePhysicsTest::scaledModelAndIndependentUnits()
{
    ReactorCore standard;
    ReactorCore large;
    large.setModel(ReactorModel::Rbmkp2400);
    boot(standard);
    boot(large);
    QCOMPARE(standard.gridSize(), 18);
    QCOMPARE(large.gridSize(), 24);
    QVERIFY(large.channelCount() > standard.channelCount());
    QVERIFY(large.telemetry().fuelChannels > standard.telemetry().fuelChannels);
    QVERIFY(large.telemetry().controlChannels > standard.telemetry().controlChannels);
    QVERIFY(std::fabs(large.telemetry().reactivityDollars) < 0.12);
    QCOMPARE(large.telemetry().powerMWt, 7500.0);
    QCOMPARE(large.telemetry().electricMW, 2400.0);
    large.setPowerTarget(5250.0);
    QCOMPARE(large.powerTarget(), 5250.0);
    QCOMPARE(large.telemetry().powerTargetMWt, 5250.0);
    large.setPump(0, false);
    QVERIFY(standard.telemetry().pumpCommand[0]);
    QVERIFY(!large.telemetry().pumpCommand[0]);
    large.advance(10.0);
    QCOMPARE(standard.telemetry().simSeconds, 0.0);
    large.loadPreset(CorePreset::LowPower);
    QCOMPARE(large.telemetry().powerMWt, 1500.0);
    large.reset();
    QCOMPARE(large.telemetry().powerMWt, 1500.0);
    QCOMPARE(large.model(), ReactorModel::Rbmkp2400);
    QCOMPARE(large.gridSize(), 24);
    QVERIFY(large.telemetry().fuelChannels > standard.telemetry().fuelChannels);
}

void CorePhysicsTest::nominalCoreIsCritical()
{
    ReactorCore core;
    boot(core);
    const Telemetry &t = core.telemetry();
    QVERIFY2(std::fabs(t.reactivityDollars) < 0.12,
             qPrintable(QStringLiteral("rho = %1 $").arg(t.reactivityDollars)));
    QVERIFY(std::fabs(t.powerMWt - 1000.0) < 1.0);
    QCOMPARE(t.fuelChannels, 180);
    QCOMPARE(t.controlChannels, 30);
    QCOMPARE(t.reflectorChannels, 38);
    QCOMPARE(t.totalChannels, 248);
}

void CorePhysicsTest::rodWorthCurveIsMonotonic()
{
    QVERIFY(std::fabs(ReactorCore::rodWorthCurve(0.0)) < 1e-9);
    QVERIFY(std::fabs(ReactorCore::rodWorthCurve(50.0) - 0.5) < 1e-9);
    QVERIFY(std::fabs(ReactorCore::rodWorthCurve(100.0) - 1.0) < 1e-9);

    double previous = -1.0;
    for (int pct = 0; pct <= 100; pct += 5) {
        const double w = ReactorCore::rodWorthCurve(pct);
        QVERIFY(w > previous);
        previous = w;
    }
}

void CorePhysicsTest::reactorPeriodSigns()
{
    QVERIFY(ReactorCore::reactorPeriod(0.05) > 0.0);
    QVERIFY(ReactorCore::reactorPeriod(-0.05) < 0.0);
    QVERIFY(ReactorCore::reactorPeriod(0.0) > 1e6);
    QVERIFY(ReactorCore::reactorPeriod(0.4) < ReactorCore::reactorPeriod(0.1));
    QVERIFY(ReactorCore::reactorPeriod(1.5) < ReactorCore::reactorPeriod(0.9));
}

void CorePhysicsTest::nominalHoldsPowerAndPressure()
{
    ReactorCore core;
    boot(core);
    core.advance(20.0);
    QVERIFY2(std::fabs(core.telemetry().powerMWt - 1000.0) < 150.0,
             qPrintable(QStringLiteral("power %1").arg(core.telemetry().powerMWt)));
    QVERIFY2(std::fabs(core.telemetry().pressureAtm - 68.0) < 6.0,
             qPrintable(QStringLiteral("pressure %1").arg(core.telemetry().pressureAtm)));
    QVERIFY(std::fabs(core.telemetry().coolantTempC - 284.0) < 12.0);
}

void CorePhysicsTest::coldPresetStaysColdAndDepressurised()
{
    ReactorCore core;
    core.initialize(CorePreset::ColdShutdown);
    core.stop();
    core.advance(30.0);

    const Telemetry &t = core.telemetry();
    QVERIFY2(t.coolantTempC < 90.0,
             qPrintable(QStringLiteral("coolant %1 degC").arg(t.coolantTempC)));
    QVERIFY2(t.pressureAtm < 20.0,
             qPrintable(QStringLiteral("pressure %1 atm").arg(t.pressureAtm)));
    QVERIFY(t.powerMWt < 5.0);
    QVERIFY(t.reactivityDollars < -0.5);
    QVERIFY(t.inletTempC < 60.0);
}

void CorePhysicsTest::extractingControlRodAddsReactivity()
{
    ReactorCore core;
    boot(core);
    const int rod = firstControlRod(core);
    QVERIFY(rod >= 0);

    const double before = core.telemetry().reactivityDollars;
    core.extractAssembly(rod);
    const double after = core.telemetry().reactivityDollars;

    QCOMPARE(core.channel(rod).type, AssemblyType::Empty);
    QCOMPARE(core.telemetry().controlChannels, 29);
    QVERIFY2(after > before + 0.05,
             qPrintable(QStringLiteral("before %1 $ after %2 $").arg(before).arg(after)));
}

void CorePhysicsTest::controlRodCanBeInsertedAndExtracted()
{
    ReactorCore core;
    boot(core);
    const int rod = firstControlRod(core);

    core.extractAssembly(rod);
    QCOMPARE(core.telemetry().controlChannels, 29);

    core.insertAssembly(rod, AssemblyType::Control);
    QCOMPARE(core.channel(rod).type, AssemblyType::Control);
    QCOMPARE(core.telemetry().controlChannels, 30);
    QVERIFY(std::fabs(core.channel(rod).rodTarget - core.bankTarget()) < 1e-6);

    // A control rod inserted straight into a fuel socket is a legal one-step swap.
    const int fuel = firstFuel(core);
    QVERIFY(fuel >= 0);
    core.swapAssembly(fuel, AssemblyType::Control);
    QCOMPARE(core.channel(fuel).type, AssemblyType::Control);
    QCOMPARE(core.telemetry().controlChannels, 31);
    QCOMPARE(core.telemetry().fuelChannels, 179);
}

void CorePhysicsTest::individualRodFollowsAndDetaches()
{
    ReactorCore core;
    boot(core);
    const int rod = firstControlRod(core);

    core.setRodTarget(rod, 5.0);
    QVERIFY(!core.channel(rod).followBank);
    core.advance(8.0); // drive moves at 14 %/s
    QVERIFY2(std::fabs(core.channel(rod).rodPosition - 5.0) < 0.5,
             qPrintable(QStringLiteral("pos = %1").arg(core.channel(rod).rodPosition)));

    // Moving the bank re-attaches every drive.
    core.setBankTarget(60.0);
    QVERIFY(core.channel(rod).followBank);
    core.advance(8.0);
    QVERIFY2(std::fabs(core.channel(rod).rodPosition - 60.0) < 0.5,
             qPrintable(QStringLiteral("pos = %1").arg(core.channel(rod).rodPosition)));
}

void CorePhysicsTest::scramDrivesEveryRodIn()
{
    ReactorCore core;
    boot(core);
    core.scram();
    core.advance(4.0);
    for (int i = 0; i < core.channelCount(); ++i) {
        if (core.channel(i).type == AssemblyType::Control)
            QVERIFY(core.channel(i).rodPosition > 99.0);
    }
    QVERIFY(core.telemetry().scrammed);
    QVERIFY(core.telemetry().powerMWt < 1000.0);
}

void CorePhysicsTest::pumpTripHeatsTheCore()
{
    ReactorCore core;
    boot(core);
    const double t0 = core.telemetry().fuelTempC;
    core.setAllPumps(false);
    core.advance(20.0);
    QVERIFY(core.telemetry().flowPct < 15.0);
    QVERIFY2(core.telemetry().fuelTempC > t0 + 40.0,
             qPrintable(QStringLiteral("fuel %1 -> %2")
                            .arg(t0)
                            .arg(core.telemetry().fuelTempC)));
}

void CorePhysicsTest::xenonBuildsAfterShutdown()
{
    ReactorCore core;
    boot(core);
    const double xe0 = core.telemetry().xenon;
    core.scram();
    core.advance(150.0);
    QVERIFY2(core.telemetry().xenon > xe0 + 0.05,
             qPrintable(QStringLiteral("xenon %1 -> %2").arg(xe0).arg(core.telemetry().xenon)));
    QVERIFY(core.telemetry().powerMWt < 50.0);
}

void CorePhysicsTest::swappingAssembliesChangesInventory()
{
    ReactorCore core;
    boot(core);
    const int fuel = firstFuel(core);
    QVERIFY(fuel >= 0);

    core.extractAssembly(fuel);
    QCOMPARE(core.channel(fuel).type, AssemblyType::Empty);
    QCOMPARE(core.telemetry().fuelChannels, 179);

    core.insertAssembly(fuel, AssemblyType::Reflector);
    QCOMPARE(core.channel(fuel).type, AssemblyType::Reflector);
    QCOMPARE(core.telemetry().reflectorChannels, 39);
}

void CorePhysicsTest::removingEveryRodDestroysTheCore()
{
    ReactorCore core;
    boot(core);

    for (int i = 0; i < core.channelCount(); ++i) {
        if (core.channel(i).type == AssemblyType::Control)
            core.extractAssembly(i);
    }
    QCOMPARE(core.telemetry().controlChannels, 0);
    QVERIFY(core.telemetry().reactivityDollars > 1.0); // prompt critical

    core.advance(60.0);
    QVERIFY2(core.telemetry().destroyed,
             qPrintable(QStringLiteral("fuel %1 degC, integrity %2")
                            .arg(core.telemetry().fuelTempC)
                            .arg(core.telemetry().integrity)));
}

void CorePhysicsTest::rebuildingRestoresTheCore()
{
    ReactorCore core;
    boot(core);

    for (int i = 0; i < core.channelCount(); ++i) {
        if (core.channel(i).type == AssemblyType::Control)
            core.extractAssembly(i);
    }
    core.advance(60.0);
    QVERIFY(core.telemetry().destroyed);

    core.reset();
    const Telemetry &t = core.telemetry();
    QVERIFY(!t.destroyed);
    QCOMPARE(t.controlChannels, 30);
    QCOMPARE(t.fuelChannels, 180);
    QCOMPARE(t.totalChannels, 248);
    QVERIFY(std::fabs(t.powerMWt - 1000.0) < 1.0);
    QVERIFY(std::fabs(t.integrity - 1.0) < 1e-9);
    QVERIFY(t.alarmLevel < 2);
}

// ---------------------------------------------------------- automatic power hold
void CorePhysicsTest::autoHoldDrivesPowerToTarget()
{
    ReactorCore core;
    boot(core);
    core.setPowerTarget(700.0);
    QVERIFY(core.autoHoldEngaged());
    core.advance(900.0);

    const Telemetry &t = core.telemetry();
    QVERIFY2(std::fabs(t.powerMWt - 700.0) < 21.0,   // within 3 %
             qPrintable(QStringLiteral("power %1 MWt").arg(t.powerMWt)));
    QVERIFY2(std::fabs(t.reactivityDollars) < 0.05,
             qPrintable(QStringLiteral("rho %1 $").arg(t.reactivityDollars)));
    QVERIFY2(t.autoHoldState == int(AutoHoldState::Holding),
             qPrintable(QStringLiteral("state %1").arg(t.autoHoldState)));
    QVERIFY(t.integrity > 0.999);
}

void CorePhysicsTest::autoHoldHoldsThroughXenonTransient()
{
    ReactorCore core;
    boot(core);
    core.setPowerTarget(400.0);
    core.advance(2400.0); // long enough for the post-reduction xenon peak

    const Telemetry &t = core.telemetry();
    QVERIFY2(std::fabs(t.powerMWt - 400.0) < 24.0,   // within 6 %
             qPrintable(QStringLiteral("power %1 MWt, xenon %2").arg(t.powerMWt).arg(t.xenon)));
    QVERIFY2(std::fabs(t.reactivityDollars) < 0.06,
             qPrintable(QStringLiteral("rho %1 $").arg(t.reactivityDollars)));
    QVERIFY(t.integrity > 0.98);
}

void CorePhysicsTest::autoHoldStartsAColdCore()
{
    ReactorCore core;
    core.initialize(CorePreset::ColdShutdown);
    core.stop();
    core.setPowerTarget(500.0);
    core.advance(1800.0);

    const Telemetry &t = core.telemetry();
    QVERIFY2(std::fabs(t.powerMWt - 500.0) < 40.0,   // within 8 %
             qPrintable(QStringLiteral("power %1 MWt").arg(t.powerMWt)));
    QVERIFY2(t.integrity > 0.98,
             qPrintable(QStringLiteral("integrity %1, peak fuel %2 degC")
                            .arg(t.integrity).arg(t.fuelTempC)));
    QVERIFY(t.autoHoldState == int(AutoHoldState::Holding)
            || t.autoHoldState == int(AutoHoldState::Raising)
            || t.autoHoldState == int(AutoHoldState::Lowering));
}

void CorePhysicsTest::autoHoldReleasesOnManualBankMove()
{
    ReactorCore core;
    boot(core);
    core.setPowerTarget(800.0);
    core.advance(5.0);
    QVERIFY(core.autoHoldEngaged());

    core.setBankTarget(60.0);
    QVERIFY(!core.autoHoldEngaged());
    QCOMPARE(core.telemetry().autoHoldState, int(AutoHoldState::Off));
    QVERIFY(std::fabs(core.bankTarget() - 60.0) < 1e-9);

    // ...and it stays released: the controller must not steal the bank back.
    core.advance(30.0);
    QVERIFY(std::fabs(core.bankTarget() - 60.0) < 1e-9);
}

void CorePhysicsTest::autoHoldReportsWhenNoDriveFollowsTheBank()
{
    ReactorCore core;
    boot(core);
    for (int i = 0; i < core.channelCount(); ++i) {
        if (core.channel(i).type == AssemblyType::Control)
            core.setRodTarget(i, 50.0, true); // trim every drive off the bank
    }
    const double bankBefore = core.bankTarget();
    core.setPowerTarget(700.0);
    core.advance(20.0);

    QCOMPARE(core.telemetry().autoHoldState, int(AutoHoldState::NoDrives));
    QVERIFY(std::fabs(core.bankTarget() - bankBefore) < 1e-9);
}

void CorePhysicsTest::autoHoldRefusesWhileScrammed()
{
    ReactorCore core;
    boot(core);
    core.scram();
    core.setPowerTarget(900.0);
    QVERIFY(!core.autoHoldEngaged());
    QCOMPARE(core.telemetry().autoHoldState, int(AutoHoldState::Off));

    // Cancelling the scram by moving the bank lets the controller take over again.
    core.setBankTarget(45.0);
    core.advance(1.0);
    core.setPowerTarget(900.0);
    QVERIFY(core.autoHoldEngaged());
}

QTEST_GUILESS_MAIN(CorePhysicsTest)
#include "core_tests.moc"
