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
#include <circle/net/igmphandler.h>
#include <circle/net/networklayer.h>
#include <circle/net/checksumcalculator.h>
#include <circle/net/in.h>
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/util.h>
#include <circle/macros.h>
#include <assert.h>

struct TIGMPHeader
{
	u8	nType;
	u8	nCode;	// Max response time (1/10 seconds)
	u16	nChecksum;
	u8	Parameter[4];		// Group IP address (0 for queries where not used)
}
PACKED;


static const char FromIGMP[] = "igmp";

CIGMPHandler::CIGMPHandler (CNetConfig *pNetConfig, CNetworkLayer *pNetworkLayer,
			    CNetQueue *pRxQueue)
:	m_pNetConfig (pNetConfig),
	m_pNetworkLayer (pNetworkLayer),
	m_pRxQueue (pRxQueue)
{
	assert (m_pNetConfig != 0);
	assert (m_pNetworkLayer != 0);
	assert (m_pRxQueue != 0);
}

CIGMPHandler::~CIGMPHandler (void)
{
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
		if (pIGMPHeader->nType == IGMP_TYPE_MEMBERSHIP_QUERY_V1)
		{
			if (m_pNetConfig->IsEnabledMulticastGroup (CIPAddress (pIGMPHeader->Parameter)))
			{
				// send a report (using received packet in place)
				pIGMPHeader->nType = IGMP_TYPE_MEMBERSHIP_REPORT_V1;
				pIGMPHeader->nChecksum = 0;
				pIGMPHeader->nChecksum = CChecksumCalculator::SimpleCalculate (Buffer, nLength);

				assert (m_pNetworkLayer != 0);
				// TODO: Send should be extended to accept variable length options
				// Here we need to pass
				m_pNetworkLayer->SendWithOptions (SourceIP, Buffer, nLength, IPPROTO_IGMP);
			}
			continue;
		}
	}

	// TODO: Handle unsolicited reports here (see DHCP for timestamping)
}

