#include "main.h"
#include "auton.h"
#include "coreConfigs.h"
#include "mcl/mcl.h"
#include "misc/controllerButtonMonitoring.h"
#include "misc/driverControllers.h"
#include "misc/util.h"

#include <cmath>
#include <cstdio>

void startTasks() { //Put your tasks in here (or you can just put them directly into initialize it doesn't rly matter lol)
    //Robot position tracking task (ODOM)
    task OdomTask(odometryTask);

    //Monte Carlo Localization correction task. It idles until you call StartMCL(x, y)
    //at the start of an autonomous routine (which seeds the particle cloud).
    // task MCLTask(mclTask);

    //Controller / brain screen data printing, functions are in main. Feel free to change the functions to whatever you want
    task controllerScreen(controllerScreenTask);
    task brainScreenTask(brainScreenTaskFcn);
    task fourBar(fourBarControlTask);

    //Put your own tasks in here too!
}