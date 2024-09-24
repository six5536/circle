//
// netconfig.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2015-2016  R. Stange <rsta2@o2online.de>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include <circle/net/netconfig.h>

CNetConfig::CNetConfig (void)
:	m_bUseDHCP (TRUE),
	m_pMulticastGroups (0)
{
	Reset ();
}

CNetConfig::~CNetConfig (void)
{
	// Delete multicast groups
	MulticastGroup *pGroup = m_pMulticastGroups;
	while (pGroup != 0)
	{
		MulticastGroup *pNext = pGroup->pNext;
		delete pGroup->pMACAddress;
		delete pGroup->pIPAddress;
		delete pGroup;
		pGroup = pNext;
	}

}

void CNetConfig::Reset (void)
{
	static const u8 NullAddress[] = {0, 0, 0, 0};

	m_IPAddress.Set (NullAddress);
	m_NetMask.Set (NullAddress);
	m_DefaultGateway.Set (NullAddress);
	m_DNSServer.Set (NullAddress);

	UpdateBroadcastAddress ();
}

void CNetConfig::SetDHCP (boolean bUsed)
{
	m_bUseDHCP = bUsed;
}

void CNetConfig::SetIPAddress (u32 nAddress)
{
	m_IPAddress.Set (nAddress);

	UpdateBroadcastAddress ();
}

void CNetConfig::SetNetMask (u32 nNetMask)
{
	m_NetMask.Set (nNetMask);

	UpdateBroadcastAddress ();
}

void CNetConfig::SetDefaultGateway (u32 nAddress)
{
	m_DefaultGateway.Set (nAddress);
}

void CNetConfig::SetDNSServer (u32 nAddress)
{
	m_DNSServer.Set (nAddress);
}

void CNetConfig::SetIPAddress (const u8 *pAddress)
{
	m_IPAddress.Set (pAddress);

	UpdateBroadcastAddress ();
}

void CNetConfig::SetNetMask (const u8 *pNetMask)
{
	m_NetMask.Set (pNetMask);

	UpdateBroadcastAddress ();
}

void CNetConfig::SetDefaultGateway (const u8 *pAddress)
{
	m_DefaultGateway.Set (pAddress);
}

void CNetConfig::SetDNSServer (const u8 *pAddress)
{
	m_DNSServer.Set (pAddress);
}

const CIPAddress *CNetConfig::GetIPAddress (void) const
{
	return &m_IPAddress;
}

boolean CNetConfig::IsDHCPUsed (void) const
{
	return m_bUseDHCP;
}

const u8 *CNetConfig::GetNetMask (void) const
{
	return m_NetMask.Get ();
}

const CIPAddress *CNetConfig::GetDefaultGateway (void) const
{
	return &m_DefaultGateway;
}

const CIPAddress *CNetConfig::GetDNSServer (void) const
{
	return &m_DNSServer;
}

const CIPAddress *CNetConfig::GetBroadcastAddress (void) const
{
	return &m_BroadcastAddress;
}

void CNetConfig::EnableMulticastGroup (const CIPAddress &rIPAddress) {

	// Check if the address is a multicast address
	if (!rIPAddress.IsMulticast ())
	{
		return;
	}

	// Create the MAC address from the IP address
	u8 TempMACAddress[MAC_ADDRESS_SIZE];
	rIPAddress.CopyTo (TempMACAddress + 2);

	TempMACAddress[0] = 0x01;
	TempMACAddress[1] = 0x00;
	TempMACAddress[2] = 0x5E;


	// Check if the address is already in the list
	MulticastGroup *pGroup = m_pMulticastGroups;
	while (pGroup != 0)
	{
		if (*pGroup->pMACAddress == TempMACAddress)
		{
			return;
		}

		pGroup = pGroup->pNext;
	}

	// Create a new multicast group
	MulticastGroup *pNewGroup = new MulticastGroup;
	assert (pNewGroup != 0);

	// Set the next pointer to 0
	pNewGroup->pNext = 0;

	// Allocate the MAC address
	pNewGroup->pMACAddress = new CMACAddress;
	assert (pNewGroup->pMACAddress != 0);

	// Allocate the IP address
	pNewGroup->pIPAddress = new CIPAddress;
	assert (pNewGroup->pIPAddress != 0);

	// Copy the MAC address
	pNewGroup->pMACAddress->Set (TempMACAddress);

	// Copy the IP address
	pNewGroup->pIPAddress->Set (rIPAddress);


	// Get a pointer to the last multicast group
	MulticastGroup *pLastGroup = m_pMulticastGroups;
	while (pLastGroup != 0)
	{
		if (pLastGroup->pNext == 0)
		{
			break;
		}
		pLastGroup = pLastGroup->pNext;
	}

	// Add the new group to the list
	if (pLastGroup == 0)
	{
		m_pMulticastGroups = pNewGroup;
	}
	else
	{
		pLastGroup->pNext = pNewGroup;
	}
}

void CNetConfig::DisableMulticastGroup (const CIPAddress &rIPAddress) {

		// Check if the address is a multicast address
	if (!rIPAddress.IsMulticast ())
	{
		return;
	}

	// Create the MAC address from the IP address
	u8 TempMACAddress[MAC_ADDRESS_SIZE];
	rIPAddress.CopyTo (TempMACAddress + 2);

	TempMACAddress[0] = 0x01;
	TempMACAddress[1] = 0x00;
	TempMACAddress[2] = 0x5E;

	// Move through the list and remove the address
	MulticastGroup *pGroup = m_pMulticastGroups;
	while (pGroup != 0)
	{
		MulticastGroup *pPrev = pGroup;
		MulticastGroup *pNext = pGroup->pNext;

		if (*pGroup->pMACAddress == TempMACAddress)
		{
			// Remove the address from the list
			pPrev->pNext = pNext;

			// Delete the address
			delete pGroup->pMACAddress;
			delete pGroup->pIPAddress;
			delete pGroup;

			break;
		}

		pGroup = pNext;
	}
}

boolean CNetConfig::IsEnabledMulticastGroup (const CIPAddress &rIPAddress) {
	// Move through the list and check if the address is in the list
	MulticastGroup *pGroup = m_pMulticastGroups;
	while (pGroup != 0)
	{
		if (*pGroup->pIPAddress == rIPAddress)
		{
			return TRUE;
		}
		pGroup = pGroup->pNext;
	}

	return FALSE;
}

boolean CNetConfig::IsEnabledMulticastMAC (const CMACAddress &rMACAddress) {
	// Move through the list and check if the address is in the list
	MulticastGroup *pGroup = m_pMulticastGroups;
	while (pGroup != 0)
	{
		if (*pGroup->pMACAddress == rMACAddress)
		{
			return TRUE;
		}
		pGroup = pGroup->pNext;
	}

	return FALSE;
}

void CNetConfig::UpdateBroadcastAddress (void)
{
	u32 nIPAddress;
	m_IPAddress.CopyTo ((u8 *) &nIPAddress);

	u32 nNetMask;
	m_NetMask.CopyTo ((u8 *) &nNetMask);

	m_BroadcastAddress.Set (nIPAddress | ~nNetMask);
}
