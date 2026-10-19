// #include "coreConfigs.h"

// brain Brain;
// controller Controller = controller(primary);

// // Chassis
// motor fl = motor(PORT10, ratio18_1, false);
// motor ml = motor(PORT9, ratio6_1, true);
// motor bl = motor(PORT8, ratio6_1, true);

// motor fr = motor(PORT18, ratio18_1, false);
// motor mr = motor(PORT20, ratio6_1, true);
// motor br = motor(PORT19, ratio6_1, false);

// motor_group leftdrive = motor_group(fl, ml, bl);
// motor_group rightdrive = motor_group(fr, mr, br);

// // Odometry and Heading
// inertial IMU1 = inertial(PORT21);
// inertial IMU2 = inertial(PORT7);

// rotation TX = rotation(PORT6, false);
// rotation TY = rotation(PORT6, true);

// //Wall reading distance sensors
// distance DistSensor1 = distance(PORT7);
// distance DistSensor2 = distance(PORT3);
// distance DistSensor3 = distance(PORT7);
// distance DistSensor4 = distance(PORT9);

// //Initiation of the odometry class
// Odometry chassis({
// //Main sensors for position tracking -- If they are unused, set them to nullptr or just delete the line. 
// //For example, if you don't have a second IMU, set imu2 = nullptr, and the code will only use imu1 for heading tracking.
//   .imu1               = &IMU1,
//   .imu2               = nullptr,
//   .yTracker           = nullptr,
//   .xTracker           = nullptr,

//   //IMUs usually need a multiplier coefficient; they are never 100% accurate. All IMUs have different multipliers.
//   .imu1_multiplier     = 1.003764115433,
//   .imu2_multiplier     = 1,

//   //Tracker wheel effective diameters
//   .yTrackerDiameter   = 1000 * 0.99625,
//   .xTrackerDiameter   = 2,

//   //Tracking wheel distance from robot tracking center on the corresponding axis.
//   .yTrackerOffset     = 0,
//   .xTrackerOffset     = 0,

//   //These are used for both motor encoder tracking (if there is no Y tracker), or for expected heading calculation if there are 2 IMUs
//   .trackwidth         = 12.5,
//   .driveWheelDiameter = 2.75,
//   .driveGearRatio     = 36.0 / 48.0,   // .0 REQUIRED: 36/48 is integer division and evaluates to 0
//   .driveEncoder       = back, //Motor choosed for no tracker wheel odometry. Either front motors, middle motors, or back motors.

//   //Distance sensor mounts -- measure each sensor face's position from the tracking center,
//   //in the ROBOT's frame: y = inches in front of center (+Y), x = inches to the right of
//   //center (+X). Both are signed. Used to translate a wall reset back to the center.
//   .sensor1Offset      = {.y = 0, .x = 0},
//   .sensor2Offset      = {.y = 0, .x = 0},
//   .sensor3Offset      = {.y = 0, .x = 0},
//   .sensor4Offset      = {.y = 0, .x = 0},
// });

#include "coreConfigs.h"

brain Brain;
controller Controller = controller(primary);

// Chassis
motor fl = motor(PORT10, ratio18_1, true);
motor ml = motor(PORT9, ratio6_1, true);
motor bl = motor(PORT8, ratio6_1, true);

motor fr = motor(PORT16, ratio18_1, false);
motor mr = motor(PORT19, ratio6_1, false);
motor br = motor(PORT18, ratio6_1, false);

motor_group leftdrive = motor_group(fl, ml, bl);
motor_group rightdrive = motor_group(fr, mr, br);

// Odometry and Heading
inertial IMU1 = inertial(PORT11);
inertial IMU2 = inertial(PORT7);

rotation TX = rotation(PORT6, false);
rotation TY = rotation(PORT6, true);

//Wall reading distance sensors
distance DistSensor1 = distance(PORT7);
distance DistSensor2 = distance(PORT3);
distance DistSensor3 = distance(PORT7);
distance DistSensor4 = distance(PORT9);

//Initiation of the odometry class
Odometry chassis({
//Main sensors for position tracking -- If they are unused, set them to nullptr or just delete the line. 
//For example, if you don't have a second IMU, set imu2 = nullptr, and the code will only use imu1 for heading tracking.
  .imu1               = &IMU1,
  .imu2               = nullptr,
  .yTracker           = nullptr,
  .xTracker           = nullptr,

  //IMUs usually need a multiplier coefficient; they are never 100% accurate. All IMUs have different multipliers.
  .imu1_multiplier     = 1,
  .imu2_multiplier     = 1,

  //Tracker wheel effective diameters
  .yTrackerDiameter   = 1000 * 0.99625,
  .xTrackerDiameter   = 2,

  //Tracking wheel distance from robot tracking center on the corresponding axis.
  .yTrackerOffset     = 0,
  .xTrackerOffset     = 0,

  //These are used for both motor encoder tracking (if there is no Y tracker), or for expected heading calculation if there are 2 IMUs
  .trackwidth         = 13.5,
  .driveWheelDiameter = 2.7994867608,
  .driveGearRatio     = 48.0/36.0,
  .driveEncoder       = middle, //Motor choosed for no tracker wheel odometry. Either front motors, middle motors, or back motors.

  //Distance sensor mounts -- measure each sensor face's position from the tracking center,
  //in the ROBOT's frame: y = inches in front of center (+Y), x = inches to the right of
  //center (+X). Both are signed. Used to translate a wall reset back to the center.
  .sensor1Offset      = {.y = 0, .x = 0},
  .sensor2Offset      = {.y = 0, .x = 0},
  .sensor3Offset      = {.y = 0, .x = 0},
  .sensor4Offset      = {.y = 0, .x = 0},
});