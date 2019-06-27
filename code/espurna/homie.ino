/*

  HOMIE MODULE

  Copyright (C) 2019 by Peter Hoeg <peter at hoeg dot com> but based *heavily* on Xose Perez's work.

*/

#if HOMIE_SUPPORT

#include <algorithm>
#include <queue>
#include <vector>

bool _homieEnabled = false;

// -----------------------------------------------------------------------------
// UTILS
// -----------------------------------------------------------------------------

String _homieFixName(String name) {
    for (unsigned char i=0; i<name.length(); i++) {
        if (!isalnum(name.charAt(i))) name.setCharAt(i, '-');
    }
    return name;
}

String _joinList(std::vector<String> items) {
    // sort and remove duplicates
    std::sort(items.begin(), items.end());
    items.erase(std::unique(items.begin(), items.end()), items.end());

    String s;
    for(int i = 0; i < items.size(); ++i) {
        if(i != 0)
            s += ",";
        s += items[i];
    }
    return s;
}

const char * _boolToHomie(bool b) {
    return (b) ? HOMIE_PAYLOAD_ON : HOMIE_PAYLOAD_OFF;
}

#if WEB_SUPPORT

bool _homieWebSocketOnReceive(const char * key, JsonVariant& value) {
    return (strncmp(key, "homie", 5) == 0);
}

void _homieWebSocketOnSend(JsonObject& root) {

    unsigned char visible = 0;
    root["homieEnabled"] = getSetting("homieEnabled", HOMIE_ENABLED).toInt() == 1;

    visible = (relayCount() > 0);

#if SENSOR_SUPPORT
    _sensorWebSocketMagnitudes(root, "homie");
    visible = visible || (magnitudeCount() > 0);
#endif

    root["homieVisible"] = visible;
}

#endif

#if NTP_SUPPORT || SENSOR_SUPPORT
String _homieSensorTopic(const char * topic) {
    String _base_topic =                          \
        String(MQTT_TOPIC_SENSOR) + "/" +               \
        String(topic) + "/";

    return _base_topic;
}
#endif

#if NTP_SUPPORT
void _homieNtp() {
    String _topic = String(MQTT_TOPIC_NTP) + "/";
    mqttSend((_topic + HOMIE_NAME).c_str(), "NTP");
    mqttSend((_topic + HOMIE_TYPE).c_str(), "NTP");
    mqttSend((_topic + HOMIE_PROPERTIES).c_str(), MQTT_TOPIC_NTP_CONNECTED);
    mqttSend((_topic + MQTT_TOPIC_NTP_CONNECTED + "/" + HOMIE_NAME).c_str(), "NTP Connected");
    mqttSend((_topic + MQTT_TOPIC_NTP_CONNECTED + "/" + HOMIE_DATATYPE).c_str(), HOMIE_DATATYPE_BOOL);
    mqttSend((String(MQTT_TOPIC_NTP) + "/" + MQTT_TOPIC_NTP_CONNECTED).c_str(), _boolToHomie(ntpSynced()));
}
#endif

void _homieRelays() {
    String _base_topic = String(MQTT_TOPIC_RELAY) + "/";
    String relaysAsString = "";
    char _name[12];
    char _relay[8];

    mqttSend((_base_topic + HOMIE_NAME).c_str(), "Relays");
    mqttSend((_base_topic + HOMIE_TYPE).c_str(), "Relays");

    for (unsigned char id=0; id < relayCount(); ++id) {
        // set up a comma separated list for $properties
        if (id != 0)
            relaysAsString += ",";
        relaysAsString += String(id);

        // send the MQTT payloads for each relay
        snprintf_P(_name, sizeof(_name), PSTR("Relay %d"), id);
        snprintf_P(_relay, sizeof(_relay), PSTR("%d/"), id);
        mqttSend((_base_topic + _relay + HOMIE_NAME).c_str(), _name);
        mqttSend((_base_topic + _relay + HOMIE_DATATYPE).c_str(), HOMIE_DATATYPE_BOOL);
        mqttSend((_base_topic + _relay + HOMIE_SETTABLE).c_str(), "true");
    }
    mqttSend((_base_topic + HOMIE_PROPERTIES).c_str(), relaysAsString.c_str());
}

#if SENSOR_SUPPORT
void _homieSensors() {
    String _topic = String(MQTT_TOPIC_SENSOR) + "/";
    mqttSend((_topic + HOMIE_NAME).c_str(), "Sensors");
    mqttSend((_topic + HOMIE_TYPE).c_str(), "Sensors");
    // hardcoded for testing
    mqttSend((_topic + HOMIE_PROPERTIES).c_str(), "humidity,temperature");
    mqttSend((_topic + "humidity/$name").c_str(), "Humidity");
    mqttSend((_topic + "humidity/$datatype").c_str(), "integer");
    mqttSend((_topic + "humidity/$format").c_str(), "0:100");
    mqttSend((_topic + "humidity/$unit").c_str(), "%");
    mqttSend((_topic + "temperature/$name").c_str(), "Temperature");
    mqttSend((_topic + "temperature/$datatype").c_str(), "float");
    mqttSend((_topic + "temperature/$unit").c_str(), "°C");
}
#endif

void _homieSend() {
    if (!mqttConnected()) return;

    DEBUG_MSG_P(PSTR("[Homie] Sending initial MQTT announcement message\n"));

    mqttSend(MQTT_TOPIC_STATUS, "init", true);
    mqttSend("$homie", HOMIE_VERSION, true);
    mqttSend(MQTT_TOPIC_APP, APP_NAME);
    mqttSend(MQTT_TOPIC_HOSTNAME, getSetting("hostname", getIdentifier()).c_str());

    std::vector<String> nodes;

    nodes.push_back(MQTT_TOPIC_RELAY);
    _homieRelays();
#if BUTTON_SUPPORT
    /* nodes.push_back(MQTT_TOPIC_BUTTON); */
#endif
#if LED_SUPPORT
    /* nodes.push_back(MQTT_TOPIC_LED); */
#endif
#if LIGHT_PROVIDER != LIGHT_PROVIDER_NONE
    /* nodes.push_back(MQTT_TOPIC_LIGHT); */
#endif
#if NTP_SUPPORT
    nodes.push_back(MQTT_TOPIC_NTP);
    _homieNtp();
#endif
#if SENSOR_SUPPORT
    nodes.push_back(MQTT_TOPIC_SENSOR);
    _homieSensors();
#endif

    mqttSend(HOMIE_NODES, _joinList(nodes).c_str(), true);
    mqttSend(MQTT_TOPIC_STATUS, MQTT_STATUS_ONLINE, true);
}

void _homieConfigure() {
    bool enabled = getSetting("homieEnabled", HOMIE_ENABLED).toInt() == 1;
    _homieEnabled = enabled;
    _homieSend();
}

void homieSetup() {

    _homieConfigure();

#if WEB_SUPPORT
    wsOnSendRegister(_homieWebSocketOnSend);
    wsOnReceiveRegister(_homieWebSocketOnReceive);
#endif

    // On MQTT connect check if we have something to send
    mqttRegister([](unsigned int type, const char * topic, const char * payload) {
            if (type == MQTT_CONNECT_EVENT) _homieSend();
        });

    // Main callbacks
    espurnaRegisterReload(_homieConfigure);
}

#endif // HOMIE_SUPPORT
