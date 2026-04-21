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

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------
void Qtopic(char *topic);
void connectMqtt(socket *s, etherHeader *ether);
void connACKMqtt(socket *s, etherHeader *ether, uint8_t *data);
void disconnectMqtt();
void publishMqtt(char strTopic[], char strData[]);
void subscribeMqtt(char strTopic[]);
void unsubscribeMqtt(char strTopic[]);

#endif

