#include "coreConfigs.h"

brain Brain;
controller Controller = controller(primary);

// Chassis
motor fl = motor(PORT10, ratio18_1, true);
motor ml = motor(PORT21, ratio6_1, true);////////////////////
motor bl = motor(PORT20, ratio18_1, true);

motor fr = motor(PORT13, ratio18_1, false);
motor mr = motor(PORT21, ratio6_1, false);///////////////////////
motor br = motor(PORT15, ratio18_1, false);

motor_group leftdrive = motor_group(fl, ml, bl);
motor_group rightdrive = motor_group(fr, mr, br);

// Odometry and Heading
inertial IMU1 = inertial(PORT1);
inertial IMU2 = inertial(PORT4);

rotation TX = rotation(PORT6, false);
rotation TY = rotation(PORT21, true);

//Wall reading distance sensors
distance DistSensor1 = distance(PORT8);
distance DistSensor2 = distance(PORT3);
distance DistSensor3 = distance(PORT16);
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
  .imu1_multiplier     = 1.003764115433,
  .imu2_multiplier     = 1,

  //Tracker wheel effective diameters
  .yTrackerDiameter   = 1000 * 0.99625,
  .xTrackerDiameter   = 2,

  //Tracking wheel distance from robot tracking center on the corresponding axis.
  .yTrackerOffset     = 0,
  .xTrackerOffset     = 0,

  //These are used for both motor encoder tracking (if there is no Y tracker), or for expected heading calculation if there are 2 IMUs
  .trackwidth         = 13.5,
  .driveWheelDiameter = 3.25,
  .driveGearRatio     = 60.0 / 36.0,
  .driveEncoder       = front,

  //Distance sensor reference readings -- park the robot at (48, 48) and record what each sensor reads to the CLOSEST WALL. You will need to rotate the robot to get all the sensors.
  .sensor1Offset      = 18.97,
  .sensor2Offset      = 11.9,
  .sensor3Offset      = 19.1,
  .sensor4Offset      = 18.7,
});