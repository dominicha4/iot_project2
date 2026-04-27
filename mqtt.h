// MQTT Library (framework only)
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: -
// Target uC:       -
// System Clock:    -

// Hardware configuration:
// -

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#ifndef MQTT_H_
#define MQTT_H_

#include <stdint.h>
#include <stdbool.h>
#include "tcp.h"

typedef enum {
    MQTT_IDLE,
    MQTT_WAIT_TCP,
    MQTT_TCP_READY,
    MQTT_CONNECTING,
    MQTT_CONNECTED
} mqttState_t;

typedef struct {
    char topic[MAX_TOPIC_LEN];
    char payload[MAX_PAYLOAD_LEN];
    bool valid;
} mqttRecord_t;

#define MAX_RECORDS 20
#define MAX_TOPIC_LEN 64
#define MAX_PAYLOAD_LEN 128

extern mqttRecord_t mqttRecords[MAX_RECORDS];

extern mqttState_t mqttState;
extern socket *mqttSocket;
extern bool mqttConnected;     // MQTT connection state
extern bool mqttConnectSent;   // Used to prevent it from sending another. Maybe used

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void connectMqtt(socket *s);
void disconnectMqtt();
void publishMqtt(char strTopic[], char strData[]);
void subscribeMqtt(char strTopic[]);
void unsubscribeMqtt(char strTopic[]);
void printMqttRecords();
void parseMqttPublish(uint8_t *payload);

#endif
