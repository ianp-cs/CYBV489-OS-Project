#pragma once

#include "Processes.h"
#include "THREADSLib.h"

typedef struct _queue
{
	Process*	head;		// First process in queue
	Process*	tail;		// Last process in queue
	int			size;		// Total number of processes in queue
	int			priority;	// The priority of the processes in the queue

} Queue;

int push(Queue* target, Process* node);
Process* pop(Queue* target);