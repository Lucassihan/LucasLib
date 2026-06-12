#include "main.h"
#include "auton.h"
#include "coreConfigs.h"
#include "misc/controllerButtonMonitoring.h"
#include "misc/driverControllers.h"
#include "misc/util.h"

#include <cmath>
#include <cstdio>

void startTasks() { //Put your tasks in here (or you can just put them directly into initialize it doesn't rly matter lol)
    //Robot position tracking task (ODOM)
    task OdomTask(odometryTask);

    //Controller / brain screen data printing, functions are in main. Feel free to change the functions to whatever you want
    task controllerScreen(controllerScreenTask);
    task brainScreenTask(brainScreenTaskFcn);

    //Roller control task used in Push Back
    //Running roller controls in a separate thread allows me to use things like delays without blocking the main driver / auton loop
    //Being able to run delays makes writing color sort possible 
    //(there is a way to do it without using delays and a separate thread but its HARD and pointless)
    task magControlTask(magControlFcn);

    //Put your own tasks in here too!
}