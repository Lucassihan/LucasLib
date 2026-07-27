#pragma once
// Schedules up to 3 condition-function pairs
void schedule(
    bool (*condition1)() = nullptr,
    void (*function1)() = nullptr,
    bool (*condition2)() = nullptr,
    void (*function2)() = nullptr,
    bool (*condition3)() = nullptr,
    void (*function3)() = nullptr
);

// Clears all scheduled functions
void clearSchedule();

// VEX task function
int schedulerTask();

bool cross(double x, double y, double crossD = 0);