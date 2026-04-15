// DHCP Library
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

/*----------DOMINIC HA 1001781759----------*/


#include <stdio.h>
#include "dhcp.h"
#include "arp.h"
#include "timer.h"
#include "uart0.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
//
// #define DHCPDISCOVER 1
// #define DHCPOFFER    2
// #define DHCPREQUEST  3
// #define DHCPDECLINE  4
// #define DHCPACK      5
// #define DHCPNAK      6
// #define DHCPRELEASE  7
// #define DHCPINFORM   8
//
// #define DHCP_DISABLED   0
// #define DHCP_INIT       1
// #define DHCP_SELECTING  2
// #define DHCP_REQUESTING 3
// #define DHCP_TESTING_IP 4
// #define DHCP_BOUND      5
// #define DHCP_RENEWING   6
// #define DHCP_REBINDING  7
// #define DHCP_INITREBOOT 8 // not used since ip not stored over reboot
// #define DHCP_REBOOTING  9 // not used since ip not stored over reboot
//
#define DHCP_OPTION_FRAME 64

uint32_t xid = 0;

bool discoverNeeded = false;
bool requestNeeded = false;
bool releaseNeeded = false;
bool arpRequestSent = false;
bool ipConflictDetected = false;
bool ipConflictDetectionMode = false;

uint32_t leaseSeconds = 0;
uint32_t leaseT1 = 0;
bool renewRequestSent = false;

uint32_t leaseT2 = 0;

uint8_t dhcpOfferedIpAdd[4];
uint8_t dhcpServerIpAdd[4];
uint8_t dhcpServerHwAddress[6];

uint8_t dhcpState = DHCP_DISABLED;
bool dhcpEnabled = false;

static socket dhcpSocket;

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// State functions

void setDhcpState(uint8_t state)
{
    dhcpState = state;
}

uint8_t getDhcpState()
{
    return dhcpState;
}
// New address functions
// Manually requested at start-up
// Discover messages sent every 15 seconds
void callbackDhcpGetNewAddressTimer()
{
    if (dhcpState == DHCP_SELECTING)
    {
        discoverNeeded = true;          //true so that another discovery message should be sent
        putsUart0("Retry DHCP DISCOVER\r\n");
    }
}

void requestDhcpNewAddress()
{
}

// Renew functions
void renewDhcp()
{
    if (!isDhcpEnabled())
    {
        putsUart0("DHCP is not enabled\r\n");
        return;
    }

    putsUart0("Manual renew requested - restarting DHCP\r\n");

    uint8_t zeroIp[4] = {0, 0, 0, 0};   //clear current IP
    setIpAddress(zeroIp);               //set ip to 0

    //reset all flags
    discoverNeeded = false;
    renewRequestSent = false;
    releaseNeeded = false;
    arpRequestSent = false;
    ipConflictDetected = false;
    ipConflictDetectionMode = false;

    //reset lease timers
    leaseSeconds = 0;
    leaseT1 = 0;
    leaseT2 = 0;

    //new xid for new transactions
    xid = random32();

    dhcpState = DHCP_INIT;    //after renew we go back to init
}

void callbackDhcpT1PeriodicTimer()
{
}

void callbackDhcpT1HitTimer()
{
    if (dhcpState == DHCP_BOUND)
    {
        putsUart0("T1 reach - moving to RENEW or ignore for T2 testing\r\n");
        dhcpState = DHCP_RENEWING;
        renewRequestSent = false;               //set to false to send DHCPREQEUST renew
        putsUart0("Renew request sent\n");
    }
}

// Rebind functions
void rebindDhcp()
{
}

void callbackDhcpT2PeriodicTimer()
{
}

void callbackDhcpT2HitTimer()
{
    if (dhcpState == DHCP_BOUND || dhcpState == DHCP_RENEWING)
    {
        putsUart0("T2 reach - moving to REBIND\r\n");
        dhcpState = DHCP_REBINDING;
        renewRequestSent = false;        //set to false to send DHCPREQUEST for rebind
    }
}

// End of lease timer
void callbackDhcpLeaseEndTimer()
{
    if (dhcpState != DHCP_BOUND && dhcpState != DHCP_RENEWING
            && dhcpState != DHCP_REBINDING)
        return;  // ignore this timer if state already changed

    putsUart0("lease end restart dhcp\r\n");

    uint8_t zeroIp[4] = { 0, 0, 0, 0 };   //release current UP because it is lease expired
    setIpAddress(zeroIp);

    //restart back to beginning
    dhcpState = DHCP_INIT;
    renewRequestSent = false;
    arpRequestSent = false;
    ipConflictDetected = false;
    ipConflictDetectionMode = false;

    xid = random32();
}

// Release functions
void releaseDhcp()
{
    if (!isDhcpEnabled())
    {
        putsUart0("DHCP is not enabled\r\n");
        return;
    }

    //only release if we have a lease
    if (dhcpState == DHCP_BOUND || dhcpState == DHCP_RENEWING || dhcpState == DHCP_REBINDING)
    {
        putsUart0("Manual release requested\r\n");
        releaseNeeded = true;   //set true for if statement in sendDhcpPendingMessages
    }
    else
    {
        putsUart0("Release ignored - no active lease\r\n");
    }
}
// IP conflict detection (after getting an ACK, we ARP check to make sure no one is using offered IP
void callbackDhcpIpConflictWindow()
{
    ipConflictDetectionMode = false;

    if (!ipConflictDetected)   //if noone answered ARP with that IP then it is safe
    {
        putsUart0("No conflict detected\r\n");     //if there is no conflict then move to bound
        setIpAddress(dhcpOfferedIpAdd);
        dhcpState = DHCP_BOUND;
    }
    else
    {
        putsUart0("Conflict detected, restarting DHCP\r\n");   //if there is then go back to init
        dhcpState = DHCP_INIT;
    }

    //reset flags
    ipConflictDetected = false;
    arpRequestSent = false;
}

//send ARP request to check if IP was taken
void requestDhcpIpConflictTest(etherHeader *ether)
{
    uint8_t zeroIp[4] = { 0, 0, 0, 0 };

    sendArpRequest(ether, zeroIp, dhcpOfferedIpAdd);

    arpRequestSent = true;
    ipConflictDetectionMode = true;

    startOneshotTimer(callbackDhcpIpConflictWindow, 2);   //wait 2 seconds for arp to reply
}

bool isDhcpIpConflictDetectionMode()
{
    return ipConflictDetectionMode;
}

// Lease functions
uint32_t getDhcpLeaseSeconds()
{
    return leaseSeconds;
}

// Determines whether packet is DHCP
// Must be a UDP packet
bool isDhcpResponse(etherHeader *ether)
{
    if (!isUdp(ether))
        return false;

    uint8_t *udpData = getUdpData(ether);
    udpHeader *udp = (udpHeader*) (udpData - sizeof(udpHeader));

    //DHCP client listens on port 68!!!!
    if (ntohs(udp->destPort) != 68)
        return false;

    dhcpFrame *dhcp = (dhcpFrame*) getUdpData(ether);

    if (ntohl(dhcp->magicCookie) != 0x63825363)
        return false;

    return true;
}

// Send DHCP message
void sendDhcpMessage(etherHeader *ether, uint8_t type)
{
    //create buffer for header and options
    uint8_t buffer[sizeof(dhcpFrame) + DHCP_OPTION_FRAME];
    memset(buffer, 0, sizeof(buffer));
    dhcpFrame *dhcp = (dhcpFrame*) buffer;

    uint8_t myMac[6];
    getEtherMacAddress(myMac);

    //dhcp field
    dhcp->op = 1;
    dhcp->htype = 1;
    dhcp->hlen = 6;
    dhcp->hops = 0;
    dhcp->xid = htonl(xid);
    dhcp->secs = 0;
    dhcp->magicCookie = htonl(0x63825363);

    //put mac address into chaddr field
    memcpy(dhcp->chaddr, myMac, 6);

    //start writing DHCP option
    uint8_t *option = dhcp->options;

    //Message type
    *option++ = 53;
    *option++ = 1;
    *option++ = type;


    if (type == DHCPREQUEST && dhcpState == DHCP_SELECTING)
    {
        dhcp->flags = htons(0x8000);    //broadcast flag

        //request IP address
        *option++ = 50;
        *option++ = 4;
        memcpy(option, dhcpOfferedIpAdd, 4);
        option += 4;

        //dhcp server identifier
        *option++ = 54;
        *option++ = 4;
        memcpy(option, dhcpServerIpAdd, 4);
        option += 4;
    }
    else if (type == DHCPREQUEST && dhcpState == DHCP_RENEWING)
    {
        dhcp->flags = 0;

        uint8_t currentIp[4];
        getIpAddress(currentIp);

        //ciaddr = my current ip
        memcpy(dhcp->ciaddr, currentIp, 4);
    }
    else if (type == DHCPREQUEST && dhcpState == DHCP_REBINDING)
    {
        dhcp->flags = htons(0x8000);  //broadcast in rebinding because original server mightbe done

        uint8_t currentIp[4];
        getIpAddress(currentIp);
        memcpy(dhcp->ciaddr, currentIp, 4);
    }
    else if (type == DHCPRELEASE)
    {
        dhcp->flags = 0;

        uint8_t currentIp[4];
        getIpAddress(currentIp);
        memcpy(dhcp->ciaddr, currentIp, 4);

        //server identifer included in release
        *option++ = 54;
        *option++ = 4;
        memcpy(option, dhcpServerIpAdd, 4);
        option += 4;
    }
    else
    {
        dhcp->flags = htons(0x8000);
    }

    //hostname
    char hostname[] = "Dom";
    *option++ = 12;
    *option++ = 3;
    memcpy(option, hostname, 3);
    option += 3;

    //client identifier
    *option++ = 61;
    *option++ = 7;
    *option++ = 1;
    memcpy(option, myMac, 6);
    option += 6;

    //parameter request list
    *option++ = 55;
    *option++ = 5;
    *option++ = 1;
    *option++ = 3;
    *option++ = 6;
    *option++ = 51;
    *option++ = 54;

    //end option
    *option++ = 255;

    //total size of dhcp message
    uint16_t size = (uint8_t*) option - buffer;

    dhcpSocket.localPort = 68;
    dhcpSocket.remotePort = 67;

    if (type == DHCPDISCOVER)
    {
        memset(dhcpSocket.remoteIpAddress, 255, 4);    //broadcast
        memset(dhcpSocket.remoteHwAddress, 255, 6);     //broadcast mac
    }
    else if (type == DHCPREQUEST && dhcpState == DHCP_SELECTING)
    {
        memset(dhcpSocket.remoteIpAddress, 255, 4);
        memset(dhcpSocket.remoteHwAddress, 255, 6);
    }
    else if (type == DHCPREQUEST && dhcpState == DHCP_RENEWING)
    {
        //unicast to original dhcp server
        memcpy(dhcpSocket.remoteIpAddress, dhcpServerIpAdd, 4);
        memcpy(dhcpSocket.remoteHwAddress, dhcpServerHwAddress, 6);
    }
    else if (type == DHCPREQUEST && dhcpState == DHCP_REBINDING)
    {
        memset(dhcpSocket.remoteIpAddress, 255, 4);    //broadcast
        memset(dhcpSocket.remoteHwAddress, 255, 6);
    }
    else if (type == DHCPRELEASE)
    {
        memcpy(dhcpSocket.remoteIpAddress, dhcpServerIpAdd, 4);
        memcpy(dhcpSocket.remoteHwAddress, dhcpServerHwAddress, 6);
    }

    // Print source and destination info
    char str[80];
    snprintf(str, sizeof(str), "SRC port: %d -> DST IP: %d.%d.%d.%d port: %d\r\n", dhcpSocket.localPort, dhcpSocket.remoteIpAddress[0], dhcpSocket.remoteIpAddress[1], dhcpSocket.remoteIpAddress[2],dhcpSocket.remoteIpAddress[3], dhcpSocket.remotePort);
    putsUart0(str);

    //send udp packet
    sendUdpMessage(ether, dhcpSocket, buffer, size);
}

uint8_t* getDhcpOption(etherHeader *ether, uint8_t option, uint8_t *length)
{
    dhcpFrame *dhcp = (dhcpFrame*) getUdpData(ether);
    uint8_t *opt = dhcp->options;

    while (*opt != 255)
    {
        uint8_t code = *opt++;
        uint8_t len = *opt++;

        if (code == option)
        {
            *length = len;
            return opt;
        }

        opt += len;
    }

    return 0;
}

// Determines whether packet is DHCP offer response to DHCP discover
// Must be a UDP packet
bool isDhcpOffer(etherHeader *ether, uint8_t ipOfferedAdd[])
{
    if (!isDhcpResponse(ether))
        return false;

    dhcpFrame *dhcp = (dhcpFrame*) getUdpData(ether);

    if (ntohl(dhcp->xid) != xid)
        return false;

    uint8_t len;
    uint8_t *msgType = getDhcpOption(ether, 53, &len);

    if (!msgType)
        return false;

    if (*msgType != DHCPOFFER)
        return false;

    //save offer IP
    memcpy(ipOfferedAdd, dhcp->yiaddr, 4);

    //save DHCP server IP
    uint8_t *server = getDhcpOption(ether, 54, &len);
    if (server && len == 4)
        memcpy(dhcpServerIpAdd, server, 4);

    //save server MAC from ethernet header
    memcpy(dhcpServerHwAddress, ether->sourceAddress, 6);

    return true;
}
// Determines whether packet is DHCP ACK response to DHCP request
// Must be a UDP packet
bool isDhcpAck(etherHeader *ether)
{
    if (!isDhcpResponse(ether))
        return false;

    dhcpFrame *dhcp = (dhcpFrame*) getUdpData(ether);

    if (ntohl(dhcp->xid) != xid)
        return false;

    uint8_t len;
    uint8_t *msgType = getDhcpOption(ether, 53, &len);

    if (msgType == 0)
        return false;

    if (*msgType != DHCPACK)
        return false;

    return true;
}

// Handle a DHCP ACK, saves lease info when server approves our request
void handleDhcpAck(etherHeader *ether)
{
    dhcpFrame *dhcp = (dhcpFrame*) getUdpData(ether);

    //save assigned IP
    memcpy(dhcpOfferedIpAdd, dhcp->yiaddr, 4);

    uint8_t len;
    uint8_t *server = getDhcpOption(ether, 54, &len);
    if (server && len == 4)
        memcpy(dhcpServerIpAdd, server, 4);

    memcpy(dhcpServerHwAddress, ether->sourceAddress, 6);

    //lease time
    uint8_t *leaseOption = getDhcpOption(ether, 51, &len);
    if (leaseOption && len == 4)
        leaseSeconds = ntohl(*(uint32_t*) leaseOption);
    else
        leaseSeconds = 60;

    //used this as test value
    leaseSeconds = 60;                  // test value
    leaseT1 = leaseSeconds / 2;         // 30 sec
    leaseT2 = (leaseSeconds * 7) / 8;   // 52 sec

    //starting timers
    startOneshotTimer(callbackDhcpT1HitTimer, leaseT1);
    startOneshotTimer(callbackDhcpT2HitTimer, leaseT2);
    startOneshotTimer(callbackDhcpLeaseEndTimer, leaseSeconds);

    putsUart0("ACK accepted\r\n");
}

// Message requests
bool isDhcpDiscoverNeeded()
{
    return false;
}

bool isDhcpRequestNeeded()
{
    return false;
}

bool isDhcpReleaseNeeded()
{
    return false;
}

void sendDhcpPendingMessages(etherHeader *ether)
{
    if (releaseNeeded)
    {
        putsUart0("Sending DHCP RELEASE\r\n");
        sendDhcpMessage(ether, DHCPRELEASE);

        //reset everything after release, 0 out ip
        releaseNeeded = false;
        renewRequestSent = false;
        arpRequestSent = false;
        ipConflictDetected = false;
        ipConflictDetectionMode = false;

        leaseSeconds = 0;
        leaseT1 = 0;
        leaseT2 = 0;

        uint8_t zeroIp[4] = { 0, 0, 0, 0 };
        setIpAddress(zeroIp);

        dhcpState = DHCP_DISABLED;
        xid = random32();
    }
    else if (dhcpState == DHCP_INIT)
    {
        //first step : send discover
        putsUart0("Sending DISCOVER\r\n");
        sendDhcpMessage(ether, DHCPDISCOVER);
        dhcpState = DHCP_SELECTING;
        discoverNeeded = false;
        startOneshotTimer(callbackDhcpGetNewAddressTimer, 15);
    }
    //if no offer from above send another one after 15 seconds
    else if (dhcpState == DHCP_SELECTING && discoverNeeded)
    {
        putsUart0("Sending DHCP DISCOVER retry\r\n");
        sendDhcpMessage(ether, DHCPDISCOVER);
        discoverNeeded = false;
        startOneshotTimer(callbackDhcpGetNewAddressTimer, 15);
    }
    //once ACK test is IP is in use -> go to conflict test
    else if (dhcpState == DHCP_TESTING_IP)
    {
        if (!arpRequestSent)
        {
            putsUart0("Sending ARP conflict test\r\n");
            requestDhcpIpConflictTest(ether);
        }
    }
    //renew lease by unicasting to original serve/ cant see at home on wireshark for some reason
    else if (dhcpState == DHCP_RENEWING && !renewRequestSent)
    {
        //to check unicast at home
        char str[60];
        snprintf(str, sizeof(str), "Renew unicast to: %d.%d.%d.%d\r\n",
                 dhcpServerIpAdd[0], dhcpServerIpAdd[1], dhcpServerIpAdd[2],
                 dhcpServerIpAdd[3]);
        putsUart0(str);
        putsUart0("Sending renew request\r\n");
        sendDhcpMessage(ether, DHCPREQUEST);
        renewRequestSent = true;
    }
    //broadcasting request NOT UNICAST
    else if (dhcpState == DHCP_REBINDING && !renewRequestSent)
    {
        putsUart0("Sending REBIND request\r\n");
        sendDhcpMessage(ether, DHCPREQUEST);
        renewRequestSent = true;
    }
}

void processDhcpResponse(etherHeader *ether)
{
    //print state so i see where i am at
    char str[40];
    snprintf(str, sizeof(str), "Current DHCP state: %d\r\n", dhcpState);
    putsUart0(str);

    if (dhcpState == DHCP_SELECTING)
    {
        putsUart0("In SELECTING state\r\n");

        //wait for offer after discover
        if (isDhcpOffer(ether, dhcpOfferedIpAdd))
        {
            discoverNeeded = false;
            putsUart0("OFFER detected\r\n");
            sendDhcpMessage(ether, DHCPREQUEST);
            putsUart0("Sending REQUEST\r\n");
            dhcpState = DHCP_REQUESTING;
        }
    }
    //wait for ACK after reqeust
    else if (dhcpState == DHCP_REQUESTING)
    {
        if (isDhcpAck(ether))
        {
            putsUart0("ACK detected\r\n");

            handleDhcpAck(ether);

            //test for conflict first if yes then go back to init
            dhcpState = DHCP_TESTING_IP;
            arpRequestSent = false;
            ipConflictDetected = false;

            putsUart0("moving to TESTING_IP\r\n");
        }
    }
    //wait for ACK to renew
    else if (dhcpState == DHCP_RENEWING)
    {
        if (isDhcpAck(ether))
        {
            putsUart0("Renew received\r\n");

            //restart timers
            startOneshotTimer(callbackDhcpT1HitTimer, leaseT1);
            startOneshotTimer(callbackDhcpT2HitTimer, leaseT2);
            startOneshotTimer(callbackDhcpLeaseEndTimer, leaseSeconds);
            //handleDhcpAck(ether);

            //if renew work go back bound
            dhcpState = DHCP_BOUND;
            renewRequestSent = false;

            putsUart0("Renew successful\r\n");
        }
    }
    else if (dhcpState == DHCP_REBINDING)
    {
        //wait for ack during rebind
        if (isDhcpAck(ether))
        {
            putsUart0("Rebind received\r\n");

            handleDhcpAck(ether);

            dhcpState = DHCP_BOUND;
            renewRequestSent = false;

            putsUart0("Rebind successful\r\n");
        }
    }
}

//this is to see if someone took our UP
void processDhcpArpResponse(etherHeader *ether)
{
    if (!ipConflictDetectionMode)
        return;

    arpPacket *arp = (arpPacket*) ether->data;

    //check if someone ARP reply
    if (ntohs(arp->op) != 2)
        return;

    //if source IP i ARP reply matches then conflict
    if (memcmp(arp->sourceIp, dhcpOfferedIpAdd, 4) == 0)
    {
        putsUart0("IP CONFLICT DETECTED\r\n");
        ipConflictDetected = true;
    }
}

// DHCP control functions
void enableDhcp()
{
    putsUart0("enableDhcp called\r\n");
    dhcpEnabled = true;
    dhcpState = DHCP_INIT;
    xid = random32();
}

void disableDhcp()
{
    dhcpEnabled = false;
    dhcpState = DHCP_DISABLED;
}

bool isDhcpEnabled()
{
    return dhcpEnabled;
}
