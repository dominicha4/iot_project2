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

mqttState_t mqttState = MQTT_IDLE;
socket *mqttSocket = NULL;
bool mqttConnected = false;     // MQTT connection state
bool mqttConnectSent = false;   // Used to prevent it from sending another. Maybe used

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
    mqttData[mqttDataSize++] = 'W';
    mqttData[mqttDataSize++] = 'E';
    mqttData[mqttDataSize++] = 'B';
    mqttData[mqttDataSize++] = 'T';
    mqttData[mqttDataSize++] = 'E';
    mqttData[mqttDataSize++] = 'A';
    mqttData[mqttDataSize++] = 'M';

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
}

void subscribeMqtt(char strTopic[])
{
}

void unsubscribeMqtt(char strTopic[])
{
}
