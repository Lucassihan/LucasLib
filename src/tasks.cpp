#include "main.h"
#include "auton.h"
#include "coreConfigs.h"
#include "misc/controllerButtonMonitoring.h"
#include "misc/driverControllers.h"
#include "misc/util.h"
#include "scheduler.h"
#include "vex_task.h"

#include <cmath>
#include <cstdio>

void startTasks() { //Put your tasks in here (or you can just put them directly into initialize it doesn't rly matter lol)
    //Robot position tracking task (ODOM)
    task OdomTask(odometryTask);

    //Controller / brain screen data printing, functions are in main. Feel free to change the functions to whatever you want
    task ControllerScreen(controllerScreenTask);
    task BrainScreenTask(brainScreenTaskFcn);
    task Scheduler(schedulerTask);

    task FourBar(fourBarControlTask);

    task AutoClamp(autoClampTask);
    task Scoop(scoopTask);
}