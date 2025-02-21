/**
 * OCE4531 Project 4: MPU6050 Accelerometer and LCD Display
 * @brief This is the solution for OCE4531's fourth and final project, the accelerometer display
 * This should not be considered the absolute correct answer that all of the students must have,
 * but it is the ball park they should be within.
 * See handout for more information
 * 
 * This is the code for the undergraduate portion of the project
 * 
 * @author Braidan Duffy
 * 
 * @date July 31, 2022
 * 
 * @version v1.0
 */

// LCD Instantiation
#include <LiquidCrystal.h>

LiquidCrystal lcd(7, 8, 9, 10, 11, 12); // RS, En, D4, D5, D6, D7

// MPU6050 Instantiation
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "Wire.h"
#include <math.h>

Adafruit_MPU6050 mpu;
sensors_event_t a, g, temp;

// Kalman filter instantiation
#include <Kalman.h>

#define N_state 9 // Number of states - Position (XYZ), Speed (XYZ), Acceleration (XYZ)
#define N_obs 3 // Number of observation - Acceleration (XYZ)
#define r_a 5.0 // Acceleration measurement noise
#define q_p 0.1 // Process error - position
#define q_s 0.1 // Process error - speed
#define q_a 0.5 // Process error - acceleration

BLA::Matrix<N_obs> obs; // Observation vector
KALMAN<N_state, N_obs> K; // Kalman filter object
unsigned long currMillis;
float dt;

// Mahony Filter Instantiation
#include <MahonyAHRS.h>
Mahony M; // Mahony filter

// General instantiations
#define PAGE_CHANGE_BUTTON_PIN 2
#define RESET_BUTTON_PIN 3
#define RESET_HOLD_TIME 500 // ms
#define BUZZER_ACTIVE_PIN 6
#define BUZZER_ACTIVE_TIME 250 // ms
#define MAX_NUM_PAGES 5

bool isBuzzerActive = false;
long buzzerStartTime = 0;
long resetStartTime = 0;
uint8_t curPageNum = 0; // 0: acceleration, 1: gyroscope, 2: orientation
bool isPageChanged = false;
bool isResetTriggered = true;

struct telemetry_t {
    long timestamp = 0;
    float accelX = 0;
    float accelY = 0;
    float accelZ = 0;
    float gyroX = 0;
    float gyroY = 0;
    float gyroZ = 0;
    float roll = 0;
    float pitch = 0;
    
    // Reference: https://forum.arduino.cc/t/integration-of-acceleration/158296/14
    float yaw = 0;
    float speedX = 0;
    float speedY = 0;
    float speedZ = 0;
    float dispX = 0;
    float dispY = 0;
    float dispZ = 0;
} data;

void setup() {
    Serial.begin(115200);
    // Pin initializations
    pinMode(BUZZER_ACTIVE_PIN, OUTPUT);
    pinMode(PAGE_CHANGE_BUTTON_PIN, INPUT_PULLUP);
    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PAGE_CHANGE_BUTTON_PIN), pageChangeISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(RESET_BUTTON_PIN), resetButtonISR, CHANGE);

    // Set up the LCD's number of columns and rows:
    Serial.print("Initializing LCD...");
    lcd.begin(16, 2);
    Serial.println("done!");

    Serial.print("Initializing IMU...");
    if (!mpu.begin()) { // Try to initialize!
        Serial.println("Failed to find MPU6050 chip");
        while (true); // Block further code execution
    }
    Serial.println("done!"); Serial.println();
    
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    Serial.print("Accelerometer range set to: ");
    switch (mpu.getAccelerometerRange()) {
    case MPU6050_RANGE_2_G:
        Serial.println("+-2G");
        break;
    case MPU6050_RANGE_4_G:
        Serial.println("+-4G");
        break;
    case MPU6050_RANGE_8_G:
        Serial.println("+-8G");
        break;
    case MPU6050_RANGE_16_G:
        Serial.println("+-16G");
        break;
    }
    
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    Serial.print("Gyro range set to: ");
    switch (mpu.getGyroRange()) {
    case MPU6050_RANGE_250_DEG:
        Serial.println("+- 250 deg/s");
        break;
    case MPU6050_RANGE_500_DEG:
        Serial.println("+- 500 deg/s");
        break;
    case MPU6050_RANGE_1000_DEG:
        Serial.println("+- 1000 deg/s");
        break;
    case MPU6050_RANGE_2000_DEG:
        Serial.println("+- 2000 deg/s");
        break;
    }

    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    Serial.print("Filter bandwidth set to: ");
    switch (mpu.getFilterBandwidth()) {
    case MPU6050_BAND_260_HZ:
        Serial.println("260 Hz");
        break;
    case MPU6050_BAND_184_HZ:
        Serial.println("184 Hz");
        break;
    case MPU6050_BAND_94_HZ:
        Serial.println("94 Hz");
        break;
    case MPU6050_BAND_44_HZ:
        Serial.println("44 Hz");
        break;
    case MPU6050_BAND_21_HZ:
        Serial.println("21 Hz");
        break;
    case MPU6050_BAND_10_HZ:
        Serial.println("10 Hz");
        break;
    case MPU6050_BAND_5_HZ:
        Serial.println("5 Hz");
        break;
    }

    // time evolution matrix (whatever... it will be updated in loop)
    //       P  | S  | A  | P  | S  | A  | P  | S  | A
    K.F = { 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, // X
            0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, // Y
            0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, // Z
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0 };

    // measurement matrix (position, velocity, acceleration)
    K.H = { 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,    // X
            0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0,    // Y
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0 };  // Z
    
    // measurement covariance matrix
    K.R = { r_a*r_a,  0.0,      0.0,
            0.0,      r_a*r_a,  0.0,
            0.0,      0.0,      r_a*r_a };
    
    // model covariance matrix
    K.Q = { q_p*q_p, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, // X
            0.0, q_s*q_s, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
            0.0, 0.0, q_a*q_a, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, q_p*q_p, 0.0, 0.0, 0.0, 0.0, 0.0, // Y
            0.0, 0.0, 0.0, 0.0, q_s*q_s, 0.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0, 0.0, q_a*q_a, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0, q_p*q_p, 0.0, 0.0, // Z
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, q_s*q_s, 0.0,
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, q_a*q_a };
    
    currMillis = millis();

    Serial.println();
    delay(100);
}

void loop() {
    // Compute time difference since last cycle
    dt = (millis() - currMillis)/1000.0;
    currMillis = millis();

    // =========================
    // === RUN KALMAN FILTER ===
    // =========================

    // Update state equation
    // Here we make use of the Taylor expansion on the (position,speed,acceleration)
    // position_{k+1}     = position_{k} + dt*speed_{k} + (dt*dt/2)*acceleration_{k}
    // speed_{k+1}        = speed_{k} + dt*acceleration_{k}
    // acceleration_{k+1} = acceleration_{k}
    K.F = { 1.0, dt,  dt*dt/2,  0.0, 0.0, 0.0,      0.0, 0.0, 0.0,
            0.0, 1.0, dt,       0.0, 0.0, 0.0,      0.0, 0.0, 0.0,
            0.0, 0.0, 1.0,      0.0, 0.0, 0.0,      0.0, 0.0, 0.0,
            0.0, 0.0, 0.0,      1.0, dt,  dt*dt/2,  0.0, 0.0, 0.0,
            0.0, 0.0, 0.0,      0.0, 1.0, dt,       0.0, 0.0, 0.0,
            0.0, 0.0, 0.0,      0.0, 0.0, 1.0,      0.0, 0.0, 0.0,
            0.0, 0.0, 0.0,      0.0, 0.0, 0.0,      1.0, dt,  dt*dt*2,
            0.0, 0.0, 0.0,      0.0, 0.0, 0.0,      0.0, 1.0, dt,
            0.0, 0.0, 0.0,      0.0, 0.0, 0.0,      0.0, 0.0, 1.0};

    // Poll IMU to get measurements
    mpu.getEvent(&a, &g, &temp);
    BLA::Matrix<N_obs> meas;
    meas(0) = a.acceleration.x;
    meas(1) = a.acceleration.y;
    meas(2) = a.acceleration.z;
    obs = meas;
    
    // Update Kalman filter
    K.update(obs);

    // --- UPDATE TELEMETRY PACKET ---

    // Displacement
    data.dispX = K.x(0);
    data.dispY = K.x(3);
    data.dispZ = K.x(6);

    // Velocity
    data.speedX = K.x(1);
    data.speedY = K.x(4);
    data.speedZ = K.x(7);

    // Acceleration
    data.accelX = K.x(2);
    data.accelY = K.x(5);
    data.accelZ = K.x(8);

    // ==========================
    // === RUN MAHONY AHRS FILTER
    // ==========================

    // Update Mahony filter
    M.updateIMU(g.gyro.x, g.gyro.y, g.gyro.z,
                data.accelX, data.accelY, data.accelZ);

    // --- UPDATE TELEMETRY PACKET ---

    // Gyroscope
    data.gyroX = g.gyro.x;
    data.gyroY = g.gyro.y;
    data.gyroZ = g.gyro.z;

    // Orientation
    data.roll = M.getRoll();
    data.pitch = M.getPitch();
    data.yaw = M.getYaw();

    // =========================
    // === UPDATE LCD MODULE ===
    // =========================

    // Wipe LCD screen on mode change
    if (isPageChanged) { // Check the page changed flag
        isPageChanged = false; // Reset the page changed flag
        lcd.setCursor(0, 0); // Set cursor to top row
        lcd.print("                "); // Clear top row
        lcd.setCursor(0, 1); // Set cursor to bottom row
        lcd.print("                "); // Clear bottom row
    }

    switch (curPageNum) {
        case 0: // Acceleration page
            lcd.setCursor(0,0); // Set cursor to top row
            lcd.print("Ax   Ay   Az  g"); // Write headers
            lcd.setCursor(0,1); // Set cursor to bottom row
            lcd.print(data.accelX/9.81, 1); data.accelX > 0 ? lcd.print("  ") : lcd.print(" ");
            lcd.print(data.accelY/9.81, 1); data.accelY > 0 ? lcd.print("  ") : lcd.print(" ");
            lcd.print(data.accelZ/9.81, 1); data.accelZ > 0 ? lcd.print("  ") : lcd.print(" ");

            break;
        
        case 1: // Velocity page
            lcd.setCursor(0,0); // Set cursor to top row
            lcd.print("Ux   Uy   Uz m/s"); // Write headers
            lcd.setCursor(0,1); // Set cursor to bottom row
            lcd.print(data.speedX, 1); data.speedX > 0 ? lcd.print("  ") : lcd.print(" ");
            lcd.print(data.speedY, 1); data.speedY > 0 ? lcd.print("  ") : lcd.print(" ");
            lcd.print(data.speedZ, 1); data.speedZ > 0 ? lcd.print("  ") : lcd.print(" ");

            break;

        case 2: // Displacement page
            lcd.setCursor(0,0); // Set cursor to top row
            lcd.print("Sx   Sy   Sz  m"); // Write headers
            lcd.setCursor(0,1); // Set cursor to bottom row
            lcd.print(data.dispX, 1); data.dispX > 0 ? lcd.print("  ") : lcd.print(" ");
            lcd.print(data.dispY, 1); data.dispY > 0 ? lcd.print("  ") : lcd.print(" ");
            lcd.print(data.dispZ, 1); data.dispZ > 0 ? lcd.print("  ") : lcd.print(" ");

            break;
        
        case 3: // Gyroscope page
            lcd.setCursor(0,0); // Set cursor to top row
            lcd.print("Gx   Gy   Gz r/s"); // Write headers
            lcd.setCursor(0,1); // Set cursor to bottom row
            lcd.print(data.gyroX, 1); data.gyroX > 0 ? lcd.print("  ") : lcd.print(" ");
            lcd.print(data.gyroY, 1); data.gyroY > 0 ? lcd.print("  ") : lcd.print(" ");
            lcd.print(data.gyroZ, 1); data.gyroZ > 0 ? lcd.print("  ") : lcd.print(" ");

            break;

        case 4: // Orientation page
            lcd.setCursor(0,0); // Set cursor to top row
            lcd.print("R    P    Y deg"); // Write headers, 0xDF is the code for °
            lcd.setCursor(0,1); // Set cursor to bottom row
            lcd.print(data.roll, 1); data.roll > 0 ? lcd.print("  ") : lcd.print(" ");
            lcd.print(data.pitch, 1); data.pitch > 0 ? lcd.print("  ") : lcd.print(" ");
            lcd.print(data.yaw, 1); data.yaw > 0 ? lcd.print("  ") : lcd.print(" ");

            break;
    }

    // Buzzer active handler
    if (isBuzzerActive) {
        if (millis() < buzzerStartTime+BUZZER_ACTIVE_TIME) {
            analogWrite(BUZZER_ACTIVE_PIN, 128); // Turn on buzzer
        }
        else {
            digitalWrite(BUZZER_ACTIVE_PIN, LOW);
            isBuzzerActive = false;
        }
    }

    // Integral reset handler
    if (isResetTriggered && !digitalRead(RESET_BUTTON_PIN) && millis() >= resetStartTime + RESET_HOLD_TIME) { // Check for a button press and if the button is held for RESET_HOLD_TIME
        isResetTriggered = false; // Reset the reset button flag
        // Reset speed values
        data.speedX = 0; 
        data.speedY = 0;
        data.speedZ = 0;

        // Reset displacement values
        data.dispX = 0;
        data.dispY = 0;
        data.dispZ = 0;

        // Reset heading value
        data.yaw = 0;
        // Serial.println("Reset values!"); // DEBUG
    }

    delay(100);
}


// ==================================
// === INTERRUPT SERVICE ROUTINES ===
// ==================================


void pageChangeISR() {
    buzzerStartTime = millis(); // Start the buzzer active timer
    isBuzzerActive = true; // Set the buzzer active flag
    isPageChanged = true; // Set the page changed flag
    curPageNum++; // Change the page number
    if (curPageNum >= MAX_NUM_PAGES) { // Increment the page number and check if it is greater than the max pages
        curPageNum = 0; // Reset current page to first
    }
    // Serial.println(curPageNum); // DEBUG
}

void resetButtonISR() {
    if (digitalRead(RESET_BUTTON_PIN)) { // Button was released
        isResetTriggered = false; // Reset the reset button flag
        // Serial.println("Set reset trigger to FALSE"); // DEBUG
    }
    else { // Button was pressed
        resetStartTime = millis();
        isResetTriggered = true; // Set the reset button flag
        // Serial.println("Set reset trigger to TRUE"); // DEBUG
    }
}