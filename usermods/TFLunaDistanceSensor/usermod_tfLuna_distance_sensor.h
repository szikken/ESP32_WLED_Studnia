#pragma once

#include "wled.h"

#include <Arduino.h>
#include <Wire.h>        // instantiate the Wire library
#include <TFLI2C.h>      // TFLuna-I2C Library v.0.2.0

/*
 * Usermods allow you to add own functionality to WLED more easily
 * See: https://github.com/Aircoookie/WLED/wiki/Add-own-functionality
 *  
 * Using a usermod:
 * 1. Copy the usermod into the sketch folder (same folder as wled00.ino)
 * 2. Register the usermod by adding #include "usermod_filename.h" in the top and registerUsermod(new MyUsermodClass()) in the bottom of usermods_list.cpp
 */

class UsermodTfLunaDistanceSensor : public Usermod
{
  private:

    TFLI2C tflI2C;

    // usermod enabled
    bool enabled = false;
    // status of initialisation
    bool initDone = false;

    static const char _name[];
    static const char _enabled[];
    static const char _nameReadInterval[];
    static const char _nameDistance[];
    static const char _nameQuality[];
    static const char _nameTemperature[];
    static const char _nameReadErrors[];

    char lidarMqttTopic[64];
    size_t lidarMqttTopicLen;

    int16_t tfAddr = TFL_DEF_ADR;    // default I2C address
    uint16_t tfFrame = TFL_DEF_FPS;   // default frame rate

    int16_t tfDistTmp = 0;
    int16_t tfFluxTmp = 0;
    int16_t tfTempTmp = 0;
    int16_t tfDistArray[5];   // distance in centimeters
    int16_t tfFluxArray[5];   // signal quality in arbitrary units
    int16_t tfTempArray[5];   // temperature in 0.01 degree Celsius
    
    uint32_t readInterval = 12000;
    unsigned long lastReadTime = 0;

    int16_t readCounter = 0;
    bool publishData = false;
    int16_t errorReadCounter = 0;
    int16_t softResetCounter = 0;
    bool publishError = false;

    void publishReadInterval()
    {
#ifndef WLED_DISABLE_MQTT
      if (WLED_MQTT_CONNECTED)
      {
        strcpy(lidarMqttTopic + lidarMqttTopicLen, "/readinterval");
        mqtt->publish(lidarMqttTopic, 0, true, String(readInterval).c_str());
        lidarMqttTopic[lidarMqttTopicLen] = '\0';
      }
#endif
    }

  public:

    //constructor
    UsermodTfLunaDistanceSensor() {}

    //desctructor
    ~UsermodTfLunaDistanceSensor() {}

    // Enable/Disable the usermod
    inline void enable(bool enable) { enabled = enable; }
    //Get usermod enabled/disabled state
    inline bool isEnabled() { return enabled; }

#ifndef WLED_DISABLE_MQTT
    /**
     * handling of MQTT message
     * topic only contains stripped topic (part after /wled/MAC)
     */
    bool onMqttMessage(char* topic, char* payload)
    {
      if (strlen(topic) == 23 && strncmp_P(topic, PSTR("/lidar/readinterval/set"), 23) == 0)
      {
        uint32_t readintervalTmp = (uint32_t) strtoul(payload, NULL, 10);
        if (readintervalTmp != 0)
        {
          readInterval = readintervalTmp;
          publishReadInterval();
          return true;
        }
      }
      return false;
    }

    /**
     * subscribe to MQTT topic for controlling read interval
     */
    void onMqttConnect(bool sessionPresent)
    {
      //(re)subscribe to required topics
      char subuf[64];
      if (mqttDeviceTopic[0] != 0)
      {
        strcpy(subuf, mqttDeviceTopic);
        strcat_P(subuf, PSTR("/lidar/readinterval/set"));
        mqtt->subscribe(subuf, 0);
        
        publishReadInterval();
      }
    }
#endif

    void setup()
    {
      if (i2c_scl < 0 || i2c_sda < 0) { enabled = false; return; } // I2C not initialised by WLED core

      sprintf(lidarMqttTopic, "%s/lidar", mqttDeviceTopic);
      lidarMqttTopicLen = strlen(lidarMqttTopic);

      initDone = true;
    }

    void loop()
    {
      if (!enabled || (strip.isUpdating() && (millis() - lastReadTime < readInterval))) return;
      
      if (millis() - lastReadTime > readInterval)
      {
        if(tflI2C.getData(tfDistTmp, tfFluxTmp, tfTempTmp, tfAddr))
        {
          tfTempTmp = int16_t(tfTempTmp / 100);

          tfDistArray[readCounter] = tfDistTmp;
          tfFluxArray[readCounter] = tfFluxTmp;
          tfTempArray[readCounter] = tfTempTmp;

          errorReadCounter = 0; // reset error streak on successful read
          readCounter++;

          if(readCounter == 5)
          {
            readCounter = 0;
            publishData = true;
          }
        }
        else
        {
          errorReadCounter++;
          readCounter = 0; // discard partially collected samples — avoid mixing stale data

          if((errorReadCounter % 25) == 0)
          {
            softResetCounter++;
            errorReadCounter = 0;
            tflI2C.Soft_Reset(tfAddr);
            publishError = true;
          }
          else if((errorReadCounter % 5) == 0)
          {
            publishError = true;
          }
        }

        lastReadTime = millis();
      }

#ifndef WLED_DISABLE_MQTT
      if (WLED_MQTT_CONNECTED)
      {
        if(publishData)
        {
          int16_t tfDistAvg = 0;
          int32_t tfFluxAvg = 0;
          int16_t tfTempAvg = 0;
          
          for (byte i = 0; i < 5; i = i + 1)
          {
            tfDistAvg = tfDistAvg + tfDistArray[i];
            tfFluxAvg = tfFluxAvg + tfFluxArray[i];
            tfTempAvg = tfTempAvg + tfTempArray[i];
          }

          tfDistAvg = int16_t (tfDistAvg / 5);
          tfFluxAvg = int32_t (tfFluxAvg / 5);
          tfTempAvg = int16_t (tfTempAvg / 5);

          int16_t tfWaterLvlAvg = 268 - tfDistAvg;
          int16_t tfWaterAmountAvg = 3.14 * 16.0 * (tfWaterLvlAvg / 10.0);

          strcpy(lidarMqttTopic + lidarMqttTopicLen, "/status");
          mqtt->publish(lidarMqttTopic, 0, false, "true");
          strcpy(lidarMqttTopic + lidarMqttTopicLen, "/distance");
          mqtt->publish(lidarMqttTopic, 0, false, String(tfDistAvg).c_str());
          strcpy(lidarMqttTopic + lidarMqttTopicLen, "/quality");
          mqtt->publish(lidarMqttTopic, 0, false, String(tfFluxAvg).c_str());
          strcpy(lidarMqttTopic + lidarMqttTopicLen, "/temperature");
          mqtt->publish(lidarMqttTopic, 0, false, String(tfTempAvg).c_str());
          strcpy(lidarMqttTopic + lidarMqttTopicLen, "/water/level");
          mqtt->publish(lidarMqttTopic, 0, false, String(tfWaterLvlAvg).c_str());
          strcpy(lidarMqttTopic + lidarMqttTopicLen, "/water/amount");
          mqtt->publish(lidarMqttTopic, 0, false, String(tfWaterAmountAvg).c_str());
          lidarMqttTopic[lidarMqttTopicLen] = '\0';

          publishData = false;
        }
        else if(publishError)
        {
          strcpy(lidarMqttTopic + lidarMqttTopicLen, "/status");
          mqtt->publish(lidarMqttTopic, 0, false, "false");
          strcpy(lidarMqttTopic + lidarMqttTopicLen, "/errors");
          mqtt->publish(lidarMqttTopic, 0, false, String(errorReadCounter).c_str());
          strcpy(lidarMqttTopic + lidarMqttTopicLen, "/resets");
          mqtt->publish(lidarMqttTopic, 0, false, String(softResetCounter).c_str());
          lidarMqttTopic[lidarMqttTopicLen] = '\0';

          publishError = false;
        }
      }
#endif
    }

    void addToJsonInfo(JsonObject& root)
    {
      if (enabled)
      {
        // if "u" object does not exist yet wee need to create it
        JsonObject user = root["u"];
        if (user.isNull()) user = root.createNestedObject("u");

        JsonArray infoArrReadInterval = user.createNestedArray(FPSTR(_nameReadInterval));
        infoArrReadInterval.add(readInterval);
        infoArrReadInterval.add(F(" msec"));

        JsonArray infoArrDistance = user.createNestedArray(FPSTR(_nameDistance));
        infoArrDistance.add(tfDistTmp);
        infoArrDistance.add(F(" cm"));
        
        JsonArray infoArrQuality = user.createNestedArray(FPSTR(_nameQuality));
        infoArrQuality.add(tfFluxTmp);
        infoArrQuality.add(F(" flux"));

        JsonArray infoArrTemperature = user.createNestedArray(FPSTR(_nameTemperature));
        infoArrTemperature.add(tfTempTmp);
        infoArrTemperature.add(F(" °C"));

        JsonArray infoArrReadErrors = user.createNestedArray(FPSTR(_nameReadErrors));
        infoArrReadErrors.add(errorReadCounter);
        infoArrReadErrors.add(F(" err"));
      }
    }

    void addToConfig(JsonObject &root)
    {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top[FPSTR(_enabled)] = enabled;
    }

    bool readFromConfig(JsonObject &root)
    {
      JsonObject top = root[FPSTR(_name)];
      
      bool configComplete = !top.isNull();
      configComplete &= getJsonValue(top["enabled"], enabled);

      return configComplete;
    }
};

const char UsermodTfLunaDistanceSensor::_name[] PROGMEM = "LIDARSensor";
const char UsermodTfLunaDistanceSensor::_enabled[] PROGMEM = "enabled";
const char UsermodTfLunaDistanceSensor::_nameReadInterval[] PROGMEM = "LIDARSensor:ReadInterval";
const char UsermodTfLunaDistanceSensor::_nameDistance[] PROGMEM = "LIDARSensor:Distance";
const char UsermodTfLunaDistanceSensor::_nameQuality[] PROGMEM = "LIDARSensor:Quality";
const char UsermodTfLunaDistanceSensor::_nameTemperature[] PROGMEM = "LIDARSensor:Temperature";
const char UsermodTfLunaDistanceSensor::_nameReadErrors[] PROGMEM = "LIDARSensor:ReadErrors";