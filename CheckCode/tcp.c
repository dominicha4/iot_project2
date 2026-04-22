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

bool mqttConnect = false;
extern socket sockets[MAX_SOCKETS];
// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// Get socket
socket *getsocket(uint8_t instance)
{
    return &sockets[instance];
}

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

///board inintiates the connection to the broker
void TcpConnection(socket *s) 
{
    //fresh buffer and ensures clean packet
    uint8_t Buff[MAX_PACKET_SIZE]; 
    memset(Buff, 0, MAX_PACKET_SIZE);
    etherHeader *ether = (etherHeader*) Buff;

    //get broker IP ADDRESS
    uint8_t BIp[4];
    getIpMqttBrokerAddress(BIp);
    memcpy(s->remoteIpAddress, BIp, 4);//store broker as a remote destination
    s->remotePort = 1883;//broker port
    s->localPort = 49152; //our port
    s->sequenceNumber = 1; //start sequence number
    s->acknowledgementNumber = 0;//nothing is recieved
    s->state = TCP_SYN_SENT; //checks to see for SYN and ACK
    sendTcpMessage(ether, s, SYN, NULL, 0);// sends message to broker
    putsUart0("TCP SYN was sent to broker\r\n");
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
    socket *s = NULL;
    ipHeader *ip;
    tcpHeader *tcp;
    uint8_t ipHeaderLength;
    uint8_t i;

    putsUart0("Entered processTcpResponse\r\n");

    if (!isTcp(ether))
        return;

    if (!isTcpPortOpen(ether))
        return;

    ip = (ipHeader*) ether->data;
    ipHeaderLength = ip->size * 4;
    tcp = (tcpHeader*) ((uint8_t*) ip + ipHeaderLength);

    uint16_t srcPort = ntohs(tcp->sourcePort);
    uint16_t dstPort = ntohs(tcp->destPort);

    bool syn = isTcpSyn(ether);
    bool ack = isTcpAck(ether);

    // -------------------------
        // 1. Final ACK handles FIN
        //
    // -------------------------
//checks if broker is closing, sends a FIN FLAG if it does occure
    uint16_t flags = ntohs(tcp->offsetFields) & 0x01FF;
    if (flags & FIN)
    {
        s->acknowledgementNumber += 1;
        sendTcpMessage(ether, s, ACK, NULL, 0);
        sendTcpMessage(ether, s, FIN | ACK, NULL, 0);
        s->state = TCP_CLOSE_WAIT;
        setTcpState(0, TCP_CLOSED);
        setTcpState(1, MQTT_UNCONNECTED);
        putsUart0("TCP connection closed\r\n");
    }
    // 2. waits for SYN+ACK
    // -------------------------
    if (syn && ack)
    {
        
        for (i = 0; i < MAX_SOCKETS; i++)
        {
            if (sockets[i].state == TCP_SYN_SENT &&
                sockets[i].localPort == dstPort &&
                sockets[i].remotePort == srcPort)
            {
                // IMPORTANT: use THIS socket
                s = &sockets[i];
                s->acknowledgementNumber = ntohl(tcp->sequenceNumber) + 1; //brokers sequence

                sockets[i].state = TCP_ESTABLISHED;

                putsUart0("TCP connection established\r\n");


                if (mqttConnect)
                {
                    mqttConnect = false;
                    connectMqtt(s, ether);
                }

                return;
            }
        }
    }

    // -------------------------
    // 3. DATA / MQTT / ACK handling
    // -------------------------
    if (ack)
    {
        uint16_t raw = ntohs(tcp->offsetFields);
        uint8_t tcpHeaderLength = (raw >> 12) * 4;

        uint16_t dataSize = ntohs(ip->length) - ipHeaderLength - tcpHeaderLength;

        // find socket again
        for (i = 0; i < MAX_SOCKETS; i++)
        {
            if (sockets[i].state == TCP_ESTABLISHED &&
                sockets[i].localPort == dstPort &&
                sockets[i].remotePort == srcPort)
            {
                s = &sockets[i];
                break;
            }
        }

        if (s == NULL)
            return;

        // ---------------- MQTT parsing ----------------
        if (dataSize > 0)
        {
            uint8_t *data = tcp->data;

            uint8_t packetType = data[0] >> 4;

            if (packetType == 2)
            {
               // putsUart0("MQTT CONNACK received\r\n");
                connACKMqtt(s, ether, data);
            }
            else if (packetType == 3)
            {
                putsUart0("MQTT PUBLISH received\r\n");
            }
        }

        s->acknowledgementNumber += dataSize;
        sendTcpMessage(ether, s, ACK, NULL, 0);
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

    // client checks source port and sends traffic from the broker to check where its from//
    //changed
    uint16_t srcPort = ntohs(tcp->sourcePort);

    uint8_t i;
    for (i = 0; i < tcpPortCount; i++)
    {
        //proccesses the packet from out port
        if (tcpPorts[i] == srcPort)
            return true;
    }

    return false;
}

void sendTcpResponse(etherHeader *ether, socket *s, uint16_t flags)
{
    putsUart0("Entered sendTcpResponse\r\n");
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

    putsUart0("Entered sendTcpMessage\r\n");

    //get the mac from the board and driver
    uint8_t tempMac[6];
    getEtherMacAddress(tempMac);
    //memcpy(tempMac, ether->destAddress, 6);//broker is the destination which is stored in tcpconnection
    memcpy(ether->destAddress, s->remoteHwAddress, 6);
    memcpy(ether->sourceAddress, tempMac, 6);//source is our mac address
    putsUart0("MAC addresses swapped\r\n");

    // Fill IP Header
    ip->rev = 0x4;                     // IPv4
    ip->size = 0x5;                    // 5 * 4 = 20 bytes (standard IP header size)
    ip->typeOfService = 0;             // No special handling
    ip->id = 0;                        // Identification (0 for now)
    ip->flagsAndOffset = 0;            // No fragmentation
    ip->ttl = 128;                     // Time to live
    ip->protocol = PROTOCOL_TCP;       // TCP protocol
    ip->headerChecksum = 0;            // Checksum (calculated later)

    //like the logic from above
    uint8_t tempIp[4];
   // memcpy(tempIp, ip->sourceIp, 4);
    getIpAddress(tempIp);
    memcpy(ip->sourceIp, tempIp, 4);
    memcpy(ip->destIp, s->remoteIpAddress, 4);
    putsUart0("IP addresses swapped\r\n");

    //swap TCP source and destination ports
    //uint16_t tempPort = tcp->sourcePort;
    tcp->sourcePort = htons(s->localPort);
    tcp->destPort = htons(s->remotePort);
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

