/*
    SDL_net:  An example cross-platform network library for use with SDL
    Copyright (C) 1997, 1998, 1999, 2000, 2001  Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    slouken@libsdl.org
*/

/* $Id: SDLnet.c,v 1.9 2003/03/07 06:44:47 slouken Exp $ */

#include <string.h>

#include "SDL_byteorder.h"

#include "SDLnetsys.h"
#include "SDL_net.h"


/* Since the UNIX/Win32/BeOS code is so different from MacOS,
   we'll just have two completely different sections here.
*/
static int SDLNet_started = 0;

#ifdef MACOS_OPENTRANSPORT

#include <Events.h>

typedef struct
{
	Uint8	stat;
	InetSvcRef dns;
}DNSStatus, *DNSStatusRef;

enum
{
	dnsNotReady = 0,
	dnsReady = 1,
	dnsResolved = 2,
	dnsError = 255
};

//static InetSvcRef dns = 0;
static DNSStatus dnsStatus;
Uint32 OTlocalhost = 0;

/* We need a notifier for opening DNS.*/
/* ( 010311 masahiro minami<elsur@aaa.letter.co.jp>) */
static pascal void OpenDNSNotifier(
	void* context, OTEventCode code, OTResult result, void* cookie )
{
	switch( code )
	{
		case T_OPENCOMPLETE:
			// DNS is ready now.
			if( result == kOTNoError )
			{
				dnsStatus.dns = (InetSvcRef)cookie;
				dnsStatus.stat = dnsReady;
			}
			else
			{
				SDLNet_SetError("T_DNRSTRINGTOADDRCOMPLETE event returned an error");
				dnsStatus.dns = NULL;
				dnsStatus.stat = dnsError;
			}
			break;
		case T_DNRSTRINGTOADDRCOMPLETE:
			// DNR resolved the name to address
			// WORK IN PROGRESS (TODO )
			dnsStatus.stat = dnsResolved;
			break;
		default:
			if( result != kOTNoError )
				dnsStatus.stat = dnsError;
	}
	// Is there anything else to be done here ???
	// ( 010311 masahiro minami<elsur@aaa.letter.co.jp> )
	// (TODO)
}

/* Local functions for initializing and cleaning up the DNS resolver */
static int OpenDNS(void)
{
	int retval;
	OSStatus status;

	retval = 0;
	status = OTAsyncOpenInternetServices(
		kDefaultInternetServicesPath, 0, OpenDNSNotifier, NULL);
	if ( status == noErr ) {
		InetInterfaceInfo	info;
		
		dnsStatus.stat = dnsNotReady;
		
		while( dnsStatus.stat != dnsError && dnsStatus.dns == NULL)
		{
			// what's to be done ? Yield ? WaitNextEvent ? or what ?
			// ( 010311 masahiro minami<elsur@aaa.letter.co.jp> )
			//YieldToAnyThread();
		}
		/* Get the address of the local system -
		   What should it be if ethernet is off?
		 */
		OTInetGetInterfaceInfo(&info, kDefaultInetInterface);
		OTlocalhost = info.fAddress;
	} else {
		SDLNet_SetError("Unable to open DNS handle");
		retval = status;
	}
	
	return(retval);
}

static void CloseDNS(void)
{
	if ( dnsStatus.dns ) {
		OTCloseProvider(dnsStatus.dns);
		dnsStatus.dns = 0;
		dnsStatus.stat = dnsNotReady;
	}
	
	OTlocalhost = 0;
}

/* Initialize/Cleanup the network API */
int  SDLNet_Init(void)
{
	OSStatus status;
	int retval;

	dnsStatus.stat = dnsNotReady;
	dnsStatus.dns = 0;


	retval = 0;
	if ( ! SDLNet_started ) {
		status = InitOpenTransport();
		if ( status == noErr ) {
			retval = OpenDNS();
			if ( retval < 0 ) {
				SDLNet_Quit();
			}
		} else {
			SDLNet_SetError("Unable to initialize Open Transport");
			retval = status;
		}
	}
	if ( retval == 0 ) {
		++SDLNet_started;
	}
	return(retval);
}

void SDLNet_Quit(void)
{
	if ( SDLNet_started == 0 ) {
		return;
	}
	if ( SDLNet_started-- == 0 ) {
		CloseDNS();
		CloseOpenTransport();
	}
}

/* Resolve a host name and port to an IP address in network form */
int SDLNet_ResolveHost(IPaddress *address, const char *host, Uint16 port)
{
	int retval = 0;

	/* Perform the actual host resolution */
	if ( host == NULL ) {
		address->host = INADDR_ANY;
	} else {
/*		int a[4];

		address->host = INADDR_NONE;
		
		if ( sscanf(host, "%d.%d.%d.%d", a, a+1, a+2, a+3) == 4 ) {
			if ( !(a[0] & 0xFFFFFF00) && !(a[1] & 0xFFFFFF00) &&
			     !(a[2] & 0xFFFFFF00) && !(a[3] & 0xFFFFFF00) ) {
				address->host = ((a[0] << 24) |
				                 (a[1] << 16) |
				                 (a[2] <<  8) | a[3]);
				if ( address->host == 0x7F000001 ) {
					address->host = OTlocalhost;
				}
			}
		}
		
		if ( address->host == INADDR_NONE ) {*/
			InetHostInfo hinfo;
			
			/* Check for special case - localhost */
			if ( strcmp(host, "localhost") == 0 )
				return(SDLNet_ResolveHost(address, "127.0.0.1", port));

			/* Have OpenTransport resolve the hostname for us */
			retval = OTInetStringToAddress(dnsStatus.dns, (char *)host, &hinfo);
			if (retval == noErr) {
				while( dnsStatus.stat != dnsResolved )
					{WaitNextEvent(everyEvent, 0, 1, NULL );}
				address->host = hinfo.addrs[0];
			}
		//}
	}
	
	address->port = SDL_SwapBE16(port);

	/* Return the status */
	return(retval);
}

/* Resolve an ip address to a host name in canonical form.
   If the ip couldn't be resolved, this function returns NULL,
   otherwise a pointer to a static buffer containing the hostname
   is returned.  Note that this function is not thread-safe.
*/
/* MacOS implementation by Roy Wood
 */
const char *SDLNet_ResolveIP(IPaddress *ip)
{
	if (ip != nil)
	{
	InetHost				theIP;
	static InetDomainName	theInetDomainName;
	OSStatus				theOSStatus;
	
		
		/*	Default result will be null string */
		
		theInetDomainName[0] = '\0';	
		
		
		/*	Do a reverse DNS lookup */
		
		theIP = ip->host;
		
		theOSStatus = OTInetAddressToName(dnsStatus.dns,theIP,theInetDomainName);
		
		/*	If successful, return the result */
			
		if (theOSStatus == kOTNoError)
		{
			while( dnsStatus.stat != dnsResolved )
				{ /*should we yield or what ? */ }
			return(theInetDomainName);
		}
	}
	
	SDLNet_SetError("Can't perform reverse DNS lookup");
	
	return(NULL);
}

#else /* !MACOS_OPENTRANSPORT */

/* Initialize/Cleanup the network API */
int  SDLNet_Init(void)
{
	if ( !SDLNet_started ) {
#ifdef __USE_W32_SOCKETS
		WORD version_wanted = MAKEWORD(1,1);
		WSADATA wsaData;

#ifdef _XBOX
		XNetStartupParams xnsp;
		int err;
		memset(&xnsp, 0, sizeof(xnsp));
		xnsp.cfgSizeOfStruct = sizeof(XNetStartupParams);
		xnsp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
		err = XNetStartup(&xnsp);
		if(err) {
			return err;
		}
#endif
		/* Start up the windows networking */

		if ( WSAStartup(version_wanted, &wsaData) != 0 ) {
			SDLNet_SetError("Couldn't initialize Winsock 1.1\n");
			return(-1);
		}
#else
		;
#endif
	}
	++SDLNet_started;
	return(0);
}
void SDLNet_Quit(void)
{
	if ( SDLNet_started == 0 ) {
		return;
	}
	if ( SDLNet_started-- == 0 ) {
#ifdef __USE_W32_SOCKETS
		/* Clean up windows networking */
		if ( WSACleanup() == SOCKET_ERROR ) {
			if ( WSAGetLastError() == WSAEINPROGRESS ) {
				WSACleanup();
			}
		}
#else
		;
#endif
	}
}

XNDNS* XResolveHost(const char *host) {
	// hostname not found in cache.
	// do a DNS lookup
	WSAEVENT hEvent = WSACreateEvent();
	XNDNS* pDns		  = NULL;
	INT err = XNetDnsLookup(host, hEvent, &pDns);
	WaitForSingleObject( (HANDLE)hEvent, INFINITE);
	if( pDns && pDns->iStatus == 0 )
	{
		//DNS lookup succeeded
//		strIpAdres.Format("%d.%d.%d.%d",(ulHostIp & 0xFF),(ulHostIp & 0xFF00) >> 8,(ulHostIp & 0xFF0000) >> 16,(ulHostIp & 0xFF000000) >> 24 );
//		XNetDnsRelease(pDns);
		WSACloseEvent(hEvent);
		return pDns;
	}
	if (pDns)
	{
//		CLog::Log(LOGERROR, "DNS lookup for %s failed: %u", strHostName.c_str(), pDns->iStatus);
		XNetDnsRelease(pDns);
	}
//	else
//		CLog::Log(LOGERROR, "DNS lookup for %s failed: %u", strHostName.c_str(), err);
	WSACloseEvent(hEvent);
	return 0;
}
/* Resolve a host name and port to an IP address in network form */
int SDLNet_ResolveHost(IPaddress *address, const char *host, Uint16 port)
{
	int retval = 0;

	/* Perform the actual host resolution */
	if ( host == NULL ) {
		address->host = INADDR_ANY;
	} else {
		address->host = inet_addr(host);
		if ( address->host == INADDR_NONE ) {
			XNDNS* pDns = NULL;
			pDns=XResolveHost(host);
			if(pDns) {
				memcpy(&address->host,&pDns->aina[0],4);
				XNetDnsRelease(pDns);
			}
			else {
				retval = -1;
			}
		}
	}
	address->port = SDL_SwapBE16(port);

	/* Return the status */
	return(retval);
}

/* Resolve an ip address to a host name in canonical form.
   If the ip couldn't be resolved, this function returns NULL,
   otherwise a pointer to a static buffer containing the hostname
   is returned.  Note that this function is not thread-safe.
*/
/* Written by Miguel Angel Blanch.
 * Main Programmer of Arianne RPG.
 * http://come.to/arianne_rpg
 */
const char *SDLNet_ResolveIP(IPaddress *ip)
{
//#error missing gethostbyaddr
//	struct hostent *hp;

//	hp = gethostbyaddr((char *)&ip->host, 4, AF_INET);
//	if ( hp != NULL ) {
//		return hp->h_name;
//	}
//			XNDNS* pDns = NULL;
//			pDns=XResolveHost(host);
//			pDns->
	SDLNet_SetError("Can't perform reverse DNS lookup");
  	return NULL;
}

#endif /* MACOS_OPENTRANSPORT */

#if !SDL_DATA_ALIGNED /* function versions for binary compatibility */

/* Write a 16 bit value to network packet buffer */
#undef SDLNet_Write16
void   SDLNet_Write16(Uint16 value, void *areap)
{
	(*(Uint16 *)(areap) = SDL_SwapBE16(value));
}

/* Write a 32 bit value to network packet buffer */
#undef SDLNet_Write32
void   SDLNet_Write32(Uint32 value, void *areap)
{
	*(Uint32 *)(areap) = SDL_SwapBE32(value);
}

/* Read a 16 bit value from network packet buffer */
#undef SDLNet_Read16
Uint16 SDLNet_Read16(void *areap)
{
	return (SDL_SwapBE16(*(Uint16 *)(areap)));
}

/* Read a 32 bit value from network packet buffer */
#undef SDLNet_Read32
Uint32 SDLNet_Read32(void *areap)
{
	return (SDL_SwapBE32(*(Uint32 *)(areap)));
}

#endif /* !SDL_DATA_ALIGNED */


#ifdef USE_GUSI_SOCKETS

/* Configure Socket Factories */

void GUSISetupFactories()
{
	GUSIwithInetSockets();
}

/* Configure File Devices */

void GUSISetupDevices()
{
	return;
}

#endif /* USE_GUSI_SOCKETS */
