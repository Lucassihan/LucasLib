#pragma once

//Stage 2 of output governance — applied to combined left/right power AFTER blending drive + turn.
//Same four fields as the per-PID governors. See "Output Governors — spec" in the notebook.
struct TotalOutput {
    double minOutput    = 0;     // floor when nonzero (overcome static friction), volts
    double maxOutput    = 12.0;  // hard cap on motor output (volts)
    double maxAccelRate = 0;     // max V/tick when ramping UP (0 = unlimited)
    double maxDecelRate = 0;     // max V/tick when ramping DOWN (0 = unlimited)
};

extern TotalOutput totalOutput;

//Applies all four total-output governors to leftOut/rightOut globals.
//Slew is per-side. Max cap is proportional (ratio-preserving). Min floor is per-side.
//Reads lastLeftOut/lastRightOut globals (must persist across loop iterations) and totalOutput.
void governOutputs();
