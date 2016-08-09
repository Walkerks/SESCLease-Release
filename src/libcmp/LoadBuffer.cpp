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



#include "LoadBuffer.h"
/*
LoadBuffer::LoadBuffer(){


}
*/











LeaseTimer::LeaseTimer(){
	running = false;
	stopAlreadyCalled = false;
	timerLength = 0;
}


void LeaseTimer::setTimerLength(uint32_t timerLengthT){
	timerLength = timerLengthT;
}
void LeaseTimer::start(){
	//Make sure the timer length has been set
	I(timerLength == 0);
	running = true;
	//schedule the timer to call doStop after timerLength amount of ticks
	doStopCB::schedule(timerLength, this);
	printf("Started callback with %llu !\n", globalClock);
}
void LeaseTimer::doStop(){
	printf("Callback executed with %llu !\n", globalClock);
/*	
	if(stopAlreadyCalled){
		return;	
	} else{
		running = false;
	}*/
}										
void LeaseTimer::stop(){
	stopAlreadyCalled = true;
	doStop();
}										
void LeaseTimer::reset(){
	
}									
