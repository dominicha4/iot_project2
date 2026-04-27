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
mqttState_t mqttState = MQTT_IDLE;
socket *mqttSocket = NULL;
bool mqttConnected = false;     // MQTT connection state
bool mqttConnectSent = false;   // Used to prevent it from sending another. Maybe used
mqttRecord_t mqttRecords[MAX_RECORDS] = {};

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------
void mqtt_ping()
{
    if (mqttSocket == NULL || mqttSocket->state != TCP_ESTABLISHED)
        return;

    uint8_t mqttData[2];
    mqttData[0] = 0xC0;    // PINGREQ
    mqttData[1] = 0x00;    // Remaining length = 0
    uint16_t mqttDataSize = 2;

    uint8_t buffer[1518];
    etherHeader *ether = (etherHeader*) buffer;

    // Build ethernet header
    uint8_t localMac[6];
    getEtherMacAddress(localMac);
    memcpy(ether->destAddress, mqttSocket->remoteHwAddress, 6);
    memcpy(ether->sourceAddress, localMac, 6);
    ether->frameType = htons(0x0800);

    // Build IP header
    ipHeader *ip = (ipHeader*) ether->data;
    ip->rev            = 4;
    ip->size           = 5;
    ip->typeOfService  = 0;
    ip->id             = 0;
    ip->flagsAndOffset = 0;
    ip->ttl            = 64;
    ip->protocol       = PROTOCOL_TCP;
    ip->headerChecksum = 0;

    uint8_t localIp[4];
    getIpAddress(localIp);
    memcpy(ip->sourceIp, localIp, 4);
    memcpy(ip->destIp, mqttSocket->remoteIpAddress, 4);

    // Build TCP header
    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    tcp->sourcePort            = htons(mqttSocket->localPort);
    tcp->destPort              = htons(mqttSocket->remotePort);
    tcp->sequenceNumber        = htonl(mqttSocket->sequenceNumber);
    tcp->acknowledgementNumber = htonl(mqttSocket->acknowledgementNumber);
    tcp->offsetFields          = htons((5 << OFS_SHIFT) | PSH | ACK);
    tcp->windowSize            = htons(1024);
    tcp->urgentPointer         = 0;
    tcp->checksum              = 0;

    memcpy(tcp->data, mqttData, mqttDataSize);

    uint16_t tcpLength     = sizeof(tcpHeader) + mqttDataSize;
    uint16_t totalIpLength = ipHeaderLength + tcpLength;
    ip->length         = htons(totalIpLength);
    ip->headerChecksum = 0;
    calcIpChecksum(ip);

    uint32_t sum = 0;
    uint16_t protocolField  = htons(ip->protocol);
    uint16_t tcpLengthField = htons(tcpLength);
    sumIpWords(ip->sourceIp, 4, &sum);
    sumIpWords(ip->destIp, 4, &sum);
    sumIpWords(&protocolField, 2, &sum);
    sumIpWords(&tcpLengthField, 2, &sum);
    sumIpWords(tcp, tcpLength, &sum);
    tcp->checksum = getIpChecksum(sum);

    putEtherPacket(ether, sizeof(etherHeader) + totalIpLength);

    mqttSocket->sequenceNumber += mqttDataSize;

    putsUart0("MQTT PINGREQ sent\r\n");
}

void connectMqtt(socket *s)
{
    uint8_t mqttData[100] = {};
    uint16_t mqttDataSize = 0;

    mqttData[mqttDataSize++] = 0x10;    // Connect
    mqttData[mqttDataSize++] = 19;      // Remaining length
    mqttData[mqttDataSize++] = 0x00;    // Protocol name MSB
    mqttData[mqttDataSize++] = 0x04;    // Protocol name LSB
    mqttData[mqttDataSize++] = 'M';
    mqttData[mqttDataSize++] = 'Q';
    mqttData[mqttDataSize++] = 'T';
    mqttData[mqttDataSize++] = 'T';
    mqttData[mqttDataSize++] = 0x04;    // Protocol level
    mqttData[mqttDataSize++] = 0x02;    // Connect flags
    mqttData[mqttDataSize++] = 0x00;    // Keep alive MSB
    mqttData[mqttDataSize++] = 0x3C;    // Keep alive LSB (60s)
    mqttData[mqttDataSize++] = 0x00;    // Client ID length MSB
    mqttData[mqttDataSize++] = 0x07;    // Client ID length LSB
    mqttData[mqttDataSize++] = 'd';
    mqttData[mqttDataSize++] = 'i';
    mqttData[mqttDataSize++] = 's';
    mqttData[mqttDataSize++] = 'p';
    mqttData[mqttDataSize++] = 'l';
    mqttData[mqttDataSize++] = 'a';
    mqttData[mqttDataSize++] = 'y';

    uint8_t buffer[1518];
    etherHeader *ether = (etherHeader*) buffer;

    // Build ethernet header
    uint8_t localMac[6];
    getEtherMacAddress(localMac);
    memcpy(ether->destAddress, s->remoteHwAddress, 6);
    memcpy(ether->sourceAddress, localMac, 6);
    ether->frameType = htons(0x0800);

    // Build IP header
    ipHeader *ip = (ipHeader*) ether->data;
    ip->rev            = 4;
    ip->size           = 5;
    ip->typeOfService  = 0;
    ip->id             = 0;
    ip->flagsAndOffset = 0;
    ip->ttl            = 64;
    ip->protocol       = PROTOCOL_TCP;
    ip->headerChecksum = 0;

    uint8_t localIp[4];
    getIpAddress(localIp);
    memcpy(ip->sourceIp, localIp, 4);
    memcpy(ip->destIp, s->remoteIpAddress, 4);

    // Build TCP header
    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    tcp->sourcePort            = htons(s->localPort);
    tcp->destPort              = htons(s->remotePort);
    tcp->sequenceNumber        = htonl(s->sequenceNumber);
    tcp->acknowledgementNumber = htonl(s->acknowledgementNumber);
    tcp->offsetFields          = htons((5 << OFS_SHIFT) | PSH | ACK);
    tcp->windowSize            = htons(1024);
    tcp->urgentPointer         = 0;
    tcp->checksum              = 0;

    // Copy MQTT payload
    memcpy(tcp->data, mqttData, mqttDataSize);

    // IP length and checksum
    uint16_t tcpLength     = sizeof(tcpHeader) + mqttDataSize;
    uint16_t totalIpLength = ipHeaderLength + tcpLength;
    ip->length         = htons(totalIpLength);
    ip->headerChecksum = 0;
    calcIpChecksum(ip);

    // TCP checksum
    uint32_t sum = 0;
    uint16_t protocolField  = htons(ip->protocol);
    uint16_t tcpLengthField = htons(tcpLength);
    sumIpWords(ip->sourceIp, 4, &sum);
    sumIpWords(ip->destIp, 4, &sum);
    sumIpWords(&protocolField, 2, &sum);
    sumIpWords(&tcpLengthField, 2, &sum);
    sumIpWords(tcp, tcpLength, &sum);
    tcp->checksum = getIpChecksum(sum);

    putEtherPacket(ether, sizeof(etherHeader) + totalIpLength);

    // Advance sequence number by payload size
    s->sequenceNumber += mqttDataSize;

    putsUart0("MQTT CONNECT sent\r\n");

    startPeriodicTimer(mqtt_ping, 30);
}

void disconnectMqtt()
{
}

void publishMqtt(char strTopic[], char strData[])
{
    if (mqttSocket == NULL || mqttSocket->state != TCP_ESTABLISHED)
        return;

    uint8_t mqttData[200] = {};
    uint16_t mqttDataSize = 0;
    uint16_t topicLen = strlen(strTopic);
    uint16_t dataLen  = strlen(strData);

    // Remaining length = 2 (topic length) + topic + data
    uint8_t remainingLength = 2 + topicLen + dataLen;

    mqttData[mqttDataSize++] = 0x30;                        // PUBLISH, QoS 0, no retain
    mqttData[mqttDataSize++] = remainingLength;
    mqttData[mqttDataSize++] = (topicLen >> 8) & 0xFF;      // Topic length MSB
    mqttData[mqttDataSize++] = topicLen & 0xFF;             // Topic length LSB
    memcpy(&mqttData[mqttDataSize], strTopic, topicLen);
    mqttDataSize += topicLen;
    memcpy(&mqttData[mqttDataSize], strData, dataLen);
    mqttDataSize += dataLen;

    uint8_t buffer[1518];
    etherHeader *ether = (etherHeader*) buffer;

    uint8_t localMac[6];
    getEtherMacAddress(localMac);
    memcpy(ether->destAddress, mqttSocket->remoteHwAddress, 6);
    memcpy(ether->sourceAddress, localMac, 6);
    ether->frameType = htons(0x0800);

    ipHeader *ip = (ipHeader*) ether->data;
    ip->rev            = 4;
    ip->size           = 5;
    ip->typeOfService  = 0;
    ip->id             = 0;
    ip->flagsAndOffset = 0;
    ip->ttl            = 64;
    ip->protocol       = PROTOCOL_TCP;
    ip->headerChecksum = 0;

    uint8_t localIp[4];
    getIpAddress(localIp);
    memcpy(ip->sourceIp, localIp, 4);
    memcpy(ip->destIp, mqttSocket->remoteIpAddress, 4);

    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    tcp->sourcePort            = htons(mqttSocket->localPort);
    tcp->destPort              = htons(mqttSocket->remotePort);
    tcp->sequenceNumber        = htonl(mqttSocket->sequenceNumber);
    tcp->acknowledgementNumber = htonl(mqttSocket->acknowledgementNumber);
    tcp->offsetFields          = htons((5 << OFS_SHIFT) | PSH | ACK);
    tcp->windowSize            = htons(1024);
    tcp->urgentPointer         = 0;
    tcp->checksum              = 0;

    memcpy(tcp->data, mqttData, mqttDataSize);

    uint16_t tcpLength     = sizeof(tcpHeader) + mqttDataSize;
    uint16_t totalIpLength = ipHeaderLength + tcpLength;
    ip->length         = htons(totalIpLength);
    ip->headerChecksum = 0;
    calcIpChecksum(ip);

    uint32_t sum = 0;
    uint16_t protocolField  = htons(ip->protocol);
    uint16_t tcpLengthField = htons(tcpLength);
    sumIpWords(ip->sourceIp, 4, &sum);
    sumIpWords(ip->destIp, 4, &sum);
    sumIpWords(&protocolField, 2, &sum);
    sumIpWords(&tcpLengthField, 2, &sum);
    sumIpWords(tcp, tcpLength, &sum);
    tcp->checksum = getIpChecksum(sum);

    putEtherPacket(ether, sizeof(etherHeader) + totalIpLength);

    mqttSocket->sequenceNumber += mqttDataSize;

    putsUart0("MQTT PUBLISH sent\r\n");
}

void subscribeMqtt(char strTopic[])
{
    if (mqttSocket == NULL || mqttSocket->state != TCP_ESTABLISHED)
        return;

    uint8_t mqttData[200] = {};
    uint16_t mqttDataSize = 0;
    uint16_t topicLen = strlen(strTopic);
    uint8_t remainingLength = 2 + 2 + topicLen + 1;

    mqttData[mqttDataSize++] = 0x82;
    mqttData[mqttDataSize++] = remainingLength;
    mqttData[mqttDataSize++] = 0x00;            // Packet ID MSB
    mqttData[mqttDataSize++] = 0x01;            // Packet ID LSB
    mqttData[mqttDataSize++] = (topicLen >> 8) & 0xFF;
    mqttData[mqttDataSize++] = topicLen & 0xFF;
    memcpy(&mqttData[mqttDataSize], strTopic, topicLen);
    mqttDataSize += topicLen;
    mqttData[mqttDataSize++] = 0x00;            // QoS 0

    uint8_t buffer[1518];
    etherHeader *ether = (etherHeader*) buffer;

    uint8_t localMac[6];
    getEtherMacAddress(localMac);
    memcpy(ether->destAddress, mqttSocket->remoteHwAddress, 6);
    memcpy(ether->sourceAddress, localMac, 6);
    ether->frameType = htons(0x0800);

    ipHeader *ip = (ipHeader*) ether->data;
    ip->rev            = 4;
    ip->size           = 5;
    ip->typeOfService  = 0;
    ip->id             = 0;
    ip->flagsAndOffset = 0;
    ip->ttl            = 64;
    ip->protocol       = PROTOCOL_TCP;
    ip->headerChecksum = 0;

    uint8_t localIp[4];
    getIpAddress(localIp);
    memcpy(ip->sourceIp, localIp, 4);
    memcpy(ip->destIp, mqttSocket->remoteIpAddress, 4);

    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    tcp->sourcePort            = htons(mqttSocket->localPort);
    tcp->destPort              = htons(mqttSocket->remotePort);
    tcp->sequenceNumber        = htonl(mqttSocket->sequenceNumber);
    tcp->acknowledgementNumber = htonl(mqttSocket->acknowledgementNumber);
    tcp->offsetFields          = htons((5 << OFS_SHIFT) | PSH | ACK);
    tcp->windowSize            = htons(1024);
    tcp->urgentPointer         = 0;
    tcp->checksum              = 0;

    memcpy(tcp->data, mqttData, mqttDataSize);

    uint16_t tcpLength     = sizeof(tcpHeader) + mqttDataSize;
    uint16_t totalIpLength = ipHeaderLength + tcpLength;
    ip->length         = htons(totalIpLength);
    ip->headerChecksum = 0;
    calcIpChecksum(ip);

    uint32_t sum = 0;
    uint16_t protocolField  = htons(ip->protocol);
    uint16_t tcpLengthField = htons(tcpLength);
    sumIpWords(ip->sourceIp, 4, &sum);
    sumIpWords(ip->destIp, 4, &sum);
    sumIpWords(&protocolField, 2, &sum);
    sumIpWords(&tcpLengthField, 2, &sum);
    sumIpWords(tcp, tcpLength, &sum);
    tcp->checksum = getIpChecksum(sum);

    putEtherPacket(ether, sizeof(etherHeader) + totalIpLength);

    mqttSocket->sequenceNumber += mqttDataSize;

    putsUart0("MQTT SUBSCRIBE sent\r\n");
}

void unsubscribeMqtt(char strTopic[])
{
}

void printMqttRecords()
{
    putsUart0(RESET_TEXT);      // Clears terminal
    char buffer[50];
    sprintf(buffer, "MQTT TOPIC          DATA");
    PRINT_EFFECT(buffer, BOLD_FONT);

    uint8_t i = 0;
    while(i < MAX_RECORDS && mqttRecords[i].valid == true)
    {
        PRINT_COLOR_FONT(mqttRecords[i].topic, GREEN_FG, UNDERLINE_FONT);
        putsUart0("     ");
        putsUart0(mqttRecords[i++].payload);
        putsUart0("\n\r");
    }
}

void printMqttRecords()
{
    putsUart0(RESET_TEXT);      // Clears terminal

    char buffer[50];
    sprintf(buffer, "MQTT TOPIC          DATA\r\n");
    PRINT_EFFECT(buffer, BOLD_FONT);

    uint8_t i = 0;
    while (i < MAX_RECORDS && mqttRecords[i].valid == true)  // && not 'and', bounds check first
    {
        PRINT_COLOR_FONT(mqttRecords[i].topic, GREEN_FG, UNDERLINE_FONT);  // [i].topic not .topic[i]
        putsUart0("  ");
        putsUart0(mqttRecords[i].payload);  // [i].payload, increment i separately
        putsUart0("\r\n");
        i++;
    }
}

void parseMqttPublish(uint8_t *payload)
{
    uint8_t i;
    int8_t freeSlot = -1;

    // Extract the topic from the payload
    uint8_t remainingLength = payload[1];
    uint16_t topicLen = ((uint16_t)payload[2] << 8) | payload[3];
    if (topicLen >= MAX_TOPIC_LEN)
        topicLen = MAX_TOPIC_LEN - 1;

    char topic[MAX_TOPIC_LEN];
    memcpy(topic, &payload[4], topicLen);
    topic[topicLen] = '\0';

    // Extract the data for the topic from the payload
    uint16_t dataLen = remainingLength - 2 - topicLen;
    if (dataLen >= MAX_PAYLOAD_LEN)
        dataLen = MAX_PAYLOAD_LEN - 1;

    char dataStr[MAX_PAYLOAD_LEN];
    memcpy(dataStr, &payload[4 + topicLen], dataLen);
    dataStr[dataLen] = '\0';

    // Search struct for matching topic to update
    for (i = 0; i < MAX_RECORDS; i++)
    {
        if (mqttRecords[i].valid && strcmp(mqttRecords[i].topic, topic) == 0)
        {
            // Topic was found. update data and return to avoid adding a dup
            strncpy(mqttRecords[i].payload, dataStr, MAX_PAYLOAD_LEN - 1);
            mqttRecords[i].payload[MAX_PAYLOAD_LEN - 1] = '\0';
            printMqttRecords();
            return;
        }
        if (!mqttRecords[i].valid && freeSlot < 0)
            freeSlot = i;
    }

    // If not found, add the data to a free slot if available
    if (freeSlot >= 0)
    {
        strncpy(mqttRecords[freeSlot].topic,   topic,   MAX_TOPIC_LEN - 1);
        strncpy(mqttRecords[freeSlot].payload, dataStr, MAX_PAYLOAD_LEN - 1);
        mqttRecords[freeSlot].topic[MAX_TOPIC_LEN - 1]     = '\0';
        mqttRecords[freeSlot].payload[MAX_PAYLOAD_LEN - 1] = '\0';
        mqttRecords[freeSlot].valid = true;
    }
    else
    {
        putsUart0("MQTT record table full — cannot store\r\n");
    }

    printMqttRecords();
}
