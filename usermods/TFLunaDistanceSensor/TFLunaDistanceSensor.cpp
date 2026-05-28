#include "wled.h"
#include "usermod_tfLuna_distance_sensor.h"

const char UsermodTfLunaDistanceSensor::_name[] PROGMEM = "LIDARSensor";
const char UsermodTfLunaDistanceSensor::_enabled[] PROGMEM = "enabled";
const char UsermodTfLunaDistanceSensor::_nameReadInterval[] PROGMEM = "LIDARSensor:ReadInterval";
const char UsermodTfLunaDistanceSensor::_nameDistance[] PROGMEM = "LIDARSensor:Distance";
const char UsermodTfLunaDistanceSensor::_nameQuality[] PROGMEM = "LIDARSensor:Quality";
const char UsermodTfLunaDistanceSensor::_nameTemperature[] PROGMEM = "LIDARSensor:Temperature";
const char UsermodTfLunaDistanceSensor::_nameReadErrors[] PROGMEM = "LIDARSensor:ReadErrors";

static UsermodTfLunaDistanceSensor tfLunaDistanceSensor;
REGISTER_USERMOD(tfLunaDistanceSensor);
