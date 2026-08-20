#pragma once

//Stage 2 of output governance — applied to combined left/right power AFTER blending drive + turn. Same four fields as the per-PID governors.
struct TotalOutput {
    double minOutput    = 0;     // floor when nonzero (overcome static friction), volts
    double maxOutput    = 12.0;  // hard cap on motor output (volts)
    double maxAccelRate = 0;     // max V/tick when ramping UP (0 = unlimited)
    double maxDecelRate = 0;     // max V/tick when ramping DOWN (0 = unlimited)
};

extern TotalOutput totalOutput;

//Applies all four governors to leftOut/rightOut: slew per-side, max cap proportional (ratio-preserving), min floor per-side.
void governOutputs();
