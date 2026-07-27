#include "misc/controllerButtonMonitoring.h"
#include "coreConfigs.h"

using namespace vex;

ButtonState l1, l2, r1, r2;
ButtonState upArrow, downArrow, leftArrow, rightArrow;
ButtonState a, b, x, y;

bool noButtonPressed = true;

//Array of all tracked buttons. Order doesn't matter for correctness, but the order here
//determines which button "wins" the pressedFirst claim when two are pressed on the same cycle.
static ButtonState* allButtons[] = {
    &l1, &l2, &r1, &r2,
    &upArrow, &downArrow, &leftArrow, &rightArrow,
    &a, &b, &x, &y
};
static const int N = sizeof(allButtons) / sizeof(allButtons[0]);

//Lazy binding: hook each ButtonState to the underlying VEX controller button on first call.
static void ensureBindings() {
    if (l1._btn != nullptr) {
        return;
    }
    l1._btn = &Controller.ButtonL1;
    l2._btn = &Controller.ButtonL2;
    r1._btn = &Controller.ButtonR1;
    r2._btn = &Controller.ButtonR2;
    upArrow._btn = &Controller.ButtonUp;
    downArrow._btn = &Controller.ButtonDown;
    leftArrow._btn = &Controller.ButtonLeft;
    rightArrow._btn = &Controller.ButtonRight;
    a._btn = &Controller.ButtonA;
    b._btn = &Controller.ButtonB;
    x._btn = &Controller.ButtonX;
    y._btn = &Controller.ButtonY;
}

void controllerButtonMonitoringFcn() {
    ensureBindings();

    static uint32_t lastTickTime = vex::timer::system();
    uint32_t now = vex::timer::system();
    uint32_t dt = now - lastTickTime;

    //Clamp dt to handle the case where this function wasn't called for a while
    //(e.g. during autonomous). Otherwise the first tick after a gap dumps the whole
    //elapsed period into HeldFor for whatever happens to be pressed.
    if (dt > 50) {
        dt = 50;
    }

    //Pass 1: read raw current state for every button.
    for (int i = 0; i < N; i++) {
        allButtons[i]->Pressing = allButtons[i]->_btn->pressing();
    }

    //Pass 2: per-button derived state.
    bool anyPressed = false;
    for (int i = 0; i < N; i++) {
        ButtonState& s = *allButtons[i];

        //Edges
        s.NewPress = (s.Pressing && !s._prev);
        s.JustReleased = (!s.Pressing && s._prev);

        //Tap vs Hold classification + PressTime snapshot — all on the release edge.
        //Capture BEFORE the HeldFor reset below so we still have the duration.
        s.WasTapped = false;
        s.WasHeld = false;
        if (s.JustReleased) {
            s.PressTime = s.HeldFor;
            if (s.HeldFor < s.TapTime) {
                s.WasTapped = true;
            } else {
                s.WasHeld = true;
            }
        } else {
            s.PressTime = 0;
        }

        //HeldFor: accumulate while held, reset on release.
        if (s.Pressing) {
            s.HeldFor += dt;
        } else {
            s.HeldFor = 0;
        }

        //User-controlled Exhausted flag auto-resets on release.
        if (s.JustReleased) {
            s.Exhausted = false;
        }

        if (s.Pressing) {
            anyPressed = true;
        }
    }

    //Pass 3: combo classification (needs cross-button visibility).
    //On a button's rising edge, it's a combo if any OTHER button is currently
    //marked pressedFirst. The classification stays until release.
    for (int i = 0; i < N; i++) {
        ButtonState& s = *allButtons[i];

        if (s.NewPress) {
            bool combo = false;
            for (int j = 0; j < N; j++) {
                if (j == i) {
                    continue;
                }
                if (allButtons[j]->pressedFirst) {
                    combo = true;
                    break;
                }
            }
            s.pressedFirst = !combo;
            s.pressedSecond = combo;
        }

        if (!s.Pressing) {
            s.pressedFirst = false;
            s.pressedSecond = false;
        }
    }

    noButtonPressed = !anyPressed;

    //Pass 4: stash current state for next-cycle edge detection.
    for (int i = 0; i < N; i++) {
        allButtons[i]->_prev = allButtons[i]->Pressing;
    }

    lastTickTime = now;
}
