#pragma once

//Passed as a parameter to the lateral movement functions in movement.cpp.
struct BlendSettings {
    bool   cosineScaling = true;  // multiply drive by cos(headingError) before blending, so a wrong-facing robot slows down
    double turnPriority  = 1;   // [0, 1]: 0 = pure proportional normalization, 1 = drive absorbs all the cut and turn is untouched
};
