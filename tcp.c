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

#define MAX_TCP_PORTS 4

uint16_t tcpPorts[MAX_TCP_PORTS];
uint8_t tcpPortCount = 0;
uint8_t tcpState[MAX_TCP_PORTS];

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
bool isTcp(etherHeader* ether)
{
    // first make sure it's IP
    if (!isIp(ether))
    return false;

    ipHeader *ip = (ipHeader*)ether->data;
    return (ip->protocol == PROTOCOL_TCP);          //if IP protocol field is 6 then it is TCP
}

bool isTcpSyn(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;            //check if SYN flag is se

    uint8_t ipHeaderLength = ip->size * 4;          //IP is not always 20 bytes so we use size field to find where TCP starts

    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);       //move oast IP header to get to TCP header

    uint16_t flags = ntohs(tcp->offsetFields) * 0x01FF;             //offsetFields contrains both TCP data offset and flags, lower bits are flags

    return ((flags & SYN) != 0);             //check if SYN flag is set in tcp flag field
}

bool isTcpAck(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;

    uint8_t ipHeaderLength = ip->size * 4;          //IP is not always 20 bytes so we use size field to find where TCP starts

    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);       //move oast IP header to get to TCP header

    uint16_t flags = ntohs(tcp->offsetFields) * 0x01FF;             //offsetFields contrains both TCP data offset and flags, lower bits are flags

    return ((flags & ACK) != 0);                //check if ACK flag is set in tcp flag field
}

void sendTcpPendingMessages(etherHeader *ether)
{
}

void processTcpResponse(etherHeader *ether)
{
}

void processTcpArpResponse(etherHeader *ether)
{
}

// stores the list of TCP ports that this board should treat as open/listening
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
