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
#define MAX 1518
#define Topics 10
#define Tlength 64

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------
static char topicQueue[Topics][Tlength];
static uint8_t count = 0;
//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------
void Qtopic(char *topic)
{
    if(count >= Topics) return;
    strncpy(topicQueue[count++], topic, Tlength - 1);
}
uint8_t getTopicCount()
{
  return count;
}

char *getTopic(uint8_t index)
{
    if(index >= count) return NULL;
    return topicQueue[index];
}

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

void connACKMqtt(socket *s, etherHeader *ether, uint8_t *data)  //check processtcpresponse
{
    uint8_t session = data[2] & 0x01; //data1 and 0 is already confirmed
    uint8_t returnCode = data[3];
    if(returnCode != 0)
    {
        putsUart0("MQTT CONNACK: Refused\r\n");
        return;
    }
    putsUart0("MQTT CONNACK: Accepted\r\n");
    setTcpState(1, MQTT_CONNECTED);

    uint8_t i;
    for (i = 0; i< getTopicCount(); i++)
    {
        subscribeMqtt(getTopic(i));
    }
/*
////for server side
void publishMqtt(char strTopic[], char strData[])
{
    if(getTcpState(0) != TCP_ESTABLISHED) return; // check if there is an current tcp connection before making packet


    uint8_t packet[200]; //holds the publish packet
    uint16_t sizes = 0; //write index

    //length of topic and payload of message
    uint16_t topics = strlen(strTopic);
    uint16_t datalength = strlen(strData);
//publish packet, 0x0011 with flags
    packet[sizes++] = 0x30;
//handles the rest of the packet by storing it into 7 bits
    int carrot = 2 + topics + datalength;
    do
    {
        uint8_t york = carrot & 0x7F;
        carrot >>= 7;
        if(carrot) york |= 0x80;
        packet[sizes++] = york;
    } while (carrot);
    //topic 2 bytes where msb is first then lsb
    packet[sizes++] = topics >> 8;
    packet[sizes++] = topics & 0xFF;
//copy both the topics and payloads into packets
    memcpy(&packet[sizes], strTopic, topics);
    sizes += topics;

    memcpy(&packet[sizes], strData, datalength);
        sizes += datalength;
        //builds frame buffer and sends the packet to tcp
        uint8_t frame[MAX];
        etherHeader *value = (etherHeader*) frame;
        sendTcpMessage(value, getsocket(0), PSH | ACK, packet, sizes);
    }



}

void subscribeMqtt(char strTopic[])
{
    if(getTcpState(0) != TCP_ESTABLISHED) return;

    uint8_t packet[200];
    uint16_t sizes = 0;

    uint16_t topic = strlen(strTopic);
    uint16_t ID = 1;

    //header

    packet[sizes++] = 0x82;

    uint32_t re = 2+ 2 + topic + 1; // id of packet + plus the length og the topic + QoS(1)
     do
     {
         uint8_t byte = re & 0x7F;
         re >>= 7;
         if (re > 0)
             byte |= 0x80;
         packet[sizes++] = byte;
     } while (re > 0);

     packet[sizes++] = ID >> 8;
     packet[sizes++] = ID & 0xFF;

     packet[sizes++] = topic >> 8;
     packet[sizes++] = topic & 0xFF;

     memcpy(&packet[sizes], strTopic, topic);

     sizes += topic;

     packet[sizes++] = 0x00; /// QoS = 0

     //sends the packet

     uint8_t frame[MAX];
     etherHeader *value = (etherHeader*) frame;

     sendTcpMessage(value, getsocket(0), PSH | ACK, packet, sizes);



     }
}

void unsubscribeMqtt(char strTopic[])
{
    if(getTcpState(0) != TCP_ESTABLISHED) return;

        uint8_t packet[200];
        uint16_t sizes = 0;

        uint16_t topic = strlen(strTopic);
        uint16_t ID = 1;

        //header

        packet[sizes++] = 0xA2; // Q0S1 unsubscribe

        uint32_t re = 2+ 2 + topic; // id of packet + plus the length og the topic + QoS(1)
         do
         {
             uint8_t byte = re & 0x7F;
             re >>= 7;
             if (re > 0)
                 byte |= 0x80;
             packet[sizes++] = byte;
         } while (re > 0);

         packet[sizes++] = ID >> 8;
         packet[sizes++] = ID & 0xFF;

         packet[sizes++] = topic >> 8;
         packet[sizes++] = topic & 0xFF;

         memcpy(&packet[sizes], strTopic, topic);

         sizes += topic;

         //packet[sizes++] = 0x00; /// QoS = 0

         //sends the packet

         uint8_t frame[MAX];
         etherHeader *value = (etherHeader*) frame;

         sendTcpMessage(value, getsocket(0), PSH | ACK, packet, sizes);



}
*/

