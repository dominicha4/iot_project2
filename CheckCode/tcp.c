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
    socket *s = NULL; //make a socket pointer and start it as null in case no socket is found
    ipHeader *ip; //pointer that will point to UP header inside the Ethernet frame
    tcpHeader *tcp; //pointer that will point to the TCP header after the IP header
    uint8_t ipHeaderLength; //variable for the actual size of Ip header in bytes
    uint8_t i;                 //loop variable for searching through socket list

    putsUart0("Entered processTcpResponse\r\n");

    if (!isTcp(ether))
    {
        putsUart0("Not TCP\n");
        return;                 //check if packet is TCP packet
    }

    if (!isTcpPortOpen(ether)) //if TCP packet is not going to open port, then ignore it
    {
        putsUart0("TCP port not open\n");
        return;
    }

    ip = (ipHeader*) ether->data; //points to IP header, which starts at the Ethernet data field
    ipHeaderLength = ip->size * 4; //IP header legnth is stored in 32 bit words, so multiply by 4 to get bytes
    tcp = (tcpHeader*) ((uint8_t*) ip + ipHeaderLength); //move past the IP header so tcp points to the tcp header

    // Extracts source and destination TCP ports from incoming packets
    uint16_t srcPort = ntohs(tcp->sourcePort);
    uint16_t dstPort = ntohs(tcp->destPort);

    for (i = 0; i < MAX_SOCKETS; i++)   // Iterate and finds the matching TCP socket
    {
        if (sockets[i].state != TCP_CLOSED) // Skips invalid sockets
        {
            // Checks for matching source and destination port
            if (sockets[i].localPort == dstPort &&
                sockets[i].remotePort == srcPort)
            {
                s = &sockets[i];
                break;
            }
        }
    }
    //checks for the first step of TCP, client sends SYN and is trying to start a new connection
    if (isTcpSyn(ether) && !isTcpAck(ether))
    {
        putsUart0("TCP SYN received\r\n");

        s = newSocket(); //use an unused socket so we can track new TCP connection
        if (s == NULL) //if no free socket available, leave becuase we have nowhere to store the new connection
        {
            putsUart0("No free socket\r\n");
            return;
        }

        putsUart0("Socket allocated\r\n");

        //function socket.c
        getSocketInfoFromTcpPacket(ether, s); //copy client IP, MAC, source port and destination port into socket
        putsUart0("Socket info copied\r\n");

        s->sequenceNumber = 1; //choose a starting sequenct number for our side of TCP connection
        s->acknowledgementNumber = ntohl(tcp->sequenceNumber) + 1; //client sent us a SYN with its own sequence number
        s->state = TCP_SYN_RECEIVED; //we are now in SYN_RECEIVED state becasue we got SYN and will reply with SYN+ACK
        putsUart0("Sending SYN+ACK\r\n");
        sendTcpResponse(ether, s, SYN | ACK); // send SYN and ACK back to client as step 2 of handshake

        s->sequenceNumber++;    //SYN consumes one sequence number
    }
    //this checks for the last step of TCP handshake, client sends ack after receiving our SYN+ACK
    else if (isTcpAck(ether) && !isTcpSyn(ether))
    {
        putsUart0("Final ACK received\r\n");

        for (i = 0; i < MAX_SOCKETS; i++)
        {
            if (sockets[i].state == TCP_SYN_RECEIVED) //only chekcs sockets that are waiting for the final ACK
            {
                if ((sockets[i].localPort == ntohs(tcp->destPort))
                        && (sockets[i].remotePort == ntohs(tcp->sourcePort)))
                {
                    sockets[i].state = TCP_ESTABLISHED; //handshake is done, so mark this socket as connected
                    putsUart0("TCP connection established\r\n");
                    break;    //stop searching becuase we found the right socket
                }
            }
        }
    }

    // If TCP connect has been established, check socket for MQTT CONNACK
    if(s != NULL && s->state == TCP_ESTABLISHED)
    {
        if(tcp->sourcePort == htons(s->remotePort))
        {
            uint8_t *payload = tcp->data;

            if(payload[0] == 0x20) // If MQTT CONNACK has been received
            {
                putsUart0("MQTT CONNACK received\r\n");

                if(payload[3] == 0x00) // Check for success
                {
                    putsUart0("MQTT SUCCESS\r\n");
                    mqttConnected = true; // optional safety lock
                }
                else    // Otherwise, failed
                {
                    putsUart0("MQTT FAILED\r\n");
                }
            }
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

socket* tcpConnect(uint8_t ip[], uint16_t port)
{
    socket *s = newSocket();    // Allocates a free socket
    if (s == NULL)  // If no sockets are available or fails (safety check)
    {
        putsUart0("No free socket for TCP connect\r\n");
        return NULL;
    }

    putsUart0("Starting TCP connection...\r\n");

    // Copies broker ip address into socket
    memcpy(s->remoteIpAddress, ip, 4);
    // Set destination port to 1883 for mqtt
    s->remotePort = port;
    // Chooses a random high number
    s->localPort = 50000 + (rand() % 1000);   // random high port

    s->sequenceNumber = 1;      // Initial sequence
    s->acknowledgementNumber = 0;
    s->state = TCP_SYN_SENT;

    uint8_t buffer[1518] = {};
    etherHeader *data = (etherHeader*) buffer;

    // Send TCPSYN to initiate 3 way handshake
    sendTcpMessage(data, s, SYN, NULL, 0);

    putsUart0("SYN sent\r\n");

    return s;
}
