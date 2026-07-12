/**
 * @file    MadgwickAHRS.h
 * @brief   Madgwick IMU/AHRS sensor fusion (Sebastian Madgwick, GPL-3.0)
 * @see     https://x-io.co.uk/open-source-imu-and-ahrs-algorithms/
 */

#ifndef MADGWICK_AHRS_H
#define MADGWICK_AHRS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void MadgwickAHRS_init(float sample_hz, float beta_gain);
void MadgwickAHRS_set_beta(float beta_gain);
void MadgwickAHRS_reset(void);

void MadgwickAHRSupdate(float gx, float gy, float gz, float ax, float ay, float az, float mx, float my,
                      float mz);
void MadgwickAHRSupdateIMU(float gx, float gy, float gz, float ax, float ay, float az);

bool MadgwickAHRS_get_euler_deg(float *roll_deg, float *pitch_deg, float *yaw_deg);

#ifdef __cplusplus
}
#endif

#endif /* MADGWICK_AHRS_H */
