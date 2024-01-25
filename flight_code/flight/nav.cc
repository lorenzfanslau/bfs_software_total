/*
* Brian R Taylor
* brian.taylor@bolderflight.com
* 
* Copyright (c) 2021 Bolder Flight Systems Inc
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the “Software”), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#include "flight/nav.h"
#include "flight/global_defs.h"
#include "flight/config.h"
#include "flight/msg.h"
#include "navigation/navigation.h"
#include "filter/filter.h"
#include "airdata/airdata.h"

namespace {
/* Nav config */
NavConfig config_;
bool nav_initialized_ = false;
/* Minimum number of satellites */
static constexpr int MIN_SAT_ = 12;
/* Frame period */
static constexpr float FRAME_PERIOD_S = static_cast<float>(FRAME_PERIOD_MS) /
                                        1000.0f;
/* Navigation filter */
bfs::Ekf15State nav_filter_;
/* Data */
Eigen::Vector3d gnss_lla_;
Eigen::Vector3f gnss_ned_vel_mps_;
Eigen::Vector3f imu_accel_mps2_;
Eigen::Vector3f imu_gyro_radps_;
Eigen::Vector3f imu_mag_ut_;
Eigen::Vector3d nav_lla_;
Eigen::Vector3f nav_ned_pos_m_;
Eigen::Vector3f nav_ned_vel_mps_;
Eigen::Vector3f nav_accel_mps2_;
Eigen::Vector3f nav_gyro_radps_;
Eigen::Vector3f nav_accel_bias_mps2_;
Eigen::Vector3f nav_gyro_bias_radps_;
/* Geoid height */
float geoid_height_m_;
/* Home position */
Eigen::Vector3d home_pos_lla_;
/* DLPF */
bfs::Iir<float> accel_dlpf_[3];
bfs::Iir<float> gyro_dlpf_[3];
bfs::Iir<float> mag_dlpf_[3];
bfs::Iir<float> static_pres_dlpf_;
bfs::Iir<float> diff_pres_dlpf_;
float mag_frame_rate_hz_;
}  // namespace
void NavInit(const NavConfig &ref) {
  /* Copy the config */
  config_ = ref;
  /* Set the mag sample rate */
  switch (FRAME_RATE_HZ) {
    case bfs::FRAME_RATE_50HZ: {
      mag_frame_rate_hz_ = 8.0f;
      break;
    }
    case bfs::FRAME_RATE_100HZ: {
      mag_frame_rate_hz_ = 100.0f;
      break;
    }
    case bfs::FRAME_RATE_200HZ: {
      mag_frame_rate_hz_ = 100.0f;
      break;
    }
  }
}
void NavRun(const SensorData &ref, NavData * const ptr) {
  if (!ptr) {return;}
  ptr->nav_initialized = nav_initialized_;
  /* Init Nav */
  if (!nav_initialized_) {
    if (ref.imu.new_imu_data && ref.imu.new_mag_data &&
        ref.gnss.new_data && ref.gnss.num_sats > MIN_SAT_) {
      /* Init air data filters */
      if (ref.pitot_static_installed) {
        if (ref.static_pres.new_data && ref.diff_pres.new_data) {
          static_pres_dlpf_.Init(config_.static_pres_cutoff_hz,
                                 static_cast<float>(FRAME_RATE_HZ),
                                 ref.static_pres.pres_pa);
          diff_pres_dlpf_.Init(config_.diff_pres_cutoff_hz,
                               static_cast<float>(FRAME_RATE_HZ),
                               ref.diff_pres.pres_pa);
        } else {
          return;
        }
      } else {
        if (ref.static_pres.new_data) {
          static_pres_dlpf_.Init(config_.static_pres_cutoff_hz,
                                 static_cast<float>(FRAME_RATE_HZ),
                                 ref.static_pres.pres_pa);
        } else {
          return;
        }
      }
      /* Geoid height */
      geoid_height_m_ = ref.gnss.alt_wgs84_m - ref.gnss.alt_msl_m;
      /* Home position */
      home_pos_lla_(0) = ref.gnss.lat_rad;
      home_pos_lla_(1) = ref.gnss.lon_rad;
      home_pos_lla_(2) = ref.gnss.alt_wgs84_m;
      /* Init IMU filters */
      accel_dlpf_[0].Init(config_.accel_cutoff_hz,
                          static_cast<float>(FRAME_RATE_HZ),
                          ref.imu.accel_mps2[0]);
      accel_dlpf_[1].Init(config_.accel_cutoff_hz,
                          static_cast<float>(FRAME_RATE_HZ),
                          ref.imu.accel_mps2[1]);
      accel_dlpf_[2].Init(config_.accel_cutoff_hz,
                          static_cast<float>(FRAME_RATE_HZ),
                          ref.imu.accel_mps2[2]);
      gyro_dlpf_[0].Init(config_.gyro_cutoff_hz,
                         static_cast<float>(FRAME_RATE_HZ),
                         ref.imu.gyro_radps[0]);
      gyro_dlpf_[1].Init(config_.gyro_cutoff_hz,
                         static_cast<float>(FRAME_RATE_HZ),
                         ref.imu.gyro_radps[1]);
      gyro_dlpf_[2].Init(config_.gyro_cutoff_hz,
                         static_cast<float>(FRAME_RATE_HZ),
                         ref.imu.gyro_radps[2]);
      mag_dlpf_[0].Init(config_.mag_cutoff_hz,
                        mag_frame_rate_hz_,
                        ref.imu.mag_ut[0]);
      mag_dlpf_[1].Init(config_.accel_cutoff_hz,
                        mag_frame_rate_hz_,
                        ref.imu.mag_ut[1]);
      mag_dlpf_[2].Init(config_.accel_cutoff_hz,
                        mag_frame_rate_hz_,
                        ref.imu.mag_ut[2]);
      imu_accel_mps2_(0) = ref.imu.accel_mps2[0];
      imu_accel_mps2_(1) = ref.imu.accel_mps2[1];
      imu_accel_mps2_(2) = ref.imu.accel_mps2[2];
      imu_gyro_radps_(0) = ref.imu.gyro_radps[0];
      imu_gyro_radps_(1) = ref.imu.gyro_radps[1];
      imu_gyro_radps_(2) = ref.imu.gyro_radps[2];
      imu_mag_ut_(0) = ref.imu.mag_ut[0];
      imu_mag_ut_(1) = ref.imu.mag_ut[1];
      imu_mag_ut_(2) = ref.imu.mag_ut[2];
      gnss_ned_vel_mps_(0) = ref.gnss.ned_vel_mps[0];
      gnss_ned_vel_mps_(1) = ref.gnss.ned_vel_mps[1];
      gnss_ned_vel_mps_(2) = ref.gnss.ned_vel_mps[2];
      /* Init the EKF */
      nav_filter_.Initialize(imu_accel_mps2_, imu_gyro_radps_,
                             imu_mag_ut_, gnss_ned_vel_mps_,
                             home_pos_lla_);
      nav_initialized_ = true;
    }
  } else {
    /* EKF time update */
    if (ref.imu.new_imu_data) {
      imu_accel_mps2_(0) = ref.imu.accel_mps2[0];
      imu_accel_mps2_(1) = ref.imu.accel_mps2[1];
      imu_accel_mps2_(2) = ref.imu.accel_mps2[2];
      imu_gyro_radps_(0) = ref.imu.gyro_radps[0];
      imu_gyro_radps_(1) = ref.imu.gyro_radps[1];
      imu_gyro_radps_(2) = ref.imu.gyro_radps[2];
      nav_filter_.TimeUpdate(imu_accel_mps2_, imu_gyro_radps_,
                             FRAME_PERIOD_S);
    }
    /* EKF measurement update */
    if (ref.gnss.new_data) {
      gnss_ned_vel_mps_(0) = ref.gnss.ned_vel_mps[0];
      gnss_ned_vel_mps_(1) = ref.gnss.ned_vel_mps[1];
      gnss_ned_vel_mps_(2) = ref.gnss.ned_vel_mps[2];
      gnss_lla_(0) = ref.gnss.lat_rad;
      gnss_lla_(1) = ref.gnss.lon_rad;
      gnss_lla_(2) = ref.gnss.alt_wgs84_m;
      nav_filter_.MeasurementUpdate(gnss_ned_vel_mps_, gnss_lla_);
    }
    /* EKF data */
    nav_accel_bias_mps2_ = nav_filter_.accel_bias_mps2();
    nav_gyro_bias_radps_ = nav_filter_.gyro_bias_radps();
    ptr->accel_bias_mps2[0] = nav_accel_bias_mps2_(0);
    ptr->accel_bias_mps2[1] = nav_accel_bias_mps2_(1);
    ptr->accel_bias_mps2[2] = nav_accel_bias_mps2_(2);
    ptr->gyro_bias_radps[0] = nav_gyro_bias_radps_(0);
    ptr->gyro_bias_radps[1] = nav_gyro_bias_radps_(1);
    ptr->gyro_bias_radps[2] = nav_gyro_bias_radps_(2);
    ptr->pitch_rad = nav_filter_.pitch_rad();
    ptr->roll_rad = nav_filter_.roll_rad();
    ptr->heading_rad = nav_filter_.yaw_rad();
    nav_lla_ = nav_filter_.lla_rad_m();
    ptr->lat_rad = nav_lla_(0);
    ptr->lon_rad = nav_lla_(1);
    ptr->alt_wgs84_m = nav_lla_(2);
    ptr->alt_msl_m = ptr->alt_wgs84_m - geoid_height_m_;
    nav_ned_vel_mps_ = nav_filter_.ned_vel_mps();
    ptr->ned_vel_mps[0] = nav_ned_vel_mps_(0);
    ptr->ned_vel_mps[1] = nav_ned_vel_mps_(1);
    ptr->ned_vel_mps[2] = nav_ned_vel_mps_(2);
    nav_ned_pos_m_ = bfs::lla2ned(nav_lla_, home_pos_lla_).cast<float>();
    ptr->ned_pos_m[0] = nav_ned_pos_m_(0);
    ptr->ned_pos_m[1] = nav_ned_pos_m_(1);
    ptr->ned_pos_m[2] = nav_ned_pos_m_(2);
    ptr->alt_rel_m = -1.0f * ptr->ned_pos_m[2];
    ptr->gnd_spd_mps = std::sqrt(ptr->ned_vel_mps[0] * ptr->ned_vel_mps[0] +
                                 ptr->ned_vel_mps[1] * ptr->ned_vel_mps[1]);
    ptr->gnd_track_rad = std::atan2(ptr->ned_vel_mps[1], ptr->ned_vel_mps[0]);
    ptr->flight_path_rad = std::atan2(-1.0f * ptr->ned_vel_mps[2],
                                      ptr->gnd_spd_mps);
    ptr->home_lat_rad = home_pos_lla_[0];
    ptr->home_lon_rad = home_pos_lla_[1];
    ptr->home_alt_wgs84_m = home_pos_lla_[2];
    /* Filtered IMU data */
    nav_accel_mps2_ = nav_filter_.accel_mps2();
    nav_gyro_radps_ = nav_filter_.gyro_radps();
    ptr->accel_mps2[0] = accel_dlpf_[0].Filter(nav_accel_mps2_(0));
    ptr->accel_mps2[1] = accel_dlpf_[1].Filter(nav_accel_mps2_(1));
    ptr->accel_mps2[2] = accel_dlpf_[2].Filter(nav_accel_mps2_(2));
    ptr->gyro_radps[0] = gyro_dlpf_[0].Filter(nav_gyro_radps_(0));
    ptr->gyro_radps[1] = gyro_dlpf_[1].Filter(nav_gyro_radps_(1));
    ptr->gyro_radps[2] = gyro_dlpf_[2].Filter(nav_gyro_radps_(2));
    if (ref.imu.new_mag_data) {
      ptr->mag_ut[0] = mag_dlpf_[0].Filter(ref.imu.mag_ut[0]);
      ptr->mag_ut[1] = mag_dlpf_[1].Filter(ref.imu.mag_ut[1]);
      ptr->mag_ut[2] = mag_dlpf_[2].Filter(ref.imu.mag_ut[2]);
    }
    /* Air data */
    if (ref.pitot_static_installed) {
      if (ref.static_pres.new_data) {
        ptr->static_pres_pa =
          static_pres_dlpf_.Filter(ref.static_pres.pres_pa);
        ptr->alt_pres_m = bfs::PressureAltitude_m(ptr->static_pres_pa);
      }
      if (ref.diff_pres.new_data) {
        ptr->diff_pres_pa =
          diff_pres_dlpf_.Filter(ref.diff_pres.pres_pa);
        ptr->ias_mps = bfs::Ias_mps(ptr->diff_pres_pa);
      }
    } else {
      if (ref.static_pres.new_data) {
        ptr->static_pres_pa =
          static_pres_dlpf_.Filter(ref.static_pres.pres_pa);
        ptr->alt_pres_m = bfs::PressureAltitude_m(ptr->static_pres_pa);
      }
    }
  }
}

