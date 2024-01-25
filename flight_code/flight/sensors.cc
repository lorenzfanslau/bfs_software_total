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

#include "flight/sensors.h"
#include "flight/global_defs.h"
#include "flight/config.h"
#include "flight/msg.h"
#include "flight/analog.h"
#include "ms4525_src/ms4525do.h"
#if defined(__FMU_R_V2__)
#include "flight/battery.h"
#endif

namespace {
/* Whether pitot static is installed */
bool pitot_static_installed_;
/* Sensors */
bfs::SbusRx inceptor;
bfs::Mpu9250 imu;
bfs::Ublox gnss;
bfs::Bme280 fmu_static_pres;
bfs::Ams5915 pitot_static_pres;
bfs::Ams5915 pitot_diff_pres;
bfs::Ms4525do arbitrary_pres(&Wire, 0x28, 100.0f, 0.0f, bfs::Ms4525do::OUTPUT_TYPE_A);
}  // namespace

void SensorsInit(const SensorConfig &cfg) {
  pitot_static_installed_ = cfg.pitot_static_installed;
  MsgInfo("Intializing sensors...");
  /* Initialize IMU */
  if (!imu.Init(cfg.imu)) {
    MsgError("Unable to initialize IMU.");
  }
  /* Initialize GNSS */
  if (!gnss.Init(cfg.gnss)) {
    MsgError("Unable to initialize GNSS.");
  }
  /* unseren zusätzlichen Sensor initialisieren (testen)*/
  if (!arbitrary_pres.pres_Begin()){
      MsgError("hat nicht hingehauen, schade.");
    }
  /* Initialize pressure transducers */
  if (pitot_static_installed_) {
    if (!pitot_static_pres.Init(cfg.static_pres)) {
      MsgError("Unable to initialize static pressure sensor.");
    }
    if (!pitot_diff_pres.Init(cfg.diff_pres)) {
      MsgError("Unable to initialize differential pressure sensor.");
    }
    
  } else {
    if (!fmu_static_pres.Init(cfg.static_pres)) {
      MsgError("Unable to initialize static pressure sensor.");
    }
  }
  MsgInfo("done.\n");
  /* Initialize inceptors */
  MsgInfo("Initializing inceptors...");
  while (!inceptor.Init(&SBUS_UART)) {}
  MsgInfo("done.\n");
}
void SensorsRead(SensorData * const data) {
  if (!data) {return;}
  /* Read inceptors */
  data->inceptor.new_data = inceptor.Read();
  if (data->inceptor.new_data) {
    data->inceptor.ch = inceptor.ch();
    data->inceptor.ch17 = inceptor.ch17();
    data->inceptor.ch18 = inceptor.ch18();
    data->inceptor.lost_frame = inceptor.lost_frame();
    data->inceptor.failsafe = inceptor.failsafe();
  }
  /* Read IMU */
  if (!imu.Read(&data->imu)) {
    MsgWarning("Unable to read IMU data.\n");
  }
  /* Auslesen des zusätzlichen Sensors*/
  if (!arbitrary_pres.pres_Read(&data->edit_pres)){
    MsgWarning("Sensor kann nicht ausgelesen werden");
  }
  /* Read GNSS */
  gnss.Read(&data->gnss);
  /* Set whether pitot static is installed */
  data->pitot_static_installed = pitot_static_installed_;
  /* Read pressure transducers */
  if (pitot_static_installed_) {
    if (!pitot_static_pres.Read(&data->static_pres)) {
      MsgError("Unable to read pitot static pressure data.\n");
    }
    if (!pitot_diff_pres.Read(&data->diff_pres)) {
      MsgError("Unable to read pitot diff pressure data.\n");
    }
  } else {
    if (!fmu_static_pres.Read(&data->static_pres)) {
      MsgError("Unable to read FMU static pressure data.\n");
    }
  }
  /* Read analog channels */
  AnalogRead(&data->adc);
  /* Read battery voltage / current */
  #if defined(__FMU_R_V2__)
  BatteryRead(&data->power_module);
  #endif
}
