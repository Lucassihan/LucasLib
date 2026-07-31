#include "scheduler.h"
#include "util.h"
#include "odometry.h"
#include "movement.h"

#include <cmath>
#include <cstdio>

bool (*conditionPtr1)() = nullptr;
void (*functionPtr1)() = nullptr;
bool (*conditionPtr2)() = nullptr;
void (*functionPtr2)() = nullptr;
bool (*conditionPtr3)() = nullptr;
void (*functionPtr3)() = nullptr;

void schedule(
    bool (*condition1)(), void (*function1)(),
    bool (*condition2)(), void (*function2)(),
    bool (*condition3)(), void (*function3)()
) {
    conditionPtr1 = condition1;
    functionPtr1 = function1;
    conditionPtr2 = condition2;
    functionPtr2 = function2;
    conditionPtr3 = condition3;
    functionPtr3 = function3;
}

void clearSchedule() {
    conditionPtr1 = nullptr;
    functionPtr1 = nullptr;
    conditionPtr2 = nullptr;
    functionPtr2 = nullptr;
    conditionPtr3 = nullptr;
    functionPtr3 = nullptr;
}

int schedulerTask() {
    while (true) {
        if (conditionPtr1 != nullptr && conditionPtr1()) {

            functionPtr1();

            conditionPtr1 = nullptr;
            functionPtr1 = nullptr;
        }

        if (conditionPtr2 != nullptr && conditionPtr2()) {

            functionPtr2();

            conditionPtr2 = nullptr;
            functionPtr2 = nullptr;
        }

        if (conditionPtr3 != nullptr && conditionPtr3()) {

            functionPtr3();

            conditionPtr3 = nullptr;
            functionPtr3 = nullptr;
        }

        delay(1);
    }
    return 0;
}