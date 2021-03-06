/*
	10/28/2017
	Authors: Connor Lundberg, Jacob Ackerman
	
	In this project we will be making an MLFQ scheduling algorithm
	that will take a PriorityQueue of PCBs and run them through our scheduler.
	It will simulate the "running" of the process by incrementing the running PCB's PCB 
	by one on each loop through. It will also incorporate various interrupts to show 
	the effects it has on the scheduling simulator.
	
	This file holds the defined functions declared in the scheduler.h header file.
*/

#include "scheduler.h"


unsigned int sysstack;
int switchCalls;

Scheduler thisScheduler;

PCB privileged[4];
int privilege_counter = 0;
int ran_term_num = 0;
int terminated = 0;
int currQuantumSize;
int quantum_tick = 0; // Use for quantum length tracking
int io_timer = 0;
time_t t;

int contextSwitchCount = 0;

// The global counts of each PCB type, the final count at end of program run 
// should be roughly 50%, 25%, 12.5%, 12.5% respectively.
int compCount;
int ioCount;
int pairCount;
int sharedCount;



/*
	This function is our main loop. It creates a Scheduler object and follows the
	steps a normal MLFQ Priority Scheduler would to "run" for a certain length of time,
	check for all interrupt types, then call the ISR, scheduler,
	dispatcher, and eventually an IRET to return to the top of the loop and start
	with the new process.
*/
void osLoop () {
	int totalProcesses = 0, iterationCount = 1, lock = 0, unlock = 0, isSwitched = 0;
	Mutex currMutex;
	
	thisScheduler = schedulerConstructor();
	totalProcesses += makePCBList(thisScheduler);
	printSchedulerState(thisScheduler);
	for(;;) {
		if (thisScheduler->running != NULL) { // In case the first makePCBList makes 0 PCBs
			if (thisScheduler->running->role == PAIR || thisScheduler->running->role == SHARED) {
				isSwitched = useMutex(thisScheduler);
				
				if (isSwitched) { // call deadlockMonitor after every 10th context switch
					contextSwitchCount++;
					if (contextSwitchCount == 10) {
						deadlockMonitor(thisScheduler);	
						contextSwitchCount = 0;
					}
								
				}
			
			}
			
			if (!isSwitched) { //If the context wasn't switched inside of useMutex
				thisScheduler->running->context->pc++;
				
				
				if (timerInterrupt(iterationCount) == 1) {
					pseudoISR(thisScheduler, IS_TIMER);
					printf("Completed Timer Interrupt\n");
					printSchedulerState(thisScheduler);
					iterationCount++;
				}
				
				if (ioTrap(thisScheduler->running) == 1) {
					printf("Iteration: %d\r\n", iterationCount);
					printf("Initiating I/O Trap\r\n");
					printf("PC when I/O Trap is Reached: %d\r\n", thisScheduler->running->context->pc);
					pseudoISR(thisScheduler, IS_IO_TRAP);
					
					printSchedulerState(thisScheduler);
					iterationCount++;
					printf("Completed I/O Trap\n");
				}
				
				if (thisScheduler->running != NULL)
				{
					if (thisScheduler->running->context->pc >= thisScheduler->running->max_pc) {
						thisScheduler->running->context->pc = 0;
						thisScheduler->running->term_count++;	//if terminate value is > 0
					}
				}
				
				if (ioInterrupt(thisScheduler->blocked) == 1) {
					printf("Iteration: %d\r\n", iterationCount);
					printf("Initiating I/O Interrupt\n");
					pseudoISR(thisScheduler, IS_IO_INTERRUPT);
					
					printSchedulerState(thisScheduler);
					iterationCount++;
					printf("Completed I/O Interrupt\n");
				}
				
				// if running PCB's terminate == running PCB's term_count, then terminate (for real).
				terminate(thisScheduler);
			}
		} else {
			iterationCount++;
			printf("Idle\n");
		}
	
		
		if (!(iterationCount % RESET_COUNT)) {
			printf("\r\nRESETTING MLFQ\r\n");
			printf("iterationCount: %d\n", iterationCount);
			resetMLFQ(thisScheduler);
			//printf("here5.9\n");
			if (rand() % MAKE_PCB_CHANCE_DOMAIN <= MAKE_PCB_CHANCE_PERCENTAGE) {
				//printf("here6\n");
				totalProcesses += makePCBList (thisScheduler);
			}
			//printf("here6.5\n");
			printSchedulerState(thisScheduler);
			//printf("here6.6\n");
			iterationCount = 1;
		}
		if (totalProcesses >= MAX_PCB_TOTAL) {
			printf("Reached max PCBs, ending Scheduler.\r\n");
			break;
		}
	}
	
	schedulerDeconstructor(thisScheduler);
}


/*
	Will look through the lockR1 and lockR2 arrays in the given pcb and see if 
	they match the given PC value. If so, then return the array number, 0 otherwise.
	
	(1 for lockR1, 2 for lockR2)
*/
int isLockPC (unsigned int pc, PCB pcb) {
	int isLock = 0;
	for (int i = 0; i < TRAP_COUNT; i++) {
		if (pc == pcb->lockR1[i]) {
			isLock = 1;
			break;
		}
	}
	
	if (pcb->role == SHARED && !isLock) {
		for (int i = 0; i < TRAP_COUNT; i++) {
			if (pc == pcb->lockR2[i]) {
				isLock = 2;
				break;
			}
		}
	}
	
	return isLock;
}


/*
	Will look through the unlockR1 and unlockR2 arrays in the given pcb and see if 
	they match the given PC value. If so, then return the array number, 0 otherwise.
	
	(1 for unlockR1, 2 for unlockR2)
*/
int isUnlockPC (unsigned int pc, PCB pcb) {
	int isUnlock = 0;
	for (int i = 0; i < TRAP_COUNT; i++) {
		if (pc == pcb->unlockR1[i]) {
			isUnlock = 1;
			break;
		}
	}
	
	if (pcb->role == SHARED && !isUnlock) {
		for (int i = 0; i < TRAP_COUNT; i++) {
			if (pc == pcb->unlockR2[i]) {
				isUnlock = 2;
				break;
			}
		}
	}
	
	return isUnlock;
}


/*
	Will look through the signal_cond array in the given pcb and see if 
	they match the given PC value. If so, then return 1, 0 otherwise.
*/
int isSignalPC (unsigned int pc, PCB pcb) {
	int isSignal = 0;
	for (int i = 0; i < TRAP_COUNT; i++) {
		if (pc == pcb->signal_cond[i]) {
			isSignal = 1;
			break;
		}
	}
	
	return isSignal;
}


/*
	Will look through the signal_cond array in the given pcb and see if 
	they match the given PC value. If so, then return 1, 0 otherwise.
*/
int isWaitPC (unsigned int pc, PCB pcb) {
	int isWait = 0;
	for (int i = 0; i < TRAP_COUNT; i++) {
		if (pc == pcb->wait_cond[i]) {
			isWait = 1;
			break;
		}
	}
	
	return isWait;
}


/*
	Checks if the global quantum tick is greater than or equal to
	the current quantum size for the running PCB. If so, then reset
	the quantum tick to 0 and return 1 so the pseudoISR can occur.
	If not, increase quantum tick by 1.
*/
int timerInterrupt(int iterationCount)
{
	if (quantum_tick >= currQuantumSize)
	{
		printf("Iteration: %d\r\n", iterationCount);
		printf("Initiating Timer Interrupt\n");
		printf("Current quantum tick: %d\r\n", quantum_tick);
		quantum_tick = 0;
		return 1;
	}
	else
	{
		quantum_tick++;
		return 0;
	}
}


/*
	Checks if the current PCB's PC is one of the premade io_traps for this
	PCB. If so, then return 1 so the pseudoISR can occur. If not, return 0.
*/
int ioTrap(PCB current)
{
	if (current) {
		unsigned int the_pc = current->context->pc;
		int c;
		for (c = 0; c < TRAP_COUNT; c++)
		{
			if(the_pc == current->io_1_traps[c])
			{
				return 1;
			}
		}
		for (c = 0; c < TRAP_COUNT; c++)
		{
			if(the_pc == current->io_2_traps[c])
			{
				return 1;
			}
		}
	}
	return 0;
}


/*
	Checks if the next up PCB in the Blocked queue has reached its max 
	blocked_timer value yet. If so, return 1 and initiate the IO Interrupt.
*/
int ioInterrupt(ReadyQueue the_blocked)
{
	if (the_blocked->first_node != NULL && q_peek(the_blocked) != NULL)
	{
		PCB nextup = q_peek(the_blocked);
		if (io_timer >= nextup->blocked_timer)
		{
			io_timer = 0;
			return 1;
		}
		else
		{
			io_timer++;
		}
	}
	
	return 0;
}


/*
	This creates the list of new PCBs for the current loop through. It simulates
	the creation of each PCB, the changing of state to new, enqueueing into the
	list of created PCBs, and moving each of those PCBs into the ready queue.
*/
int makePCBList (Scheduler theScheduler) {
	//printf("making new PCBs\n");
	// change this to make 2 at a time.
	//int newPCBCount = rand() % MAX_PCB_IN_ROUND;
	int newPCBCount = 2;
	//int mutexCount = rand() & MAX_MUTEX_IN_ROUND;
	
	int lottery = rand();
	//for (int i = 0; i < newPCBCount; i++) {
		
	Mutex sharedMutexR1 = mutex_create();
	Mutex sharedMutexR2 = mutex_create();
	
	PCB newPCB1 = PCB_create();
	PCB newPCB2 = PCB_create();
	
	//printf("newPCB1 == null? %d\n", (newPCB1 == NULL));
	//printf("newPCB2 == null? %d\n", (newPCB2 == NULL));
	
	newPCB2->parent = newPCB1->pid;
	
	newPCB1->role = COMP;
	newPCB2->role = COMP;
	initialize_pcb_type (newPCB1, 1, sharedMutexR1, sharedMutexR2); 
	initialize_pcb_type (newPCB2, 0, sharedMutexR1, sharedMutexR2); 
	
	incrementRoleCount(newPCB1->role);
	incrementRoleCount(newPCB2->role);
	
	//printf("\r\nM: ");
	//toStringReadyQueueMutexes(theScheduler->killedMutexes);
	if (newPCB1->role == COMP || newPCB1->role == IO) { //if the role isn't one that uses a mutex, then destroy it.
		//printf("Role wasn't PAIR or SHARED, freeing sharedMutexR1 M%d\r\n", sharedMutexR1->mid);
		//printf("Role wasn't PAIR or SHARED, freeing sharedMutexR2 M%d\r\n", sharedMutexR2->mid);

		free(sharedMutexR1);
		free(sharedMutexR2);
	} else {
		//printf("Role was PAIR or SHARED, adding sharedMutexR1 M%d\r\n", sharedMutexR1->mid);
		//printf("Role was PAIR or SHARED, adding sharedMutexR2 M%d\r\n", sharedMutexR2->mid);
		if (newPCB1->role == SHARED) {
			printf("=============================\n");
			printf("Made Shared Resource pair\n");
			
			if (DEADLOCK) {
				populateMutexTraps1221(newPCB1, newPCB1->max_pc / MAX_DIVIDER);
				populateMutexTraps2112(newPCB2, newPCB1->max_pc / MAX_DIVIDER);
				
			} else {
				populateMutexTraps1221(newPCB1, newPCB1->max_pc / MAX_DIVIDER);
				populateMutexTraps1221(newPCB2, newPCB2->max_pc / MAX_DIVIDER);
			}
			
			// toStringMutexTraps(newPCB1, newPCB2);
		
			printf("ADDING IN M%d from Shared Resource\n", sharedMutexR1->mid);
			printf("ADDING IN M%d from Shared Resource\n", sharedMutexR2->mid);
			
			add_to_mutx_map(theScheduler->mutexes, sharedMutexR1, sharedMutexR1->mid);
			add_to_mutx_map(theScheduler->mutexes, sharedMutexR2, sharedMutexR2->mid);
			
			printf("pcb1->mutex_R1_id: M%d\n", newPCB1->mutex_R1_id);
			printf("pcb2->mutex_R2_id: M%d\n", newPCB2->mutex_R2_id);
		} else {
			/* LOOK AT THIS */
			printf("=============================\n");
			printf("Made Producer/Consumer\n");
			populateProducerConsumerTraps(newPCB1, newPCB1->max_pc / MAX_DIVIDER, newPCB1->isProducer);
			populateProducerConsumerTraps(newPCB2, newPCB2->max_pc / MAX_DIVIDER, newPCB2->isProducer);
			printf("ADDING IN M%d from Producer/Consumer\n", sharedMutexR1->mid);
			add_to_mutx_map(theScheduler->mutexes, sharedMutexR1, sharedMutexR1->mid);
			printf("pcb1->mutex_R1_id: M%d\n", newPCB1->mutex_R1_id);
			printf("pcb2->mutex_R1_id: M%d\n", newPCB2->mutex_R1_id);
	
			free(sharedMutexR2);
		}
		
	}
	
	newPCB1->state = STATE_NEW;
	newPCB2->state = STATE_NEW;
	//printf("theScheduler->created == NULL: %d\n", (theScheduler->created == NULL));
	//printf("enqueueing PCBs\n");
	q_enqueue(theScheduler->created, newPCB1);
	//printf("enqueued pcb1\n");
	q_enqueue(theScheduler->created, newPCB2);
	//printf("PCBs enqueued\n");
	//}
	//printf("Making New PCBs: \r\n");
	if (newPCBCount) {
		//printf("q_is_empty: %d\r\n", q_is_empty(theScheduler->created));
		while (!q_is_empty(theScheduler->created)) {
			PCB nextPCB = q_dequeue(theScheduler->created);
			//toStringPCB(nextPCB, 0);
			nextPCB->state = STATE_READY;
			//printf("\r\n");
			pq_enqueue(theScheduler->ready, nextPCB);
		}
		//printf("\r\n");
		
		//toStringPriorityQueue(theScheduler->ready);
		if (theScheduler->isNew) {
			//printf("Dequeueing PCB ");
			//printf("\r\nNEW!!\r\n");
			//toStringPCB(pq_peek(theScheduler->ready), 0);
			//printf("\r\n\r\n");
			theScheduler->running = pq_dequeue(theScheduler->ready);
			if (theScheduler->running) {
				theScheduler->running->state = STATE_RUNNING;
			}
			theScheduler->isNew = 0;
			currQuantumSize = theScheduler->ready->queues[0]->quantum_size;
		}
	}
	
	return newPCBCount;
}


/*
	Given the roleType of the newly created PCBs, the corresponding global role 
	type count variable will be incremented. Results will be printed at the end of 
	program execution. See documentation in pcb.c chooseRole() function for 
	more details on percentage for each role type count.
*/
void incrementRoleCount (enum pcb_type roleType) {
	switch (roleType) {
		case COMP:
			compCount++;
			break;
		case IO:
			ioCount++;
			break;
		case PAIR:
			pairCount++;
			break;
		case SHARED:
			sharedCount++;
			break;
	}
}


/*
	Creates a random number between 3000 and 4000 and adds it to the current PC.
	It then returns that new PC value.
*/
unsigned int runProcess (unsigned int pc, int quantumSize) {
	//quantumSize is the difference in time slice length between
	//priority levels.
	unsigned int jump;
	if (quantumSize != 0) {
		jump = rand() % quantumSize;
	}
	
	pc += jump;
	return pc;
}


/*
	Marks the running PCB as terminated. This means it will increment its term_count, if the
	term_count is then over its maximum terminate amount, then it will be enqueued into the
	Killed queue which will empty when it reaches its TOTAL_TERMINATED size.
*/
void terminate(Scheduler theScheduler) {
	if(theScheduler->running != NULL && theScheduler->running->terminate > 0 && theScheduler->running->terminate == theScheduler->running->term_count)
	{
		printf("Marking for termination...\r\n");
		theScheduler->running->state = STATE_HALT;
		printf("...\r\n");
		scheduling(IS_TERMINATING, theScheduler);	
	}
	
}


/*
	This acts as an Interrupt Service Routine, but only for the Timer interrupt.
	It handles changing the running PCB state to Interrupted, moving the running
	PCB to interrupted, saving the PC to the SysStack and calling the scheduler.
*/
void pseudoISR (Scheduler theScheduler, int interruptType) {
	if (theScheduler->running && theScheduler->running->state != STATE_HALT) {
		theScheduler->running->state = STATE_INT;
		theScheduler->interrupted = theScheduler->running;
	}
	scheduling(interruptType, theScheduler);
	//printf("finished scheduling\n");
	//printf("entering IRET\n");
	pseudoIRET(theScheduler);
	//printf("finished IRET\n");
	printf("Exiting ISR\n");
}


/*
	Prints the state of the Scheduler. Mostly this consists of the MLFQ, the next
	highest priority PCB in line, the one that will be run on next iteration, and
	the current list of "privileged PCBs" that will not be terminated.
*/
void printSchedulerState (Scheduler theScheduler) {
	
	printf("MLFQ State\r\n");
	toStringPriorityQueue(theScheduler->ready);
	printf("\r\n");
	
	int index = 0;
	// PRIVILIGED PID
	while(privileged[index] != NULL && index < MAX_PRIVILEGE) {
		printf("PCB PID %d, PRIORITY %d, PC %d\n", 
		privileged[index]->pid, privileged[index]->priority, 
		privileged[index]->context->pc);
		index++;
	}
	printf("blocked size: %d\r\n", theScheduler->blocked->size);
	printf("killed size: %d\r\n", theScheduler->killed->size);
	printf("killedMutexes size: %d\r\n", theScheduler->killedMutexes->size);
	printf("\r\n");
	
	if (pq_peek(theScheduler->ready) != NULL) {
		printf("Going to be running ");
		if (theScheduler->running) {
			toStringPCB(theScheduler->running, 0);
		} else {
			printf("\r\n");
		}
		printf("Next highest priority PCB ");
		toStringPCB(pq_peek(theScheduler->ready), 0);
		printf("\r\n\r\n\r\n");
	} else {
		
		if (theScheduler->running != NULL) {
			printf("Going to be running ");
			toStringPCB(theScheduler->running, 0);
		} else {
			printf("\r\n");
		}

		printf("Next highest priority PCB contents: The MLFQ is empty!\r\n");
		printf("\r\n\r\n\r\n");
	}
}


/*
	Used to move every value in the MLFQ back to the highest priority
	ReadyQueue after a predetermined time. It does this by taking the first value
	of each ReadyQueue (after the 0 *highest priority* queue) and setting it to
	be the new last value of the 0 queue.
*/
void resetMLFQ (Scheduler theScheduler) {
	int allEmpty = 1;
	for (int i = 1; i < NUM_PRIORITIES; i++) {
		ReadyQueue curr = theScheduler->ready->queues[i];
		if (!q_is_empty(curr)) {
			if (!q_is_empty(theScheduler->ready->queues[0])) {
				theScheduler->ready->queues[0]->last_node->next = curr->first_node;
				theScheduler->ready->queues[0]->last_node = curr->last_node;
				theScheduler->ready->queues[0]->size += curr->size;
				allEmpty = 0;
			} else {
				theScheduler->ready->queues[0]->first_node = curr->first_node;
				theScheduler->ready->queues[0]->last_node = curr->last_node;
				theScheduler->ready->queues[0]->size = curr->size;
			}
			resetReadyQueue(curr);
		}
	}
	
	if (allEmpty) {
		theScheduler->isNew = 1;
	}
}


/*
	Resets the given ReadyQueue to be empty.
*/
void resetReadyQueue (ReadyQueue queue) {
	ReadyQueueNode ptr = queue->first_node;
	while (ptr) {
		ptr->pcb->priority = 0;
		ptr = ptr->next;
	}
	queue->first_node = NULL;
	queue->last_node = NULL;
	queue->size = 0;
}


/*
	If the interrupt that occurs was a Timer interrupt, it will simply set the 
	interrupted PCBs state to Ready and enqueue it into the Ready queue. If it is
	an IO Trap, then it will put the running PCB into the Blocked queue. If it is
	an IO Interrupt, then it will take the top of the Blocked queue and enqueue it
	back into the MLFQ. If it is a termination, then the running PCB will be marked 
	as such and, if its term_count is greater than its maximum terminate amount, will 
	be enqueued into the Killed queue. Then, if the Killed queue is at or above its own 
	TOTAL_TERMINATED size, it will be emptied. It then calls the dispatcher to get the 
	next PCB in the queue.
*/
void scheduling (int interrupt_code, Scheduler theScheduler) {
	//Mutex currMutex;
	
	if (interrupt_code == IS_TIMER) {
		printf("Entering Timer Interrupt\r\n");
		theScheduler->interrupted->state = STATE_READY;
		if (theScheduler->interrupted->priority < (NUM_PRIORITIES - 1)) {
			theScheduler->interrupted->priority++;
		} else {
			theScheduler->interrupted->priority = 0;
		}
		printf("\r\nEnqueueing into MLFQ\r\n");
		toStringPCB(theScheduler->running, 0);
		//printf("after1\n");
		
		pq_enqueue(theScheduler->ready, theScheduler->interrupted);
		//printf("done enqueueing\n");
		//toStringPriorityQueue(theScheduler->ready);
		//printf("ENQ!\r\n");
		int index = isPrivileged(theScheduler->running);
		
		if (index != 0) {
			privileged[index] = theScheduler->running;
		}
		printf("Exiting Timer Interrupt\r\n");
	}
	else if (interrupt_code == IS_IO_TRAP)
	{
		// Do I/O trap handling
		printf("Entering IO Trap\r\n");
		int timer = (rand() % TIMER_RANGE + 1);
		theScheduler->interrupted->blocked_timer = timer;
		theScheduler->interrupted->state = STATE_WAIT;
		printf("\r\nEnqueueing into Blocked queue\r\n");
		toStringPCB(theScheduler->interrupted, 0);
		//printf("after\n");
		//exit(0);
		//printf("going to enqueue\n");
		q_enqueue(theScheduler->blocked, theScheduler->interrupted);
		//printf("done enqueueing\n");
		theScheduler->interrupted = NULL;
		
		// schedule a new process
		printf("Exiting IO Trap\r\n");
	}
	else if (interrupt_code == IS_IO_INTERRUPT)
	{
		printf("Entering IO Interrupt\r\n");
		// Do I/O interrupt handling
		printf("\r\nEnqueueing into MLFQ from Blocked queue\r\n");
		toStringPCB(q_peek(theScheduler->blocked), 0);
		pq_enqueue(theScheduler->ready, q_dequeue(theScheduler->blocked));
		//printSchedulerState(theScheduler);
		if (theScheduler->interrupted != NULL)
		{
			theScheduler->running = theScheduler->interrupted;
			theScheduler->running->state = STATE_RUNNING;
		
			sysstack = theScheduler->running->context->pc;
		}
		theScheduler->interrupted = NULL;
		printf("Exiting IO Interrupt\r\n");
	}
	
	if (theScheduler->interrupted != NULL && theScheduler->interrupted->state == STATE_HALT) {
		printf("entering handleKilledQueueInsertion\n");
		handleKilledQueueInsertion(theScheduler);
		printf("finished handleKilledQueueInsertion\n");
	}
	
	// I/O interrupt does not require putting a new process
	// into the running state, so we ignore this.
	if(interrupt_code != IS_IO_INTERRUPT)
	{
		//this was calling pq_peek, why?
		theScheduler->running = pq_dequeue(theScheduler->ready);
	}
	
	if (theScheduler->killed->size >= TOTAL_TERMINATED) {
		handleKilledQueueEmptying(theScheduler);
	}

	// I/O interrupt does not require putting a new process
	// into the running state, so we ignore this.
	if(interrupt_code != IS_IO_INTERRUPT) 
	{
		//printf("entering dispatcher\n");
		dispatcher(theScheduler);
		//printf("exited dispatcher\n");
	}
}


/*
	This simply gets the next ready PCB from the Ready queue and moves it into the
	running state of the Scheduler.
*/
void dispatcher (Scheduler theScheduler) {
	if (pq_peek(theScheduler->ready) != NULL && pq_peek(theScheduler->ready)->state != STATE_HALT) {
		//printf("inside dispatcher\n");
		currQuantumSize = getNextQuantumSize(theScheduler->ready);
		theScheduler->running = pq_dequeue(theScheduler->ready);
		theScheduler->running->state = STATE_RUNNING;
		theScheduler->interrupted = NULL;
	}
}


/*
	This simply sets the running PCB's PC to the value in the SysStack;
*/
void pseudoIRET (Scheduler theScheduler) {
	if (theScheduler->running != NULL) {
		theScheduler->running->context->pc = sysstack;
	}
}


/*
	This will construct the Scheduler, along with its numerous ReadyQueues and
	important PCBs.
*/
Scheduler schedulerConstructor () {
	Scheduler newScheduler = (Scheduler) malloc (sizeof(struct scheduler));
	newScheduler->created = q_create();
	newScheduler->killed = q_create();
	newScheduler->blocked = q_create();
	newScheduler->killedMutexes = q_create();
	newScheduler->mutexes = create_mutx_map();
	newScheduler->ready = pq_create();
	newScheduler->running = NULL;
	newScheduler->interrupted = NULL;
	newScheduler->isNew = 1;
	
	return newScheduler;
}


/*
	This will do the opposite of the constructor with the exception of 
	the interrupted PCB which checks for equivalancy of it and the running
	PCB to see if they are pointing to the same freed process (so the program
	doesn't crash).
*/
void schedulerDeconstructor (Scheduler theScheduler) {
	//printf("\r\n");
	if (theScheduler) {
		
		if (theScheduler->ready) {
			//printf("destroying ready\n");
			//toStringPriorityQueue(theScheduler->ready);
			pq_destroy(theScheduler->ready);
		}
		
		if (theScheduler->created) {
			//printf("destroying created\n");
			q_destroy(theScheduler->created);
		}		
		
		if (theScheduler->killed) {
			//printf("destroying killed\n");
			q_destroy(theScheduler->killed);
		}
		
		if (theScheduler->blocked) {
			//printf("destroying blocked\n");
			q_destroy(theScheduler->blocked);
		}
		
		if (theScheduler->killedMutexes) {
			//printf("destroying killedMutexes\n");
			q_destroy_m(theScheduler->killedMutexes);
			//printf("destroyed killedMutexes\n");
		}
		
		if (theScheduler->mutexes) {
			//destroy mutex map
		}
		
		if (theScheduler->running) {
			//printf("destroying running\n");
			PCB_destroy(theScheduler->running);
		}
		
		if (theScheduler->interrupted && theScheduler->running && theScheduler->interrupted == theScheduler->running) {
			//printf("destroying interrupted\n");
			PCB_destroy(theScheduler->interrupted);
		}
		
		//printf("destroying scheduler\n");
		free (theScheduler);
	}
	
	displayRoleCountResults();
}


void displayRoleCountResults() {
	printf("\r\nTOTAL ROLE TYPES: %d\r\n\r\n", (compCount + ioCount + pairCount + sharedCount));
	
	printf("COMP: \t%d\r\n", compCount);
	printf("IO: \t%d\r\n", ioCount);
	printf("PAIR: \t%d\r\n", pairCount);
	printf("SHARED: %d\r\n", sharedCount);
}

int isPrivileged(PCB pcb) {
	if (pcb != NULL) {
		for (int i = 0; i < 4; i++) {
			if (privileged[i] == pcb) {
				return i;
			}	
		}
	}
	
	return 0;	
}


//normal main
// void main () {
	// setvbuf(stdout, NULL, _IONBF, 0);
	// srand((unsigned) time(&t));
	// sysstack = 0;
	// switchCalls = 0;
	// currQuantumSize = 0;
	
	// initialize pcb type counts
	// compCount = 0;
	// ioCount = 0;
	// pairCount = 0;
	// sharedCount = 0;
	
	// osLoop();
// }


// lock\unlock tests
void main () {
	setvbuf(stdout, NULL, _IONBF, 0);
	srand((unsigned) time(&t));
	
	int outerLoop = 100;
	int innerLoop = 300;
	int count = 0;
	int mutexCount = rand() % MAX_MUTEX_IN_ROUND;
	if (!mutexCount) {
		mutexCount++;
	}
	Scheduler scheduler = schedulerConstructor();
	
	for (int i = 0; i < outerLoop; i++) {
		makePCBList(scheduler);
	}
	while (count < 30000) {
		if (scheduler->running->role == PAIR || scheduler->running->role == SHARED) {
			printf("the running mutex_R1_id: %d\n", scheduler->running->mutex_R1_id);
			useMutex (scheduler);
		}
		scheduler->running->context->pc++;
		
		if (count % 6 == 0) {
			pq_enqueue(scheduler->ready, scheduler->running);
			scheduler->running = pq_dequeue(scheduler->ready);
		}
		count++;
	}
	
	schedulerDeconstructor(scheduler);
}


/*
	If the Running PCB is of role PAIR or SHARED, then this function is called. What this
	does is handle the locking, unlocking, signalling, and waiting for the Mutexes and 
	their ConditionVariables. If it's determined to be a lock or unlock operation, it will 
	return a 1 or 2 from the respective isLockPC or isUnlockPC saying which of the two SHARED 
	resource the PC is found within, so we know which sharedMutex to look for in the MutexMap.
	
	If a Mutex tries to lock, but the Mutex is already locked, it will simply stall the Mutex.
	Meaning, it will simply perform a simple context switch on the running PCB and put it back 
	into the MLFQ and set the running to the MLFQ dequeue. This happens for lock and wait.
	
	Returns 1 if a context switched happen so the osLoop knows to start over, otherwise 0.
*/
int useMutex (Scheduler thisScheduler) {	
	printf("the running mutex_R1_id: %d\n", thisScheduler->running->mutex_R1_id);
	int wait = 0, signal = 0; 
	int lock = isLockPC(thisScheduler->running->context->pc, thisScheduler->running);
	int unlock = isUnlockPC(thisScheduler->running->context->pc, thisScheduler->running);
	printf("the running mutex_R1_id: %d\n", thisScheduler->running->mutex_R1_id);
	
	if (thisScheduler->running->role = PAIR) {
		if (thisScheduler->running->isProducer) {
			signal = isSignalPC(thisScheduler->running->context->pc, thisScheduler->running);
		} else {
			wait = isWaitPC(thisScheduler->running->context->pc, thisScheduler->running);
		}
	}
	printf("the running mutex_R1_id: %d\n", thisScheduler->running->mutex_R1_id);
	
	Mutex currMutex = NULL;
	
	if (lock) {
		printf("lock   the running mutex_R1_id: %d\n", thisScheduler->running->mutex_R1_id);
		printPCLocations(thisScheduler->running->lockR1);
		printPCLocations(thisScheduler->running->lockR2);
		if (lock == 1) { //is mutex_R1_id
			printf("lock   the running mutex_R1_id: %d\n", thisScheduler->running->mutex_R1_id);
			currMutex = get_mutx(thisScheduler->mutexes, thisScheduler->running->mutex_R1_id);
		} else { //is mutex_R2_id
			currMutex = get_mutx(thisScheduler->mutexes, thisScheduler->running->mutex_R2_id);
		}
		
		if (currMutex) {
			mutex_lock (currMutex, thisScheduler->running);
			if (currMutex->hasLock != thisScheduler->running) {
				printf("M%d already locked, going to wait for unlock\r\n");
				pq_enqueue(thisScheduler->ready, thisScheduler->running);
				thisScheduler->running = pq_dequeue(thisScheduler->ready);
				return 1;
			} else {
				printf("M%d locked at PC %d\n", currMutex->mid, thisScheduler->running->context->pc);
			}
		} else {
			printf("\r\n\t\t\tcurrMutex was null!!!\r\n\r\n");
			exit(0);
		}
	} else if (unlock) {
		printf("unlock   the running mutex_R1_id: %d\n", thisScheduler->running->mutex_R1_id);
		if (unlock == 1) { //is mutex_R1_id
			currMutex = get_mutx(thisScheduler->mutexes, thisScheduler->running->mutex_R1_id);
		} else { //is mutex_R2_id
			currMutex = get_mutx(thisScheduler->mutexes, thisScheduler->running->mutex_R2_id);
		}
		
		if (currMutex) {
			mutex_unlock (currMutex, thisScheduler->running);
			printf("M%d unlocked at PC %d\n", currMutex->mid, thisScheduler->running->context->pc);
		} else {
			printf("\r\n\t\t\tcurrMutex was null!!!\r\n\r\n");
			exit(0);
		}
	} else if (signal) {
		printf("signal   the running mutex_R1_id: %d\n", thisScheduler->running->mutex_R1_id);
		currMutex = get_mutx(thisScheduler->mutexes, thisScheduler->running->mutex_R1_id);
		
		if (currMutex) {
			cond_var_signal (currMutex->condVar);
			printf("M%d condition variable signalled at PC %d\n", currMutex->mid, thisScheduler->running->context->pc);
		} else {
			printf("\r\n\t\t\tcurrMutex was null!!!\r\n\r\n");
			exit(0);
		}
	} else if (wait) {
		printf("wait   the running mutex_R1_id: %d\n", thisScheduler->running->mutex_R1_id);
		currMutex = get_mutx(thisScheduler->mutexes, thisScheduler->running->mutex_R1_id);
		
		if (currMutex) {
			int isWaiting = cond_var_wait (currMutex->condVar);
			if (isWaiting) {
				pq_enqueue(thisScheduler->ready, thisScheduler->running);
				thisScheduler->running = pq_dequeue(thisScheduler->ready);
				printf("M%d condition variable waiting at PC %d\n", currMutex->mid, thisScheduler->running->context->pc);
				return 1;
			} else { //this part resets the condition variable so we don't need to keep making a new one
				cond_var_init(currMutex->condVar);
			}
		} else {
			printf("\r\n\t\t\tcurrMutex was null!!!\r\n\r\n");
			exit(0);
		}
	} else {
		printf("\r\n\t\t\tSomething went wrong in useMutex!!!\r\n");
		exit(0);
	}
	
	return 0;
}


//killed tests
/*void main () {
	setvbuf(stdout, NULL, _IONBF, 0);
	srand((unsigned) time(&t));
	
	int outerLoop = 100;
	int innerLoop = 300;
	int mutexCount = rand() % MAX_MUTEX_IN_ROUND;
	if (!mutexCount) {
		mutexCount++;
	}
	Scheduler scheduler = schedulerConstructor();
	
	for (int i = 0; i < 150; i++) {
		makePCBList(scheduler);
	}
	
	
	printSchedulerState(scheduler);
	toStringMutexMap(scheduler->mutexes);
	printf("BEFORE KILLING^^\n");
	
	while (!pq_is_empty(scheduler->ready)) {
		PCB toRun = NULL;
		if (!(scheduler->running)) {
			toRun = pq_dequeue(scheduler->ready);
		}
		if (!(scheduler->running)) {
			scheduler->running = toRun;
		} else {
			printf("\r\nRunning is already P%d\r\n", scheduler->running->pid);
		}
		handleKilledQueueInsertion(scheduler);
		printf("Killed List: ");
		toStringReadyQueueMutexes(scheduler->killedMutexes);
		if (scheduler->killed->size >= TOTAL_TERMINATED) {
			handleKilledQueueEmptying(scheduler);
			printSchedulerState(scheduler);
			toStringMutexMap(scheduler->mutexes);
		}
		
	}
	
	printSchedulerState(scheduler);
	toStringMutexMap(scheduler->mutexes);
	printf("AFTER KILLING^^\n");
	
	schedulerDeconstructor(scheduler);
}*/


void handleKilledQueueInsertion (Scheduler theScheduler) {
	Mutex mutex1 = NULL, mutex2 = NULL;
	PCB found = NULL;
	
	if (theScheduler->running->role == PAIR || theScheduler->running->role == SHARED) {
		mutex1 = take_n_remove_from_mutx_map(theScheduler->mutexes, theScheduler->running->mutex_R1_id);
		mutex2 = take_n_remove_from_mutx_map(theScheduler->mutexes, theScheduler->running->mutex_R2_id);
		if (!mutex1) {
			printf("\r\n\t\t\tmutex1 was null! Tried to find M%d but it wasn't in the map!!!\r\n\r\n", theScheduler->running->mutex_R1_id);
			exit(0);
		}
		
		if (!mutex2) {
			printf("\r\n\t\t\tmutex2 was null! Tried to find M%d but it wasn't in the map!!!\r\n\r\n", theScheduler->running->mutex_R2_id);
			exit(0);
		}
		
		if (mutex1 && mutex2 && mutex1->pcb2 == theScheduler->running) { //if running is the pcb2 in the mutex, find the matching pcb1
			found = pq_remove_matching_pcb(theScheduler->ready, mutex1->pcb1);
		} else { //otherwise the running is pcb1, so find pcb2
			found = pq_remove_matching_pcb(theScheduler->ready, mutex1->pcb2);
		}
		
		q_enqueue(theScheduler->killed, theScheduler->running);
		if (found) { //if found is null then the partnering pcb is already in the killed queue
			q_enqueue(theScheduler->killed, found);
		}
		
		q_enqueue_m(theScheduler->killedMutexes, mutex1);
		q_enqueue_m(theScheduler->killedMutexes, mutex2);
	} else {
		q_enqueue(theScheduler->killed, theScheduler->running);
	}
	
	printf("Killed List: ");
toStringReadyQueue(theScheduler->killed);
	theScheduler->running = NULL;
}


void handleKilledQueueEmptying (Scheduler theScheduler) {
	
	//PCB emptying
	printf("Emptying killed PCB list\r\n");
	PCB toKill = NULL;
	if (!(theScheduler->killed)) {
		printf("\r\n\t\t\tkilled PCB queue is NULL!\r\n\r\n");
		exit(0);
	}
	
	if (theScheduler->killed && !q_is_empty(theScheduler->killed)) {
		toKill = q_dequeue(theScheduler->killed);
	}
	
	if (theScheduler->killed && !q_is_empty(theScheduler->killed)) {
		while(!q_is_empty(theScheduler->killed)) {
			//if (theScheduler->killed->size > 1) {
				//printf("pid to kill: P%d\n", toKill->pid);
			if (toKill) {
				printf("toKill: P%d\r\n", toKill->pid);
				PCB_destroy(toKill);
			} else {
				printf("\r\n\t\t\ttoKill PCB was NULL!\r\n\r\n");
				exit(0);
			}
				//printf("toKill == null: %d\n", (toKill == NULL));
				//printf("pid after kill: P%d\n\n", toKill->pid);
			//}
			printf("Killed List: ");
			toStringReadyQueue(theScheduler->killed);
			toKill = q_dequeue(theScheduler->killed);
		} 
	} else {
		if (toKill) {
			PCB_destroy(toKill);
		} else {
			printf("\r\n\t\t\ttoKill PCB was NULL!\r\n\r\n");
			exit(0);
		}
	}
	printf("After emptying\n");
	printf("Killed List: ");
	toStringReadyQueue(theScheduler->killed);
	printf("is killed PCB list empty? ");
	if (q_is_empty(theScheduler->killed)) {
		printf("true\r\n");
	} else {
		printf("false\r\n");
	}
	
	
	//Mutex emptying
	printf("Emptying killed MUTEX list\r\n");
	Mutex toKillMutex = NULL;
	if (!(theScheduler->killedMutexes)) {
		printf("\r\n\t\t\tkilled MUTEX queue is NULL!\r\n\r\n");
		exit(0);
	}
	
	if (theScheduler->killedMutexes && !q_is_empty(theScheduler->killedMutexes)) {
		printf("here\r\n");
		toKillMutex = q_dequeue_m(theScheduler->killedMutexes);
	}
	
	if (theScheduler->killedMutexes && !q_is_empty(theScheduler->killedMutexes)) {
		while(!q_is_empty(theScheduler->killedMutexes)) {
			//if (theScheduler->killed->size > 1) {
				//printf("pid to kill: P%d\n", toKill->pid);
			if (toKillMutex) {
				printf("toKillMutex: M%d\r\n", toKillMutex->mid);
				mutex_destroy(toKillMutex);
			} else {
				printf("\r\n\t\t\tin here toKill MUTEX was NULL!\r\n\r\n");
				exit(0);
			}
				//printf("toKill == null: %d\n", (toKill == NULL));
				//printf("pid after kill: P%d\n\n", toKill->pid);
			//}
			printf("Killed List: ");
			toStringReadyQueueMutexes(theScheduler->killedMutexes);
			toKillMutex = q_dequeue_m(theScheduler->killedMutexes);
		} 
	} else {
		if (toKillMutex) {
			mutex_destroy(toKillMutex);
		} else {
			if (!q_is_empty(theScheduler->killedMutexes)) {
				printf("\r\n\t\t\tor here toKill MUTEX was NULL!\r\n\r\n");
				exit(0);
			}
		}
	}
	
	printf("After emptying\n");
	printf("Killed List: ");
	toStringReadyQueueMutexes(theScheduler->killedMutexes);
	printf("is killed MUTEX list empty? ");
	if (q_is_empty(theScheduler->killedMutexes)) {
		printf("true\r\n");
	} else {
		printf("false\r\n");
	}
		//exit(0);
}

void toStringMutexTraps(PCB newPCB1, PCB newPCB2) {
		printf("PCB 1 ==================================\r\n");
			
			
		printf("mutex_1_traps lock\n");
		printf("lock pcb1\r\n");
		for (int i = 0; i < TRAP_COUNT; i++) {
			
			printf("%d ", newPCB1->lockR1[i]);
		}
		printf("unlock pcb1\r\n");
		for (int i = 0; i < TRAP_COUNT; i++) {
			
			printf("%d ", newPCB1->unlockR1[i]);
		}
		printf("\r\nmutex_2_traps lock\r\n");
		printf("lock2 pcb1\r\n");
		for (int i = 0; i < TRAP_COUNT; i++) {
		
			printf("%d ", newPCB1->lockR2[i]);
		}
		
		printf("lock2 pcb1\r\n");
		for (int i = 0; i < TRAP_COUNT; i++) {
			
			printf("%d ", newPCB1->unlockR2[i]);
		}
		printf("\n");
		
		
		printf("PCB 2 ==================================\r\n");
		
		
		printf("mutex_1_traps lock\n");
		printf("lock pcb2\r\n");
		for (int i = 0; i < TRAP_COUNT; i++) {
			
			printf("%d ", newPCB2->lockR1[i]);
		}
		
			printf("unlock pcb2\r\n");
		for (int i = 0; i < TRAP_COUNT; i++) {
		
			printf("%d ", newPCB2->unlockR1[i]);
		}
		printf("\r\nmutex_2_traps lock\r\n");
		printf("lock2 pcb2\r\n");
		for (int i = 0; i < TRAP_COUNT; i++) {
			
			printf("%d ", newPCB2->lockR2[i]);
		}
		printf("lock2 pcb2\r\n");
		
		for (int i = 0; i < TRAP_COUNT; i++) {
			
			printf("%d ", newPCB2->unlockR2[i]);
		}
	
	
}

void deadlockMonitor(Scheduler thisScheduler) {
	
	// PCB1 owns a lock, see if owns the other lock as well
	// if not, deadlock is detected
	
	Mutex mutex1;
	Mutex mutex2;
	
	mutex1 = get_mutx(thisScheduler->mutexes, thisScheduler->running->mutex_R1_id);
	
	
	mutex2 = get_mutx(thisScheduler->mutexes, thisScheduler->running->mutex_R2_id);
	
	if (mutex1->isLocked && (mutex1->hasLock == thisScheduler->running) && mutex2->isLocked && (mutex2->hasLock == thisScheduler->running)) {
		printf("NO DEADLOCK DETECTED FOR PROCESSES PID%d & PID%d\r\n", mutex1->pcb1->pid, mutex1->pcb2->pid);
	} else {
		printf("DEADLOCK DETECTED FOR PROCESSES PID%d & PID%d\r\n", mutex1->pcb1->pid, mutex1->pcb2->pid);
	}
}



