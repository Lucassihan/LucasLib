#pragma once
#include "vex.h"

//Per-button state. Every controller button (no joysticks) gets one of these.
struct ButtonState {
    uint32_t TapTime = 250;        //ms threshold: released under it sets WasTapped, over it sets WasHeld

    bool Pressing = false;         //true while the button is currently pressed

    //--- One-cycle edge events ---
    bool NewPress = false;         //rising edge of a press
    bool JustReleased = false;     //falling edge of a release
    bool WasTapped = false;        //released after less than TapTime
    bool WasHeld = false;          //released after TapTime or more

    uint32_t HeldFor = 0;          //ms accumulated while pressed; resets to 0 on release
    uint32_t PressTime = 0;        //total hold duration, valid for one cycle alongside JustReleased, else 0

    //--- Combo classification, set on the rising edge and stable until release ---
    bool pressedFirst = false;     //this button was the FIRST of any held combination
    bool pressedSecond = false;    //pressed while another was already held

    bool Exhausted = false;        //set in user code to mark a press consumed; auto-resets on release

    //--- Internal ---
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
