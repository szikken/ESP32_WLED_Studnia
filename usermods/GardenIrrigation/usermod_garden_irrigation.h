#pragma once

#include "wled.h"
#include "../multi_relay/multi_relay.h"
#include "../TFLunaDistanceSensor/usermod_tfLuna_distance_sensor.h"

/*
 * GardenIrrigation usermod
 *
 * Replicates the OpenHab rule_garden_irrigation sequence:
 *   STAGE_0 : pump + valve1 ON  → stage1Duration minutes
 *   STAGE_1 : pump + valve1 + valve2 ON → stage2Duration minutes
 *   STAGE_2 : pump + valve2 ON  → stage3Duration minutes
 *   IDLE    : all OFF
 *
 * Water level is read from TFLunaDistanceSensor.  Irrigation is aborted
 * when the sensor reports an error or the level drops to/below criticalLevel.
 *
 * OpenHab sends MQTT "on" to the control topic; all timing and safety logic
 * runs inside this usermod.
 *
 * MQTT control topics  (base = {mqttDeviceTopic}/irrigation):
 *   .../control                         → payload "on" = start, "off" = stop
 *   .../config/stage1_duration/set      → stage 0 duration (minutes, 1-120)
 *   .../config/stage2_duration/set      → stage 1 duration (minutes, 1-120)
 *   .../config/stage3_duration/set      → stage 2 duration (minutes, 1-120)
 *   .../config/min_start_level/set      → minimum water level to allow start (cm)
 *   .../config/critical_level/set       → abort threshold (cm)
 *
 * MQTT status topics (retain where noted):
 *   .../status         idle | running   (retain)
 *   .../remaining      MM:SS            (no retain, every statusInterval sec)
 *   .../event          started:Xl | finished:used Xl | aborted:<reason>
 *   .../config/stage1_duration          (retain, echoed on change + connect)
 *   .../config/stage2_duration          (retain, echoed on change + connect)
 *   .../config/stage3_duration          (retain, echoed on change + connect)
 *   .../config/min_start_level          (retain, echoed on change + connect)
 *   .../config/critical_level           (retain, echoed on change + connect)
 *
 * Relay indices and status interval are configured only via WLED UI (cfg.json).
 *
 * Written and maintained for ESP32_WLED_Studnia project.
 */

class GardenIrrigationUsermod : public Usermod {

  private:

    enum IrrigationStage : uint8_t {
      STAGE_IDLE = 0,
      STAGE_0,   // pump + valve1
      STAGE_1,   // pump + valve1 + valve2
      STAGE_2,   // pump + valve2
    };

    IrrigationStage stage          = STAGE_IDLE;
    unsigned long   stageStartTime = 0;
    unsigned long   irrigStartTime = 0;
    unsigned long   lastStatusMs   = 0;
    int16_t         startWaterAmount = 0;

    bool enabled  = false;
    bool initDone = false;

    // ---- configurable parameters ----
    uint16_t stage1Duration   = 15;  // minutes – pump + valve1
    uint16_t stage2Duration   = 5;   // minutes – pump + valve1 + valve2
    uint16_t stage3Duration   = 15;  // minutes – pump + valve2
    int16_t  minStartLevel    = 80;  // cm  – minimum level to allow start
    int16_t  criticalLevel    = 20;  // cm  – abort threshold during irrigation
    uint16_t statusInterval   = 15;  // seconds – countdown publish interval (UI only)
    uint8_t  pumpRelayIndex   = 0;   // UI only
    uint8_t  valve1RelayIndex = 1;   // UI only
    uint8_t  valve2RelayIndex = 2;   // UI only

    // MQTT base topic: {mqttDeviceTopic}/irrigation
    char   irrigBase[100];
    size_t irrigBaseLen = 0;

    // PROGMEM config keys
    static const char _name[];
    static const char _enabled[];
    static const char _stg1[];
    static const char _stg2[];
    static const char _stg3[];
    static const char _minStart[];
    static const char _critical[];
    static const char _interval[];
    static const char _pump[];
    static const char _valve1[];
    static const char _valve2[];

    // ---- helpers ----

    UsermodTfLunaDistanceSensor* getTfLuna() {
      return static_cast<UsermodTfLunaDistanceSensor*>(
        UsermodManager::lookup(USERMOD_ID_TFLUNADISTANCESENSOR));
    }

    MultiRelay* getRelay() {
      return static_cast<MultiRelay*>(
        UsermodManager::lookup(USERMOD_ID_MULTI_RELAY));
    }

    void setRelay(uint8_t idx, bool state) {
      MultiRelay* r = getRelay();
      if (r) r->switchRelay(idx, state);
    }

    void stopAll() {
      setRelay(pumpRelayIndex,   false);
      setRelay(valve1RelayIndex, false);
      setRelay(valve2RelayIndex, false);
    }

    // Publish to {irrigBase}/{subtopic}
    void publishSub(const char* subtopic, const char* payload, bool retain) {
#ifndef WLED_DISABLE_MQTT
      if (!WLED_MQTT_CONNECTED) return;
      strcpy(irrigBase + irrigBaseLen, subtopic);
      mqtt->publish(irrigBase, 0, retain, payload);
      irrigBase[irrigBaseLen] = '\0';
#endif
    }

    void publishStatus(const char* status) {
      publishSub("/status", status, true);
    }

    void publishEvent(const char* ev) {
      publishSub("/event", ev, false);
    }

    void publishRemaining(uint32_t secs) {
#ifndef WLED_DISABLE_MQTT
      if (!WLED_MQTT_CONNECTED) return;
      char buf[8];
      snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
      publishSub("/remaining", buf, false);
#endif
    }

    void publishConfigUint(const char* key, uint32_t val) {
#ifndef WLED_DISABLE_MQTT
      if (!WLED_MQTT_CONNECTED) return;
      char sub[48];
      snprintf(sub, sizeof(sub), "/config/%s", key);
      char buf[12];
      snprintf(buf, sizeof(buf), "%u", (unsigned)val);
      publishSub(sub, buf, true);
#endif
    }

    uint32_t totalSeconds() const {
      return (uint32_t)(stage1Duration + stage2Duration + stage3Duration) * 60UL;
    }

    uint32_t remainingSeconds() const {
      if (stage == STAGE_IDLE) return 0;
      uint32_t elapsed = (uint32_t)((millis() - irrigStartTime) / 1000UL);
      uint32_t total   = totalSeconds();
      return (elapsed < total) ? (total - elapsed) : 0;
    }

    void publishAllConfig() {
      publishConfigUint("stage1_duration", stage1Duration);
      publishConfigUint("stage2_duration", stage2Duration);
      publishConfigUint("stage3_duration", stage3Duration);
      publishConfigUint("min_start_level", (uint32_t)(int32_t)minStartLevel);
      publishConfigUint("critical_level",  (uint32_t)(int32_t)criticalLevel);
    }

    // Returns false and calls abortIrrigation() if water is insufficient or sensor failed.
    bool checkWater(const char* tag) {
      UsermodTfLunaDistanceSensor* tf = getTfLuna();
      if (!tf || !tf->hasData() || !tf->isSensorOk()) {
        char buf[48];
        snprintf(buf, sizeof(buf), "sensor_error@%s", tag);
        abortIrrigation(buf);
        return false;
      }
      int16_t lvl = tf->getWaterLevel();
      if (lvl <= criticalLevel) {
        char buf[48];
        snprintf(buf, sizeof(buf), "low_water@%s:%dcm", tag, (int)lvl);
        abortIrrigation(buf);
        return false;
      }
      return true;
    }

    void startIrrigation() {
      if (stage != STAGE_IDLE) {
        publishEvent("already_running");
        return;
      }
      if (!getRelay()) {
        publishEvent("aborted:no_relay_module");
        return;
      }
      UsermodTfLunaDistanceSensor* tf = getTfLuna();
      if (!tf || !tf->isEnabled()) {
        publishEvent("aborted:no_sensor_module");
        return;
      }
      if (!tf->hasData() || !tf->isSensorOk()) {
        publishEvent(!tf->hasData() ? "aborted:sensor_no_data_yet" : "aborted:sensor_error");
        return;
      }
      int16_t lvl = tf->getWaterLevel();
      if (lvl < minStartLevel) {
        char buf[48];
        snprintf(buf, sizeof(buf), "aborted:low_water_start:%dcm", (int)lvl);
        publishEvent(buf);
        return;
      }
      startWaterAmount = tf->getWaterAmount();

      unsigned long now = millis();
      stage          = STAGE_0;
      stageStartTime = now;
      irrigStartTime = now;
      lastStatusMs   = now;

      setRelay(pumpRelayIndex,   true);
      setRelay(valve1RelayIndex, true);

      publishStatus("running");

      char buf[32];
      snprintf(buf, sizeof(buf), "started:%dl", (int)startWaterAmount);
      publishEvent(buf);
    }

    void abortIrrigation(const char* reason) {
      stopAll();
      stage = STAGE_IDLE;
      publishStatus("idle");
      publishRemaining(0);
      char buf[64];
      snprintf(buf, sizeof(buf), "aborted:%s", reason);
      publishEvent(buf);
    }

    void finishIrrigation() {
      stopAll();
      stage = STAGE_IDLE;
      publishStatus("idle");
      publishRemaining(0);
      UsermodTfLunaDistanceSensor* tf = getTfLuna();
      int16_t endAmt = (tf && tf->hasData()) ? tf->getWaterAmount() : 0;
      int16_t used   = startWaterAmount - endAmt;
      if (used < 0) used = 0;
      char buf[32];
      snprintf(buf, sizeof(buf), "finished:used %dl", (int)used);
      publishEvent(buf);
    }

  public:

    GardenIrrigationUsermod()  {}
    ~GardenIrrigationUsermod() {}

    inline void enable(bool e) { enabled = e; }
    inline bool isEnabled()    { return enabled; }
    inline uint16_t getId() override { return USERMOD_ID_GARDEN_IRRIGATION; }

    void setup() override {
      snprintf(irrigBase, sizeof(irrigBase), "%s/irrigation", mqttDeviceTopic);
      irrigBaseLen = strlen(irrigBase);
      initDone = true;
    }

    void loop() override {
      if (!enabled || !initDone || stage == STAGE_IDLE) return;

      unsigned long now = millis();

      // Periodic: check water level + publish countdown
      if (now - lastStatusMs >= (unsigned long)statusInterval * 1000UL) {
        lastStatusMs = now;
        const char* tag = (stage == STAGE_0) ? "s0" :
                          (stage == STAGE_1) ? "s1" : "s2";
        if (!checkWater(tag)) return;  // aborted inside checkWater
        publishRemaining(remainingSeconds());
      }

      // Stage transition logic
      unsigned long stageElapsed = now - stageStartTime;

      if (stage == STAGE_0) {
        if (stageElapsed >= (unsigned long)stage1Duration * 60000UL) {
          if (!checkWater("s0_end")) return;
          setRelay(valve2RelayIndex, true);   // add valve2
          stage          = STAGE_1;
          stageStartTime = now;
        }
      } else if (stage == STAGE_1) {
        if (stageElapsed >= (unsigned long)stage2Duration * 60000UL) {
          if (!checkWater("s1_end")) return;
          setRelay(valve1RelayIndex, false);  // remove valve1
          stage          = STAGE_2;
          stageStartTime = now;
        }
      } else if (stage == STAGE_2) {
        if (stageElapsed >= (unsigned long)stage3Duration * 60000UL) {
          finishIrrigation();
        }
      }
    }

#ifndef WLED_DISABLE_MQTT
    void onMqttConnect(bool sessionPresent) override {
      if (mqttDeviceTopic[0] == '\0') return;
      // Re-build base topic (mqttDeviceTopic may change on reconnect)
      snprintf(irrigBase, sizeof(irrigBase), "%s/irrigation", mqttDeviceTopic);
      irrigBaseLen = strlen(irrigBase);

      char sub[80];
      strlcpy(sub, mqttDeviceTopic, sizeof(sub));
      strlcat(sub, "/irrigation/#", sizeof(sub));
      mqtt->subscribe(sub, 0);

      publishAllConfig();
      publishStatus(stage == STAGE_IDLE ? "idle" : "running");
    }

    bool onMqttMessage(char* topic, char* payload) override {
      // topic arrives stripped of mqttDeviceTopic prefix
      if (strncmp_P(topic, PSTR("/irrigation/"), 12) != 0) return false;
      const char* sub = topic + 12;  // after "/irrigation/"

      if (strcmp_P(sub, PSTR("control")) == 0) {
        if      (strcmp_P(payload, PSTR("on"))  == 0) startIrrigation();
        else if (strcmp_P(payload, PSTR("off")) == 0 && stage != STAGE_IDLE) abortIrrigation("manual_stop");
        return true;
      }

      if (strncmp_P(sub, PSTR("config/"), 7) == 0) {
        const char* cfg = sub + 7;
        uint32_t val = (uint32_t)strtoul(payload, nullptr, 10);

        if (strcmp_P(cfg, PSTR("stage1_duration/set")) == 0) {
          if (val > 0 && val <= 120) { stage1Duration = (uint16_t)val; publishConfigUint("stage1_duration", val); serializeConfig(); }
          return true;
        }
        if (strcmp_P(cfg, PSTR("stage2_duration/set")) == 0) {
          if (val > 0 && val <= 120) { stage2Duration = (uint16_t)val; publishConfigUint("stage2_duration", val); serializeConfig(); }
          return true;
        }
        if (strcmp_P(cfg, PSTR("stage3_duration/set")) == 0) {
          if (val > 0 && val <= 120) { stage3Duration = (uint16_t)val; publishConfigUint("stage3_duration", val); serializeConfig(); }
          return true;
        }
        if (strcmp_P(cfg, PSTR("min_start_level/set")) == 0) {
          if (val <= 1000) { minStartLevel = (int16_t)val; publishConfigUint("min_start_level", val); serializeConfig(); }
          return true;
        }
        if (strcmp_P(cfg, PSTR("critical_level/set")) == 0) {
          if (val <= 1000) { criticalLevel = (int16_t)val; publishConfigUint("critical_level", val); serializeConfig(); }
          return true;
        }
      }
      return false;
    }
#endif  // WLED_DISABLE_MQTT

    void addToJsonInfo(JsonObject& root) override {
      if (!enabled) return;
      JsonObject user = root[F("u")];
      if (user.isNull()) user = root.createNestedObject(F("u"));

      JsonArray stageArr = user.createNestedArray(F("Irrigation stage"));
      const char* stageStr = "idle";
      if      (stage == STAGE_0) stageStr = "stage0 (pump+valve1)";
      else if (stage == STAGE_1) stageStr = "stage1 (pump+valve1+valve2)";
      else if (stage == STAGE_2) stageStr = "stage2 (pump+valve2)";
      stageArr.add(stageStr);

      if (stage != STAGE_IDLE) {
        JsonArray remArr = user.createNestedArray(F("Irrigation remaining"));
        uint32_t r = remainingSeconds();
        char buf[8];
        snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(r / 60), (unsigned)(r % 60));
        remArr.add(buf);
      }
    }

    void addToConfig(JsonObject& root) override {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top[FPSTR(_enabled)] = enabled;
      top[FPSTR(_stg1)]    = stage1Duration;
      top[FPSTR(_stg2)]    = stage2Duration;
      top[FPSTR(_stg3)]    = stage3Duration;
      top[FPSTR(_minStart)]  = minStartLevel;
      top[FPSTR(_critical)]  = criticalLevel;
      top[FPSTR(_interval)]  = statusInterval;
      top[FPSTR(_pump)]    = pumpRelayIndex;
      top[FPSTR(_valve1)]  = valve1RelayIndex;
      top[FPSTR(_valve2)]  = valve2RelayIndex;
    }

    bool readFromConfig(JsonObject& root) override {
      JsonObject top = root[FPSTR(_name)];
      bool ok = !top.isNull();
      ok &= getJsonValue(top[FPSTR(_enabled)],  enabled);
      ok &= getJsonValue(top[FPSTR(_stg1)],     stage1Duration);
      ok &= getJsonValue(top[FPSTR(_stg2)],     stage2Duration);
      ok &= getJsonValue(top[FPSTR(_stg3)],     stage3Duration);
      ok &= getJsonValue(top[FPSTR(_minStart)], minStartLevel);
      ok &= getJsonValue(top[FPSTR(_critical)], criticalLevel);
      ok &= getJsonValue(top[FPSTR(_interval)], statusInterval);
      ok &= getJsonValue(top[FPSTR(_pump)],     pumpRelayIndex);
      ok &= getJsonValue(top[FPSTR(_valve1)],   valve1RelayIndex);
      ok &= getJsonValue(top[FPSTR(_valve2)],   valve2RelayIndex);
      return ok;
    }
};

// PROGMEM strings (defined outside the class to satisfy the ODR)
const char GardenIrrigationUsermod::_name[]    PROGMEM = "GardenIrrigation";
const char GardenIrrigationUsermod::_enabled[] PROGMEM = "enabled";
const char GardenIrrigationUsermod::_stg1[]    PROGMEM = "stage1_duration_min";
const char GardenIrrigationUsermod::_stg2[]    PROGMEM = "stage2_duration_min";
const char GardenIrrigationUsermod::_stg3[]    PROGMEM = "stage3_duration_min";
const char GardenIrrigationUsermod::_minStart[] PROGMEM = "min_start_level_cm";
const char GardenIrrigationUsermod::_critical[] PROGMEM = "critical_level_cm";
const char GardenIrrigationUsermod::_interval[] PROGMEM = "status_interval_sec";
const char GardenIrrigationUsermod::_pump[]    PROGMEM = "pump_relay_idx";
const char GardenIrrigationUsermod::_valve1[]  PROGMEM = "valve1_relay_idx";
const char GardenIrrigationUsermod::_valve2[]  PROGMEM = "valve2_relay_idx";
