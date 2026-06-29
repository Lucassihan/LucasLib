#pragma once

//These are initiated as parameters inside lateral movement functions in movement.cpp.
struct BlendSettings {
    bool   cosineScaling = true;  // multiplies drive by cos(headingError) before blending — slows forward motion when robot is pointed wrong
    double turnPriority  = 1;   // [0, 1]: 0 = pure proportional normalization, 1 = full turn-priority (drive absorbs all the cut, turn untouched)
};
