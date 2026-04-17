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

//void processTcpResponse(etherHeader *ether)
//{
//    socket *s = NULL; //make a socket pointer and start it as null in case no socket is found
//    ipHeader *ip; //pointer that will point to UP header inside the Ethernet frame
//    tcpHeader *tcp; //pointer that will point to the TCP header after the IP header
//    uint8_t ipHeaderLength; //variable for the actual size of Ip header in bytes
//    uint8_t i;                 //loop variable for searching through socket list
//
//    putsUart0("Entered processTcpResponse\r\n");
//
//    if (!isTcp(ether)) //check if packet is TCP packet
//    {
//        putsUart0("Not TCP\n");
//        return;
//    }
//
//    if (!isTcpPortOpen(ether)) //if TCP packet is not going to open port, then ignore it
//    {
//        putsUart0("TCP port not open\n");
//        return;
//    }
//
//    ip = (ipHeader*) ether->data; //points to IP header, which starts at the Ethernet data field
//    ipHeaderLength = ip->size * 4; //IP header legnth is stored in 32 bit words, so multiply by 4 to get bytes
//    tcp = (tcpHeader*) ((uint8_t*) ip + ipHeaderLength); //move past the IP header so tcp points to the tcp header
//
//    // Extracts source and destination TCP ports from incoming packets
//    uint16_t srcPort = ntohs(tcp->sourcePort);
//    uint16_t dstPort = ntohs(tcp->destPort);
//    uint16_t offsetFields = ntohs(tcp->offsetFields);
//
//    //checks for the first step of TCP, client sends SYN and is trying to start a new connection
//    if (isTcpSyn(ether) && !isTcpAck(ether))
//    {
//        putsUart0("TCP SYN received\r\n");
//
//        s = newSocket(); //use an unused socket so we can track new TCP connection
//        if (s == NULL) //if no free socket available, leave becuase we have nowhere to store the new connection
//        {
//            putsUart0("No free socket\r\n");
//            return;
//        }
//
//        putsUart0("Socket allocated\r\n");
//
//        //function socket.c
//        getSocketInfoFromTcpPacket(ether, s); //copy client IP, MAC, source port and destination port into socket
//        putsUart0("Socket info copied\r\n");
//
//        s->sequenceNumber = 1; //choose a starting sequenct number for our side of TCP connection
//        s->acknowledgementNumber = ntohl(tcp->sequenceNumber) + 1; //client sent us a SYN with its own sequence number
//        s->state = TCP_SYN_RECEIVED; //we are now in SYN_RECEIVED state becasue we got SYN and will reply with SYN+ACK
//        putsUart0("Sending SYN+ACK\r\n");
//        sendTcpResponse(ether, s, SYN | ACK); // send SYN and ACK back to client as step 2 of handshake
//
//        s->sequenceNumber++;    //SYN consumes one sequence number
//    }
//    //this checks for the last step of TCP handshake, client sends ack after receiving our SYN+ACK
//    else if(isTcpAck(ether) && !isTcpSyn(ether))
//    {
//        putsUart0("Final ACK received\r\n");
//
//        //looks through sockets for matching connection
//        for (i = 0; i < MAX_SOCKETS; i++)
//        {
//            if (sockets[i].state == TCP_SYN_RECEIVED) //only chekcs sockets that are waiting for the final ACK
//            {
//                if ((sockets[i].localPort == ntohs(tcp->destPort))
//                        && (sockets[i].remotePort == ntohs(tcp->sourcePort)))
//                {
//                    sockets[i].state = TCP_ESTABLISHED; //handshake is done, so mark this socket as connected
//                    putsUart0("TCP connection established\r\n");
//
//                    if(mqttConnect)
//                    {
//                        mqttConnect = false;
//                        connectMqtt(&sockets[i], ether);
//                        putsUart0("Sending MQTT Connect\n\r");
//                    }
//
//                    break;    //stop searching becuase we found the right socket
//                }
//            }
//        }
//    }
//
//    else if(isTcpAck(ether)) //general ack based traffic for SYN-ACK, FIN, etc
//    {
//        //Extracts TCP header length from upper 4 bits of offset field
//        uint16_t rawOffsetFields = ntohs(tcp->offsetFields);
//
//        //The top 4 bits of offsetFields specify the number of 4-byte words
//        uint8_t tcpHeaderLength = (rawOffsetFields >> 12) * 4;
//        //Computes the payload size of IP length minus the headers
//        uint16_t dataSize = ntohs(ip->length) - ipHeaderLength - tcpHeaderLength;
//
//        if(isTcpSyn(ether)) //SYN-ACK response handling
//        {
//            //Updates ACK number using server sequence number
//            s->acknowledgementNumber += (ntohl(tcp->sequenceNumber) + 1);
//            //sends ACK back to server
//            sendTcpMessage(ether, s, ACK, NULL, 0);
//        }
//        else if((FIN & offsetFields) == FIN) //Terminating the Connection
//        {
//            uint8_t state = getTcpState(0);
//
//            if(state == TCP_ESTABLISHED) //Other device closes the connection
//            {
//                s->acknowledgementNumber += 1;
//
//                //Reply with FIN+ACK to close request
//                sendTcpMessage(ether, s, FIN | ACK, NULL, 0);
//
//                s->sequenceNumber += 1;
//                setTcpState(0, TCP_CLOSE_WAIT);
//            }
//
//            else if(state == TCP_FIN_WAIT_1) //We finish the active closing sequence
//            {
//                s->acknowledgementNumber += 1;
//                //Other device ACK
//                sendTcpMessage(ether, s, ACK, NULL, 0);
//
//                //Send funal FIN
//                sendTcpMessage(ether, s, FIN | ACK, NULL, 0);
//
//                setTcpState(0, TCP_CLOSED);
//            }
//            else if ((offsetFields & PSH) == PSH) //Processing MQTT
//            {
//                uint8_t *data = tcp->data;
//                uint8_t buffer[100] = {0};
//
//                //Copy the payload into the buffer
//                memcpy(buffer, data, dataSize);
//
//                //MQTT control packet type from upper 4 bits of the 1st byte
//                uint8_t flag = buffer[0] >> 4;
//
//                if (flag == 2) // CONNACK RECEIVED
//                {
//                    // set the flag for MQTT_CONNECTED
//                    setTcpState(1, MQTT_CONNECTED);
//                    putsUart0("MQTT CONNECTED!!!\n\r");
//                }
//                else if (flag == 3) // PUBLISH
//                {
//                                    }
//                else if (flag == 9) // SUBACK
//                {
//
//                }
//                else if (flag == 12) // PUBACK
//                {
//
//                }
//                else if (flag == 11) // UNSUBACK
//                {
//
//                }
//                //else return;
//                s->acknowledgementNumber += dataSize; // update ack number
//                sendTcpMessage(ether, s, ACK, NULL, 0);
//            }
//
//        }
//
//        //Reset packets from connection error or abort
//        else if ((offsetFields & RST) == RST)
//        {
//            setTcpState(0, TCP_CLOSED);
//        }
//    }
//}
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
    // 1. SYN received
    // -------------------------
    if (syn && !ack)
    {
        putsUart0("TCP SYN received\r\n");

        s = newSocket();
        if (s == NULL)
        {
            putsUart0("No free socket\r\n");
            return;
        }

        getSocketInfoFromTcpPacket(ether, s);

        s->sequenceNumber = 1;
        s->acknowledgementNumber = ntohl(tcp->sequenceNumber) + 1;
        s->state = TCP_SYN_RECEIVED;

        sendTcpResponse(ether, s, SYN | ACK);
        s->sequenceNumber++;

        return;
    }

    // -------------------------
    // 2. Final ACK (handshake complete)
    // -------------------------
    if (ack && !syn)
    {
        for (i = 0; i < MAX_SOCKETS; i++)
        {
            if (sockets[i].state == TCP_SYN_RECEIVED &&
                sockets[i].localPort == dstPort &&
                sockets[i].remotePort == srcPort)
            {
                sockets[i].state = TCP_ESTABLISHED;

                putsUart0("TCP connection established\r\n");

                // IMPORTANT: use THIS socket
                s = &sockets[i];

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
                putsUart0("MQTT CONNACK received\r\n");
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

    //swap destination and source MAC addresses so packet goes back to sender
    uint8_t tempMac[6];
    memcpy(tempMac, ether->destAddress, 6);
    memcpy(ether->destAddress, ether->sourceAddress, 6);
    memcpy(ether->sourceAddress, tempMac, 6);
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
