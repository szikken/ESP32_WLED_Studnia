#pragma once

#include "wled.h"

/*
 * Class declaration for MultiRelay usermod.
 * Extracted to a separate header so other usermods can obtain the MultiRelay*
 * via UsermodManager::lookup(USERMOD_ID_MULTI_RELAY) and call switchRelay().
 *
 * Full implementation remains in multi_relay.cpp.
 */

// MULTI_RELAY_MAX_RELAYS must be defined before the class declaration because
// the relay array size depends on it.  Build-flag overrides are respected via
// the #ifndef guard.
#ifndef MULTI_RELAY_MAX_RELAYS
  #define MULTI_RELAY_MAX_RELAYS 4
#else
  #if MULTI_RELAY_MAX_RELAYS > 8
    #undef MULTI_RELAY_MAX_RELAYS
    #define MULTI_RELAY_MAX_RELAYS 8
  #endif
#endif

typedef struct relay_t {
  int8_t pin;
  struct { // reduces memory footprint
    bool active   : 1;  // is the relay waiting to be switched
    bool invert   : 1;  // does On mean 1 or 0
    bool state    : 1;  // 1 relay is On, 0 relay is Off
    bool external : 1;  // is the relay externally controlled
    int8_t button : 4;  // which button triggers relay
  };
  uint16_t delay;       // amount of ms to wait after it is activated
} Relay;


class MultiRelay : public Usermod {

  private:
    // array of relays
    Relay    _relay[MULTI_RELAY_MAX_RELAYS];

    uint32_t _switchTimerStart; // switch timer start time
    bool     _oldMode;          // old brightness
    bool     enabled;           // usermod enabled
    bool     initDone;          // status of initialisation
    bool     usePcf8574;
    uint8_t  addrPcf8574;
    bool     HAautodiscovery;
    uint16_t periodicBroadcastSec;
    unsigned long lastBroadcast;

    // strings to reduce flash memory usage (used more than twice)
    static const char _name[];
    static const char _enabled[];
    static const char _relay_str[];
    static const char _delay_str[];
    static const char _activeHigh[];
    static const char _external[];
    static const char _button[];
    static const char _broadcast[];
    static const char _HAautodiscovery[];
    static const char _pcf8574[];
    static const char _pcfAddress[];
    static const char _switch[];
    static const char _toggle[];
    static const char _Command[];

    void handleOffTimer();
    void InitHtmlAPIHandle();
    int getValue(String data, char separator, int index);
    uint8_t getActiveRelayCount();

    byte IOexpanderWrite(byte address, byte _data);
    byte IOexpanderRead(int address);

    void publishMqtt(int relay);
#ifndef WLED_DISABLE_MQTT
    void publishHomeAssistantAutodiscovery();
#endif

  public:
    MultiRelay();

    inline void enable(bool enable) { enabled = enable; }
    inline bool isEnabled() { return enabled; }
    inline uint16_t getId() override { return USERMOD_ID_MULTI_RELAY; }

    // Switch relay on (true) or off (false).  Publishes MQTT status automatically.
    void switchRelay(uint8_t relay, bool mode);

    inline void toggleRelay(uint8_t relay) {
      switchRelay(relay, !_relay[relay].state);
    }

    void setup() override;
    inline void connected() override { InitHtmlAPIHandle(); }
    void loop() override;

#ifndef WLED_DISABLE_MQTT
    bool onMqttMessage(char* topic, char* payload) override;
    void onMqttConnect(bool sessionPresent) override;
#endif

    bool handleButton(uint8_t b) override;
    void addToJsonInfo(JsonObject &root) override;
    void addToJsonState(JsonObject &root) override;
    void readFromJsonState(JsonObject &root) override;
    void addToConfig(JsonObject &root) override;
    void appendConfigData() override;
    bool readFromConfig(JsonObject &root) override;
};
