// debugger.cpp — see debugger/debugger.h.
//
// Captures movements + a 10ms tick stream into fixed RAM buffers during an
// auton, then prints a LUCASLIB_TRACE to the console. All hooks are no-ops
// until start() arms capture, so this is free when unused.

#include "debugger/debugger.h"

#include "coreConfigs.h"
#include "movement/movementUtils.h"
#include "movement/pid.h"
#include "movement/totalOutput.h"
#include "movement/odometry.h"
#include "ramsete/util/ramseteController.h"   // ramsete.kLat, for the header

#include <cstdio>
#include <cstdarg>
#include <cmath>

// External engine state we sample from (defined across movementUtils/movement).
extern double turnDisableLength;        // moveTo heading-disable radius (header field)

namespace debugger {

// ---- Capacity (tune if your autons are longer; ~10ms/tick) ----
static const int TICK_CAP  = 4000;   // 40 s of ticks
static const int MOVE_CAP  = 96;
static const int WALL_CAP  = 64;
static const int SCHED_CAP = 2000;   // baked rows, pooled across every trajectory

// Console dump is repeated this many times: the 1st copy is mangled when VEXcode
// (re)opens the USB port mid-stream; the rest stream clean once it's connected.
static const int DUMP_REPEATS = 4;

// Per-line console marker. The app keeps only lines that start with it and
// strips it, so screen noise on the terminal is filtered out on import.
#define DBG_MARK "~LLT|"

enum Kind { K_TURN, K_STRAIGHT, K_POINT, K_BOOMERANG, K_VOLTAGE };

struct Mv {
    int          index;
    const char*  type;        // string literal (static lifetime)
    Kind         kind;
    float        targetX, targetY, targetH, distance, heading, partiality;
    int          backwards, direction;
    float        dLead, gLead, leadChase, pathHeading, turnDisable, turnPriority;
    bool         cosineScaling;
    // armed exit conditions (settle is derivative-magnitude based: once inside
    // the exit zone, the move waits until |D term| < settleD before exiting)
    float        exTimeout, settleD, exDist, exPerp, exAng;
    const char*  exReason;
    uint32_t     startTime, endTime;
    // locked PID constants for the channels (captured on the movement's first tick)
    float        dKp, dKi, dKd, tKp, tKi, tKd;
    bool         constsCaptured;
    PID*         drivePidPtr;
    PID*         turnPidPtr;
    float        leftVolts, rightVolts;  // voldrive only
    // slice of the schedule pool this movement's baked rows occupy (0 rows = not
    // a trajectory, or the pool was already full)
    int          schedFirst, schedLen;
};

// One baked row, straight off TrajPoint. Kept apart from the tick stream because
// it is the PLAN: sampled by arc, not by time, and it exists in full before the
// first tick runs.
struct Sched {
    uint16_t mi;
    float    t;              // ms into the schedule's own clock
    float    x, y, h;
    float    v, w;           // in/s (signed) and deg/s
};

struct Tk {
    uint32_t t;
    uint16_t mi;
    uint8_t  stage;
    float    x, y, h;
    float    le, he, dp, hp, tp, sp, driveO, turnO, lo, ro;
    float    yi, xi;
    float    driveI, driveD, turnI, turnD;
    float    carrotX, carrotY, ghostX, ghostY, arcRadius, maxSlip, leadRet, perp;
    float    xt;          // ramsete.moveTo cross-track error (in)
    float    refX, refY, refH;  // where the trajectory said to be this tick
    float    refV, refW;        // …and how fast the schedule wanted to be going
    float    cmdV, cmdW;        // what the law asked the velocity layer for
    float    errFwd, errCross, errHead;  // robot-frame gap to the reference
    float    clockRate;         // the adaptive clock's throttle on reference time
    float    v, w;        // measured chassis speed: in/s and deg/s
    float    bat, mtemp;  // battery % + drive-motor temp average (°C), per tick
};

struct Wall {
    uint32_t t;
    uint16_t mi;
    int      wall, sensor;
    float    preX, preY, preH, postX, postY, postH;
};

// ---- State ----
static bool     capturing = false;
static bool     muted     = false;
static int      moveCount = 0;
static int      tickCount = 0;
static int      wallCount = 0;
static int      schedCount = 0;
static int      curMi     = -1;
static uint32_t startMs   = 0;

static Mv    moves[MOVE_CAP];
static Tk    ticks[TICK_CAP];
static Wall  walls[WALL_CAP];
static Sched scheds[SCHED_CAP];

static char autonName[40] = "auton";
static float startPoseX = 0, startPoseY = 0, startPoseH = 0;
static float batteryPct = -1, batteryVolts = 0;  // snapshot at start(); -1 = never armed

// Overlay telemetry published by boomerang / ramsete.moveTo, consumed by the
// next tick().
struct Pending {
    bool  set = false;
    float carrotX = 0, carrotY = 0, ghostX = 0, ghostY = 0;
    float leadRet = 0, maxSlip = 0;
    float crossTrack = 0;
    float refX = 0, refY = 0, refH = 0;   // trajectory reference pose
    float refV = 0, refW = 0;             // …and the schedule's velocity there
    bool  refSet = false;
    float cmdV = 0, cmdW = 0;
    float errFwd = 0, errCross = 0, errHead = 0;
    float clockRate = 0;
    int   stage = -1;  // -1 = use default staging
};
static Pending pending;

// Per-instance PID mirror. calculateOutput() pushes its running integral + locked
// gains here via samplePID(); tick() reads them back by pointer. This is what lets
// the PID class stay clean (no debugger-only fields on it). ≤8 slots covers the six
// shared PIDs. derivative isn't mirrored — it's a real public field tick() reads.
struct PidSnap {
    const PID* p;
    float      integral, kP, kI, kD;
};
static PidSnap pidSnaps[8];
static int     pidSnapCount = 0;

static const PidSnap* findSnap(const PID* p) {
    for (int i = 0; i < pidSnapCount; i++) {
        if (pidSnaps[i].p == p) return &pidSnaps[i];
    }
    return nullptr;
}

static uint32_t nowMs() {
    return (uint32_t) Brain.timer(msec);
}

// A pending nextMoveType(), waiting for the tag it renames.
static const char* overrideType = nullptr;

void nextMoveType(const char* type) {
    overrideType = type;
}

// Start a movement record. Returns a reference to fill in the type-specific bits.
static Mv& newMove(const char* type, Kind kind) {
    // Cleared even when the record is dropped below, so a movement that ran uncaptured can't
    // rename one that runs later.
    if (overrideType) {
        type = overrideType;
        overrideType = nullptr;
    }

    if (!capturing || moveCount >= MOVE_CAP) {
        static Mv sink;  // overflow sink — keeps callers safe past capacity
        return sink;
    }
    Mv& m = moves[moveCount];
    m = Mv{};
    m.index          = moveCount;
    m.type           = type;
    m.kind           = kind;
    m.exReason       = "settle";
    m.startTime      = nowMs() - startMs;
    m.endTime        = m.startTime;
    m.cosineScaling  = true;
    m.leadChase      = -1;
    m.constsCaptured = false;
    curMi = moveCount;
    moveCount++;
    return m;
}

// settleD carries ec.v now — the settle threshold is a chassis SPEED (in/s here),
// not the PID D term it used to be. The trace column keeps its old name so the
// debugger app's parser still reads it.
static void copyLateralEC(Mv& m, const LateralExitConditions& ec) {
    m.exTimeout = ec.t;
    m.settleD   = ec.v;
    m.exDist    = ec.settleWithin;  // settle activation radius
    m.exPerp    = ec.d;           // cross line offset toward the robot (perp-line exit)
    if (ec.d != -1)      m.exReason = "perpLine";
    else if (ec.v != -1) m.exReason = "settle";
    else                 m.exReason = "timeout";
}

// Same remap on the turn channel, where settleD is an angular speed (rad/s).
static void copyTurnEC(Mv& m, const TurnExitConditions& ec) {
    m.exTimeout = ec.t;
    m.settleD   = ec.v;
    m.exDist    = ec.settleWithin;  // settle activation zone — DEGREES on a turn
    m.exAng     = ec.d;           // ungated "close enough" angle exit
    // settle's zone is normally the wider of the two, so it trips first
    if (ec.v != -1)      m.exReason = "settle";
    else if (ec.d != -1) m.exReason = "angle";
    else                 m.exReason = "timeout";
}

// =========================================================================
// Lifecycle
// =========================================================================
void start(const char* name) {
    moveCount = tickCount = wallCount = schedCount = 0;
    pidSnapCount = 0;
    curMi = -1;
    startMs = nowMs();
    startPoseX = chassis.x;
    startPoseY = chassis.y;
    startPoseH = chassis.h;
    batteryPct   = (float) Brain.Battery.capacity();
    batteryVolts = (float) Brain.Battery.voltage(vex::voltageUnits::volt);
    snprintf(autonName, sizeof(autonName), "%s", name ? name : "auton");
    pending = Pending{};
    overrideType = nullptr;
    capturing = true;
}

// =========================================================================
// Movement tags
// =========================================================================
void moveTurn(const char* type, double targetHeading, const TurnExitConditions& ec, double partiality) {
    Mv& m = newMove(type, K_TURN);
    m.targetH     = targetHeading;
    m.heading     = targetHeading;
    m.partiality  = partiality;
    m.turnPidPtr  = (partiality < 0) ? &turnPID : (type[0] == 's' ? &swingPID : &turnPID);
    copyTurnEC(m, ec);
}

void movePointTurn(const char* type, double tx, double ty, const TurnExitConditions& ec, double partiality) {
    Mv& m = newMove(type, K_TURN);
    m.targetX     = tx;
    m.targetY     = ty;
    m.partiality  = partiality;
    m.turnPidPtr  = (partiality < 0) ? &turnPID : (type[0] == 's' ? &swingPID : &turnPID);
    copyTurnEC(m, ec);
}

void moveStraight(const char* type, double distance, double heading, const LateralExitConditions& ec, bool backward) {
    Mv& m = newMove(type, K_STRAIGHT);
    m.distance    = distance;
    m.heading     = heading;
    m.targetH     = heading;
    m.backwards   = backward ? 1 : 0;
    m.drivePidPtr = &drivePID;
    m.turnPidPtr  = &headingPID;
    copyLateralEC(m, ec);
}

void movePoint(const char* type, double tx, double ty, const LateralExitConditions& ec, bool backward,
               bool cosineScaling) {
    Mv& m = newMove(type, K_POINT);
    m.targetX     = tx;
    m.targetY     = ty;
    m.backwards   = backward ? 1 : 0;
    m.cosineScaling = cosineScaling;
    m.drivePidPtr = &drivePID;
    m.turnPidPtr  = &headingPID;
    copyLateralEC(m, ec);
}

void moveBoomerang(const BoomParams& bp, const LateralExitConditions& ec) {
    Mv& m = newMove("boomerangTo", K_BOOMERANG);
    m.targetX     = bp.x;
    m.targetY     = bp.y;
    m.targetH     = bp.heading;
    m.dLead       = bp.lead;
    m.gLead       = bp.gLead;
    m.leadChase   = bp.leadChasePowerLimit;
    m.turnPriority = 0.4f;
    m.cosineScaling = false;  // boomerang blends with cosine off
    m.backwards   = backward ? 1 : 0;
    m.drivePidPtr = &drivePID;
    m.turnPidPtr  = &headingPID;
    copyLateralEC(m, ec);
    if (ec.d < 0) m.exReason = "perpLine";  // boomerang's natural exit
}

void moveVoltage(double left, double right) {
    Mv& m = newMove("voldrive", K_VOLTAGE);
    m.leftVolts  = left;
    m.rightVolts = right;
    m.exReason   = "duration";
}

void movementExit(const char* reason) {
    if (!capturing || curMi < 0 || !reason) return;
    moves[curMi].exReason = reason;
}

void trajectorySchedule(const Trajectory& traj) {
    if (!capturing || curMi < 0) return;

    // All or nothing: half a schedule would draw as a path that stops in the
    // middle of the field, which is exactly the lie this section exists to stop
    // telling. Out of room, the app falls back to the per-tick reference trail.
    int rows = (int) traj.size();
    if (rows < 2 || schedCount + rows > SCHED_CAP) return;

    Mv& m = moves[curMi];
    m.schedFirst = schedCount;
    m.schedLen   = rows;

    for (int i = 0; i < rows; i++) {
        Sched& s = scheds[schedCount++];
        s.mi = (uint16_t) curMi;
        s.t  = (float) (traj[i].t * 1000.0);   // ms, like every other section
        s.x  = (float) traj[i].x;
        s.y  = (float) traj[i].y;
        s.h  = (float) traj[i].heading;
        s.v  = (float) traj[i].v;
        s.w  = (float) traj[i].ω;
    }
}

// =========================================================================
// Per-tick sampling (called from voltageDrive)
// =========================================================================
void tick() {
    if (!capturing || curMi < 0 || tickCount >= TICK_CAP) return;

    Mv& m = moves[curMi];

    // Lock in the channels' PID constants on the movement's first sampled tick
    // (by now each PID has run once, so its locked constants are valid).
    if (!m.constsCaptured) {
        if (const PidSnap* s = findSnap(m.drivePidPtr)) { m.dKp = s->kP; m.dKi = s->kI; m.dKd = s->kD; }
        if (const PidSnap* s = findSnap(m.turnPidPtr))  { m.tKp = s->kP; m.tKi = s->kI; m.tKd = s->kD; }
        m.constsCaptured = true;
    }

    Tk& t = ticks[tickCount++];
    t.t  = nowMs() - startMs;
    t.mi = (uint16_t) curMi;

    t.x = chassis.x;  t.y = chassis.y;  t.h = chassis.h;
    t.le = lateralError;  t.he = headingError;
    t.dp = drivePow;  t.hp = headingPow;  t.tp = turnPow;  t.sp = swingPow;
    t.driveO = driveOut;  t.turnO = turnOut;  t.lo = leftOut;  t.ro = rightOut;
    t.yi = chassis.yInchesTracked;  t.xi = chassis.xInchesTracked;
    t.arcRadius = chassis.travelArcRadius();

    // health: battery % + all six drive temps averaged. Both are reads of the
    // device-status cache vexos already maintains — no bus traffic, ~µs cost.
    t.bat   = (float) Brain.Battery.capacity();
    t.mtemp = (float)((fl.temperature(celsius) + ml.temperature(celsius) + bl.temperature(celsius) +
                       fr.temperature(celsius) + mr.temperature(celsius) + br.temperature(celsius)) / 6.0);

    // per-channel integral (from the samplePID snapshot) + derivative (real PID field)
    if (m.drivePidPtr) {
        if (const PidSnap* s = findSnap(m.drivePidPtr)) t.driveI = s->integral;
        t.driveD = m.drivePidPtr->derivative;
    }
    if (m.turnPidPtr) {
        if (const PidSnap* s = findSnap(m.turnPidPtr)) t.turnI = s->integral;
        t.turnD = m.turnPidPtr->derivative;
    }

    // overlay telemetry (boomerang carrot/ghost/slip, moveTo cross-track)
    t.carrotX = pending.carrotX;  t.carrotY = pending.carrotY;
    t.ghostX = pending.ghostX;    t.ghostY = pending.ghostY;
    t.leadRet = pending.leadRet;  t.maxSlip = pending.maxSlip;
    t.xt = pending.crossTrack;
    t.refX = pending.refX;  t.refY = pending.refY;  t.refH = pending.refH;
    t.refV = pending.refV;  t.refW = pending.refW;
    t.cmdV = pending.cmdV;  t.cmdW = pending.cmdW;

    // The stop command after the follow loop ticks too, and no probe ran behind
    // it — so the reference pose would read (0, 0, 0) and drag the drawn
    // reference path back to the field origin. Hold the last one instead. The
    // commands and errors below it stay zero, which is the truth on a stop tick.
    if (not pending.refSet and tickCount > 1 and ticks[tickCount - 2].mi == curMi) {
        const Tk& prev = ticks[tickCount - 2];
        t.refX = prev.refX;  t.refY = prev.refY;  t.refH = prev.refH;
    }

    t.errFwd = pending.errFwd;  t.errCross = pending.errCross;  t.errHead = pending.errHead;
    t.clockRate = pending.clockRate;

    // What the velocity layer is actually being asked to realize. ω is stored in
    // deg/s, the unit omegaController is scaled to and the tuner logs.
    t.v = (float) chassis.v;
    t.w = (float) TO_DEG(chassis.ω);

    // stage
    if (pending.stage >= 0)              t.stage = (uint8_t) pending.stage;
    else if (m.kind == K_POINT)          t.stage = close ? 1 : 0;
    else                                 t.stage = 0;

    m.endTime = t.t;
    pending = Pending{};
}

// =========================================================================
// PID internals mirror (called at the end of PID::calculateOutput)
// =========================================================================
void samplePID(const PID* pid, double integral, double kP, double kI, double kD) {
    if (!capturing || !pid) return;
    for (int i = 0; i < pidSnapCount; i++) {
        if (pidSnaps[i].p == pid) {
            pidSnaps[i] = { pid, (float) integral, (float) kP, (float) kI, (float) kD };
            return;
        }
    }
    if (pidSnapCount < (int) (sizeof(pidSnaps) / sizeof(pidSnaps[0]))) {
        pidSnaps[pidSnapCount++] = { pid, (float) integral, (float) kP, (float) kI, (float) kD };
    }
}

// =========================================================================
// Overlay telemetry probes
// =========================================================================
void probeTrajectory() {
    if (!capturing) return;
    const RamseteTelemetry& tm = ramsete.telemetry;

    pending.set       = true;
    pending.refSet    = true;
    pending.refX      = (float) tm.refX;
    pending.refY      = (float) tm.refY;
    pending.refH      = (float) tm.refHeading;
    pending.refV      = (float) tm.refV;
    pending.refW      = (float) tm.refω;
    pending.cmdV      = (float) tm.cmdV;
    pending.cmdW      = (float) tm.cmdω;
    pending.errFwd    = (float) tm.errX;
    pending.errCross  = (float) tm.errY;
    pending.errHead   = (float) tm.errHeading;
    pending.clockRate = (float) tm.clockRate;
}

void probeBoomerang(double carrotX, double carrotY, double ghostX, double ghostY,
                    double leadRetraction, double maxSlip, int stage) {
    if (!capturing) return;
    pending.carrotX = carrotX;  pending.carrotY = carrotY;
    pending.ghostX = ghostX;    pending.ghostY = ghostY;
    pending.leadRet = leadRetraction;  pending.maxSlip = maxSlip;
    pending.stage = stage;  pending.set = true;
}

void probeRamseteMove(double crossTrack) {
    if (!capturing) return;
    pending.crossTrack = crossTrack;  pending.set = true;
}

// =========================================================================
// Read-wall corrections
// =========================================================================
// Pre-correction pose, snapshotted by readWallBefore() just before setPos() runs.
static float preWallX = 0, preWallY = 0, preWallH = 0;

void readWallBefore() {
    if (!capturing) return;
    preWallX = chassis.x;  preWallY = chassis.y;  preWallH = chassis.h;
}

void readWall(int wall, int sensor) {
    if (!capturing || wallCount >= WALL_CAP) return;
    Wall& w = walls[wallCount++];
    w.t = nowMs() - startMs;  w.mi = (uint16_t) (curMi < 0 ? 0 : curMi);
    w.wall = wall;  w.sensor = sensor;
    w.preX = preWallX;    w.preY = preWallY;    w.preH = preWallH;       // from readWallBefore()
    w.postX = chassis.x;  w.postY = chassis.y;  w.postH = chassis.h;      // corrected pose (exact, post-setPos)
}

bool screensMuted() { return muted; }

// =========================================================================
// Dump
// =========================================================================
// When outFile is set, lines go to the SD card (bare, fast). Otherwise they go
// to the console with the marker prefix AND a throttle: the V5 serial TX buffer
// is small and silently DROPS bytes if you printf a whole trace flat-out, so we
// pause periodically to let it drain.
static FILE* outFile = nullptr;

static void emit(const char* s) {
    if (outFile) {
        fprintf(outFile, "%s\n", s);
        return;
    }
    // Console: wait until the serial FIFO (channel 1) has room for the WHOLE
    // line, THEN write it in one shot — so a line is never split or partially
    // dropped. The FIFO only frees as the host reads, so this also self-paces to
    // the host's real read rate (no brain-side overflow).
    char buf[700];
    int len = snprintf(buf, sizeof(buf), DBG_MARK "%s\n", s);
    while (vexSerialWriteFree(1) < len + 16) vex::this_thread::sleep_for(1);
    vexSerialWriteBuffer(1, (uint8_t*) buf, len);
}

static void emitf(const char* fmt, ...) {
    char buf[640];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    emit(buf);
}

static void dumpTrace() {
    emit("#LUCASLIB_TRACE v1");
    emitf("#auton_name=%s", autonName);

    // The brain has a real-time clock, so a trace says when it was taken. The
    // struct reports the year as an offset from 1980 on some firmware and in
    // full on others, so normalize. Unknown #keys are ignored by the importer.
    struct date d;
    struct time tm;
    vexGetdate(&d);
    vexGettime(&tm);
    int year = d.da_year < 1980 ? d.da_year + 1980 : d.da_year;
    emitf("#timestamp=%04d-%02d-%02d %02d:%02d:%02d",
          year, d.da_mon, d.da_day, tm.ti_hour, tm.ti_min, tm.ti_sec);
    emitf("#start_pose=%g,%g,%g", startPoseX, startPoseY, startPoseH);
    emitf("#battery_pct=%g", batteryPct);
    emitf("#battery_volts=%g", batteryVolts);
    emitf("#trackwidth=%g", chassis.getTrackwidth());
    emitf("#headingDisableDist=%g", turnDisableLength);
    emitf("#kLat=%g", ramsete.kLat);
    emitf("#gov_total_min=%g", totalOutput.minOutput);
    emitf("#gov_total_max=%g", totalOutput.maxOutput);
    emitf("#gov_total_accel=%g", totalOutput.maxAccelRate);
    emitf("#gov_total_decel=%g", totalOutput.maxDecelRate);
    emitf("#gov_drive_min=%g", drivePID.minOutput);
    emitf("#gov_drive_max=%g", drivePID.maxOutput);
    emitf("#gov_drive_accel=%g", drivePID.maxAccelRate);
    emitf("#gov_drive_decel=%g", drivePID.maxDecelRate);
    emitf("#gov_turn_min=%g", turnPID.minOutput);
    emitf("#gov_turn_max=%g", turnPID.maxOutput);
    emitf("#gov_turn_accel=%g", turnPID.maxAccelRate);
    emitf("#gov_turn_decel=%g", turnPID.maxDecelRate);

    emit("");
    emit("[MOVEMENTS]");
    emit("index,type,targetX,targetY,targetH,distance,backwards,direction,partiality,dLead,n,heading,"
         "cosineScaling,turnPriority,exitTimeout,settleD,exitDist,exitPerpLine,exitAng,exitReason,"
         "startTime,endTime,driveKp,driveKi,driveKd,turnKp,turnKi,turnKd,gLead,leadChasePowerLimit,"
         "pathHeading,turnDisableLength,leftVolts,rightVolts");
    for (int i = 0; i < moveCount; i++) {
        Mv& m = moves[i];
        emitf("%d,%s,%g,%g,%g,%g,%d,%d,%g,%g,0,%g,%d,%g,%g,%g,%g,%g,%g,%s,%u,%u,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g",
            m.index, m.type, m.targetX, m.targetY, m.targetH, m.distance, m.backwards, m.direction,
            m.partiality, m.dLead, m.heading, m.cosineScaling ? 1 : 0, m.turnPriority,
            m.exTimeout, m.settleD, m.exDist, m.exPerp, m.exAng, m.exReason, m.startTime, m.endTime,
            m.dKp, m.dKi, m.dKd, m.tKp, m.tKi, m.tKd, m.gLead, m.leadChase, m.pathHeading,
            m.turnDisable, m.leftVolts, m.rightVolts);
    }

    // The plan, per trajectory movement: every baked row, in order. Rows are
    // PathSettings.dt apart on the CLOCK, so they crowd up in distance where the
    // profile is slow — that cadence is the schedule's own, not a resampling of it.
    emit("");
    emit("[SCHEDULE]");
    emit("mi,t,x,y,heading,v,w");
    for (int i = 0; i < schedCount; i++) {
        Sched& s = scheds[i];
        emitf("%u,%g,%g,%g,%g,%g,%g", (unsigned) s.mi, s.t, s.x, s.y, s.h, s.v, s.w);
    }

    emit("");
    emit("[TICKS]");
    emit("t,mi,x,y,h,stage,le,he,dp,hp,tp,sp,do,to,lo,ro,yi,xi,leadX,leadY,leadRetraction,perpDist,"
         "driveI,driveD,turnI,turnD,carrotX,carrotY,ghostX,ghostY,arcRadius,maxSlip,xt,"
         "refX,refY,refH,refV,refW,cmdV,cmdW,errFwd,errCross,errHead,clockRate,v,w,bat,mtemp");
    for (int i = 0; i < tickCount; i++) {
        Tk& t = ticks[i];
        emitf("%u,%u,%g,%g,%g,%u,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,0,0,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g",
            t.t, (unsigned) t.mi, t.x, t.y, t.h, (unsigned) t.stage, t.le, t.he, t.dp, t.hp, t.tp, t.sp,
            t.driveO, t.turnO, t.lo, t.ro, t.yi, t.xi, t.leadRet, t.perp,
            t.driveI, t.driveD, t.turnI, t.turnD,
            t.carrotX, t.carrotY, t.ghostX, t.ghostY, t.arcRadius, t.maxSlip, t.xt,
            t.refX, t.refY, t.refH, t.refV, t.refW, t.cmdV, t.cmdW, t.errFwd, t.errCross, t.errHead,
            t.clockRate, t.v, t.w, t.bat, t.mtemp);
    }

    emit("");
    emit("[READWALLS]");
    emit("t,mi,wall,sensor,preX,preY,preH,postX,postY,postH");
    for (int i = 0; i < wallCount; i++) {
        Wall& w = walls[i];
        emitf("%u,%u,%d,%d,%g,%g,%g,%g,%g,%g",
            w.t, (unsigned) w.mi, w.wall, w.sensor, w.preX, w.preY, w.preH, w.postX, w.postY, w.postH);
    }

    emit("#END");
}

// Console dump, fired automatically once minSecondsSinceStart have elapsed since
// start(). VEXcode opens its terminal a beat AFTER the program starts, so a dump
// that fires immediately streams the start out before the terminal is connected
// (header/early ticks get lost). Holding off a few seconds guarantees it's up —
// no controller/screen input needed (the field disables the controller in auton).
// Pick a value that lands after your auton finishes but before the period ends.
// Then every line is written to the serial FIFO with retry-on-full so nothing
// drops; each carries the marker prefix.
static void stopToConsole(double minSecondsSinceStart = 3.0) {
    if (!capturing) return;
    capturing = false;

    uint32_t target = startMs + (uint32_t)(minSecondsSinceStart * 1000.0);
    while (nowMs() < target) vex::this_thread::sleep_for(20);

    muted = true;  // clean console + a steady brain status while dumping
    vex::this_thread::sleep_for(120);
    Brain.Screen.clearScreen();
    Brain.Screen.printAt(10, 30, "Exporting trace (hold ~10s)...");

    outFile = nullptr;
    // Dump several times. The 1st copy blocks until VEXcode (re)opens the USB
    // port, and the brain flushes the buffered start on connect, so that copy is
    // partial. Once the port is open the link is stable, so the later copies
    // stream complete — and the app imports only the LAST complete one.
    for (int i = 0; i < DUMP_REPEATS; i++) {
        dumpTrace();
        fflush(stdout);
        vex::this_thread::sleep_for(400);
    }

    Brain.Screen.printAt(10, 60, "Done. Select all + import.");
    muted = false;
}

// The only way out. The file is named for whatever start() was given, with a
// number appended if that name is taken, so a rerun never clobbers the trace
// you just collected.
void stop() {
    if (!capturing) return;
    capturing = false;

    // The name is user text, so anything FAT might choke on is folded to an underscore.
    char base[48];
    size_t j = 0;
    for (size_t i = 0; autonName[i] && j < sizeof(base) - 1; i++) {
        char c  = autonName[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-';
        base[j++] = ok ? c : '_';
    }
    if (j == 0) base[j++] = 't';   // never produce a bare ".txt"
    base[j] = '\0';

    char filename[56];
    snprintf(filename, sizeof filename, "%s.txt", base);
    for (int n = 2; n < 100 && Brain.SDcard.exists(filename); n++) {
        snprintf(filename, sizeof filename, "%s%d.txt", base, n);
    }

    FILE* f = fopen(filename, "w");
    if (!f) {
        // no card / open failed — fall back to the console dump so data isn't lost
        Brain.Screen.clearScreen();
        Brain.Screen.printAt(10, 30, "No SD card - dumping to console");
        capturing = true;  // re-arm so the console path runs
        stopToConsole();
        return;
    }
    outFile = f;
    dumpTrace();
    fclose(f);
    outFile = nullptr;

    Brain.Screen.clearScreen();
    Brain.Screen.printAt(10, 30, false, "Saved %s", filename);
    Brain.Screen.printAt(10, 60, false, "%d ticks / %d movements", tickCount, moveCount);
}

} // namespace debugger
