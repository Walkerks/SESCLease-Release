/*
   SESC: Super ESCalar simulator
   Copyright (C) 2003 University of Illinois.

   Contributed by Walker Sensabaugh

This file is part of SESC.

SESC is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation;
either version 2, or (at your option) any later version.

SESC is    distributed in the  hope that  it will  be  useful, but  WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should  have received a copy of  the GNU General  Public License along with
SESC; see the file COPYING.  If not, write to the  Free Software Foundation, 59
Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/



#ifndef LEASE_COUNTER_H
#define LEASE_COUNTER_H

#include "libsuc/callback.h"

/**
	LeaseTimer puts a bounded time in a lease instruction.
	This prevents a cache line/group from becoming indefinitly leased (which could cause deadlock)
	Tied to the global clock tick of the processor 
*/

class LeaseTimer{
protected:
	bool running;
	bool stopAlreadyCalled;			//If the timer is stopped before the callback has been triggered
	uint32_t timerLength;
	void doStop();
	typedef CallbackMember0<LeaseTimer, &LeaseTimer::doStop> doStopCB;
public:
	//This is what a SESC callback looks likely
	//CallbackMember0 stands for a callback function with zero arguments
	//Now to actually schedule the callback we would do doStopCB::schedule(Delta timer (int), this (or any LeaseTimer class))
	LeaseTimer();
	void setTimerLength(uint32_t timerLength);			//How long the timer will count down
	void start();										//Starts the timer
	void stop();										//stops the timer
	
	void reset();										//stops and resets the internal values to default
};

#endif //LEASE_COUNTER_H





#ifndef LOADBUFF_H
#define LOADBUFF_H

#include <map>
#include <vector>

/** Lease/Release
	Leases are handled by the load buffer which handles outstanding cache coherence requests
	States: 
		LOADBUFFER_NOT_LEASED - There is no lease
		LOADBUFFER_LEASE - The group/line is in the cache, is leased, and the timer has started
		LOADBUFFER_LEASE_TRANS - The group/line is in the process of becoming leased. This entails getting exclusive
			ownership of the cache line, asserting the group/lines to lease, and assigning and starting the lease timer
*/



enum BufferStates {
			LOADBUFFER_NOT_LEASED,						//The group/line is not leased 
			LOADBUFFER_LEASE, 							//The group/line is leased
			LOADBUFFER_LEASE_TRANS						//The group/line is becoming leased
			};


class loadBuffEntry{
protected:
	BufferStates state;
	LeaseTimer timer;
	MAX_LEASE_TIMERS;
public:
loadBuffEntry();

};

class LoadBuffer{
private:
	std::vector<LeaseTimer> leaseTimers;
	std::map<PAddr, loadBuffEntry > lease-table;

public:
	LoadBuffer();
	
}

#endif //LOADBUFF_H





