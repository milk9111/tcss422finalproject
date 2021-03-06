/*
	12/6/2017
	Authors: Connor Lundberg, Jacob Ackerman, Jasmine Dacones
*/

#ifndef THREADS_H
#define THREADS_H

#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>


typedef struct MUTEX {
	int isLocked;
	PCB pcb1;
	PCB pcb2;
	PCB hasLock;
	ReadyQueue blocked;
} mutex_s;

typedef mutex_s * Mutex;


void mutex_init (Mutex mutex);

void toStringMutex (Mutex mutex);

#endif