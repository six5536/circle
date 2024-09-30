//
// igmphandler.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2015-2020  R. Stange <rsta2@o2online.de>
// Copyright (C) 2024  R.A. Sewell <richsewell@gmail.com>
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

// RFC 2236 (https://datatracker.ietf.org/doc/html/rfc2236)
// This is a simple handler for IGMPv2 only.

#include <circle/net/igmphandler.h>
#include <circle/net/networklayer.h>
#include <circle/net/checksumcalculator.h>
#include <circle/net/in.h>
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/util.h>
#include <circle/macros.h>
#include <assert.h>

#define INITIAL_REPORT_COUNT 2
#define MAX_INITIAL_REPORT_DELAY_MS	1000	//  1s


// CLogger::Get ()->Write (FromIGMP, LogDebug, "Time %u", nTimestampMs);

struct TIGMPHeader
{
	u8	nType;
	u8	nCode;	// Max response time (1/10 seconds)
	u16	nChecksum;
	u8	Parameter[4];		// Group IP address (0 for queries where not used)
}
PACKED;


static const char FromIGMP[] = "igmp";

// Forward declarations
u32 rand(u32 min, u32 max);


CIGMPHandler::CIGMPHandler (CNetConfig *pNetConfig, CNetworkLayer *pNetworkLayer,
			    CNetQueue *pRxQueue)
:	m_pNetConfig (pNetConfig),
	m_pNetworkLayer (pNetworkLayer),
	m_pRxQueue (pRxQueue),
	m_pMulticastGroupStates (0),
	m_nTimestampMs (0),
	m_nLastTicksMs (0)
{
	assert (m_pNetConfig != 0);
	assert (m_pNetworkLayer != 0);
	assert (m_pRxQueue != 0);
}

CIGMPHandler::~CIGMPHandler (void)
{
	// Delete multicast group states
	MulticastGroupState *pGroup = m_pMulticastGroupStates;
	m_pMulticastGroupStates = 0; // Ensure no further access to the list

	while (pGroup != 0)
	{
		MulticastGroupState *pNext = pGroup->pNext;
		delete pGroup->pIPAddress;
		delete pGroup;
		pGroup = pNext;
	}

	m_pRxQueue = 0;
	m_pNetworkLayer = 0;
	m_pNetConfig = 0;
}

void CIGMPHandler::Process (void)
{
	u8 Buffer[FRAME_BUFFER_SIZE];
	unsigned nLength;
	void *pParam;
	assert (m_pRxQueue != 0);
	assert (m_pNetworkLayer != 0);

	// Get the current timestamp in milliseconds, using the system ticks
	const unsigned ticks = CTimer::Get()->GetTicks();
  const unsigned nTimestampDiffMs = (ticks - m_nLastTicksMs) * 1000 / HZ;
	m_nTimestampMs += nTimestampDiffMs;
	m_nLastTicksMs = ticks;


	// Process the configured multicast groups, creating data for new initial reports and leaves
	ProcessMulticastGroupChanges (m_nTimestampMs);


	// Process the received IGMP packets
	while ((nLength = m_pRxQueue->Dequeue (Buffer, &pParam)) != 0)
	{
		TNetworkPrivateData *pData = (TNetworkPrivateData *) pParam;
		assert (pData != 0);
		assert (pData->nProtocol == IPPROTO_IGMP);

		CIPAddress SourceIP (pData->SourceAddress);
		CIPAddress DestIP (pData->DestinationAddress);

		delete pData;
		pData = 0;

		assert (m_pNetConfig != 0);
		if (!m_pNetConfig->IsEnabledMulticastGroup (DestIP))
		{
			continue;
		}

		if (nLength < sizeof (TIGMPHeader))
		{
			continue;
		}
		TIGMPHeader *pIGMPHeader = (TIGMPHeader *) Buffer;

		if (CChecksumCalculator::SimpleCalculate (Buffer, nLength) != CHECKSUM_OK)
		{
			continue;
		}

		// handle membership query messages
		if (pIGMPHeader->nType == IGMP_TYPE_MEMBERSHIP_QUERY)
		{
			// Check if this is a general query, and if so, respond with reports for all groups
			if (   pIGMPHeader->Parameter[0] == 0
					&& pIGMPHeader->Parameter[1] == 0
					&& pIGMPHeader->Parameter[2] == 0
					&& pIGMPHeader->Parameter[3] == 0)
			{
				// General query, queue reports for all groups
				ProcessMulticastGroupReportAll (m_nTimestampMs, pIGMPHeader->nCode * 100);
			}
			else if (m_pNetConfig->IsEnabledMulticastGroup (CIPAddress (pIGMPHeader->Parameter)))
			{
				// Specific query, send a report immediately (using received packet in place)
				pIGMPHeader->nType = IGMP_TYPE_MEMBERSHIP_REPORT_V2;
				pIGMPHeader->nChecksum = 0;
				pIGMPHeader->nChecksum = CChecksumCalculator::SimpleCalculate (pIGMPHeader, sizeof(TIGMPHeader));

				// TODO: Send should be extended to accept variable length options
				// Here we need to pass
				m_pNetworkLayer->SendWithOptions (SourceIP, Buffer, nLength, IPPROTO_IGMP);
			}
			continue;
		}
	}

	// Send any pending reports or leaves for multicast groups
	SendPendingReportsAndLeaves (m_nTimestampMs);
}

void CIGMPHandler::SendPendingReportsAndLeaves (u64 nTimestampMs) {
	MulticastGroupState *pState = m_pMulticastGroupStates;
	MulticastGroupState *pPrev = 0;
	while (pState != 0)
	{
		MulticastGroupState *pNext = pState->pNext;

		if (pState->bLeavePending) {
			// Send a leave message
			TIGMPHeader IGMPHeader;
			IGMPHeader.nType = IGMP_TYPE_LEAVE_GROUP_V2;
			IGMPHeader.nCode = 0;
			IGMPHeader.nChecksum = 0;
			pState->pIPAddress->CopyTo (IGMPHeader.Parameter);
			IGMPHeader.nChecksum = CChecksumCalculator::SimpleCalculate (&IGMPHeader, sizeof(TIGMPHeader));

			// TODO: Send should be extended to accept variable length options
			// Here we need to pass
			m_pNetworkLayer->SendWithOptions (*pState->pIPAddress, &IGMPHeader, sizeof(TIGMPHeader), IPPROTO_IGMP);

			// Remove the group state from the list
			if (pPrev == 0)
			{
				m_pMulticastGroupStates = pNext;
			}
			else
			{
				pPrev->pNext = pNext;
			}

			// Delete the group state
			delete pState->pIPAddress;
			delete pState;
			pState = pNext;
		}
		else if (pState->nReportsPending > 0 && nTimestampMs > pState->nNextReportTime)
		{
			// Send a report message
			TIGMPHeader IGMPHeader;
			IGMPHeader.nType = IGMP_TYPE_MEMBERSHIP_REPORT_V2;
			IGMPHeader.nCode = 0;
			IGMPHeader.nChecksum = 0;
			pState->pIPAddress->CopyTo (IGMPHeader.Parameter);
			IGMPHeader.nChecksum = CChecksumCalculator::SimpleCalculate (&IGMPHeader, sizeof(TIGMPHeader));

			// TODO: Send should be extended to accept variable length options
			// Here we need to pass
			m_pNetworkLayer->SendWithOptions (*pState->pIPAddress, &IGMPHeader, sizeof(TIGMPHeader), IPPROTO_IGMP);


			// Decrement the reports pending count, if still > 0, set the next initial report time
			pState->nReportsPending -= 1;
			if (pState->nReportsPending > 0)
			{
				pState->nNextReportTime = nTimestampMs + rand(0, MAX_INITIAL_REPORT_DELAY_MS);
			}
			else
			{
				pState->nNextReportTime = 0;
			}
		}

		pPrev = pState;
		pState = pState->pNext;
	}
}

void CIGMPHandler::ProcessMulticastGroupChanges (u64 nTimestampMs) {
	const MulticastGroup *pGroup = m_pNetConfig->GetMulticastGroups ();


	// Loop the multicast group states and update the leave pending flag to true
	// The flag will be cleared in the later loop if the multicast group still exists
	MulticastGroupState *pState = m_pMulticastGroupStates;
	MulticastGroupState *pLastState = pState;
	while (pState != 0)
	{
		pState->bLeavePending = TRUE;
		pLastState = pState;
		pState = pState->pNext;
	}

	// Move through the list of multicast groups looking for any additions / deletions
	// If a deletion is found, the leave pending flag will remain set
	// If an addition is found, and new state will be created with the report pending flag set
	while (pGroup != 0)
	{
		// Find this group in the list of states
		MulticastGroupState *pState = m_pMulticastGroupStates;
		while (pState != 0)
		{
			if (*pState->pIPAddress == *pGroup->pIPAddress)
			{
				break;
			}
			pState = pState->pNext;
		}

		if (pState == 0) {
			// This group is not in the state list, add it and set the report pending flag
			pState = new MulticastGroupState;
			pState->pIPAddress = new CIPAddress (*pGroup->pIPAddress);
			pState->nReportsPending = INITIAL_REPORT_COUNT;
			pState->bLeavePending = FALSE;
			pState->nNextReportTime = nTimestampMs + rand(0, MAX_INITIAL_REPORT_DELAY_MS);
			pState->nLastReportTime = 0;
			pState->pNext = 0;

			// Add to the end of the list
			if (pLastState == 0)
			{
				m_pMulticastGroupStates = pState;
			}
			else
			{
				pLastState->pNext = pState;
			}
			pLastState = pState;
		}
		else
		{
			// This group is already in the list, clear the leave pending flag
			pState->bLeavePending = FALSE;
		}

		pGroup = pGroup->pNext;
	}
}

void CIGMPHandler::ProcessMulticastGroupReportAll(u64 nTimestampMs, u32 nMaxDelay)
{
	MulticastGroupState *pState = m_pMulticastGroupStates;
	while (pState != 0)
	{
		if (pState->nReportsPending == 0) {
			pState->nReportsPending = 1;
			pState->nNextReportTime = nTimestampMs + rand(0, nMaxDelay);
			pState = pState->pNext;
		}
	}
}

/**
 * Generate a pseudo-random number between min and max
 * Will be good enough for our purposes
 */
u32 rand(u32 min, u32 max)
{
    static u32 a = 0xABCD1234; /*Seed*/

    /*Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs"*/
    u32 x = a;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    a = x;

    return (a % (max - min + 1)) + min;
}

