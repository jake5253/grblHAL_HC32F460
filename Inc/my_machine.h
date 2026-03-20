/*
  my_machine.h - configuration for HC32F460 ARM processors

  Part of grblHAL
*/

#pragma once

// Default local board target.
#define BOARD_VOXELAB_AQUILA_V102

#define N_AXIS                  3
#define COMPATIBILITY_LEVEL     0
#define BUILD_INFO              "FFP0173_Aquila_Main_Board_V1.0.2"

// Board-default machine profile for the stock Aquila kinematics.
// These values become active after reflashing and resetting settings with $RST=*.
#define DEFAULT_X_STEPS_PER_MM  1600.0f
#define DEFAULT_Y_STEPS_PER_MM  1600.0f
#define DEFAULT_Z_STEPS_PER_MM  1600.0f

#define DEFAULT_X_MAX_RATE      1200.0f
#define DEFAULT_Y_MAX_RATE      1200.0f
#define DEFAULT_Z_MAX_RATE      800.0f

#define DEFAULT_X_ACCELERATION  300.0f
#define DEFAULT_Y_ACCELERATION  300.0f
#define DEFAULT_Z_ACCELERATION  100.0f

#define DEFAULT_X_MAX_TRAVEL    300.0f
#define DEFAULT_Y_MAX_TRAVEL    200.0f
#define DEFAULT_Z_MAX_TRAVEL    45.0f

// Default probe input is enabled by the core when not overridden.
// Keep coolant enabled for baseline support.
