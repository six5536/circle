//
// igmphandler.h
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
#ifndef _circle_net_igmphandler_h
#define _circle_net_igmphandler_h

#include <circle/net/netconfig.h>
#include <circle/net/netqueue.h>
#include <circle/net/ipaddress.h>
#include <circle/types.h>

#define IGMP_TYPE_MEMBERSHIP_QUERY		0x11
#define IGMP_TYPE_MEMBERSHIP_REPORT_V1		0x12
#define IGMP_TYPE_MEMBERSHIP_REPORT_V2		0x16
#define IGMP_TYPE_MEMBERSHIP_REPORT_V3		0x22
#define IGMP_TYPE_LEAVE_GROUP_V2		0x17

struct MulticastGroupState
{
	CIPAddress *pIPAddress;
	u32 nReportsPending;
	boolean bLeavePending;
	u64 nNextReportTime;
	u64 nLastReportTime;
	MulticastGroupState *pNext;
};

class CNetworkLayer;

class CIGMPHandler
{
public:
	CIGMPHandler (CNetConfig *pNetConfig, CNetworkLayer *pNetworkLayer,
		      CNetQueue *pRxQueue);
	~CIGMPHandler (void);

	void Process (void);

private:
	void ProcessMulticastGroupChanges (u64 nTimestampMs);
	void ProcessMulticastGroupReportAll (u64 nTimestampMs, u32 nMaxDelay);
	void SendPendingReportsAndLeaves (u64 nTimestampMs);

private:
	CNetConfig	*m_pNetConfig;
	CNetworkLayer	*m_pNetworkLayer;
	CNetQueue	*m_pRxQueue;
	MulticastGroupState *m_pMulticastGroupStates;
	u64 m_nTimestampMs;
	u32 m_nLastTicksMs;
};

#endif
