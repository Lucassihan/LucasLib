#pragma once
#include "vex.h"

//Per-button state. Every controller button (no joysticks) gets one of these.
struct ButtonState {
    //--- Configurable ------------------------------------------------------

    //ms threshold for tap vs hold classification.
    //If a button is released with HeldFor < TapTime, WasTapped is set. Otherwise WasHeld is set.
    uint32_t TapTime = 250;

    //--- Raw state ---------------------------------------------------------

    //true while the button is currently pressed
    bool Pressing = false;

    //--- One-cycle edge events --------------------------------------------

    //true for exactly one cycle on the rising edge of a press
    bool NewPress = false;

    //true for exactly one cycle on the falling edge of a release
    bool JustReleased = false;

    //true for exactly one cycle on release if held < TapTime
    bool WasTapped = false;

    //true for exactly one cycle on release if held >= TapTime
    bool WasHeld = false;

    //--- Duration ---------------------------------------------------------

    //ms accumulated while currently pressed; resets to 0 on release
    uint32_t HeldFor = 0;

    //snapshot of total hold duration; valid for one cycle alongside JustReleased, else 0
    uint32_t PressTime = 0;

    //--- Combo classification ---------------------------------------------
    //Set on the press's rising edge. Stays stable until release.
    //pressedFirst  = this button was the FIRST of any held combination
    //pressedSecond = this button was pressed while another was already held

    bool pressedFirst = false;
    bool pressedSecond = false;

    //--- User exhaustion flag ---------------------------------------------
    //Set true in user code to mark a press's action as consumed. Auto-resets on release.

    bool Exhausted = false;

    //--- Internal ---------------------------------------------------------

    const vex::controller::button* _btn = nullptr; //link to underlying VEX button
    bool _prev = false;                      //previous-cycle Pressing
};

//The 12 controller buttons (no joysticks).
extern ButtonState l1, l2, r1, r2;
extern ButtonState upArrow, downArrow, leftArrow, rightArrow;
extern ButtonState a, b, x, y;

//True when none of the 12 tracked buttons are currently pressed.
extern bool noButtonPressed;

//Call every cycle in the driver-control loop to refresh all button states.
void controllerButtonMonitoringFcn();
