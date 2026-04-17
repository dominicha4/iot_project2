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

#include <stdio.h>
#include <string.h>
#include "mqtt.h"
#include "timer.h"
#include "tcp.h"
#include "uart0.h"

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void connectMqtt(socket *s, etherHeader *ether)
{
    putsUart0("Sending MQTT CONNECT\r\n");

    uint8_t mqttData[100] = {};     // MQTT data initialized to NULL
    uint16_t mqttDataSize = 0;      // sendTcpMessage expects uint16

    mqttData[mqttDataSize++] = 0x10;    // Connect
    uint16_t lenIndex = mqttDataSize++;          // mqttDataSize temp index

    mqttData[mqttDataSize++] = 0x00;    // Protocol name (MSB)
    mqttData[mqttDataSize++] = 0x04;    // Protocol name (LSB)
    mqttData[mqttDataSize++] = 'M';     // Protocol name
    mqttData[mqttDataSize++] = 'Q';     // Protocol name
    mqttData[mqttDataSize++] = 'T';     // Protocol name
    mqttData[mqttDataSize++] = 'T';     // Protocol name

    mqttData[mqttDataSize++] = 0x04;    // Protocol level

    mqttData[mqttDataSize++] = 0x00;    // Connect flags

    mqttData[mqttDataSize++] = 0x00;    // Keep alive (MSB)
    mqttData[mqttDataSize++] = 0x3C;    // Keep alive (LSB) - 60 sec

    mqttData[mqttDataSize++] = 0x00;    // Client ID length (MSB)
    mqttData[mqttDataSize++] = 0x07;    // Client ID length (LSB
    mqttData[mqttDataSize++] = 'W';     // Client ID
    mqttData[mqttDataSize++] = 'E';     // Client ID
    mqttData[mqttDataSize++] = 'B';     // Client ID
    mqttData[mqttDataSize++] = 'T';     // Client ID
    mqttData[mqttDataSize++] = 'E';     // Client ID
    mqttData[mqttDataSize++] = 'A';     // Client ID
    mqttData[mqttDataSize++] = 'M';     // Client ID

    mqttData[lenIndex] = mqttDataSize - 2;  // MQTT remaining length

    sendTcpMessage(ether, s, PSH | ACK, mqttData, mqttDataSize);
}

void disconnectMqtt()
{
}

void publishMqtt(char strTopic[], char strData[])
{
}

void subscribeMqtt(char strTopic[])
{
}

void unsubscribeMqtt(char strTopic[])
{
}
