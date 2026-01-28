
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include "THREADSLib.h"
#include "Scheduler.h"
#include "Processes.h"

#define NUM_PRIORITIES (HIGHEST_PRIORITY + 1)

Process processTable[MAX_PROCESSES];
Process *runningProcess = NULL;
Queue readyLists[NUM_PRIORITIES]; // +1 to account for priority 0; index = priority
int nextPid = 1;
int debugFlag = 1;

/* Provided functions */
static int watchdog(char*);
static inline void disableInterrupts();
void dispatcher();
static int launch(void *);
static void check_deadlock();
static void DebugConsole(char* format, ...);

/* New functions */
static int push(Queue* target, Process* node);
static Process* pop(Queue* target);
static int boolAvailableProcesses();
static int isHighestPriorityProcess(Process* target);
static Process* getHighestPriorityProcess();
static void cleanUpChild(Process* target);

/* DO NOT REMOVE */
extern int SchedulerEntryPoint(void* pArgs);
int check_io_scheduler();
check_io_function check_io;


/*************************************************************************
   bootstrap()

   Purpose - This is the first function called by THREADS on startup.

             The function must setup the OS scheduler and primitive
             functionality and then spawn the first two processes.  
             
             The first two process are the watchdog process 
             and the startup process SchedulerEntryPoint.  
             
             The statup process is used to initialize additional layers
             of the OS.  It is also used for testing the scheduler 
             functions.

   Parameters - Arguments *pArgs - these arguments are unused at this time.

   Returns - The function does not return!

   Side Effects - The effects of this function is the launching of the kernel.

 *************************************************************************/
int bootstrap(void *pArgs)
{
    set_debug_level(0); // COMMENT THIS LINE OUT TO ENABLE DEBUG MESSAGES

    int result; /* value returned by call to spawn() */
    set_psr(PSR_KERNEL_MODE);

    /* set this to the scheduler version of this function.*/
    check_io = check_io_scheduler;

    /* Initialize the process table. */


    /* Initialize the Ready list, etc. */
    for (int i = 0; i < NUM_PRIORITIES; i++)
    {
        readyLists[i].head = NULL;
        readyLists[i].tail = NULL;
        readyLists[i].size = 0;
        readyLists[i].priority = i;
    }

    /* Initialize the clock interrupt handler */


    /* startup a watchdog process */
    result = k_spawn("watchdog", watchdog, NULL, THREADS_MIN_STACK_SIZE, LOWEST_PRIORITY); // Will always be pid = 1
    if (result < 0)
    {
        console_output(debugFlag, "Scheduler(): spawn for watchdog returned an error (%d), stopping...\n", result);
        stop(1);
    }

    /* start the test process, which is the main for each test program.  */
    result = k_spawn("Scheduler", SchedulerEntryPoint, NULL, 2 * THREADS_MIN_STACK_SIZE, HIGHEST_PRIORITY); // Will always be pid = 2
    if (result < 0)
    {
        console_output(debugFlag,"Scheduler(): spawn for SchedulerEntryPoint returned an error (%d), stopping...\n", result);
        stop(1);
    }

    /* Initialized and ready to go!! */
    dispatcher();

    /* This should never return since we are not a real process. */

    stop(-3);
    return 0;

}

/*************************************************************************
   k_spawn()

   Purpose - spawns a new process.
   
             Finds an empty entry in the process table and initializes
             information of the process.  Updates information in the
             parent process to reflect this child process creation.

   Parameters - the process's entry point function, the stack size, and
                the process's priority.

   Returns - The Process ID (pid) of the new child process 
             The function must return if the process cannot be created.

************************************************************************ */
int k_spawn(char* name, int (*entryPoint)(void *), void* arg, int stacksize, int priority)
{
    int proc_slot;
    struct _process* pNewProc;

    /*
    if (get_psr() != PSR_KERNEL_MODE)
    {
        console_output(debugFlag, "spawn(): Not in Kernel Mode.\n");
        return -3;
    }
    */

    set_psr(PSR_KERNEL_MODE);

    DebugConsole("spawn(): creating process %s\n", name);

    disableInterrupts();

    /* Validate all of the parameters, starting with the name. */
    if (name == NULL)
    {
        console_output(debugFlag, "spawn(): Name value is NULL.\n");
        return -1;
    }
    if (strlen(name) >= (MAXNAME - 1))
    {
        console_output(debugFlag, "spawn(): Process name is too long.  Halting...\n");
        stop( 1);
    }

    if (stacksize < THREADS_MIN_STACK_SIZE)
    {
        console_output(debugFlag, "spawn(): Stack size is too small.\n");
        return -4;
    }

    if (priority < LOWEST_PRIORITY || priority > HIGHEST_PRIORITY)
    {
        console_output(debugFlag, "spawn(): Invalid priority.\n");
        return -5;
    }


    /* Find an empty slot in the process table */
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (processTable[i].pid == 0)
        {
            proc_slot = i;
            break;
        }
    }
    
    pNewProc = &processTable[proc_slot];

    /* Setup the entry in the process table. */
    strcpy(pNewProc->name, name);
    pNewProc->pid = nextPid;
    nextPid++;  // Increment Pid counter
    pNewProc->entryPoint = entryPoint;
    pNewProc->priority = priority;
    pNewProc->stacksize = stacksize;
    pNewProc->status = READY;
    pNewProc->exitCode = 0;

    if (arg != NULL)
    {
        strcpy(pNewProc->startArgs, arg);
    }

    /* If there is a parent process,add this to the list of children. */
    if (runningProcess != NULL)
    {
        if (runningProcess->pChildren == NULL)
        {
            runningProcess->pChildren = pNewProc;
        }
        else
        {
            Process* child = runningProcess->pChildren;

            while (child->nextSiblingProcess != NULL)
            {
                child = child->nextSiblingProcess;
            }

            child->nextSiblingProcess = pNewProc;
        }

        pNewProc->pParent = runningProcess;
    }

    /* Add the process to the ready list. */
    if (push(&readyLists[pNewProc->priority], pNewProc) == 0)
    {
        return -2;
    }

    /* Initialize context for this process, but use launch function pointer for
     * the initial value of the process's program counter (PC)
    */
    pNewProc->context = context_initialize(launch, stacksize, arg);

    if (pNewProc->pid != 1) // Skip this function call for Watchdog
    {
        dispatcher();
    }

    return pNewProc->pid;


} /* spawn */

/**************************************************************************
   Name - launch

   Purpose - Utility function that makes sure the environment is ready,
             such as enabling interrupts, for the new process.  

   Parameters - none

   Returns - nothing
*************************************************************************/
static int launch(void *args)
{
    int result;

    DebugConsole("launch(): started: %s\n", runningProcess->name);

    /* Enable interrupts */
    set_psr(PSR_INTERRUPTS);

    /* Call the function passed to spawn and capture its return value */
    result = runningProcess->entryPoint(runningProcess->startArgs);

    DebugConsole("Process %d returned to launch\n", runningProcess->pid);

    /* Stop the process gracefully */
    k_exit(result);

    return 0;
} 

/**************************************************************************
   Name - k_wait

   Purpose - Wait for a child process to quit.  Return right away if
             a child has already quit.

   Parameters - Output parameter for the child's exit code. 

   Returns - the pid of the quitting child, or
        -1 if the process has no children
        -5 if the process was signaled in the join

************************************************************************ */
int k_wait(int* code)
{
    int result = 0;
    Process* child = runningProcess->pChildren;

    if (child == NULL) // Case: Process has no children
    {
        return -1;
    }

    while (child != NULL) // Case: a child has already exited
    {
        if (child->status == QUIT)
        {
            *code = child->exitCode;
            result = child->pid;
            cleanUpChild(child);
            return result;
        }

        child = child->nextSiblingProcess;
    }

    child = runningProcess->pChildren; // Reset to head of children linked list

    runningProcess->status = BLOCKED; // Case: no child has exited yet and this process must wait
    dispatcher();

    while (child != NULL) // Find exited child
    {
        if (child->status == QUIT)
        {
            *code = child->exitCode;
            result = child->pid;
            cleanUpChild(child);
            break;
        }

        child = child->nextSiblingProcess;
    }

    return result;

} 

/**************************************************************************
   Name - k_exit

   Purpose - Exits a process and coordinates with the parent for cleanup 
             and return of the exit code.

   Parameters - the code to return to the grieving parent

   Returns - nothing
   
*************************************************************************/
void k_exit(int code)
{
    // Exiting process should not have children, halt program if it does
    if (runningProcess->pChildren != NULL) 
    {
        console_output(debugFlag, "k_exit(): Exiting process has active children, closing program.\n");
        stop(1);
    }

    // If the process has a blocked parent, unblock it
    if (runningProcess->pParent != NULL && runningProcess->pParent->status == BLOCKED) 
    {
        int index = runningProcess->pParent->priority;

        runningProcess->pParent->status = READY;
        push(&readyLists[index], runningProcess->pParent);
    }
    
    runningProcess->status = QUIT;
    runningProcess->exitCode = code;

    dispatcher();
}

/**************************************************************************
   Name - k_kill

   Purpose - Signals a process with the specified signal

   Parameters - Signal to send

   Returns -
*************************************************************************/
int k_kill(int pid, int signal)
{
    int result = 0;
    return 0;
}

/**************************************************************************
   Name - k_getpid
*************************************************************************/
int k_getpid()
{
    return 0;
}

/**************************************************************************
   Name - k_join
***************************************************************************/
int k_join(int pid, int* pChildExitCode)
{
    return 0;
}

/**************************************************************************
   Name - unblock
*************************************************************************/
int unblock(int pid)
{
    return 0;
}

/*************************************************************************
   Name - block
*************************************************************************/
int block(int newStatus)
{
    return 0;
}

/*************************************************************************
   Name - signaled
*************************************************************************/
int signaled()
{
    return 0;
}
/*************************************************************************
   Name - readtime
*************************************************************************/
int read_time()
{
    return 0;
}

/*************************************************************************
   Name - readClock
*************************************************************************/
DWORD read_clock()
{
    return system_clock();
}

void display_process_table()
{

}

/**************************************************************************
   Name - dispatcher

   Purpose - This is where context changes to the next process to run.

   Parameters - none

   Returns - nothing

*************************************************************************/
void dispatcher()
{
    Process* nextProcess = NULL;

    // No process was running
    if (runningProcess == NULL) 
    {
        nextProcess = getHighestPriorityProcess();
    }
    // Current process has been blocked by another process or has exited, do not add to readyLists
    else if (runningProcess->status == BLOCKED || runningProcess->status == QUIT)
    {
        nextProcess = getHighestPriorityProcess();
    }
    // Current process is still the higest priority
    else if (isHighestPriorityProcess(runningProcess)) 
    {
        return;
    }
    // Otherwise, reassess which process should run after adding current process back into readyLists
    else 
    {
        Process* previousProcess = runningProcess;
        runningProcess = NULL;

        for (int i = 0; i < NUM_PRIORITIES; i++) // Add previous process back into the ready lists
        {
            if (previousProcess->priority == i)
            {
                previousProcess->status = READY;
                push(&readyLists[i], previousProcess);
                break;
            }
        }

        nextProcess = getHighestPriorityProcess();
    }

    if (nextProcess == NULL)
    {
        console_output(debugFlag, "Dispatcher(): Attempting to switch context to NULL process, closing program...\n");
        stop(1);
    }

    runningProcess = nextProcess;
    context_switch(runningProcess->context);

    // If currently running process is highest priority, don't change context
}


/**************************************************************************
   Name - watchdog

   Purpose - The watchdoog keeps the system going when all other
         processes are blocked.  It can be used to detect when the system
         is shutting down as well as when a deadlock condition arises.

   Parameters - none

   Returns - nothing
   *************************************************************************/
static int watchdog(char* dummy)
{
    DebugConsole("watchdog(): called\n");
    while (1)
    {
        check_deadlock();
    }
    return 0;
} 

/* check to determine if deadlock has occurred... */
static void check_deadlock()
{
    if (check_io() == 1)
    {
        return;
    }

    if (boolAvailableProcesses())
    {
        stop(1);
    }
    else
    {
        console_output(false, "All processes completed.\n");
        stop(0);
    }
}

/*
 * Disables the interrupts.
 */
static inline void disableInterrupts()
{

    /* We ARE in kernel mode */


    int psr = get_psr();

    psr = psr & ~PSR_INTERRUPTS;

    set_psr( psr);

} /* disableInterrupts */

/**************************************************************************
   Name - DebugConsole
   Purpose - Prints  the message to the console_output if in debug mode
   Parameters - format string and va args
   Returns - nothing
   Side Effects -
*************************************************************************/
static void DebugConsole(char* format, ...)
{
    char buffer[2048];
    va_list argptr;

    if (debugFlag)
    {
        va_start(argptr, format);
        vsprintf(buffer, format, argptr);
        console_output(TRUE, buffer);
        va_end(argptr);

    }
}


/* there is no I/O yet, so return false. */
int check_io_scheduler()
{
    return false;
}



/* NEW FUNCTIONS */


static int push(Queue* target, Process* node)
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


static Process* pop(Queue* target)
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


static int boolAvailableProcesses()
{
    for (int i = 1; i < NUM_PRIORITIES; i++) // Skip lowest priority
    {
        if (readyLists[i].size > 0)
        {
            return true;
        }
    }
    if (readyLists[0].size > 1) // Watchdog should be running
    {
        return true;
    }

    return false;
}


static int isHighestPriorityProcess(Process* target)
{
    for (int i = HIGHEST_PRIORITY; i >= target->priority; i--)
    {
        if (readyLists[i].size > 0)
        {
            return false;
        }
    }

    return true;
}


static Process* getHighestPriorityProcess()
{
    for (int i = HIGHEST_PRIORITY; i >= 0; i--)
    {
        if (readyLists[i].size > 0)
        {
            return pop(&readyLists[i]);
        }
    }

    return NULL;
}


static void cleanUpChild(Process* target)
{
    Process blankProcess;

    // Remove from parent, if it is the head of the children list
    if (runningProcess->pChildren->pid == target->pid)
    {
        runningProcess->pChildren = target->nextSiblingProcess;
    }
    // Remove from sibling chain if it isn't
    else
    {
        Process* child = runningProcess->pChildren;

        while (child->nextSiblingProcess->pid != target->pid);
        {
            child = child->nextSiblingProcess;
        }

        child->nextSiblingProcess = target->nextSiblingProcess;
    }

    // Clear child from the process table
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (processTable[i].pid == target->pid)
        {
            blankProcess.pid = 0;
            processTable[i] = blankProcess;
        }
    }
}