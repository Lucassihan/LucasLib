#pragma once

// Turn PID auto-tuner. Pass your desired kP and an optional starting kD (0 =
// auto, starts at kP*2). It searches for the best kD and prints the result to
// the Brain + Controller screens. Blocking (~30-60s); trigger it ONCE behind a
// button press — do NOT call it unconditionally in the driver loop, or it will
// relaunch the instant it finishes. Hold B (or set stopTuning = true) to abort.
void turnTunerAuton(double kP, double startKD = 0);

// Drive PID auto-tuner. Same idea as turnTunerAuton but drives forward/backward
// in a straight line instead of turning. Pass your desired kP and an optional
// starting kD. Needs ~4+ feet of clear floor in front of the robot.
void driveTunerAuton(double kP, double startKD = 0);

// Set true to abort an in-progress tuner. The motors stop within ~10ms and the
// best-so-far values stay frozen on screen. Auto-reset to false each time a
// tuner starts.
extern bool stopTuning;
