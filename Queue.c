#include "Queue.h"


int push(Queue* target, Process* node)
{
	if (target->size == (MAX_PROCESSES - 1))
	{
		console_output(TRUE, "Could not add Process '%s' to Queue %d, the Queue is full.\n", node->name, target->priority);
		return -1;
	}
	else if (target->priority != node->priority)
	{
		console_output(TRUE, "Could not add Process '%s' to Queue %d, Process and Queue priorities do not match.\n", node->name, target->priority);
		return -1;
	}
	else if (target->size == 0)
	{
		target->head = node;
		target->tail = node;
		target->size += 1;
		return target->size;
	}
	else
	{
		target->tail->nextReadyProcess = node;
		target->tail = node;
		target->size += 1;
		return target->size;
	}
}


Process* pop(Queue* target)
{
	Process* node = NULL;

	if (target->size == 0)
	{
		console_output(TRUE, "Could not pop from Queue %d, the Queue is empty.\n", target->priority);
	}
	else if (target->size == 1)
	{
		node = target->head;
		target->head = NULL;
		target->tail = NULL;
		target->size -= 1;
	}
	else
	{
		node = target->head;
		target->head = node->nextReadyProcess;
		node->nextReadyProcess = NULL;
		target->size -= 1;
	}

	return node;
}
