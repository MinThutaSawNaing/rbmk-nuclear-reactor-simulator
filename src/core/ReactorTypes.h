#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

namespace rbmk {

/*! Every core socket is a physical channel. It can be empty, or hold one
 *  removable assembly: a fuel bundle, a control rod drive, or a water
 *  reflector column. The user can exchange these freely at run time. */
enum class AssemblyType {
    Empty = 0,
    Fuel,
    Control,
    Reflector
};

inline const char *assemblyName(AssemblyType t)
{
    switch (t) {
    case AssemblyType::Fuel:      return "FUEL";
    case AssemblyType::Control:   return "CONTROL ROD";
    case AssemblyType::Reflector: return "REFLECTOR";
    case AssemblyType::Empty:     break;
    }
    return "EMPTY";
}

inline QString assemblyNameQ(AssemblyType t)
{
    return QString::fromLatin1(assemblyName(t));
}

/*! One channel / socket of the IZ-250 upper biological shield grid. */
struct Channel
{
    int row = -1;
    int col = -1;
    int index = -1;
    QString coord;            //!< e.g. "K-09"
    double x = 0.0;           //!< layout position in the 600x600 core drawing space
    double y = 0.0;
    double distance = 0.0;    //!< radial distance from core centre (layout units)
    bool inside = false;      //!< socket lies inside the circular shield

    AssemblyType type = AssemblyType::Empty;

    double rodPosition = 0.0; //!< actual insertion, 0 = fully withdrawn, 100 = fully inserted
    double rodTarget = 0.0;   //!< drive set point
    bool followBank = true;   //!< true while the rod tracks the bank set point
    double rodWorth = 0.0;    //!< relative worth of this drive position (radial weight)

    double localTemp = 0.0;   //!< local channel temperature, deg C
    double localFlux = 0.0;   //!< local flux relative to nominal
    double localXenon = 0.0;  //!< local xenon load (0..2, normalised)
    double burnup = 0.0;      //!< 0..1, slowly accumulated

    bool hasAssembly() const { return type != AssemblyType::Empty; }
    bool isControlRod() const { return type == AssemblyType::Control; }
};

/*! Snapshot of everything the control room displays. */
struct Telemetry
{
    double powerMWt = 0.0;
    double fuelTempC = 0.0;
    double coolantTempC = 0.0;
    double pressureAtm = 0.0;
    double flowPct = 0.0;
    double inletTempC = 270.0;
    double reactivityDollars = 0.0;
    double reactivityPcm = 0.0;
    double flux = 0.0;
    double xenon = 0.0;
    double iodine = 0.0;
    double xenonDollars = 0.0;
    double voidFraction = 0.0;
    double periodSeconds = 0.0;   //!< +inf stable, negative = decaying
    double orm = 0.0;             //!< equivalent control rods inserted
    double integrity = 1.0;       //!< 0..1 core integrity
    double burnup = 0.0;
    double energyMWh = 0.0;
    double simSeconds = 0.0;
    double bankTarget = 0.0;
    double bankAverage = 0.0;

    int fuelChannels = 0;
    int controlChannels = 0;
    int reflectorChannels = 0;
    int emptyChannels = 0;
    int totalChannels = 0;
    int rodsBelowHalf = 0;

    //! One flag per main circulation pump, in MCP-1..MCP-n order; true = running.
    //! The vector is sized at run time by ReactorCore::pumpCount() - eight on the
    //! 2400 MW-class machine, where a four-pump train cannot carry the heat load.
    QVector<bool> pumps;
    int pumpCount = 0;

    // ---- steam turbine / generator train -----------------------------------
    double turbineRpm = 3000.0;      //!< shaft speed, rated 3000 rpm
    double turbineValvePct = 100.0;  //!< governor valve position, 0..100
    double turbineMW = 0.0;          //!< electrical export at the generator
    bool turbineTripped = false;     //!< trip latched, governor valve shut
    bool turbineSynced = true;       //!< shaft on grid speed (>= 2950 rpm)
    bool scrammed = false;
    bool destroyed = false;
    bool pipeRupture = false;
    bool reliefOpen = false;
    bool paused = false;

    // power-hold autopilot
    bool autoHold = false;            //!< controller engaged
    double powerTargetMWt = 0.0;      //!< set point, 0 while released
    int autoHoldState = 0;            //!< rbmk::AutoHoldState as an int for the UI
    QString autoHoldStatus;           //!< one-line controller state

    int alarmLevel = 0;           //!< 0 nominal, 1 warning, 2 critical
    QString statusText;           //!< short banner text
    QString statusDetail;         //!< longer explanation for the tooltip
};

struct LogEntry
{
    QString timestamp;
    QString text;
    int severity = 0;             //!< 0 info, 1 warning, 2 critical, 3 operation
};
Q_DECLARE_METATYPE(rbmk::LogEntry)
Q_DECLARE_METATYPE(rbmk::AssemblyType)

} // namespace rbmk
