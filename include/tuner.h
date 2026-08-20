#pragma once

// Turn PID auto-tuner: searches for the best kD (startKD 0 = auto, kP*2) and prints it to the Brain + Controller.
// Blocking, ~30-60s. Trigger it ONCE behind a button press or it relaunches the instant it finishes. Hold B to abort.
void turnTunerAuton(double kP, double startKD = 0);

// Same, driving in a straight line instead of turning. Needs ~4+ feet of clear floor.
void driveTunerAuton(double kP, double startKD = 0);

// Set true to abort an in-progress tuner; motors stop within ~10ms and the best-so-far values stay on screen. Auto-resets when a tuner starts.
extern bool stopTuning;
