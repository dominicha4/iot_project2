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
socket sockets[MAX_SOCKETS];

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
bool isTcp(etherHeader* ether)
{
    // first make sure it's IP
    if (!isIp(ether))
    return false;

    ipHeader *ip = (ipHeader*)ether->data;
    return (ip->protocol == PROTOCOL_TCP);          //if IP protocol field is 6 then it is TCP
}

//locate the TCP header, read the flags and see if SYN is set
bool isTcpSyn(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;            //check if SYN flag is se

    uint8_t ipHeaderLength = ip->size * 4;          //figure out where TCP header begins
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);       //points to TCP header

    uint16_t flags = ntohs(tcp->offsetFields) & 0x01FF;             //extract only the TCP flag bits

    return ((flags & SYN) != 0);             //check if SYN flag is set in tcp flag field
}

bool isTcpAck(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;

    uint8_t ipHeaderLength = ip->size * 4;          //figure out where TCP header begins
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);       //points to TCP header

    uint16_t flags = ntohs(tcp->offsetFields) & 0x01FF;             //extract only the TCP flag bits

    return ((flags & ACK) != 0);                //check if ACK flag is set in tcp flag field
}

void sendTcpPendingMessages(etherHeader *ether)
{
}

void processTcpResponse(etherHeader *ether)
{
    socket *s = NULL;             //make a socket pointer and start it as null in case no socket is found
    ipHeader *ip;               //pointer that will point to UP header inside the Ethernet frame
    tcpHeader *tcp;             //pointer that will point to the TCP header after the IP header
    uint8_t ipHeaderLength;     //variable for the actual size of Ip header in bytes
    uint8_t i;                  //loop variable for searching through socket list

    if (!isTcp(ether))
        return;                 //check if packet is TCP packet

    if (!isTcpPortOpen(ether))      //if TCP packet is not going to open port, then ignore it
        return;

    ip = (ipHeader*)ether->data;           //points to IP header, which starts at the Ethernet data field

    ipHeaderLength = ip->size * 4;          //IP header legnth is stored in 32 bit words, so multiply by 4 to get bytes
    tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);      //move past the IP header so tcp points to the tcp header

    //checks for the frist step of TCP, client sends SYN and is trying to start a new connection
    if (isTcpSyn(ether) && !isTcpAck(ether))
    {
        s = newSocket();            //use an unused socket so we can track new TCP connection
        if (s == NULL)              //if no free socket available, leave becuase we have nowhere to store the new connection
            return;

        //function socket.c
        getSocketInfoFromTcpPacket(ether, s);       //copy client IP, MAC, source port and destination port into socket
        s->sequenceNumber = 1;                      //choose a starting sequenct number for our side of TCP connection
        s->acknowledgementNumber = ntohl(tcp->sequenceNumber) + 1;       //client sent us a SYN with its own sequence number
        s->state = TCP_SYN_RECEIVED;            //we are now in SYN_RECEIVED state becasue we got SYN and will reply with SYN+ACK
        sendTcpResponse(ether, s, SYN | ACK); // send SYN and ACK back to client as step 2 of handshake
    }
    //this checks for the last step of TCP handshake, client sends ack after receiving our SYN+ACK
    else if (isTcpAck(ether) && !isTcpSyn(ether))
    {
        for (i = 0; i < MAX_SOCKETS; i++)
        {
            if (sockets[i].state == TCP_SYN_RECEIVED)  //only chekcs sockets that are waiting for the final ACK
            {
                if ((sockets[i].localPort == ntohs(tcp->destPort)) && (sockets[i].remotePort == ntohs(tcp->sourcePort)))
                {
                    sockets[i].state = TCP_ESTABLISHED;   //handshake is done, so mark this socket as connected
                    break;              //stop searching becuase we found the right socket
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
    ipHeader *ip = (ipHeader*)ether->data;

    // skip over the IP header to get to the TCP header
    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);

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

void sendTcpResponse(etherHeader *ether, socket* s, uint16_t flags)
{
}

// Send TCP message
void sendTcpMessage(etherHeader *ether, socket *s, uint16_t flags, uint8_t data[], uint16_t dataSize)
{
}
