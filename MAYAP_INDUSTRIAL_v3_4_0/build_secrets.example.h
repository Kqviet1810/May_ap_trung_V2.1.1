#pragma once

// LOCAL BUILD ONLY:
// 1) Copy file nay thanh build_secrets.h trong CUNG thu muc sketch.
// 2) Dien tai khoan HiveMQ that.
// 3) build_secrets.h da nam trong .gitignore - KHONG commit credential that.
//
// Neu bo qua buoc nay, firmware se FAIL COMPILE co chu dich thay vi tao .bin
// co user/pass rong roi lap MQTT state=5 (UNAUTHORIZED) tren may that.
#define MAYAP_MQTT_USERNAME ""
#define MAYAP_MQTT_PASSWORD ""
