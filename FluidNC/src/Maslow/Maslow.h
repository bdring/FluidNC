// Copyright (c) 2024 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#pragma once
#include <Arduino.h>
#include "MotorUnit.h"
#include "Calibration.h"
#include "../System.h"  // sys.*
#include "../Planner.h"
#include <nvs.h>
#include "FreeRTOS.h"
#include "semphr.h"

#define TCAADDR 0x70

#define CALIBRATION_GRID_SIZE_MAX (10*10)+2

#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4

#define HORIZONTAL 0
#define VERTICAL 1

#define MASLOW_TELEM_FILE "M4_telemetry.bin"

// Common Default strings - especially used by config
const std::string M = "Maslow";
// Non-volatile storage name
//const char * nvs_t = "maslow";

struct TelemetryFileHeader {
    unsigned int structureSize; // 4 bytes
    char version[10];       // 10
    // if you add to the header take bytes from this
    char _unused[64]; // 64 bytes
};

struct TelemetryData {
    unsigned long timestamp;
    // motors
    double tlCurrent;
    double trCurrent;
    double blCurrent;
    double brCurrent;
    // power
    double tlPower;
    double trPower;
    double blPower;
    double brPower;
    // speed
    double tlSpeed;
    double trSpeed;
    double blSpeed;
    double brSpeed;
     // position
    double tlPos;
    double trPos;
    double blPos;
    double brPos;

    int tlState;
    int trState;
    int blState;
    int brState;

    bool extendedTL;
    bool extendedTR;
    bool extendedBL;
    bool extendedBR;

    bool extendingALL;
    bool complyALL;
    bool takeSlack;

    bool safetyOn;
    double targetX;
    double targetY;
    double targetZ;
    double x;
    double y;
    double z;

    bool test;
    int pointCount;
    int waypoint;
    int calibrationGridSize;
    unsigned long holdTimer;
    bool holding;
    unsigned long holdTime;
    float centerX;
    float centerY;
    unsigned long lastCallToPID;
    unsigned long lastMiss;
    unsigned long lastCallToUpdate;
    unsigned long extendCallTimer;
    unsigned long complyCallTimer;
};

class Maslow_ {
private:
    Maslow_() = default;  // Make constructor private

public:
    static Maslow_& getInstance();  // Accessor for singleton instance

    Maslow_(const Maslow_&)            = delete;  // no copying
    Maslow_& operator=(const Maslow_&) = delete;

public:
    //main utility functions
    void   begin(void (*sys_rt)());
    void   update();
    void   blinkIPAddress();
    void   heartBeat();
    bool   updateEncoderPositions();
    void   setTargets(float xTarget, float yTarget, float zTarget, bool tl = true, bool tr = true, bool bl = true, bool br = true);
    double getTargetX();
    double getTargetY();
    double getTargetZ();
    void   recomputePID();

    //math
    float computeBL(float x, float y, float z);
    float computeBR(float x, float y, float z);
    float computeTR(float x, float y, float z);
    float computeTL(float x, float y, float z);

    //Save and load z-axis position, set z-stop
    void saveZPos();
    void loadZPos();
    /** Sets the 'bottom' Z position, this is a 'stop' beyond which travel cannot continue */
    void setZStop();

    void stopMotors();

    void   stop();
    void   eStop(String message = "Emergency stop triggered.");
    void   panic();
    String axis_id_to_label(int axis_id);
    void   safety_control();
    bool   axis_homed[4] = { false, false, false, false };
    bool   retractingTL  = false;
    bool   retractingTR  = false;
    bool   retractingBL  = false;
    bool   retractingBR  = false;

    bool extendedTL = false;
    bool extendedTR = false;
    bool extendedBL = false;
    bool extendedBR = false;

    bool extendingALL = false;
    bool complyALL    = false;
    bool takeSlack    = false;

    bool safetyOn = true;

    bool setupIsComplete = false;

    Calibration calibration;

    void getInfo();
    bool telemetry_enabled = false;
    // TODO: probably need to use this for all fields in telemetry, but we'll try without first
    // SemaphoreHandle_t telemetry_mutex = xSemaphoreCreateMutex();
    TelemetryData get_telemetry_data();
    // turns on or off telemetry gathering
    void set_telemetry(bool enabled);
    void dump_telemetry(const char * filename);
    // writes whatever is in teh telemetry buffer to SD card
    void write_telemetry_buffer(uint8_t* buffer, size_t length);

    //These are the current targets set by the setTargets function used for moving the machine during normal operations
    double targetX = 0;
    double targetY = 0;
    double targetZ = 0;

    MotorUnit axisTL;
    MotorUnit axisTR;
    MotorUnit axisBL;
    MotorUnit axisBR;

    bool readingFromSD = false;  //Used to turn off reading from the encoders when reading from the - i dont think we need this anymore TODO
    bool using_default_config = false;
    QWIICMUX I2CMux;

    bool   error                                     = false;
    String errorMessage;
    
    void   test_();
    void   reset_all_axis();
    //keep track of where Maslow actually is
    double x;
    double y;

    float tlX;
    float tlY;
    float tlZ;
    float trX;
    float trY;
    float trZ;
    float blX;
    float blY;
    float blZ;
    float brX;
    float brY;
    float brZ;

    float _beltEndExtension = 30;  //Based on the CAD model these should add to 153.4
    float _armLength        = 123.4;

    float centerX;
    float centerY;

    bool   test = false;

private:

    //Used to keep track of how often the PID controller is updated
    unsigned long lastCallToPID    = millis();
    unsigned long lastMiss         = millis();
    unsigned long lastCallToUpdate = millis();
    unsigned long extendCallTimer  = millis();
    unsigned long complyCallTimer  = millis();

    //Stores a reference to the global system runtime function to be called when blocking operations are needed
    void (*_sys_rt)() = nullptr;

    //Used to overide and drive motors directly...dangerous
    bool TLIOveride = false;
    bool TRIOveride = false;
    bool BLIOveride = false;
    bool BRIOveride = false;
    bool TLOOveride = false;
    bool TROOveride = false;
    bool BLOOveride = false;
    bool BROOveride = false;
    unsigned long overideTimer = millis();

    bool HeartBeatEnabled = true;
    void log_telem_hdr_csv();
    void log_telem_pt_csv(TelemetryData data);
};

extern Maslow_& Maslow;

// actual task loop for gathering telemetry data (runs on utility core)
void   telemetry_loop(void* unused);
