#pragma once

#include <QString>

// Собственный 128-битный GATT-профиль лабораторной.
#define LAB6_SERVICE_UUID "6c1f0001-5a3e-4b7a-9c2d-7e8f9a0b1c2d"
#define LAB6_DATA_UUID    "6c1f0002-5a3e-4b7a-9c2d-7e8f9a0b1c2d"
#define LAB6_CTRL_UUID    "6c1f0003-5a3e-4b7a-9c2d-7e8f9a0b1c2d"

namespace bleconst {

inline QString serviceUuid() { return QStringLiteral(LAB6_SERVICE_UUID); }
inline QString dataUuid() { return QStringLiteral(LAB6_DATA_UUID); }
inline QString controlUuid() { return QStringLiteral(LAB6_CTRL_UUID); }

inline QString appPath() { return QStringLiteral("/org/lab6/ble"); }
inline QString servicePath() { return QStringLiteral("/org/lab6/ble/service0"); }
inline QString dataPath() { return QStringLiteral("/org/lab6/ble/service0/char_data"); }
inline QString controlPath() { return QStringLiteral("/org/lab6/ble/service0/char_ctrl"); }
inline QString advertPath() { return QStringLiteral("/org/lab6/ble/advert"); }

inline QString localName() { return QStringLiteral("lab6-ble"); }

}  // namespace bleconst
