// TCP Library
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
#include <time.h>
#include "arp.h"
#include "tcp.h"
#include "timer.h"
#include "uart0.h"
#include "mqtt.h"
#include <stdint.h>
#include <stdlib.h>

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------
#define MAX_SOCKETS 10
#define MAX_TCP_PORTS 4

uint16_t tcpPorts[MAX_TCP_PORTS];
uint8_t tcpPortCount = 0;
uint8_t tcpState[MAX_TCP_PORTS];
//socket sockets[MAX_SOCKETS];

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// Set TCP state
void setTcpState(uint8_t instance, uint8_t state)
{
    tcpState[instance] = state;
}

// Get TCP state
uint8_t getTcpState(uint8_t instance)
{
    return tcpState[instance];
}

// Determines whether packet is TCP packet
// Must be an IP packet
//this makes sure ethernet frame contains IP "is this traffic TCP traffic??
bool isTcp(etherHeader *ether)
{
    // first make sure it's IP
    if (!isIp(ether))
        return false;

    ipHeader *ip = (ipHeader*) ether->data;
    return (ip->protocol == PROTOCOL_TCP); //if IP protocol field is 6 then it is TCP
}

//locate the TCP header, read the flags and see if SYN is set
bool isTcpSyn(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*) ether->data;            //check if SYN flag is se

    uint8_t ipHeaderLength = ip->size * 4;  //figure out where TCP header begins
    tcpHeader *tcp = (tcpHeader*) ((uint8_t*) ip + ipHeaderLength); //points to TCP header

    uint16_t flags = ntohs(tcp->offsetFields) & 0x01FF; //extract only the TCP flag bits

    return ((flags & SYN) != 0);    //check if SYN flag is set in tcp flag field
}

bool isTcpAck(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*) ether->data;

    uint8_t ipHeaderLength = ip->size * 4;  //figure out where TCP header begins
    tcpHeader *tcp = (tcpHeader*) ((uint8_t*) ip + ipHeaderLength); //points to TCP header

    uint16_t flags = ntohs(tcp->offsetFields) & 0x01FF; //extract only the TCP flag bits

    return ((flags & ACK) != 0);    //check if ACK flag is set in tcp flag field
}

void sendTcpPendingMessages(etherHeader *ether)
{
}

void processTcpResponse(etherHeader *ether)
{
    socket *s = NULL;           // Pointer to matching socket (if found)
    ipHeader *ip;               // Pointer to IP header inside Ethernet frame
    tcpHeader *tcp;             // Pointer to TCP header (after IP header)
    uint8_t ipHeaderLength;     // Length of IP header in bytes
    uint8_t i;                  // Loop variable for socket search

    //putsUart0("Entered processTcpResponse\r\n");

    if (!isTcp(ether)) // Ignore packet if it's not TCP
    {
        putsUart0("Not TCP\n");
        return;
    }

    // Extract both IP and TCP headers
    ip = (ipHeader*) ether->data; //points to IP header, which starts at the Ethernet data field
    ipHeaderLength = ip->size * 4; //IP header legnth is stored in 32 bit words, so multiply by 4 to get bytes
    tcp = (tcpHeader*) ((uint8_t*) ip + ipHeaderLength); //move past the IP header so tcp points to the tcp header

    // Extract source and destination ports (convert from network byte order)
    uint16_t srcPort = ntohs(tcp->sourcePort);
    uint16_t dstPort = ntohs(tcp->destPort);

    /**** FINDS THE MATCHING *****/
    for (i = 0; i < MAX_SOCKETS; i++)
    {
        if (sockets[i].state != TCP_CLOSED) // Only check active sockets
        {
            // Match local and remote ports
            if (sockets[i].localPort == dstPort
                    && sockets[i].remotePort == srcPort)
            {
                s = &sockets[i];
                break;
            }
        }
    }

    /***** CLIENT SIDE TO HANDLE SYN-ACK *****/
    if (s != NULL && s->state == TCP_SYN_SENT)
    {
        if (isTcpSyn(ether) && isTcpAck(ether)) // SYN+ACK received from server
        {
            putsUart0("SYN-ACK received\r\n");

            // Acknowledge server's sequence number
            s->acknowledgementNumber = ntohl(tcp->sequenceNumber) + 1;

            // Increment our sequence number (SYN consumes 1)
            s->sequenceNumber += 1;

            // Send final ACK (Step 3 of handshake)
            sendTcpResponse(ether, s, ACK);

            // Connection established
            s->state = TCP_ESTABLISHED;

            putsUart0("TCP connection established (client)\r\n");

            if (mqttState == MQTT_WAIT_TCP)
            {
                mqttState = MQTT_TCP_READY;
            }
        }
    }

    /***** MQTT PART TO CHECK FOR CONNACK AFTER TCP IS ESTABLISHED *****/
    if (s != NULL && s->state == TCP_ESTABLISHED)
    {
        uint16_t dataLen = ntohs(ip->length) - (ipHeaderLength + sizeof(tcpHeader));
        if (dataLen == 0)
            return;
        uint8_t *payload = tcp->data;

        if (payload[0] == 0x20)
        {
            putsUart0("MQTT CONNACK received\r\n");

            if (payload[3] == 0x00)
            {
                putsUart0("MQTT SUCCESS\r\n");
                mqttConnected = true;
                mqttState = MQTT_CONNECTED;

                // Update ack number by the size of the CONNACK payload
                s->acknowledgementNumber += ntohs(ip->length)
                        - (ipHeaderLength + sizeof(tcpHeader));

                // Send bare ACK back to broker
                sendTcpResponse(ether, s, ACK);
            }
            else
            {
                putsUart0("MQTT FAILED\r\n");
            }
        }
        else if (payload[0] == 0xD0)
        {
            putsUart0("MQTT PINGRESP received\r\n");
            s->acknowledgementNumber += ntohs(ip->length)
                    - (ipHeaderLength + sizeof(tcpHeader));
            sendTcpResponse(ether, s, ACK);
        }
        else if (payload[0] == 0x90)  // SUBACK
        {
            putsUart0("MQTT SUBACK received\r\n");
            s->acknowledgementNumber += ntohs(ip->length)
                    - (ipHeaderLength + sizeof(tcpHeader));
            sendTcpResponse(ether, s, ACK);
        }
        else if ((payload[0] & 0xF0) == 0x30)  // PUBLISH
        {
            putsUart0("MQTT PUBLISH received\r\n");
            s->acknowledgementNumber += ntohs(ip->length)
                    - (ipHeaderLength + sizeof(tcpHeader));
            sendTcpResponse(ether, s, ACK);

            parseMqttPublish(payload);
        }
        else if (payload[0] == 0xB0)  // UNSUBACK
        {
            putsUart0("MQTT UNSUBACK received\r\n");
            s->acknowledgementNumber += ntohs(ip->length)
                    - (ipHeaderLength + sizeof(tcpHeader));
            sendTcpResponse(ether, s, ACK);
        }

    }
}

void processTcpArpResponse(etherHeader *ether)
{
}

// stores the list of TCP ports that this board should treat as open/listening
//MQTT is on port 1883
void setTcpPortList(uint16_t ports[], uint8_t count)
{
    uint8_t i;

    // do not allow more ports than the array can hold
    tcpPortCount = count;
    if (tcpPortCount > MAX_TCP_PORTS)
        tcpPortCount = MAX_TCP_PORTS;

    // copy the port numbers into the global TCP port list
    for (i = 0; i < tcpPortCount; i++)
        tcpPorts[i] = ports[i];
}

// checks whether the destination port in the incoming TCP packet
// matches one of the ports we marked as open
bool isTcpPortOpen(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*) ether->data;

    // skip over the IP header to get to the TCP header
    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader *tcp = (tcpHeader*) ((uint8_t*) ip + ipHeaderLength);

    // destination port tells us which local port the packet is trying to reach
    uint16_t destPort = ntohs(tcp->destPort);

    uint8_t i;
    for (i = 0; i < tcpPortCount; i++)
    {
        if (tcpPorts[i] == destPort)
            return true;
    }

    return false;
}

void sendTcpResponse(etherHeader *ether, socket *s, uint16_t flags)
{
    //putsUart0("Entered sendTcpResponse\r\n");
    sendTcpMessage(ether, s, flags, NULL, 0); //send TCP response packet with no payload data
}

void sendTcpMessage(etherHeader *ether, socket *s, uint16_t flags,
                    uint8_t data[], uint16_t dataSize)
{
    ipHeader *ip = (ipHeader*) ether->data; //point to IP header inside ethernet frame
    uint8_t ipHeaderLength = ip->size * 4; //IP header length is stored in 32-bit words, so convert to bytes
    tcpHeader *tcp = (tcpHeader*) ((uint8_t*) ip + ipHeaderLength); //move past IP header so tcp points to TCP header

    uint16_t tcpHeaderLength = sizeof(tcpHeader); //normal TCP header is 20 bytes when there are no options
    uint16_t tcpLength = tcpHeaderLength + dataSize; //TCP length is header plus payload
    uint16_t totalIpLength = ipHeaderLength + tcpLength; //full IP packet length without ethernet header

    uint32_t sum = 0; //used for checksum calculation
    uint16_t protocolField; //used when adding protocol to pseudo header
    uint16_t tcpLengthField; //used when adding TCP length to pseudo header

    //putsUart0("Entered sendTcpMessage\r\n");

    //swap destination and source MAC addresses so packet goes back to sender
    uint8_t tempMac[6];
    memcpy(tempMac, ether->destAddress, 6);
    memcpy(ether->destAddress, ether->sourceAddress, 6);
    memcpy(ether->sourceAddress, tempMac, 6);
    putsUart0("MAC addresses swapped\r\n");

    //swap source and destination IP addresses so reply goes back to the other device
    uint8_t tempIp[4];
    memcpy(tempIp, ip->sourceIp, 4);
    memcpy(ip->sourceIp, ip->destIp, 4);
    memcpy(ip->destIp, tempIp, 4);
    putsUart0("IP addresses swapped\r\n");

    //swap TCP source and destination ports
    uint16_t tempPort = tcp->sourcePort;
    tcp->sourcePort = tcp->destPort;
    tcp->destPort = tempPort;
    putsUart0("TCP ports swapped\r\n");

    //set our TCP sequence number from the socket
    tcp->sequenceNumber = htonl(s->sequenceNumber);

    //set our TCP acknowledgement number from the socket
    tcp->acknowledgementNumber = htonl(s->acknowledgementNumber);
    putsUart0("Sequence and ACK numbers set\r\n");

    //top 4 bits are TCP header length in 32-bit words
    //bottom bits are the TCP flags like SYN, ACK, FIN
    tcp->offsetFields = htons((5 << OFS_SHIFT) | flags);

    tcp->windowSize = htons(1024); //simple window size for now
    tcp->urgentPointer = 0; //not using urgent data
    tcp->checksum = 0; //clear TCP checksum before recalculating it
    putsUart0("TCP header fields filled\r\n");

    //copy TCP payload data after the header if there is any
    if ((data != NULL) && (dataSize > 0))
    {
        memcpy(tcp->data, data, dataSize);
        putsUart0("TCP payload copied\r\n");
    }
    else
    {
        putsUart0("No TCP payload\r\n");
    }

    //update IP total length field
    ip->length = htons(totalIpLength);
    putsUart0("IP total length updated\r\n");

    //recalculate IP header checksum
    ip->headerChecksum = 0;
    calcIpChecksum(ip);
    putsUart0("IP checksum calculated\r\n");

    //TCP checksum uses a pseudo header plus the TCP header and payload
    tcp->checksum = 0; //checksum field must be zero before calculating
    sum = 0; //start checksum sum at zero

    //add source IP and destination IP from the pseudo header
    sumIpWords(ip->sourceIp, 4, &sum);
    sumIpWords(ip->destIp, 4, &sum);

    //add protocol field from pseudo header
    protocolField = htons(ip->protocol);
    sumIpWords(&protocolField, 2, &sum);

    //add TCP length from pseudo header
    tcpLengthField = htons(tcpLength);
    sumIpWords(&tcpLengthField, 2, &sum);

    //add TCP header and TCP payload
    sumIpWords(tcp, tcpLength, &sum);

    //finish checksum and store it in TCP header
    tcp->checksum = getIpChecksum(sum);
    putsUart0("TCP checksum calculated\r\n");

    //send the ethernet frame
    putEtherPacket(ether, sizeof(etherHeader) + totalIpLength);
    putsUart0("TCP packet sent\r\n");
}

void tcpConnect(etherHeader *data, uint16_t port)
{
    socket *s = newSocket();
    if (s == NULL)
    {
        putsUart0("No free socket for TCP connect\r\n");
        return;
    }

    putsUart0("Starting TCP connection...\r\n");

    srand(time(NULL));
    s->remotePort = port;
    s->localPort = 50000 + (rand() % 1000);
    s->sequenceNumber = 1;
    s->acknowledgementNumber = 0;
    s->state = TCP_SYN_SENT;

    s->remoteIpAddress[0] = 192;    // Sean's IP address
    s->remoteIpAddress[1] = 168;
    s->remoteIpAddress[2] = 1;
    s->remoteIpAddress[3] = 131;

    s->remoteHwAddress[0] = 0x02;   // Sean's MAC Address
    s->remoteHwAddress[1] = 0x03;
    s->remoteHwAddress[2] = 0x04;
    s->remoteHwAddress[3] = 0x05;
    s->remoteHwAddress[4] = 0x06;
    s->remoteHwAddress[5] = 0x83;

//    s->remoteIpAddress[0] = 10;   // My IP when using mosquitto for broker
//    s->remoteIpAddress[1] = 232;
//    s->remoteIpAddress[2] = 19;
//    s->remoteIpAddress[3] = 100;
//
//    s->remoteHwAddress[0] = 0x10; // My MAC when using mosquitto for broker
//    s->remoteHwAddress[1] = 0xF6;
//    s->remoteHwAddress[2] = 0x0A;
//    s->remoteHwAddress[3] = 0x7B;
//    s->remoteHwAddress[4] = 0x10;
//    s->remoteHwAddress[5] = 0x4C;

    // Build ethernet header
    uint8_t localMac[6];
    getEtherMacAddress(localMac);
    memcpy(data->destAddress, s->remoteHwAddress, 6);
    memcpy(data->sourceAddress, localMac, 6);
    data->frameType = htons(0x0800);

    // Build IP header
    ipHeader *ip = (ipHeader*) data->data;
    ip->rev = 4;
    ip->size = 5;
    ip->typeOfService = 0;
    ip->id = 0;
    ip->flagsAndOffset = 0;
    ip->ttl = 64;
    ip->protocol = PROTOCOL_TCP;
    ip->headerChecksum = 0;

    uint8_t localIp[4];
    getIpAddress(localIp);
    memcpy(ip->sourceIp, localIp, 4);
    memcpy(ip->destIp, s->remoteIpAddress, 4);

    // Build TCP header
    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader *tcp = (tcpHeader*) ((uint8_t*) ip + ipHeaderLength);
    tcp->sourcePort = htons(s->localPort);
    tcp->destPort = htons(s->remotePort);
    tcp->sequenceNumber = htonl(s->sequenceNumber);
    tcp->acknowledgementNumber = htonl(0);
    tcp->offsetFields = htons((5 << OFS_SHIFT) | SYN);
    tcp->windowSize = htons(1024);
    tcp->urgentPointer = 0;
    tcp->checksum = 0;

    // IP length and checksum
    uint16_t tcpLength = sizeof(tcpHeader);
    uint16_t totalIpLength = ipHeaderLength + tcpLength;
    ip->length = htons(totalIpLength);
    ip->headerChecksum = 0;
    calcIpChecksum(ip);

    // TCP checksum
    uint32_t sum = 0;
    uint16_t protocolField = htons(ip->protocol);
    uint16_t tcpLengthField = htons(tcpLength);
    sumIpWords(ip->sourceIp, 4, &sum);
    sumIpWords(ip->destIp, 4, &sum);
    sumIpWords(&protocolField, 2, &sum);
    sumIpWords(&tcpLengthField, 2, &sum);
    sumIpWords(tcp, tcpLength, &sum);
    tcp->checksum = getIpChecksum(sum);

    putEtherPacket(data, sizeof(etherHeader) + totalIpLength);

    mqttSocket = s;

    putsUart0("SYN sent\r\n");
}
