/************************************************************************

 This code forms the base of the operating system you will
 build.  It has only the barest rudiments of what you will
 eventually construct; yet it contains the interfaces that
 allow test.c and z502.c to be successfully built together.

 Revision History:
 1.0 August 1990
 1.1 December 1990: Portability attempted.
 1.3 July     1992: More Portability enhancements.
 Add call to SampleCode.
 1.4 December 1992: Limit (temporarily) printout in
 interrupt handler.  More portability.
 2.0 January  2000: A number of small changes.
 2.1 May      2001: Bug fixes and clear STAT_VECTOR
 2.2 July     2002: Make code appropriate for undergrads.
 Default program start is in test0.
 3.0 August   2004: Modified to support memory mapped IO
 3.1 August   2004: hardware interrupt runs on separate thread
 3.11 August  2004: Support for OS level locking
 4.0  July    2013: Major portions rewritten to support multiple threads
 4.20 Jan     2015: Thread safe code - prepare for multiprocessors
 ************************************************************************/

#include             "global.h"
#include             "syscalls.h"
#include             "protos.h"
#include             "string.h"
#include             <stdlib.h>
#include             <ctype.h>

#define              MAX_NUMBER_OF_USER_PROCESSES 15

#define				TRUE	1
#define				FALSE   0
#define DO_LOCK 1
#define DO_UNLOCK 0

#define LOCK_OF_READYQUEUE      MEMORY_INTERLOCK_BASE
#define LOCK_OF_TIMERQUEUE      MEMORY_INTERLOCK_BASE + 1
#define LOCK_OF_PCBLIST         MEMORY_INTERLOCK_BASE + 2
#define LOCK_OF_DISKQUEUE       MEMORY_INTERLOCK_BASE + 3
#define LOCK_OF_DISKQUEUE_BASE       MEMORY_INTERLOCK_BASE + 3
#define LOCK_OF_DISKQUEUE_0       MEMORY_INTERLOCK_BASE + 3 // to +10
#define LOCK_OF_DISKQUEUE_1       MEMORY_INTERLOCK_BASE + 4
#define LOCK_OF_DISKQUEUE_2       MEMORY_INTERLOCK_BASE + 5
#define LOCK_OF_DISKQUEUE_3       MEMORY_INTERLOCK_BASE + 6
#define LOCK_OF_DISKQUEUE_4       MEMORY_INTERLOCK_BASE + 7
#define LOCK_OF_DISKQUEUE_5       MEMORY_INTERLOCK_BASE + 8
#define LOCK_OF_DISKQUEUE_6       MEMORY_INTERLOCK_BASE + 9
#define LOCK_OF_DISKQUEUE_7       MEMORY_INTERLOCK_BASE + 10
//      ... we could define other explicit lock of diskQueue for different disks here
#define LOCK_OF_DISK			MEMORY_INTERLOCK_BASE + 11
#define LOCK_OF_DISK0           MEMORY_INTERLOCK_BASE + 11// to 18
#define LOCK_OF_PIDISUSED		MEMORY_INTERLOCK_BASE + 19
#define LOCK_OF_INODEISUSED		MEMORY_INTERLOCK_BASE + 20
#define LOCK_OF_INODETOSECTOR   MEMORY_INTERLOCK_BASE + 21
#define LOCK_OF_PROCESSFILEINFO MEMORY_INTERLOCK_BASE + 22
#define LOCK_OF_BITMAP          MEMORY_INTERLOCK_BASE + 23
#define LOCK_OF_REQUESTDISK     MEMORY_INTERLOCK_BASE + 24 // to 31
#define LOCK_OF_SPACE			MEMORY_INTERLOCK_BASE + 32
#define LOCK_OF_SUSPENDLIST		MEMORY_INTERLOCK_BASE + 33
#define LOCK_OF_RUNNINGLIST		MEMORY_INTERLOCK_BASE + 34
#define LOCK_OF_SP				MEMORY_INTERLOCK_BASE + 35
#define LOCK_OF_TERMINATELIST	MEMORY_INTERLOCK_BASE + 36

#define MAX_INODE 32

void timerInterruptHandler();
void diskInterruptHandler(int diskID);
void dispatcher();
void scheduler();
void printQueueStatus();
long startContext(long contextAddr);
void getInterruptInfo(MEMORY_MAPPED_IO* mmio);
long getCurrentContext();
long getCurrentClock();
long osOpenDir(int diskID, char* dirName);
long osCreateDir(char* dirName);

//  Allows the OS and the hardware to agree on where faults occur
extern void *TO_VECTOR[];

char *call_names[] = { "mem_read ", "mem_write", "read_mod ", "get_time ",
"sleep    ", "get_pid  ", "create   ", "term_proc", "suspend  ",
"resume   ", "ch_prior ", "send     ", "receive  ", "PhyDskRd ",
"PhyDskWrt", "def_sh_ar", "Format   ", "CheckDisk", "Open_Dir ",
"OpenFile ", "Crea_Dir ", "Crea_File", "ReadFile ", "WriteFile",
"CloseFile", "DirContnt", "Del_Dir  ", "Del_File " };


// Self defined global variables
int isRunningInMultiProcessorMode;
Queue* timerQueue;
Queue* readyQueue;
Queue* diskQueue;
Queue* PCBList;
Queue* runningList;
Queue*  diskQueueList[MAX_NUMBER_OF_DISKS];
Queue* suspendList;
Queue* terminatedList;

int pidIsUsed[MAX_NUMBER_OF_USER_PROCESSES];
char InodeIsUsed[MAX_INODE];
int InodeToSector[32];
int space[MAX_NUMBER_OF_DISKS];
int SPStatus; //scheduler printer status. 0 for no output, 1 for full output, 2 for limited output.
SP_INPUT_DATA* input;
/************************************************************************
BASIC OPERATIONS
Lists all the basic operation on Z502
************************************************************************/
void getInterruptInfo(MEMORY_MAPPED_IO* mmio) {
	mmio->Mode = Z502GetInterruptInfo;
	mmio->Field1 = mmio->Field2 = mmio->Field3 = mmio->Field4 = 0;
	MEM_READ(Z502InterruptDevice, mmio);
}
void clearInterruptStatus(long DeviceID) {
	MEMORY_MAPPED_IO mmio;
	mmio.Mode = Z502ClearInterruptStatus;
	mmio.Field1 = DeviceID;
	mmio.Field2 = mmio.Field3 = mmio.Field4 = 0;
	MEM_WRITE(Z502InterruptDevice, &mmio);
}
long getCurrentClock() {
	MEMORY_MAPPED_IO mmio;
	mmio.Field1 = mmio.Field2 = mmio.Field3 = mmio.Field4 = 0;
	MEM_READ(Z502Clock, &mmio);
	return mmio.Field1;
}
long getTimerStatus() {
	MEMORY_MAPPED_IO mmio;
	mmio.Mode = Z502Status;
	mmio.Field1 = mmio.Field2 = mmio.Field3 = mmio.Field4 = 0;
	MEM_READ(Z502Timer, &mmio);
	if (mmio.Field4 == ERR_SUCCESS) {
		return mmio.Field1;
	}
	else {
		printf("Error in getting the status of the timer!\n");
		return mmio.Field4;
	}
}
void setTimer(long timePeriod) {
	MEMORY_MAPPED_IO mmio;
	mmio.Mode = Z502Start;
	mmio.Field1 = timePeriod;   // You pick the time units
	mmio.Field2 = mmio.Field3 = mmio.Field4 = 0;	
	MEM_WRITE(Z502Timer, &mmio);
}
long getCurrentContext() {
	MEMORY_MAPPED_IO mmio;
	mmio.Mode = Z502GetCurrentContext;
	mmio.Field1 = mmio.Field2 = mmio.Field3 = mmio.Field4 = 0;
	MEM_READ(Z502Context, &mmio);
	if (mmio.Field4 == ERR_SUCCESS) {
		return mmio.Field1;
	}
	else {
		printf("Error in getting the current context!\n");
		return mmio.Field4;
	}
}
long startContext(long contextAddr) {
	MEMORY_MAPPED_IO mmio;
	mmio.Mode = Z502StartContext;
	mmio.Field1 = contextAddr;
	if (isRunningInMultiProcessorMode == 0) {
		mmio.Field2 = START_NEW_CONTEXT_AND_SUSPEND; //single processor mode		
	}
	else {
		mmio.Field2 = START_NEW_CONTEXT_ONLY; //multiple processor mode
	}
	mmio.Field3 = mmio.Field4 = 0;
	MEM_WRITE(Z502Context, &mmio);
	return mmio.Field4;
}
long suspendContext(long contextAddr) {
	MEMORY_MAPPED_IO mmio;
	mmio.Mode = Z502StartContext;
	mmio.Field1 = 0;
	mmio.Field2 = SUSPEND_CURRENT_CONTEXT_ONLY; 
	mmio.Field3 = mmio.Field4 = 0;
	MEM_WRITE(Z502Context, &mmio);
	return mmio.Field4;
}
void initializeContext(long contextAddr, long pageAddr, MEMORY_MAPPED_IO* mmio) {
	mmio->Mode = Z502InitializeContext;
	mmio->Field1 = 0;
	mmio->Field2 = contextAddr;
	mmio->Field3 = pageAddr;
	mmio->Field4 = 0;
	MEM_WRITE(Z502Context, mmio);
}
long getDiskStatus(long diskID) {
	MEMORY_MAPPED_IO mmio;
	mmio.Mode = Z502Status;
	mmio.Field1 = diskID;
	mmio.Field2 = mmio.Field3 = mmio.Field4 = 0;
	MEM_READ(Z502Disk, &mmio);
	if (mmio.Field4 == ERR_SUCCESS) {
		return mmio.Field2;
	}
	else {
		printf("Error in checking whether disk is busy!\n");
		return mmio.Field4;
	}
}
long writeToDisk(char* content, long diskID, long diskSector) {
	MEMORY_MAPPED_IO mmio;
	mmio.Mode = Z502DiskWrite;
	mmio.Field1 = diskID;
	mmio.Field2 = diskSector;
	mmio.Field3 = (long)content;
	mmio.Field4 = 0;
	//printf("%ld, %ld, %ld\n", mmio.Field1, mmio.Field2, mmio.Field3);
	MEM_WRITE(Z502Disk, &mmio);
	if (mmio.Field4 != ERR_SUCCESS) {
		printf("%ld, %ld, %ld\n", mmio.Field1, mmio.Field2, mmio.Field3);
		printf("Error in writing to disk. Error Code: %ld\n", mmio.Field4);
	}
	return mmio.Field4;
}
long readFromDisk(char* content, long diskID, long diskSector) {
	MEMORY_MAPPED_IO mmio;
	mmio.Mode = Z502DiskRead;
	mmio.Field1 = diskID;
	mmio.Field2 = diskSector;
	mmio.Field3 = (long)content;
	mmio.Field4 = 0;
	//printf("%ld, %ld, %ld\n", mmio.Field1, mmio.Field2, mmio.Field3);
	MEM_WRITE(Z502Disk, &mmio);
	if (mmio.Field4 != ERR_SUCCESS) {
		printf("%ld, %ld, %ld\n", mmio.Field1, mmio.Field2, mmio.Field3);
		printf("Error in reading from disk. Error Code: %ld\n", mmio.Field4);
	}
	return mmio.Field4;
}
void idle() {
	MEMORY_MAPPED_IO mmio;
	mmio.Mode = Z502Action; // set the system to Idle, waiting for interrupt
	mmio.Field1 = mmio.Field2 = mmio.Field3 = mmio.Field4 = 0;
	MEM_WRITE(Z502Idle, &mmio);
}
void halt() {
	MEMORY_MAPPED_IO mmio;
	int Success_Or_Failure_Returned;
	READ_MODIFY(LOCK_OF_TIMERQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_READYQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_DISKQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	destroyQueue(timerQueue);//no task to execute anymore, halt
	destroyQueue(readyQueue);
	destroyQueue(diskQueue);
	destroyQueue(PCBList);
	mmio.Mode = Z502Action;
	mmio.Field1 = mmio.Field2 = mmio.Field3 = mmio.Field4 = 0;
	MEM_WRITE(Z502Halt, &mmio);
}
/************************************************************************
 INTERRUPT_HANDLER
 When the Z502 gets a hardware interrupt, it transfers control to
 this routine in the OS.
 ************************************************************************/
void timerInterruptHandler() {
	//First, get current time
	long currentTime = getCurrentClock();

	//Second, start polling and enqueue them into ready queue
	QueueNode* now;
	ProcessInfomation* pinfo;

	int Success_Or_Failure_Returned;
	READ_MODIFY(LOCK_OF_TIMERQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_READYQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	while (QueueIsEmpty(timerQueue) == 0) {
		now = QueuePeekFirst(timerQueue);
		if (now->info->timeToGoOut > currentTime) {
			break;
		}
		QueueOfferLast(readyQueue, QueuePollFirst(timerQueue));
	}
	
	//reset the timer
	if (timerQueue->head != NULL) {
		currentTime = getCurrentClock();
		pinfo = (ProcessInfomation *)timerQueue->head->info;
		setTimer((long)pinfo->timeToGoOut - currentTime);

	}
	READ_MODIFY(LOCK_OF_READYQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_TIMERQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);

}

void diskInterruptHandler(int diskID) {
	int Success_Or_Failure_Returned;
	QueueNode* now;

	READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_READYQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	now = QueuePeekFirst(diskQueueList[diskID]);
	if (now->nodeType != NODE_TYPE_MISSIONCOMPLETE) {

		while (getDiskStatus(diskID) != DEVICE_FREE) {
			CALL(1);
		}

		if (getDiskStatus(diskID) == DEVICE_FREE) {
			if (now->nodeType == NODE_TYPE_DISKWRITE) {
				writeToDisk(now->info->content, diskID, now->info->diskSector);
			}
			else if (now->nodeType == NODE_TYPE_DISKREAD) {
				readFromDisk(now->info->content, diskID, now->info->diskSector);
			}
			now->nodeType = NODE_TYPE_MISSIONCOMPLETE;
		}
		else {
			//printf("There must be something wrong with the scheduling!\n");
		}
	}
	else {
		
		QueueOfferLast(readyQueue, QueuePollFirst(diskQueueList[diskID]));
		now = QueuePeekFirst(diskQueueList[diskID]);
		if (now != NULL) {
			if (now->nodeType != NODE_TYPE_MISSIONCOMPLETE) {
				if (getDiskStatus(diskID) == DEVICE_FREE) {
					if (now->nodeType == NODE_TYPE_DISKWRITE) {
						writeToDisk(now->info->content, diskID, now->info->diskSector);
					}
					else if (now->nodeType == NODE_TYPE_DISKREAD) {
						readFromDisk(now->info->content, diskID, now->info->diskSector);
					}
					now->nodeType = NODE_TYPE_MISSIONCOMPLETE;
				}
				else {
					// do nothing
				}
			}
		}

	}
	READ_MODIFY(LOCK_OF_READYQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);



	//int Success_Or_Failure_Returned;
	//QueueNode* now;
	//long err;

	//READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	//READ_MODIFY(LOCK_OF_READYQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	//QueueOfferLast(readyQueue, QueuePollFirst(diskQueueList[diskID]));
	//if (diskQueueList[diskID]->head != NULL) {
	//	err = getDiskStatus(diskID);
	//	now = diskQueueList[diskID]->head;
	//	if (err == DEVICE_FREE) {
	//		if (now->nodeType == NODE_TYPE_DISKWRITE) {
	//			writeToDisk(now->info->content, diskID, now->info->diskSector);
	//		}
	//		else if (now->nodeType == NODE_TYPE_DISKREAD) {
	//			readFromDisk(now->info->content, diskID, now->info->diskSector);
	//		}
	//	}
	//	else {
	//		printf("There must be something wrong with the scheduling! %ld\n", err);
	//		printf("%ld, %ld\n", now->info->diskSector, (long)now->info->content);
	//		//printQueueStatus();

	//	}
	//}

	//READ_MODIFY(LOCK_OF_READYQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	//READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);

	//int Success_Or_Failure_Returned;
	//QueueNode* now;
	//long err;

	//READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	//READ_MODIFY(LOCK_OF_READYQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	//QueueOfferLast(readyQueue, QueuePollFirst(diskQueueList[diskID]));
	//if (diskQueueList[diskID]->head != NULL) {
	//	err = getDiskStatus(diskID);
	//	now = diskQueueList[diskID]->head;

	//	if (err == DEVICE_FREE) {
	//		if (now->nodeType == NODE_TYPE_DISKWRITE) {
	//			writeToDisk(now->info->content, diskID, now->info->diskSector);
	//		}
	//		else if (now->nodeType == NODE_TYPE_DISKREAD) {
	//			readFromDisk(now->info->content, diskID, now->info->diskSector);
	//		}
	//	}
	//	else {
	//		printf("There must be something wrong with the scheduling! %ld\n", err);
	//		printf("%ld, %ld\n", now->info->diskSector, (long)now->info->content);
	//		//printQueueStatus();
	//	}
	//}

	//READ_MODIFY(LOCK_OF_READYQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	//READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	//printQueueStatus();
}

void InterruptHandler(void) {
	int Success_Or_Failure_Returned;
	INT32 DeviceID;
	INT32 Status;
	MEMORY_MAPPED_IO mmio;       // Enables communication with hardware
	// Get cause of interrupt
	getInterruptInfo(&mmio);
	//printf("\n%d, %d\n\n", DeviceID, Status);
	while (mmio.Field4 == ERR_SUCCESS) {
		DeviceID = mmio.Field1;
		Status = mmio.Field2;
		//printQueueStatus();
		if (DeviceID == TIMER_INTERRUPT) {
			//printf("Go to TimeInterruptHandler()\n");
			timerInterruptHandler();
		}
		else if (DeviceID >= DISK_INTERRUPT && DeviceID < ((DISK_INTERRUPT)+(MAX_NUMBER_OF_DISKS))) {
			int diskID = DeviceID - (DISK_INTERRUPT);
			//printf("Interrupt from disk %d!\n", diskID);
			//printf("Go to DiskInterruptHandler()!\n");
			diskInterruptHandler(diskID);
		}
		clearInterruptStatus(DeviceID);
		getInterruptInfo(&mmio);
	}
	// Clear out this device - we're done with it
	clearInterruptStatus(DeviceID);
}           // End of InterruptHandler


/************************************************************************
 FAULT_HANDLER
 The beginning of the OS502.  Used to receive hardware faults.
 ************************************************************************/

void FaultHandler(void) {
	INT32 DeviceID;
	INT32 Status;
	MEMORY_MAPPED_IO mmio;       // Enables communication with hardware

	// Get cause of interrupt
	mmio.Field1 = mmio.Field2 = mmio.Field3 = 0;
	mmio.Mode = Z502GetInterruptInfo;
	MEM_READ(Z502InterruptDevice, &mmio);
	DeviceID = mmio.Field1;
	Status = mmio.Field2;

	printf("Fault_handler: Found vector type %d with value %d\n", DeviceID,
		Status);
	// Clear out this device - we're done with it
	mmio.Mode = Z502ClearInterruptStatus;
	mmio.Field1 = DeviceID;
	MEM_WRITE(Z502InterruptDevice, &mmio);

} // End of FaultHandler

/************************************************************************
Function Component in SVC
************************************************************************/

/************************************************************************
Get time of the day
************************************************************************/
void osGetTimeOfTheDay(SYSTEM_CALL_DATA *SystemCallData, MEMORY_MAPPED_IO* mmio) {
	*(long *)SystemCallData->Argument[0] = getCurrentClock();
}

/************************************************************************
Create process
************************************************************************/
int getNewPID() {
	int err;
	READ_MODIFY(LOCK_OF_PIDISUSED, DO_LOCK, TRUE, &err);
	int pid = -1;
	for (int i = 0; i < MAX_NUMBER_OF_USER_PROCESSES; i++) {
		if (pidIsUsed[i] == FALSE) {
			pid = i;
			pidIsUsed[i] = TRUE;
			break;
		}
	}
	READ_MODIFY(LOCK_OF_PIDISUSED, DO_UNLOCK, TRUE, &err);
	return pid;
}
void osCreateProcess(SYSTEM_CALL_DATA *SystemCallData, MEMORY_MAPPED_IO* mmio) {
	//printQueueStatus();
	int total = QueueSize(PCBList);
	//printf("%d\n", total);
	if (total + 1 > MAX_NUMBER_OF_USER_PROCESSES) {
		printf("You are creating too many processes!\n");
		*(long *)SystemCallData->Argument[4] = ERR_BAD_PARAM;
		return;
	}
	if ((long)SystemCallData->Argument[2] < 0) {
		printf("Illegal Priority: %d\n", (long)SystemCallData->Argument[2]);
		*(long *)SystemCallData->Argument[4] = ERR_BAD_PARAM;
		return;
	}
	char* name = (char*)SystemCallData->Argument[0];
	//printf("%s\n", name);

	if (searchNameInQueue(PCBList, name) >= 0) {
		printf("The name is used twice: %s\n", name);
		*(long *)SystemCallData->Argument[4] = ERR_BAD_PARAM;
		return;
	}

	void *PageTable = (void *)calloc(2, NUMBER_VIRTUAL_PAGES);
	initializeContext((long)SystemCallData->Argument[1], (long)PageTable, mmio);//initialize this new Context Sequence  
	*(long *)SystemCallData->Argument[3] = mmio->Field1;//This field contains the ContextID of the new context
	*(long *)SystemCallData->Argument[4] = mmio->Field4;//Contains error value, or ERR_SUCCESS

	int pid = getNewPID();
	if (pid < 0) {
		printf("Error is assigning new PID\n");
	}

	ProcessInfomation* pinfo;
	QueueNode* currentProcess;
	int Success_Or_Failure_Returned;
	READ_MODIFY(LOCK_OF_READYQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	pinfo = createProcessInfomation2(name, mmio->Field1, 0);
	pinfo->pid = pid;
	currentProcess = createQueueNode(pinfo, NODE_TYPE_MISSIONCOMPLETE);
	QueueOfferLast(readyQueue, currentProcess);
	READ_MODIFY(LOCK_OF_READYQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	pinfo = createProcessInfomation2(name, mmio->Field1, 0);
	pinfo->pid = pid;
	currentProcess = createQueueNode(pinfo, NODE_TYPE_MISSIONCOMPLETE);
	QueueOfferLast(PCBList, currentProcess); //add the newly created process into PCBList
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
}

/************************************************************************
Get process id
************************************************************************/
void osGetProcessID(SYSTEM_CALL_DATA *SystemCallData, MEMORY_MAPPED_IO* mmio) {
	int index = -1;
	char* getname = (char*)SystemCallData->Argument[0];
	QueueNode* node;
	long currentContext;
	int Success_Or_Failure_Returned;
	if (strlen(getname) == 0) {
		currentContext = getCurrentContext();
		READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
		if ((index = searchProcessIDInQueue(PCBList, currentContext)) >= 0) {
			node = QueueGet(PCBList, index);
			*(long *)SystemCallData->Argument[1] = node->info->pid;
			*(long *)SystemCallData->Argument[2] = ERR_SUCCESS;
			//printf("Default: The process ID is %ld\n", node->info->contextAddr);
		}
		else {
			*(long *)SystemCallData->Argument[1] = -1;
			*(long *)SystemCallData->Argument[2] = ERR_BAD_PARAM;
		}
		READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	}
	else {
		READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
		if ((index = searchNameInQueue(PCBList, getname)) >= 0) {
			node = QueueGet(PCBList, index);
		}
		else {
			node = NULL;
		}
		READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
		if (node == NULL) {
			printf("No such process with name: %s\n", getname);
			//printQueueStatus();
			*(long *)SystemCallData->Argument[1] = -1;
			*(long *)SystemCallData->Argument[2] = ERR_BAD_PARAM;
		}
		else {
			ProcessInfomation* pcb = (ProcessInfomation*)node->info;
			*(long *)SystemCallData->Argument[1] = pcb->pid;
			*(long *)SystemCallData->Argument[2] = ERR_SUCCESS;
			//printf("The process ID is %ld\n", pcb->contextAddr);
		}
	}
}

/************************************************************************
Terminate process
************************************************************************/
void osTerminateProcess(SYSTEM_CALL_DATA *SystemCallData, MEMORY_MAPPED_IO* mmio) {
	if (isRunningInMultiProcessorMode == 1) {
		int index;
		int Success_Or_Failure_Returned;
		QueueNode* node;
		if ((long)SystemCallData->Argument[0] == -2) halt();
		if ((long)SystemCallData->Argument[0] < 0) {
			long currentContext = getCurrentContext();
			READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
			READ_MODIFY(LOCK_OF_RUNNINGLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
			READ_MODIFY(LOCK_OF_PIDISUSED, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
			index = searchProcessIDInQueue(PCBList, currentContext);
			node = QueueRemove(PCBList, index);
			index = searchProcessIDInQueue(runningList, currentContext);
			node = QueueRemove(runningList, index);
			pidIsUsed[node->info->pid] = FALSE;
			destroyQueueNode(node);

			if (QueueIsEmpty(PCBList) == TRUE) {
				halt();
			}
			READ_MODIFY(LOCK_OF_PIDISUSED, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
			READ_MODIFY(LOCK_OF_RUNNINGLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
			READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
		}
		scheduler();
		return;
	}
	int index;
	int Success_Or_Failure_Returned;
	if ((long)SystemCallData->Argument[0] == -2) halt();
	if ((long)SystemCallData->Argument[0] < 0) {
		long currentContext = getCurrentContext();
		READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
		READ_MODIFY(LOCK_OF_PIDISUSED, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
		if ((index = searchProcessIDInQueue(PCBList, currentContext)) >= 0) {
			QueueNode* node = QueueRemove(PCBList, index);
			if (node != NULL) {
				pidIsUsed[node->info->pid] = FALSE;
				READ_MODIFY(LOCK_OF_TERMINATELIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
				QueueOfferLast(terminatedList, node);
				READ_MODIFY(LOCK_OF_TERMINATELIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
			}
		}
		if (QueueIsEmpty(PCBList) == TRUE) {
			
			READ_MODIFY(LOCK_OF_PIDISUSED, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
			READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
			halt();
		}	
		READ_MODIFY(LOCK_OF_PIDISUSED, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
		READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
		//printQueueStatus();
		scheduler();
	}
	else {
		long processContext = (long)SystemCallData->Argument[0];
		*(long *)SystemCallData->Argument[1] = ERR_SUCCESS;
		QueueNode* n1;
		READ_MODIFY(LOCK_OF_TIMERQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
		READ_MODIFY(LOCK_OF_READYQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
		READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
		
		for (int i = 0; i < MAX_NUMBER_OF_DISKS; i++) {
			READ_MODIFY(LOCK_OF_DISKQUEUE + i, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
		}
		if ((index = searchProcessIDInQueue(PCBList, processContext)) >= 0) {
			//printf("\nBefore destory curr: %d\n", QueueSize(PCBList));
			READ_MODIFY(LOCK_OF_PIDISUSED, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
			n1 = QueueRemove(PCBList, index);
			pidIsUsed[n1->info->pid] = FALSE;
			READ_MODIFY(LOCK_OF_PIDISUSED, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
			READ_MODIFY(LOCK_OF_TERMINATELIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
			QueueOfferLast(terminatedList, n1);
			READ_MODIFY(LOCK_OF_TERMINATELIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
			//printf("After destory curr: %d\n", QueueSize(PCBList));		
			//printQueue(PCBList);
		}
		else {
			printf("No such process with id: %ld\n", processContext);
			*(long *)SystemCallData->Argument[1] = ERR_BAD_PARAM;
			READ_MODIFY(LOCK_OF_TIMERQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
			READ_MODIFY(LOCK_OF_READYQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
			READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
			for (int i = 0; i < MAX_NUMBER_OF_DISKS; i++) {
				READ_MODIFY(LOCK_OF_DISKQUEUE + i, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
			}
			return;
		}		

		if ((index = searchProcessIDInQueue(readyQueue, processContext)) >= 0) {
			n1 = QueueRemove(readyQueue, index);
			free(n1);
		}
		if ((index = searchProcessIDInQueue(timerQueue, processContext)) >= 0) {
			n1 = QueueRemove(timerQueue, index);
			free(n1);
		}
		for (int i = 0; i < MAX_NUMBER_OF_DISKS; i++) {
			if ((index = searchProcessIDInQueue(diskQueueList[i], processContext)) >= 0) {
				n1 = QueueRemove(diskQueueList[i], index);
				free(n1);
			}
		}		
		READ_MODIFY(LOCK_OF_TIMERQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
		READ_MODIFY(LOCK_OF_READYQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
		READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
		for (int i = 0; i < MAX_NUMBER_OF_DISKS; i++) {
			READ_MODIFY(LOCK_OF_DISKQUEUE + i, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
		}
	}
	//printQueueStatus();
}

/************************************************************************
System sleep
************************************************************************/
void osSleep(SYSTEM_CALL_DATA *SystemCallData, MEMORY_MAPPED_IO* mmio) {
	int Success_Or_Failure_Returned;
	long currentTime = getCurrentClock();
	long currentProcessAddr = getCurrentContext();

	//save the current process information and push it into the end of the timer queue
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	int index = searchProcessIDInQueue(PCBList, currentProcessAddr);
	char * name = { "" };
	int pid = 0;
	if (index >= 0) {
		QueueNode* pcb = QueueGet(PCBList, index);
		ProcessInfomation* pcbinfo = (ProcessInfomation *)pcb->info;
		name = pcbinfo->name;
		pid = pcbinfo->pid;
	}
	else {
		printf("Error in terminate!\n");
	}
	ProcessInfomation* pinfo = createProcessInfomation2(name, currentProcessAddr, currentTime + (long)SystemCallData->Argument[0]);
	pinfo->pid = pid;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	QueueNode* currentProcess = createQueueNode(pinfo, NODE_TYPE_TIMER);
	//printf("Current time is %ld. Sleep time is %ld. The record for time to go out of queue: %ld\n", currentTime, SystemCallData->Argument[0], pinfo->timeToGoOut);
	
	READ_MODIFY(LOCK_OF_TIMERQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	QueueInsert(timerQueue, currentProcess, NODE_TYPE_TIMER);	

	//start or reset the timer
	long DeviceStatus = getTimerStatus();
	int wakeupTime;
	if (mmio->Field1 == DEVICE_FREE) {
		setTimer((long)SystemCallData->Argument[0]);
		wakeupTime = currentTime + (long)SystemCallData->Argument[0];
		//printf("Set alarm at %ld\n", wakeupTime);
	}
	else {
		setTimer(timerQueue->head->info->timeToGoOut - currentTime);
		wakeupTime = currentTime + (timerQueue->head->info->timeToGoOut - currentTime);
		//printf("reset alarm at %ld\n", wakeupTime);
	}
	currentTime = getCurrentClock();
	//printf("Current time is: %ld. Next wakeup time is: %ld\n", currentTime, wakeupTime);
	//printQueueStatus();
	READ_MODIFY(LOCK_OF_TIMERQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	scheduler();
}




/***********************************************************************
Disk write
************************************************************************/

void osDiskWrite(int diskID, int sector, char* content) {
	//printf("Branch for write\n");
	/*int Success_Or_Failure_Returned;
	long diskID = (long)SystemCallData->Argument[0];
	long currentContext = getCurrentContext();
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	int index = searchProcessIDInQueue(PCBList, currentContext);
	char * name = { "" };
	int pid = 0;
	if (index >= 0) {
		QueueNode* pcb = QueueGet(PCBList, index);
		ProcessInfomation* pcbinfo = (ProcessInfomation *)pcb->info;
		name = pcbinfo->name;
		pid = pcbinfo->pid;
	}
	else {
		printf("Error in terminate!\n");
	}
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	ProcessInfomation* pinfo = createProcessInfomation3(name, currentContext, (char*)SystemCallData->Argument[2], (long)SystemCallData->Argument[0], (long)SystemCallData->Argument[1]);
	pinfo->pid = pid;
	pinfo->timeToGoOut = -1;//NA
	QueueNode* node = createQueueNode(pinfo, NODE_TYPE_DISKWRITE);


	READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_LOCK, TRUE, &Success_Or_Failure_Returned);	
	long errCode;
	if (getDiskStatus(diskID) == DEVICE_FREE) {
		errCode = writeToDisk((char*)SystemCallData->Argument[2], (long)SystemCallData->Argument[0], (long)SystemCallData->Argument[1]);
		node->nodeType = NODE_TYPE_MISSIONCOMPLETE;
		if (errCode != ERR_SUCCESS) {
			printf("Write failed. Error Code: %ld\n", errCode);
		}
		QueueOfferLast(diskQueueList[diskID], node);
	}
	else{
		node->nodeType = NODE_TYPE_DISKWRITE;
		QueueOfferLast(diskQueueList[diskID], node);
	}
	READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);

	scheduler();*/

	int Success_Or_Failure_Returned;
	long currentContext = getCurrentContext();
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	int index = searchProcessIDInQueue(PCBList, currentContext);
	char * name = { "" };
	int pid = 0;
	if (index >= 0) {
		QueueNode* pcb = QueueGet(PCBList, index);
		ProcessInfomation* pcbinfo = (ProcessInfomation *)pcb->info;
		name = pcbinfo->name;
		pid = pcbinfo->pid;
	}
	else {
		printf("Error!\n");
	}
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	ProcessInfomation* pinfo = createProcessInfomation3(name, currentContext, content, diskID, sector);
	pinfo->pid = pid;
	pinfo->timeToGoOut = -1;//NA
	QueueNode* node = createQueueNode(pinfo, NODE_TYPE_DISKWRITE);

	READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_LOCK, TRUE, &Success_Or_Failure_Returned);	

	long errCode;
	if (getDiskStatus(diskID) == DEVICE_FREE) {
		node->nodeType = NODE_TYPE_MISSIONCOMPLETE; //just added

		QueueOfferLast(diskQueueList[diskID], node);

		errCode = writeToDisk(content, diskID, sector);
		if (errCode != ERR_SUCCESS) {
			printf("Write failed. Error Code: %ld\n", errCode);
		}

	}
	else{
		QueueOfferLast(diskQueueList[diskID], node);
	}	

	READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	scheduler();
}

/***********************************************************************
Disk read
************************************************************************/

void osDiskRead(int diskID, int sector, char* content) {
	//printf("branch for read\n");
	/*int Success_Or_Failure_Returned;
	int diskID = (long)SystemCallData->Argument[0];
	long currentContext = getCurrentContext();

	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	int index = searchProcessIDInQueue(PCBList, currentContext);
	char * name = { "" };
	int pid = 0;
	if (index >= 0) {
		QueueNode* pcb = QueueGet(PCBList, index);
		ProcessInfomation* pcbinfo = (ProcessInfomation *)pcb->info;
		name = pcbinfo->name;
		pid = pcbinfo->pid;
	}
	else {
		printf("Error in terminate!\n");
	}
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);

	ProcessInfomation* pinfo = createProcessInfomation3(name, currentContext, (char*)SystemCallData->Argument[2], (long)SystemCallData->Argument[0], (long)SystemCallData->Argument[1]);
	pinfo->pid = pid;
	pinfo->timeToGoOut = -1;//NA
	QueueNode* node = createQueueNode(pinfo, NODE_TYPE_DISKREAD);
	

	READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	long errCode;
	if (getDiskStatus(diskID) == DEVICE_FREE) {
		errCode = readFromDisk((char*)SystemCallData->Argument[2], (long)SystemCallData->Argument[0], (long)SystemCallData->Argument[1]);
		//printf("---------------%s, %ld\n", (char*)SystemCallData->Argument[2], (long)SystemCallData->Argument[2]);
		node->nodeType = NODE_TYPE_MISSIONCOMPLETE;
		if (errCode != ERR_SUCCESS) {
			printf("Read failed. Error Code: %ld\n", errCode);
		}
		QueueOfferLast(diskQueueList[diskID], node);
	}
	else {
		node->nodeType = NODE_TYPE_DISKREAD;
		QueueOfferLast(diskQueueList[diskID], node);
	}
	READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);

	scheduler();*/


	//int Success_Or_Failure_Returned;
	//long currentContext = getCurrentContext();

	//READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	//int index = searchProcessIDInQueue(PCBList, currentContext);
	//char * name = { "" };
	//int pid = 0;
	//if (index >= 0) {
	//	QueueNode* pcb = QueueGet(PCBList, index);
	//	ProcessInfomation* pcbinfo = (ProcessInfomation *)pcb->info;
	//	name = pcbinfo->name;
	//	pid = pcbinfo->pid;
	//}
	//else {
	//	printf("Error in terminate!\n");
	//}
	//READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);

	//ProcessInfomation* pinfo = createProcessInfomation3(name, currentContext, content, diskID, sector);
	//pinfo->pid = pid;
	//pinfo->timeToGoOut = -1;//NA
	//QueueNode* node = createQueueNode(pinfo, NODE_TYPE_DISKREAD);


	//READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_LOCK, TRUE, &Success_Or_Failure_Returned);

	//long errCode;
	//if (getDiskStatus(diskID) == DEVICE_FREE) {
	//	node->nodeType = NODE_TYPE_MISSIONCOMPLETE; //just added
	//	QueueOfferLast(diskQueueList[diskID], node);

	//	errCode = readFromDisk(content, diskID, sector);
	//	//printf("---------------%s, %ld\n", (char*)SystemCallData->Argument[2], (long)SystemCallData->Argument[2]);
	//	if (errCode != ERR_SUCCESS) {
	//		printf("Read failed. Error Code: %ld\n", errCode);
	//	}
	//	
	//}
	//else {
	//	QueueOfferLast(diskQueueList[diskID], node);
	//}
	//
	//READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);

	//scheduler();

	int Success_Or_Failure_Returned;
	long currentContext = getCurrentContext();

	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	int index = searchProcessIDInQueue(PCBList, currentContext);
	char * name = { "" };
	int pid = 0;
	if (index >= 0) {
		QueueNode* pcb = QueueGet(PCBList, index);
		ProcessInfomation* pcbinfo = (ProcessInfomation *)pcb->info;
		name = pcbinfo->name;
		pid = pcbinfo->pid;
	}
	else {
		printf("Error in terminate!\n");
	}
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);

	ProcessInfomation* pinfo = createProcessInfomation3(name, currentContext, content, diskID, sector);
	pinfo->pid = pid;
	pinfo->timeToGoOut = -1;//NA
	QueueNode* node = createQueueNode(pinfo, NODE_TYPE_DISKREAD);

	READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	
	long errCode;
	if (getDiskStatus(diskID) == DEVICE_FREE) {
		node->nodeType = NODE_TYPE_MISSIONCOMPLETE; //just added
		QueueOfferLast(diskQueueList[diskID], node);

		errCode = readFromDisk(content, diskID, sector);
		//printf("---------------%s, %ld\n", (char*)SystemCallData->Argument[2], (long)SystemCallData->Argument[2]);
		if (errCode != ERR_SUCCESS) {
			printf("Read failed. Error Code: %ld\n", errCode);
		}

	}
	else {
		QueueOfferLast(diskQueueList[diskID], node);
	}
	
	READ_MODIFY(LOCK_OF_DISKQUEUE + diskID, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);

	scheduler();

}

/***********************************************************************
Disk format
************************************************************************/

void charToBinaryString(unsigned char a, char* binary) {
	for (int i = 0; i < 8; i++) {
		binary[i] = a % 2 + '0';
		a = a / 2;
	}
}

unsigned char binaryStringToChar(char* binary) {
	unsigned char a = 0;
	int pow = 1;
	for (int i = 0; i < 8; i++) {
		a += binary[i] * pow;
		pow *= 2;
	}
	return a;
}

//char convert(char input) {//convert from LSB to MSB
//	char byte[8];
//	for (int i = 0; i < 8; i++) {
//		byte[i] = input % 2;
//		input = input / 2;
//	}
//
//
//}
void printByte(char* binary) {
	for (int i = 0; i < 8; i++) {
		printf("%c", binary[i]);
	}
	printf("\n");
}

int getNewInode() {
	int err;
	READ_MODIFY(LOCK_OF_INODEISUSED, DO_LOCK, TRUE, &err);
	int Inode = -1;
	for (int i = 0; i < MAX_INODE; i++) {
		if (InodeIsUsed[i] == FALSE) {
			Inode = i;
			InodeIsUsed[i] = TRUE;
			break;
		}
	}
	READ_MODIFY(LOCK_OF_INODEISUSED, DO_UNLOCK, TRUE, &err);
	return Inode;
}

int isValidName(char* name) {
	for (int i = 0; i < 7; i++) {
		if (name[i] == '\0') {
			return TRUE;
		}
	}
	if (name[7] == '\0') {
		return TRUE;
	}
	return FALSE;
}

/****************************************************************************
NUMBER_LOGICAL_SECTORS    (short)2048
DISK_LENGTH = 2048
BITMAP_SIZE = 2048 bits = 256 bytes = 16 sectors
In the filed bitmap size, value should be 16 / 4 = 4

ROOT_SIZE 2 sector

SWAP_SIZE 8 sector, 8 / 4 = 2

DISK_LENGTH = 2048 LSBit[0 0 0 0 0 0 0 0 | 0 0 0 1 0 0 0 0]MSBit LSByte 0 MSByte [0 0 0 0 1 0 0 0] = 8 : 8 * 256 + 0

BITMAP_LOCATION 1, just begin at next block (1 - 16)

ROOTDIR_LOCATION 25, (25 - 26)

SWAP_LOCATION 17, (17 - 24)

RESERVED '\0'

Sector 0        Disk Header
Sector 1 - 16   Bitmap
Sector 17 - 24  swap
Sector 25 - 26  Root directory
*****************************************************************************/
#define SWAP_SIZE 8
#define SWAP_LOCATION 17
#define ROOTDIR_LOCATION 25
#define SWAP_LOCATION 17
#define BITMAP_SIZE 16
#define BITMAP_LOCATION 1

/************************************************************************
Format the disk
************************************************************************/
long osDiskFormat(int diskID) {

	if (diskID < 0 || diskID >= MAX_NUMBER_OF_DISKS) {
		printf("Invalid disk number!\n");
		return ERR_BAD_PARAM;
	}
	char byte[8];
	char header[PGSIZE];

	//initialize header for the diskID sector 0
	header[0] = diskID;
	header[1] = 4; //Bitmap Size
	header[2] = 2; //RootDir Size
	header[3] = 2; //Swap Size
	header[4] = 0; //Disk length LSB
	header[5] = 8; //Disk length MSB
	header[6] = 1; // Bitmap Location LSB
	header[7] = 0; // Bitmpa Location MSB
	header[8] = 25; //RootDir Location LSB
	header[9] = 0; //RootDir Location MSB
	header[10] = 17; //Swap Location LSB
	header[11] = 0; //Swap Location MSB 
	header[12] = '\0'; // RESERVED
	header[13] = '\0'; // RESERVED
	header[14] = '\0'; // RESERVED
	header[15] = '\0'; // RESERVED

	osDiskWrite(diskID, 0, header);


	for (int i = 0; i < MAX_INODE; i++) {
		InodeToSector[i] = 0;
		InodeIsUsed[i] = FALSE;
	}
	int inode = 31; //thus the parent inode of root is the inode of itself
	InodeToSector[inode] = ROOTDIR_LOCATION;
	InodeIsUsed[inode] = TRUE;

	//initialize root directory (header) sector 25
	memset(header, '\0', 16);
	header[0] = (unsigned char)inode; // Inode 1 byte
	memcpy(header + 1, "root", 5); // filename 7 bytes
	byte[7] = 1; byte[6] = 1; byte[5] = 1; byte[4] = 1; byte[3] = 1; byte[2] = 0; byte[1] = 1; byte[0] = 1;
	header[8] = binaryStringToChar(byte);
	//printf("%d", (unsigned char)header[8]);
	long currentTime = getCurrentClock();
	header[9] = (unsigned char)(currentTime % 256);
	currentTime = currentTime / 256;
	header[10] = (unsigned char)(currentTime % 256);
	currentTime = currentTime / 256;
	header[11] = (unsigned char)(currentTime % 256);
	header[12] = ROOTDIR_LOCATION + 1;  //next sector 
	header[13] = 0;
	header[14] = 0; //empty file
	header[15] = 0;
	osDiskWrite(diskID, ROOTDIR_LOCATION, header);

	//initialize root directory (index) sector 26
	memset(header, '\0', 16);
	osDiskWrite(diskID, ROOTDIR_LOCATION + 1, header);

	//initialize swap 
	for (int i = 0; i < SWAP_SIZE; i++) {
		memset(header, '\0', 16);
		osDiskWrite(diskID, SWAP_LOCATION + i, header);
	}

	//initialize bitmap 1-16
	memset(header, '\0', 16);
	header[0] = (unsigned char)0xFF; //0 - 7
	header[1] = (unsigned char)0xFF; //8 - 15
	header[2] = (unsigned char)0xFF; //16 - 23
	header[3] = (unsigned char)0xE0; //24 - 26
	osDiskWrite(diskID, BITMAP_LOCATION, header);
	for (int i = 1; i < BITMAP_SIZE; i++) {
		memset(header, '\0', 16);
		osDiskWrite(diskID, BITMAP_LOCATION + i, header);
	}

	int err;
	READ_MODIFY(LOCK_OF_SPACE, DO_LOCK, TRUE, &err);
	space[diskID] = 27;
	READ_MODIFY(LOCK_OF_SPACE, DO_UNLOCK, TRUE, &err);
	return ERR_SUCCESS;
}

/************************************************************************
Check disk : a basic z502 operation
************************************************************************/
long osDiskCheck(int diskID) {
	MEMORY_MAPPED_IO mmio;
	mmio.Mode = Z502CheckDisk;
	mmio.Field1 = (long)diskID;
	mmio.Field2 = mmio.Field3 = mmio.Field4 = 0;
	MEM_READ(Z502Disk, &mmio);
	return mmio.Field4;
}

/*
find directory under the current directory
output the header sector of that directory
output -1 when do not find
*/
int stringcmp(char* a, char* b) {
	int len = strlen(b);
	for (int i = 0; i < len; i++) {
		if (a[i] > b[i]) {
			return 1;
		}
		else if (a[i] < b[i]) {
			return -1;
		}
	}
	return 0;
}

int isOccupied(int sector, dir* currentDir) {
	int bitmapSectorOffset = sector / 128;
	int bitByte = (sector % 128) / 8;
	int offsetInByte = (sector % 128) - 8 * bitByte;
	char bitmap[PGSIZE];
	osDiskRead(currentDir->diskID, bitmapSectorOffset + BITMAP_LOCATION, bitmap);
	if (((bitmap[bitByte] >> offsetInByte) & 1) == 1) {
		return TRUE;
	}
	return FALSE;
}

void setOccupied(int sector, dir* currentDir) {
	int bitmapSectorOffset = sector / 128;
	int bitByte = (sector % 128) / 8;
	int offsetInByte = (sector % 128) - 8 * bitByte;
	char bitmap[PGSIZE];
	//int err;
	//READ_MODIFY(LOCK_OF_BITMAP, DO_LOCK, TRUE, &err);
	osDiskRead(currentDir->diskID, bitmapSectorOffset + BITMAP_LOCATION, bitmap);
	bitmap[bitByte] = (bitmap[bitByte] | (1<<offsetInByte));
	osDiskWrite(currentDir->diskID, bitmapSectorOffset + BITMAP_LOCATION, bitmap);
	//READ_MODIFY(LOCK_OF_BITMAP, DO_UNLOCK, TRUE, &err);
}

void setUnoccupied(int sector, dir* currentDir) {
	int bitmapSectorOffset = sector / 128;
	int bitByte = (sector % 128) / 8;
	int offsetInByte = (sector % 128) - 8 * bitByte;
	char bitmap[PGSIZE];
//	int err;
	//READ_MODIFY(LOCK_OF_BITMAP, DO_LOCK, TRUE, &err);
	osDiskRead(currentDir->diskID, bitmapSectorOffset + BITMAP_LOCATION, bitmap);
	bitmap[bitByte] = (bitmap[bitByte] & ~(1 << offsetInByte));
	osDiskWrite(currentDir->diskID, bitmapSectorOffset + BITMAP_LOCATION, bitmap);
	//READ_MODIFY(LOCK_OF_BITMAP, DO_UNLOCK, TRUE, &err);
}

void getAllBitmaps(dir* currentDir, char* allBitmaps) {
	char buffer[PGSIZE];
	for (int i = 0; i < BITMAP_SIZE; i++) {
		osDiskRead(currentDir->diskID, BITMAP_LOCATION + i, buffer);
		memcpy(allBitmaps + i * PGSIZE, buffer, PGSIZE);
	}
}

void setAllBitmaps(dir* currentDir, char* allBitmaps) {
	char buffer[PGSIZE];
	for (int i = 0; i < BITMAP_SIZE; i++) {
		memcpy(buffer, allBitmaps + i * PGSIZE, PGSIZE);
		osDiskWrite(currentDir->diskID, BITMAP_LOCATION + i, buffer);
	}
}

int findNexEmptySectorAndSet(dir* currentDir, char* allBitmaps) {
	int emptySpace = -1;
	for (int i = 0; i < BITMAP_SIZE; i++) {
		for (int j = 0; j < PGSIZE; j++) {
			if ((unsigned char)allBitmaps[i * PGSIZE + j] != 0xFF) {
				for (int k = 0; k < 8; k++) {
					if ((allBitmaps[i * PGSIZE + j] & (1 << (7 - k))) == 0) {
						emptySpace = i * 128 + j * 8 + k;
						allBitmaps[i * PGSIZE + j] = (allBitmaps[i * PGSIZE + j] | (1 << (7 - k)));
						break;
					}
				}
				if (emptySpace >= 0) {
					break;
				}
			}
		}
		if (emptySpace >= 0) {
			break;
		}
	}
	//printf("%d\n", emptySpace);
	return emptySpace;

	//int emptySpace = -1;
	//int err;
	////READ_MODIFY(LOCK_OF_BITMAP, DO_LOCK, TRUE, &err);
	//char bitmap[PGSIZE];
	//for (int i = 0; i < BITMAP_SIZE; i++) {
	//	osDiskRead(currentDir->diskID, BITMAP_LOCATION + i, bitmap);
	//	for (int j = 0; j < PGSIZE; j++) {
	//		//printf("%02x, %d\n", (unsigned char)bitmap[j], (unsigned char)(bitmap[j]));
	//		if (bitmap[j] != 0xFF) {
	//			//printf("%02x, %d: ", (unsigned char)bitmap[j], (unsigned char)(bitmap[j]));
	//			for (int k = 0; k < 8; k++) {
	//				//printf(" | %02x, %d | ", (unsigned char)bitmap[j] & (1 << k), (unsigned char)bitmap[j] & (1 << k));
	//				//printf(" %d, ", (unsigned char)bitmap[j] & (1 << k));
	//				if ((bitmap[j] & (1 << (7 -k))) == 0) {
	//					emptySpace = i * 128 + j * 8 + k;
	//					printf("%d\n", emptySpace);
	//					bitmap[j] = (bitmap[j] | (1 << (7 - k)));
	//					osDiskWrite(currentDir->diskID, BITMAP_LOCATION + i, bitmap);
	//					break;
	//				}
	//			}
	//			//printf("\n");
	//		}
	//		if (emptySpace >= 0) {
	//			break;
	//		}
	//	}
	//	if (emptySpace >= 0) {
	//		break;
	//	}
	//}
	////READ_MODIFY(LOCK_OF_BITMAP, DO_UNLOCK, TRUE, &err);
	//return emptySpace;

	//int emptySpace = -1;
	//int err;
	//READ_MODIFY(LOCK_OF_SPACE, DO_LOCK, TRUE, &err);
	//emptySpace = space[currentDir->diskID];
	//if (emptySpace >= NUMBER_LOGICAL_SECTORS) {
	//	emptySpace = -1;
	//	printf("Disk is full!\n");
	//}
	//space[currentDir->diskID]++;
	//READ_MODIFY(LOCK_OF_SPACE, DO_UNLOCK, TRUE, &err);

	//if (emptySpace >= 0) {
	//	int bitmapSectorOffset = emptySpace / 128;
	//	int bitByte = (emptySpace % 128) / 8;
	//	int offsetInByte = (emptySpace % 128) - 8 * bitByte;
	//	char bitmap[PGSIZE];
	//	int err;
	//	//READ_MODIFY(LOCK_OF_BITMAP, DO_LOCK, TRUE, &err);
	//	osDiskRead(currentDir->diskID, bitmapSectorOffset + BITMAP_LOCATION, bitmap);
	//	bitmap[bitByte] = (bitmap[bitByte] | (1 << (7 - offsetInByte)));
	//	osDiskWrite(currentDir->diskID, bitmapSectorOffset + BITMAP_LOCATION, bitmap);
	//}

	//return emptySpace;
}

void showBitMap(char* allBitmaps) {
	for (int i = 0; i < 1; i++) {
		for (int j = 0; j < PGSIZE; j++) {
			printf("%02x  ", (unsigned char)allBitmaps[i * PGSIZE + j]);
		}
		printf("\n");
	}	
}

void showCurrentDir(dir* currentDir) {
	if (currentDir->isSet == TRUE) {
		printf("%s, %d, %02X\n", currentDir->dirName, currentDir->diskID, currentDir->sector);
	}
	else {
		printf("current directory is not set.\n");
	}
}
	

void printBuffer(char buffer[PGSIZE]) {
	printf("     ");
	for (int j = 0; j < PGSIZE; j++) {
		printf("%02X ", (unsigned char)buffer[j]);
	}
	printf("\n");
}

int makeEmptyIndexLevels(int level, dir* currentDir, char* allBitmaps) {
	if (level < 1) {
		printf("Wrong level!\n");
		return -1;
	}
	char buffer[PGSIZE];
	if (level == 1) {
		memset(buffer, '\0', PGSIZE);
		int newSector = findNexEmptySectorAndSet(currentDir, allBitmaps);
		if (newSector < 0) {
			return -1; //disk is full
		}
		osDiskWrite(currentDir->diskID, newSector, buffer);
		//setOccupied(newSector, currentDir);
		return newSector;
	}
	int newSector = findNexEmptySectorAndSet(currentDir, allBitmaps);
	int nextLevelSector;
	for (int i = 0; i < PGSIZE; i += 2) {
		nextLevelSector = makeEmptyIndexLevels(level - 1, currentDir, allBitmaps);
		buffer[i] = nextLevelSector % 256;
		buffer[i + 1] = nextLevelSector / 256;
	}
	osDiskWrite(currentDir->diskID, newSector, buffer);
	return newSector;
}
/************************************************************************
Find directory or file under the current directory
return -1 if not find
return the header sector if find
************************************************************************/
int findDirOrFile(char* dirName, int sector, int level, int dirOrFile, dir* currentDir) { // 1 for dir, 0 for file
	if (level < 0) {
		printf("Wrong level for finding Dir or File!\n");
		return -1;
	}
	char buffer[PGSIZE];
	if (level == 0) {
		if (sector == 0) {
			return -1;
		}
		osDiskRead(currentDir->diskID, sector, buffer);
		//printBuffer(buffer);
		//printf("%d\n", strlen(dirName));
		if (stringcmp(buffer + 1, dirName) == 0 && (buffer[8] & 1) == dirOrFile) {
			return sector;
		}
		else {
			return -1;
		}
	}
	int nextSector;
	int dir;
	osDiskRead(currentDir->diskID, sector, buffer);
	//printBuffer(buffer);
	for (int i = 0; i < PGSIZE; i += 2) {
		nextSector = (unsigned char)buffer[i] + (unsigned char)buffer[i + 1] * 256;
		//printf("nextSector: %d\n", nextSector);
		if (nextSector != 0) {
			dir = findDirOrFile(dirName, nextSector, level - 1, dirOrFile, currentDir);
			if (dir >= 0) {
				return dir;
			}
		}
	}
	return -1;
}

/************************************************************************
Find an empty sector in index
return -1 if not find
return the sector if find
************************************************************************/
int findNextEmptyPositionInIndex(int currSector, int parentSector, int level, dir* currentDir) {
	if (level < 0) {
		printf("Wrong level in findNextEmptyPositionInIndex()!\n");
		return -1;
	}
	if (level == 0) {
		if (currSector == 0) {
			return parentSector;
		}
		else {
			return -1;
		}
	}

	char buffer[PGSIZE];
	osDiskRead(currentDir->diskID, currSector, buffer);
	//printBuffer(buffer);
	int nextSector;
	int sectorNum;
	for (int i = 0; i < PGSIZE; i += 2) {
		nextSector = (unsigned)buffer[i] + (unsigned)buffer[i + 1] * 256;
		if ((sectorNum = findNextEmptyPositionInIndex(nextSector, currSector, level - 1, currentDir)) >= 0) {
			return sectorNum;
		}
	}
	return -1;
}

/************************************************************************
Expand the index level and making more spaces for index
************************************************************************/
void expandIndex(int parent_location, int index_location, int index_level, dir* currentDir, char* allBitmaps) {
	if (index_level < 0) {
		printf("Wrong level in findNextEmptyPositionInIndex()!\n");
		return;
	}
	char buffer[PGSIZE];
	if (index_level == 0) {
		int newLevel = findNexEmptySectorAndSet(currentDir, allBitmaps);
		//setOccupied(newLevel, currentDir);
		//printf("%02X, %02X\n", parent_location, newLevel);

		memset(buffer, '\0', PGSIZE);
		buffer[0] = index_location % 256;
		buffer[1] = index_location / 256;
		osDiskWrite(currentDir->diskID, newLevel, buffer);

		osDiskRead(currentDir->diskID, parent_location, buffer);
		buffer[12] = newLevel % 256;
		buffer[13] = newLevel / 256;
		buffer[8] = (unsigned)buffer[8] + 2; // level++
		//printBuffer(buffer);
		osDiskWrite(currentDir->diskID, parent_location, buffer);
		//osDiskRead(currentDir->diskID, parent_location, buffer);
		//printBuffer(buffer);
		return;
	}

	int newLevel = findNexEmptySectorAndSet(currentDir, allBitmaps);
	//setOccupied(newLevel, currentDir);
	buffer[0] = index_location % 256;
	buffer[1] = index_location / 256;
	int nextLevelSector;
	for (int i = 2; i < PGSIZE; i += 2) {
		nextLevelSector = makeEmptyIndexLevels(index_level, currentDir, allBitmaps);
		buffer[i] = nextLevelSector % 256;
		buffer[i + 1] = nextLevelSector / 256;
	}
	osDiskWrite(currentDir->diskID, newLevel, buffer);

	osDiskRead(currentDir->diskID, parent_location, buffer);
	buffer[12] = newLevel % 256;
	buffer[13] = newLevel / 256;
	buffer[8] = (unsigned)buffer[8] + 2; // level++
	osDiskWrite(currentDir->diskID, parent_location, buffer);
}



long osOpenDir(int diskID, char* dirName) {
	//showCurrentDir();
	int err;

	if (strlen(dirName) > 7) {
		printf("dirName too long!\n");
		return ERR_BAD_PARAM;
	}
	//printf("%s, %d\n", dirName, strcmp(dirName, "root"));

	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &err);
	int index = searchProcessIDInQueue(PCBList, getCurrentContext());
	QueueNode* currNode = QueueGet(PCBList, index);
	dir* currentDir = currNode->info->currentDir;
	stack* dirStack = currNode->info->dirStack;
	//printf("%d\n", currNode->info->pid);
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &err);


	if (currentDir->isSet == FALSE) {
		//printf("%s, %d\n", dirName, strcmp(dirName, "root"));
		if (diskID < 0 || diskID >= MAX_NUMBER_OF_DISKS) {
			return ERR_BAD_PARAM;
		}
		if (strcmp(dirName, "root") == 0) {			
			memset(currentDir->dirName, '\0', 7);
			memcpy(currentDir->dirName, "root", 5);
			currentDir->diskID = diskID;
			currentDir->sector = ROOTDIR_LOCATION;
			currentDir->isSet = TRUE;
			dir* newdir = createDir(currentDir->diskID, currentDir->sector, currentDir->dirName, currentDir->isSet);
			push(dirStack, createStackItem(newdir));
			//("%d\n", stackSize(dirStack));
			return ERR_SUCCESS;
		}
		else { 
			printf("No such directory!\n");
			return ERR_BAD_PARAM;
		}
	}
	else {
		if (diskID != -1 && diskID != currentDir->diskID) {
			printf("Wrong disk ID!\n");
			return ERR_BAD_PARAM;
		}
		if (strcmp(dirName, "..") == 0) {
			if (stackSize(dirStack) == 1) {
				printf("Already in root!\n");
				return ERR_BAD_PARAM;
			}
			currentDir = pop(dirStack)->dir;
			currentDir->isSet = TRUE;
			return ERR_SUCCESS;
		}
		//printf("hello!\n");
		
		//READ_MODIFY(LOCK_OF_DISK + currentDir, DO_LOCK, TRUE, &err);
		//char buffer[PGSIZE];
		char header[PGSIZE];
		//printf("%d, %d\n", currentDir->diskID, currentDir->sector);
		osDiskRead(currentDir->diskID, currentDir->sector, header);
		
		
		int index_location = (unsigned)header[13] * 256 + (unsigned)header[12];
		int index_level = ((unsigned)(header[8] >> 1)) % 4;
		int dir_location = findDirOrFile(dirName, index_location, index_level, 1, currentDir);
		//printf("%d\n",dir_location);
		if (dir_location < 0) {
			//create dir and update dir_location
			//if cannot create dir, return error
			osCreateDir(dirName);

		}
		//printf("%d\n", dir_location);
		osDiskRead(currentDir->diskID, currentDir->sector, header);
		//printBuffer(header);
		index_location = (unsigned)header[13] * 256 + (unsigned)header[12];
		index_level = ((unsigned)(header[8] >> 1)) % 4;
		dir_location = findDirOrFile(dirName, index_location, index_level, 1, currentDir);
		memset(currentDir->dirName, '\0', 7);
		memcpy(currentDir->dirName, dirName, strlen(dirName));
		//currentDir->diskID = diskID; // diskID should not be changed
		currentDir->sector = dir_location;
		currentDir->isSet = TRUE;
		dir* newdir = createDir(currentDir->diskID, currentDir->sector, currentDir->dirName, currentDir->isSet);
		push(dirStack, createStackItem(newdir));
		//showCurrentDir();
		//READ_MODIFY(LOCK_OF_DISK + currentDir, DO_UNLOCK, TRUE, &err);
		return ERR_SUCCESS;
	}
	return ERR_SUCCESS;
}

int getCurrentDisk() {
	int err;
	int diskID;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &err);
	int index = searchProcessIDInQueue(PCBList, getCurrentContext());
	QueueNode* currNode = QueueGet(PCBList, index);
	diskID = currNode->info->currentDir->diskID;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &err);
	return diskID;
}

long osCreateDir(char* dirName) {
	if (strlen(dirName) > 7) {
		return ERR_BAD_PARAM;
	}
	
	char allBitmaps[BITMAP_SIZE * PGSIZE];

	int err;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &err);
	int index = searchProcessIDInQueue(PCBList, getCurrentContext());
	QueueNode* currNode = QueueGet(PCBList, index);
	dir* currentDir = currNode->info->currentDir;
	stack* dirStack = currNode->info->dirStack;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &err);

	
	getAllBitmaps(currentDir, allBitmaps);

	char header[PGSIZE];
	osDiskRead(currentDir->diskID, currentDir->sector, header);
	int index_location = (unsigned)header[13] * 256 + (unsigned)header[12];
	int index_level = ((unsigned)(header[8] >> 1)) % 4;

	//printf("%d, %d\n", index_location, index_level);

	int dir_location = findDirOrFile(dirName, index_location, index_level, 1, currentDir);

	//printf("%d\n", dir_location);

	if (dir_location >= 0) {
		//printf("Directory already exist!\n");
		return ERR_BAD_PARAM;
	}

	//showBitMap(currentDir);
	int newSector = findNexEmptySectorAndSet(currentDir, allBitmaps);
	//setOccupied(newSector, currentDir);
	//printf("%d\n", newSector);
	//showBitMap(currentDir);

	if (newSector < 0) {
		return ERR_BAD_PARAM; // disk is full;
	}

	char buffer[PGSIZE];
	memset(buffer, '\0', PGSIZE);
	buffer[0] = getNewInode();
	//printf("%d\n", buffer[0]);
	for (int i = 0; i < strlen(dirName); i++) {
		buffer[1 + i] = dirName[i];
	}
	//memcpy(buffer + 1, dirName, strlen(dirName) + 1);
	//printf("%02x\n", header[0]);
	buffer[8] = (unsigned char)(header[0] << 3) + 1; //directory with index level 0
	//printf("%02x\n", (unsigned char)buffer[8]);
	long currTime = getCurrentClock();
	//printf("%ld", currTime);
	buffer[9] = currTime % 256;
	currTime = currTime / 256;
	buffer[10] = currTime % 256;
	currTime = currTime / 256;
	buffer[11] = currTime / 256;
	memset(buffer + 12, '\0', 4); // no index, directory size zero
	osDiskWrite(currentDir->diskID, newSector, buffer);
	READ_MODIFY(LOCK_OF_INODETOSECTOR, DO_LOCK, TRUE, &err);
	InodeToSector[buffer[0]] = newSector;
	READ_MODIFY(LOCK_OF_INODETOSECTOR, DO_UNLOCK, TRUE, &err);

	//int data;
	if (index_level == 0) {
		if (index_location == 0) {
			osDiskRead(currentDir->diskID, currentDir->sector, header);
			header[12] = (unsigned char)newSector % 256;
			header[13] = (unsigned char)newSector / 256;
			osDiskWrite(currentDir->diskID, currentDir->sector, header);
			setAllBitmaps(currentDir, allBitmaps);
			return ERR_SUCCESS;
		}
	}

	dir_location = findNextEmptyPositionInIndex(index_location, currentDir->sector, index_level, currentDir);

	//printf("%d\n", dir_location);

	if (dir_location < 0 && index_level == 3) {
		return ERR_BAD_PARAM; //index is full
	}

	if (dir_location < 0) {
		expandIndex(currentDir->sector, index_location, index_level, currentDir, allBitmaps);

		index_level++;

		osDiskRead(currentDir->diskID, currentDir->sector, header);
		index_location = (unsigned)header[13] * 256 + (unsigned)header[12];
		index_level = ((unsigned)(header[8] >> 1)) % 4;
		dir_location = findNextEmptyPositionInIndex(index_location, currentDir->sector, index_level, currentDir);
	}

	osDiskRead(currentDir->diskID, dir_location, buffer);
	for (int i = 0; i < PGSIZE; i += 2) {
		if (buffer[i] == 0 && buffer[i + 1] == 0) {
			buffer[i] = newSector % 256;
			buffer[i + 1] = newSector / 256;
			break;
		}
	}
	osDiskWrite(currentDir->diskID, dir_location, buffer);

	setAllBitmaps(currentDir, allBitmaps);

	return ERR_SUCCESS;
}

long osCreateFile(char* fileName) {
	if (strlen(fileName) > 7) {
		return ERR_BAD_PARAM;
	}

	char allBitmaps[BITMAP_SIZE * PGSIZE];

	int err;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &err);
	int index = searchProcessIDInQueue(PCBList, getCurrentContext());
	QueueNode* currNode = QueueGet(PCBList, index);
	dir* currentDir = currNode->info->currentDir;
	stack* dirStack = currNode->info->dirStack;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &err);

	getAllBitmaps(currentDir, allBitmaps);

	char header[PGSIZE];
	osDiskRead(currentDir->diskID, currentDir->sector, header);
	int index_location = (unsigned)header[13] * 256 + (unsigned)header[12];
	int index_level = ((unsigned)(header[8] >> 1)) % 4;

	//printf("%02X, %d\n", index_location, index_level);

	int file_location = findDirOrFile(fileName, index_location, index_level, 0, currentDir);

	//printf("file_location: %d\n", file_location);

	if (file_location >= 0) {
		//printf("Directory already exist!\n");
		return ERR_BAD_PARAM;
	}

	//READ_MODIFY(LOCK_OF_BITMAP, DO_LOCK, TRUE, &err);

	int newSector = findNexEmptySectorAndSet(currentDir, allBitmaps);

	if (newSector < 0) {
		//READ_MODIFY(LOCK_OF_BITMAP, DO_UNLOCK, TRUE, &err);
		return ERR_BAD_PARAM; // disk is full;
	}

	char buffer[PGSIZE];
	memset(buffer, '\0', PGSIZE);
	buffer[0] = getNewInode();
	//printf("%d\n", buffer[0]);
	//memcpy(buffer + 1, fileName, strlen(fileName));
	for (int i = 0; i < strlen(fileName); i++) {
		buffer[1 + i] = fileName[i];
	}
	//printf("%02x\n", header[0]);
	buffer[8] = (unsigned char)(header[0] << 3) + 0; //directory with index level 0
	//printf("%02x\n", (unsigned char)buffer[8]);
	long currTime = getCurrentClock();
	//printf("%ld", currTime);
	buffer[9] = currTime % 256;
	currTime = currTime / 256;
	buffer[10] = currTime % 256;
	currTime = currTime / 256;
	buffer[11] = currTime / 256;
	memset(buffer + 12, '\0', 4); // no index, directory size zero
	osDiskWrite(currentDir->diskID, newSector, buffer);
	READ_MODIFY(LOCK_OF_INODETOSECTOR, DO_LOCK, TRUE, &err);
	InodeToSector[buffer[0]] = newSector;
	READ_MODIFY(LOCK_OF_INODETOSECTOR, DO_UNLOCK, TRUE, &err);

	//printf("%02X, %02X, %02X\n", currentDir->sector, index_location, newSector);
	int data;
	if (index_level == 0) {
		if (index_location == 0) {
			osDiskRead(currentDir->diskID, currentDir->sector, header);
			header[12] = (unsigned char)newSector % 256;
			header[13] = (unsigned char)newSector / 256;
			osDiskWrite(currentDir->diskID, currentDir->sector, header);
			setAllBitmaps(currentDir, allBitmaps);
			return ERR_SUCCESS;
		}
	}

	file_location = findNextEmptyPositionInIndex(index_location, currentDir->sector, index_level, currentDir);

	//printf("%d\n", file_location);

	if (file_location < 0 && index_level == 3) {
		return ERR_BAD_PARAM; //index is full
	}

	if (file_location < 0) {
		expandIndex(currentDir->sector, index_location, index_level, currentDir, allBitmaps);

		osDiskRead(currentDir->diskID, currentDir->sector, header);
		index_location = (unsigned)header[13] * 256 + (unsigned)header[12];
		index_level = ((unsigned)(header[8] >> 1)) % 4;

		//printf("%02X, %02X, %d\n", index_location, currentDir->sector, index_level);
		file_location = findNextEmptyPositionInIndex(index_location, currentDir->sector, index_level, currentDir);
	}
	//printf("%d\n", file_location);

	

	osDiskRead(currentDir->diskID, file_location, buffer);
	for (int i = 0; i < PGSIZE; i += 2) {
		if (buffer[i] == 0 && buffer[i + 1] == 0) {
			buffer[i] = newSector % 256;
			buffer[i + 1] = newSector / 256;
			break;
		}
	}
	osDiskWrite(currentDir->diskID, file_location, buffer);

	//printf("----------------------------------------\n");
	setAllBitmaps(currentDir, allBitmaps);
	
	return ERR_SUCCESS;
}

long osOpenFile(char* filename, int* Inode) {
	if (strlen(filename) > 7) {
		return ERR_BAD_PARAM;
	}

	

	int err;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &err);
	int index = searchProcessIDInQueue(PCBList, getCurrentContext());
	QueueNode* currNode = QueueGet(PCBList, index);
	dir* currentDir = currNode->info->currentDir;
	File* currentFile = currNode->info->currentFile;
	stack* dirStack = currNode->info->dirStack;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &err);

	char header[PGSIZE];
	osDiskRead(currentDir->diskID, currentDir->sector, header);
	int index_location = (unsigned)header[13] * 256 + (unsigned)header[12];
	int index_level = ((unsigned)(header[8] >> 1)) % 4;
	//printf("%s\n", filename);
	//printf("index location and level: %d, %d\n", index_location, index_level);

	int fileSector = findDirOrFile(filename, index_location, index_level, 0, currentDir);
	//printf("%d\n", fileSector);
	if (fileSector < 0) {
		if (osCreateFile(filename) != ERR_SUCCESS) {
			printf("Something is wrong!\n");
			return ERR_BAD_PARAM; // disk is full
		}
		//printf("Hello!\n");
		osDiskRead(currentDir->diskID, currentDir->sector, header);
		index_location = (unsigned)header[13] * 256 + (unsigned)header[12];
		index_level = ((unsigned)(header[8] >> 1)) % 4;
		fileSector = findDirOrFile(filename, index_location, index_level, 0, currentDir);
		//printf("New file sector is: %02X\n", fileSector);
	}
	
	//printf("%d\n", fileSector);

	char buffer[PGSIZE];
	osDiskRead(currentDir->diskID, fileSector, buffer);
	*Inode = (unsigned int)buffer[0];
	//printf("%d, %d\n", *Inode, strlen(filename));

	currentFile->fileSector = 0;
	currentFile->Inode = *Inode;
	//currentFile->name = (char*)malloc(strlen(filename));
	memcpy(currentFile->name, filename, strlen(filename));
	currentFile->isSet = TRUE;

	return ERR_SUCCESS;
}

int isRoot(int Inode, int diskID) {
	int sector = InodeToSector[Inode];
	char buffer[PGSIZE];
	osDiskRead(diskID, sector, buffer);
	int size = (unsigned char)buffer[14] + (unsigned char)buffer[15] * 256 + 1;
	buffer[14] = size % 256;
	buffer[15] = size / 256;
	int parent = (unsigned char)(buffer[8] >> 3) % 32;
	if (parent == Inode) {
		return 1;
	}
	else {
		return 0;
	}
}
long osWriteFile(int Inode, int index, char* writeBuffer) {
	//printf("%d, %d, %s\n", Inode, index, writeBuffer);
	
	if (index < 0) {
		return ERR_BAD_PARAM; //invalid index
	}

	int previouslyWritten = TRUE;

	char allBitmaps[BITMAP_SIZE * PGSIZE];

	int err;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &err);
	int ind = searchProcessIDInQueue(PCBList, getCurrentContext());
	QueueNode* currNode = QueueGet(PCBList, ind);
	dir* currentDir = currNode->info->currentDir;
	File* currentFile = currNode->info->currentFile;
	stack* dirStack = currNode->info->dirStack;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &err);

	getAllBitmaps(currentDir, allBitmaps);

	READ_MODIFY(LOCK_OF_INODETOSECTOR, DO_LOCK, TRUE, &err);
	int sector = InodeToSector[Inode];
	READ_MODIFY(LOCK_OF_INODETOSECTOR, DO_UNLOCK, TRUE, &err);

	char buffer[PGSIZE];
	osDiskRead(currentDir->diskID, sector, buffer);
	int index_level = (unsigned)(buffer[8] >> 1) % 4;
	int index_location = (unsigned)(buffer[12]) + (unsigned)(buffer[13]) * 256;
	//printf("%d, %d, %d\n", Inode, index_level, index_location);

	while (index >= (unsigned)(1 << (3 * index_level)) && index_level < 3) {
		//READ_MODIFY(LOCK_OF_BITMAP, DO_LOCK, TRUE, &err);
		expandIndex(sector, index_location, index_level, currentDir, allBitmaps);
		//READ_MODIFY(LOCK_OF_BITMAP, DO_UNLOCK, TRUE, &err);
		index_level++;
		previouslyWritten = FALSE;
	}

	//printf("%d\n", index_level);

	if (index >= (unsigned)(1 << (3 * index_level))) {
		return ERR_BAD_PARAM; //index is full
	}

	//buffer[8] = buffer[8] & ~(3 << 1);
	//buffer[8] = buffer[8] | (index_level << 1); //reset index leve
	//osDiskWrite(currentDir->diskID, sector, buffer);

	int data;
	int lv1Addr, lv2Addr, lv3Addr;
	if (index_level == 0) {
		osDiskRead(currentDir->diskID, sector, buffer);
		if (buffer[12] == 0 && buffer[13] == 0) {
			previouslyWritten = FALSE;

			//READ_MODIFY(LOCK_OF_BITMAP, DO_LOCK, TRUE, &err);
			data = findNexEmptySectorAndSet(currentDir, allBitmaps);
			//setOccupied(data, currentDir);	
			//READ_MODIFY(LOCK_OF_BITMAP, DO_UNLOCK, TRUE, &err);
			buffer[12] = (unsigned char)data % 256;
			buffer[13] = (unsigned char)data / 256;
			osDiskWrite(currentDir->diskID, sector, buffer);
		}
		else {
			data = (unsigned char)buffer[12] + (unsigned char)buffer[13] * 256;
		}
		osDiskWrite(currentDir->diskID, data, writeBuffer);
	}
	else if (index_level == 1) {
		osDiskRead(currentDir->diskID, sector, buffer);
		lv1Addr = (unsigned char)(buffer[12]) + (unsigned char)(buffer[13]) * 256;

		osDiskRead(currentDir->diskID, lv1Addr, buffer);

		if (buffer[2 * index] == 0 && buffer[2 * index + 1] == 0) {
			previouslyWritten = FALSE;

			data = findNexEmptySectorAndSet(currentDir, allBitmaps);
			buffer[2 * index] = (unsigned char)data % 256;
			buffer[2 * index + 1] = (unsigned char)data / 256;
			osDiskWrite(currentDir->diskID, lv1Addr, buffer);
		}
		else {
			data = (unsigned char)buffer[2 * index] + (unsigned char)buffer[2 * index + 1] * 256;
		}
		osDiskWrite(currentDir->diskID, data, writeBuffer);
	}
	else if (index_level == 2) {
		osDiskRead(currentDir->diskID, sector, buffer);
		lv1Addr = (unsigned)(buffer[12]) + (unsigned)(buffer[13]) * 256;
		osDiskRead(currentDir->diskID, lv1Addr, buffer);
		lv2Addr = (unsigned)buffer[(index / 8) * 2] + (unsigned)buffer[(index / 8) * 2 + 1];
		osDiskRead(currentDir->diskID, lv2Addr, buffer);

		if (buffer[2 * (index % 8)] == 0 && buffer[2 * (index % 8) + 1] == 0) {
			previouslyWritten = FALSE;

			data = findNexEmptySectorAndSet(currentDir, allBitmaps);
			buffer[2 * (index % 8)] = (unsigned)data % 256;
			buffer[2 * (index % 8) + 1] = (unsigned)data / 256;
			osDiskWrite(currentDir->diskID, lv2Addr, buffer);
		}
		else {
			data = (unsigned char)buffer[2 * (index % 8)] + (unsigned char)buffer[2 * (index % 8)] * 256;
		}

		osDiskWrite(currentDir->diskID, data, writeBuffer);
	}
	else if (index_level == 3) {
		osDiskRead(currentDir->diskID, sector, buffer);
		lv1Addr = (unsigned)(buffer[12]) + (unsigned)(buffer[13]) * 256;
		osDiskRead(currentDir->diskID, lv1Addr, buffer);
		lv2Addr = (unsigned)buffer[(index / 64) * 2] + (unsigned)buffer[(index / 64) * 2 + 1] * 256;
		index = index % 64;
		osDiskRead(currentDir->diskID, lv2Addr, buffer);
		lv3Addr = (unsigned)buffer[(index / 8) * 2] + (unsigned)buffer[(index / 8) * 2 + 1] * 256;
		osDiskRead(currentDir->diskID, lv3Addr, buffer);

		if (buffer[2 * (index % 8)] == 0 && buffer[2 * (index % 8) + 1] == 0) {
			previouslyWritten = FALSE;

			data = findNexEmptySectorAndSet(currentDir, allBitmaps);
			buffer[2 * (index % 8)] = (unsigned)data % 256;
			buffer[2 * (index % 8) + 1] = (unsigned)data / 256;
			osDiskWrite(currentDir->diskID, lv3Addr, buffer);
		}
		else {
			data = (unsigned char)buffer[2 * (index % 8)] + (unsigned char)buffer[2 * (index % 8)] * 256;
		}

		osDiskWrite(currentDir->diskID, data, writeBuffer);
	}

	if (previouslyWritten == FALSE) {
		int cInode = Inode;
		int cSector;
		while (cInode != 31) {
			cSector = InodeToSector[cInode];
			osDiskRead(currentDir->diskID, cSector, buffer);
			int size = (unsigned char)buffer[14] + (unsigned char)buffer[15] * 256 + 1;
			buffer[14] = size % 256;
			buffer[15] = size / 256;
			cInode = (unsigned char)(buffer[8] >> 3) % 32;
			osDiskWrite(currentDir->diskID, cSector, buffer);
		}

		cSector = InodeToSector[cInode];
		osDiskRead(currentDir->diskID, cSector, buffer);
		int size = (unsigned char)buffer[14] + (unsigned char)buffer[15] * 256 + 1;
		buffer[14] = size % 256;
		buffer[15] = size / 256;
		cInode = (unsigned char)(buffer[8] >> 3) % 32;
		osDiskWrite(currentDir->diskID, cSector, buffer);
	}
	
	//showBitMap();
	setAllBitmaps(currentDir, allBitmaps);

	return ERR_SUCCESS;
}

long osReadFile(int Inode, int index, char* readBuffer) {
	//printf("%d, %d, %s\n", Inode, index, writeBuffer);
	if (index < 0) {
		return ERR_BAD_PARAM; //invalid index
	}

	int err;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &err);
	int ind = searchProcessIDInQueue(PCBList, getCurrentContext());
	QueueNode* currNode = QueueGet(PCBList, ind);
	dir* currentDir = currNode->info->currentDir;
	File* currentFile = currNode->info->currentFile;
	stack* dirStack = currNode->info->dirStack;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &err);

	READ_MODIFY(LOCK_OF_INODETOSECTOR, DO_LOCK, TRUE, &err);
	int sector = InodeToSector[Inode];
	READ_MODIFY(LOCK_OF_INODETOSECTOR, DO_UNLOCK, TRUE, &err);
	char buffer[PGSIZE];
	osDiskRead(currentDir->diskID, sector, buffer);
	int index_level = (unsigned char)(buffer[8] >> 1) % 4;
	int index_location = (unsigned char)(buffer[12]) + (unsigned char)(buffer[13]) * 256;
	//printf("%d, %d, %d\n", sector, index_level, index_location);

	int data;
	int lv1Addr, lv2Addr, lv3Addr;
	if (index_level == 0) {

		osDiskRead(currentDir->diskID, index_location, readBuffer);

	}
	else if (index_level == 1) {
		lv1Addr = index_location;

		osDiskRead(currentDir->diskID, lv1Addr, buffer);
		data = (unsigned char)buffer[2 * index] + (unsigned char)buffer[2 * index + 1] * 256;
		osDiskRead(currentDir->diskID, data, readBuffer);
	}
	else if (index_level == 2) {
		lv1Addr = index_location;

		osDiskRead(currentDir->diskID, lv1Addr, buffer);
		lv2Addr = (unsigned char)buffer[(index / 8) * 2] + (unsigned char)buffer[(index / 8) * 2 + 1];

		osDiskRead(currentDir->diskID, lv2Addr, buffer);
		data = (unsigned)buffer[2 * (index % 8)] + (unsigned)buffer[2 * (index % 8) + 1] * 256;
		osDiskRead(currentDir->diskID, data, readBuffer);
	}
	else if (index_level == 3) {
		lv1Addr = index_location;

		osDiskRead(currentDir->diskID, lv1Addr, buffer);
		lv2Addr = (unsigned)buffer[(index / 64) * 2] + (unsigned)buffer[(index / 64) * 2 + 1];
		index = index % 64;

		osDiskRead(currentDir->diskID, lv2Addr, buffer);
		lv3Addr = (unsigned)buffer[(index / 8) * 2] + (unsigned)buffer[(index / 8) * 2 + 1];

		osDiskRead(currentDir->diskID, lv3Addr, buffer);
		data = (unsigned)buffer[(index % 8) * 2] + (unsigned)buffer[(index % 8) * 2 + 1];
		osDiskRead(currentDir->diskID, data, readBuffer);
	}
	//showBitMap();
	return ERR_SUCCESS;
}

void showAllDirOrFile(int sector, int level, dir* currentDir) { // 1 for dir, 0 for file
	char buffer[PGSIZE];
	if (level == 0) {
		if (sector == 0) {
			return;
		}
		osDiskRead(currentDir->diskID, sector, buffer);
		//printBuffer(buffer);
		//printf("%d\n", strlen(dirName));
		/*if (stringcmp(buffer + 1, dirName) == 0 && (buffer[8] & 1) == dirOrFile) {
		return sector;
		}
		else {
		return -1;
		}*/
		int Inode = buffer[0];
		long time = (unsigned char)buffer[9] + (unsigned char)buffer[10] * 256 + (unsigned char)buffer[11] * 256L * 256L;
		int fileSize = (unsigned char)buffer[14] + (unsigned char)buffer[15] * 256;
		printf("%d	", buffer[0]);
		for (int i = 0; i < 7; i++) {
			if (buffer[1 + i] == '\0') {
				break;
			}
			printf("%c", buffer[1 + i]);
		}
		if ((buffer[8] & 1) == 1) {
			printf("		D	");
		}
		else {
			printf("		F	");
		}
		printf("%ld    ", time);
		printf("	%d", fileSize);
		printf("\n");
		return;
	}
	if (sector == 0) {
		return;
	}
	int nextSector;
	osDiskRead(currentDir->diskID, sector, buffer);
	//printBuffer(buffer);
	for (int i = 0; i < PGSIZE; i += 2) {
		nextSector = (unsigned char)buffer[i] + (unsigned char)buffer[i + 1] * 256;
		//printBuffer(buffer);
		//printf("%02X, %02X\n", buffer[i], buffer[i + 1]);
		//printf("nextSector: %d\n", nextSector);
		if (nextSector != 0) {
			showAllDirOrFile(nextSector, level - 1, currentDir);
		}
	}
	return;
}

long osDirContent() {
	int err;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &err);
	int ind = searchProcessIDInQueue(PCBList, getCurrentContext());
	QueueNode* currNode = QueueGet(PCBList, ind);
	dir* currentDir = currNode->info->currentDir;
	File* currentFile = currNode->info->currentFile;
	stack* dirStack = currNode->info->dirStack;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &err);
	//showCurrentDir(currentDir);
	char buffer[PGSIZE];
	osDiskRead(currentDir->diskID, currentDir->sector, buffer);
	int index_level = (unsigned)(buffer[8] >> 1) % 4;
	int index_location = (unsigned)(buffer[12]) + (unsigned)(buffer[13]) * 256;
	//printf("%d, %02X\n", index_level, index_location);

	printf("Inode	FileName	D/F	Creation Time	File Size\n");

	showAllDirOrFile(index_location, index_level, currentDir);
	
	return ERR_SUCCESS;
}

long osCloseFile(int Inode) {
	long returnType = ERR_SUCCESS;

	int err;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &err);
	int index = searchProcessIDInQueue(PCBList, getCurrentContext());
	QueueNode* currNode = QueueGet(PCBList, index);
	File* currentFile = currNode->info->currentFile;

	QueueNode* node = PCBList->head;
	while (node != NULL) {
		if (node->info->currentFile->isSet == TRUE && node->info->currentFile->Inode == Inode) {
			returnType = ERR_BAD_PARAM; // Inode is opened
		}
		node = node->next;
	}
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &err);

	if (currentFile->isSet == FALSE || Inode != currentFile->Inode) {
		return ERR_BAD_PARAM; //no current file
	}
	currentFile->isSet = FALSE;
	return returnType;
}

void freeAllSpace(int sector, int level, dir* currentDir) {
	char buffer[PGSIZE];
	if (level == 0) {
		memset(buffer, '\0', PGSIZE);
		osDiskWrite(currentDir->diskID, sector, buffer);
		setUnoccupied(sector, currentDir);
		return;
	}
	
	if (sector == 0) {
		return;
	}
	osDiskRead(currentDir->diskID, sector, buffer);
	int nextLevelSector;
	for (int i = 0; i < PGSIZE; i += 2) {
		nextLevelSector = (unsigned)buffer[i] + (unsigned)buffer[i + 1] * 256;
		if (buffer[i] != 0 || buffer[i + 1] != 0) {
			freeAllSpace(currentDir->diskID, nextLevelSector, currentDir);
		}
	}
	memset(buffer, '\0', PGSIZE);
	osDiskWrite(currentDir->diskID, sector, buffer);
	setUnoccupied(sector, currentDir);
}

void setIndexByLevel(int parentSector, int fileSector, int index_level, int tag, dir* currentDir) {
	char buffer[PGSIZE];
	int num;
	if (index_level == 1) {
		osDiskRead(currentDir->diskID, parentSector, buffer);
		for (int i = 0; i < PGSIZE; i += 2) {
			num = (unsigned)buffer[i] + (unsigned)buffer[i + 1] * 256;
			if (num == fileSector) {
				buffer[i] = (unsigned)tag % 256;
				buffer[i + 1] = (unsigned)tag / 256;
				osDiskWrite(currentDir->diskID, parentSector, buffer);
				return;
			}
		}
	}

	if (parentSector == 0) {
		return;
	}

	osDiskRead(currentDir->diskID, parentSector, buffer);

	for (int i = 0; i < PGSIZE; i += 2) {
		num = (unsigned)buffer[i] + (unsigned)buffer[i + 1] * 256;
		if (num != 0) {
			setIndexByLevel(num, fileSector, index_level - 1, tag, currentDir);
		}
	}

}

//void setIndex(int parentSector, int fileSector, int tag) {
//	char buffer[PGSIZE];
//	osDiskRead(currentDir->diskID, parentSector, buffer);
//
//	int index_level = (buffer[8] >> 1) % 4;
//	int index_location = (unsigned)buffer[12] + (unsigned)buffer[13] * 256;
//	if (index_level == 0) {
//		if (index_location == fileSector) {
//			buffer[12] = '\0';
//			buffer[13] = '\0';
//			osDiskWrite(currentDir->diskID, parentSector, buffer);
//		}
//		else {
//			printf("Critical Disk Error!\n");
//		}
//		return;
//	}
//	setIndexByLevel(index_location, parentSector, fileSector, index_level - 1, tag);
//}
//
//
//
long osDeleteFile(char* filename) {
	int err;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &err);
	int ind = searchProcessIDInQueue(PCBList, getCurrentContext());
	QueueNode* currNode = QueueGet(PCBList, ind);
	dir* currentDir = currNode->info->currentDir;
	File* currentFile = currNode->info->currentFile;
	stack* dirStack = currNode->info->dirStack;
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &err);

	char header[PGSIZE];
	osDiskRead(currentDir->diskID, currentDir->sector, header);
	int index_location = (unsigned)header[13] * 256 + (unsigned)header[12];
	int index_level = ((unsigned)(header[8] >> 1)) % 4;
	int fileHeaderSector;
	if (index_level == 0) {
		fileHeaderSector = index_location;
		header[12] = '\0';
		header[13] = '\0';
		osDiskWrite(currentDir->diskID, currentDir->sector, header); // set parent index for file to 0
		//memset(header, '\0', PGSIZE);
		//osDiskWrite(currentDir->diskID, index_location, header);
		//setUnoccupied(index_location);
	}
	else {
		//int* Inode;
		fileHeaderSector = findDirOrFile(filename, index_location, index_level, 0, currentDir);
		if (fileHeaderSector < 0) {
			return ERR_BAD_PARAM; // no such file in current directory
		}
		setIndexByLevel(index_location, fileHeaderSector, index_level, 0, currentDir); // set parent index for file to 0
		
	}

	//next, free the space for the file
	char buffer[PGSIZE];
	osDiskRead(currentDir->diskID, fileHeaderSector, buffer);
	index_location = (unsigned)buffer[12] + (unsigned)buffer[13] * 256;
	index_level = (unsigned)(buffer[8] >> 1) % 4;
	freeAllSpace(index_location, index_level, currentDir); // free index for the file

	memset(buffer, '\0', PGSIZE);
	osDiskWrite(currentDir->diskID, fileHeaderSector, buffer);
	setUnoccupied(fileHeaderSector, currentDir);

	return ERR_SUCCESS;
}

void requestLockOrSuspend() {
	int err;
	int Success_Or_Failure_Returned;
	//READ_MODIFY(LOCK_OF_REQUESTDISK, DO_LOCK, FALSE, &err);
	//printf("%d\n", err);
	//if (err != TRUE) {
	//	long currentProcessAddr = getCurrentContext();
	//	//save the current process information and push it into the end of the ready queue
	//	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	//	int index = searchProcessIDInQueue(PCBList, currentProcessAddr);
	//	char * name = { "" };
	//	int pid = 0;
	//	if (index >= 0) {
	//		QueueNode* pcb = QueueGet(PCBList, index);
	//		ProcessInfomation* pcbinfo = (ProcessInfomation *)pcb->info;
	//		name = pcbinfo->name;
	//		pid = pcbinfo->pid;
	//	}
	//	else {
	//		printf("Error in terminate!\n");
	//	}
	//	ProcessInfomation* pinfo = createProcessInfomation(currentProcessAddr, -1);
	//	pinfo->pid = pid;
	//	pinfo->name = name;
	//	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	//	QueueNode* currentProcess = createQueueNode(pinfo, NODE_TYPE_MISSIONCOMPLETE);

	//	READ_MODIFY(LOCK_OF_READYQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	//	QueueOfferLast(readyQueue, currentProcess);
	//	READ_MODIFY(LOCK_OF_READYQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);

	//	//suspendContext(currentProcessAddr);

	//	scheduler();
	//}
	//else {
	//	READ_MODIFY(LOCK_OF_REQUESTDISK, DO_LOCK, TRUE, &err);
	//}

	while (TRUE) {
		READ_MODIFY(LOCK_OF_REQUESTDISK, DO_LOCK, FALSE, &err);
		//printf("%d\n", err);
		if (err != TRUE) {
			long currentProcessAddr = getCurrentContext();
			//save the current process information and push it into the end of the ready queue
			READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
			int index = searchProcessIDInQueue(PCBList, currentProcessAddr);
			char * name = { "" };
			int pid = 0;
			if (index >= 0) {
				QueueNode* pcb = QueueGet(PCBList, index);
				ProcessInfomation* pcbinfo = (ProcessInfomation *)pcb->info;
				name = pcbinfo->name;
				pid = pcbinfo->pid;
			}
			else {
				printf("Error in terminate!\n");
			}
			ProcessInfomation* pinfo = createProcessInfomation(currentProcessAddr, -1);
			pinfo->pid = pid;
			pinfo->name = name;
			READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
			QueueNode* currentProcess = createQueueNode(pinfo, NODE_TYPE_MISSIONCOMPLETE);

			READ_MODIFY(LOCK_OF_READYQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
			QueueOfferLast(readyQueue, currentProcess);
			READ_MODIFY(LOCK_OF_READYQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);

			//suspendContext(currentProcessAddr);

			scheduler();
		}
		else {
			//READ_MODIFY(LOCK_OF_REQUESTDISK, DO_LOCK, TRUE, &err);
			break;
		}
	}
}

/************************************************************************
 SVC
 The beginning of the OS502.  Used to receive software interrupts.
 All system calls come to this point in the code and are to be
 handled by the student written code here.
 The variable do_print is designed to print out the data for the
 incoming calls, but does so only for the first ten calls.  This
 allows the user to see what's happening, but doesn't overwhelm
 with the amount of data.
 ************************************************************************/

void svc(SYSTEM_CALL_DATA *SystemCallData) {
	short call_type;
	static short do_print = 10;
	short i;
	MEMORY_MAPPED_IO mmio;

	call_type = (short)SystemCallData->SystemCallNumber;
	if (do_print > 0) {
		printf("SVC handler: %s\n", call_names[call_type]);
		for (i = 0; i < SystemCallData->NumberOfArguments - 1; i++) {
			//Value = (long)*SystemCallData->Argument[i];
			printf("Arg %d: Contents = (Decimal) %8ld,  (Hex) %8lX\n", i,
				(unsigned long)SystemCallData->Argument[i],
				(unsigned long)SystemCallData->Argument[i]);
		}
		do_print--;
	}
//	ProcessInfomation* pinfo;
//	QueueNode* currentProcess;
//	int diskID;
	int err;
	switch (call_type) {
	case SYSNUM_GET_TIME_OF_DAY: //This value is found in syscalls.h
		osGetTimeOfTheDay(SystemCallData, &mmio);
		break;
	case SYSNUM_TERMINATE_PROCESS:		
		osTerminateProcess(SystemCallData, &mmio);
		break;
	case SYSNUM_SLEEP:
		osSleep(SystemCallData, &mmio);
		break;
	case SYSNUM_GET_PROCESS_ID:
		osGetProcessID(SystemCallData, &mmio);
		break;
	case SYSNUM_CREATE_PROCESS:
		osCreateProcess(SystemCallData, &mmio);
		break;
	case SYSNUM_PHYSICAL_DISK_WRITE:
		osDiskWrite((int)SystemCallData->Argument[0], (int)SystemCallData->Argument[1], (char*)SystemCallData->Argument[2]);
		break;
	case SYSNUM_PHYSICAL_DISK_READ:
		osDiskRead((int)SystemCallData->Argument[0], (int)SystemCallData->Argument[1], (char*)SystemCallData->Argument[2]);
		break;
	case SYSNUM_FORMAT:
		requestLockOrSuspend();
		*(long *)SystemCallData->Argument[1] = osDiskFormat((int)SystemCallData->Argument[0]);
		READ_MODIFY(LOCK_OF_REQUESTDISK, DO_UNLOCK, TRUE, &err);
		break;
	case SYSNUM_CHECK_DISK:
		requestLockOrSuspend();
		*(long *)SystemCallData->Argument[1] = osDiskCheck((int)SystemCallData->Argument[0]);
		READ_MODIFY(LOCK_OF_REQUESTDISK, DO_UNLOCK, TRUE, &err);
		break;
	case SYSNUM_OPEN_DIR:
		requestLockOrSuspend();
		*(long *)SystemCallData->Argument[2] = osOpenDir((int)SystemCallData->Argument[0], (char*)SystemCallData->Argument[1]);
		READ_MODIFY(LOCK_OF_REQUESTDISK, DO_UNLOCK, TRUE, &err);
		break;
	case SYSNUM_OPEN_FILE:
		requestLockOrSuspend();
		*(long *)SystemCallData->Argument[2] = osOpenFile((char*)SystemCallData->Argument[0], (int*)SystemCallData->Argument[1]);
		READ_MODIFY(LOCK_OF_REQUESTDISK, DO_UNLOCK, TRUE, &err);
		break;
	case SYSNUM_CREATE_DIR:
		requestLockOrSuspend();
		*(long *)SystemCallData->Argument[1] = osCreateDir((char*)SystemCallData->Argument[0]);
		READ_MODIFY(LOCK_OF_REQUESTDISK, DO_UNLOCK, TRUE, &err);
		break;
	case SYSNUM_CREATE_FILE:
		requestLockOrSuspend();
		*(long *)SystemCallData->Argument[1] = osCreateFile((char*)SystemCallData->Argument[0]);
		READ_MODIFY(LOCK_OF_REQUESTDISK, DO_UNLOCK, TRUE, &err);
		break;
	case SYSNUM_WRITE_FILE:
		requestLockOrSuspend();
		*(long *)SystemCallData->Argument[3] = osWriteFile((int)SystemCallData->Argument[0], (int)SystemCallData->Argument[1], (char*)SystemCallData->Argument[2]);
		READ_MODIFY(LOCK_OF_REQUESTDISK, DO_UNLOCK, TRUE, &err);
		break;
	case SYSNUM_READ_FILE:
		requestLockOrSuspend();
		*(long *)SystemCallData->Argument[3] = osReadFile((int)SystemCallData->Argument[0], (int)SystemCallData->Argument[1], (char*)SystemCallData->Argument[2]);
		READ_MODIFY(LOCK_OF_REQUESTDISK, DO_UNLOCK, TRUE, &err);
		break;
	case SYSNUM_CLOSE_FILE:
		requestLockOrSuspend();
		*(long *)SystemCallData->Argument[1] = osCloseFile((int)SystemCallData->Argument[0]);
		READ_MODIFY(LOCK_OF_REQUESTDISK, DO_UNLOCK, TRUE, &err);
		break;
	case SYSNUM_DIR_CONTENTS:
		requestLockOrSuspend();
		*(long *)SystemCallData->Argument[0] = osDirContent();
		READ_MODIFY(LOCK_OF_REQUESTDISK, DO_UNLOCK, TRUE, &err);
		break;
	default:
		printf("ERROR! call_type not recognized!\n");
		printf("Call_type is - %i\n", call_type);
		//End of switch
	}
}                                               // End of svc
/************************************************************************
 osInit
 This is the first routine called after the simulation begins.  This
 is equivalent to boot code.  All the initial OS components can be
 defined and initialized here.
 ************************************************************************/

void osInit(int argc, char *argv[]) {
	//Initialize
	void *PageTable = (void *)calloc(2, NUMBER_VIRTUAL_PAGES);
	INT32 i;
	MEMORY_MAPPED_IO mmio;

	isRunningInMultiProcessorMode = 0;

	timerQueue = createQueue();
	readyQueue = createQueue();
	diskQueue = createQueue();
	PCBList = createQueue();
	runningList = createQueue();
	suspendList = createQueue();
	terminatedList = createQueue();

	for (i = 0; i < MAX_NUMBER_OF_DISKS; i++) {
		diskQueueList[i] = createQueue();
	}

	for (i = 0; i < MAX_NUMBER_OF_USER_PROCESSES; i++) {
		pidIsUsed[i] = FALSE;
	}

	for (i = 0; i < MAX_INODE; i++) {
		InodeIsUsed[i] = FALSE;
	}

	for (int i = 0; i < MAX_NUMBER_OF_DISKS; i++) {
		space[i] = -1;
	}
	
	SPStatus = 0;
	
	input = (SP_INPUT_DATA*)malloc(sizeof(SP_INPUT_DATA));

	char* name = { "" };
	long pid;
	long err;
	/*********************************************************/


	// Demonstrates how calling arguments are passed thru to here

	printf("Program called with %d arguments:", argc);
	for (i = 0; i < argc; i++)
		printf(" %s", argv[i]);
	printf("\n");
	printf("Calling with argument 'sample' executes the sample program.\n");

	// Here we check if a second argument is present on the command line.
	// If so, run in multiprocessor mode
	if (argc > 2) {
		if (strcmp(argv[2], "M") || strcmp(argv[2], "m")) {
			printf("Simulation is running as a MultProcessor\n\n");
			mmio.Mode = Z502SetProcessorNumber;
			mmio.Field1 = MAX_NUMBER_OF_PROCESSORS;
			mmio.Field2 = (long)0;
			mmio.Field3 = (long)0;
			mmio.Field4 = (long)0;
			MEM_WRITE(Z502Processor, &mmio);   // Set the number of processors
			isRunningInMultiProcessorMode = 1;
			SPStatus = 1;
		}
	}
	else {
		printf("Simulation is running as a UniProcessor\n");
		printf("Add an 'M' to the command line to invoke multiprocessor operation.\n\n");
	}

	//          Setup so handlers will come to code in base.c

	TO_VECTOR[TO_VECTOR_INT_HANDLER_ADDR] = (void *)InterruptHandler;
	TO_VECTOR[TO_VECTOR_FAULT_HANDLER_ADDR] = (void *)FaultHandler;
	TO_VECTOR[TO_VECTOR_TRAP_HANDLER_ADDR] = (void *)svc;

	//  Determine if the switch was set, and if so go to demo routine.

	PageTable = (void *)calloc(2, NUMBER_VIRTUAL_PAGES);
	if ((argc > 1) && (strcmp(argv[1], "sample") == 0)) {
		mmio.Mode = Z502InitializeContext;
		mmio.Field1 = 0;
		mmio.Field2 = (long)SampleCode;
		mmio.Field3 = (long)PageTable;

		MEM_WRITE(Z502Context, &mmio);   // Start of Make Context Sequence
		mmio.Mode = Z502StartContext;
		// Field1 contains the value of the context returned in the last call
		mmio.Field2 = START_NEW_CONTEXT_AND_SUSPEND;
		MEM_WRITE(Z502Context, &mmio);     // Start up the context
	} // End of handler for sample code - This routine should never return here
	//  By default test0 runs if no arguments are given on the command line
	//  Creation and Switching of contexts should be done in a separate routine.
	//  This should be done by a "OsMakeProcess" routine, so that
	//  test0 runs on a process recognized by the operating system.
	free(PageTable);

	//printf(argv[1]);
	//printf("\n");
	//CREATE_PROCESS("test3_a", testX, ILLEGAL_PRIORITY, &ProcessID1, &ErrorReturned);
	SYSTEM_CALL_DATA* SystemCallData = (SYSTEM_CALL_DATA*)malloc(sizeof(SYSTEM_CALL_DATA));
	SystemCallData->NumberOfArguments = 6;                         
	SystemCallData->SystemCallNumber = SYSNUM_CREATE_PROCESS;
	SystemCallData->Argument[0] = (long*)name;
	//printf("%s\n", (char*)SystemCallData->Argument[0]);
	SystemCallData->Argument[2] = (long*)10;
	SystemCallData->Argument[3] = &pid;
	SystemCallData->Argument[4] = &err;
	if (strcmp(argv[1], "test0") == 0) {
		SystemCallData->Argument[1] = (long *)test0;
	}
	else if (strcmp(argv[1], "test1") == 0) {
		SystemCallData->Argument[1] = (long *)test1;
	}
	else if (strcmp(argv[1], "test2") == 0) {
		SystemCallData->Argument[1] = (long *)test2;
	}
	else if (strcmp(argv[1], "test3") == 0) {
		SPStatus = 1;
		SystemCallData->Argument[1] = (long *)test3;
	}
	else if (strcmp(argv[1], "test4") == 0) {
		SPStatus = 1;
		SystemCallData->Argument[1] = (long *)test4;
	}
	else if (strcmp(argv[1], "test5") == 0) {
		SPStatus = 1;
		SystemCallData->Argument[1] = (long *)test5;
	}
	else if (strcmp(argv[1], "test6") == 0) {
		SPStatus = 1;
		SystemCallData->Argument[1] = (long *)test6;
	}
	else if (strcmp(argv[1], "test7") == 0) {
		SPStatus = 1;
		SystemCallData->Argument[1] = (long *)test7;
	}
	else if (strcmp(argv[1], "test8") == 0) {
		SPStatus = 1;
		SystemCallData->Argument[1] = (long *)test8;
	}
	else if (strcmp(argv[1], "test9") == 0) {
		SystemCallData->Argument[1] = (long *)test9;
	}
	else if (strcmp(argv[1], "test10") == 0) {
		SystemCallData->Argument[1] = (long *)test10;
	}
	else if (strcmp(argv[1], "test11") == 0) {
		SystemCallData->Argument[1] = (long *)test11;
	}
	else if (strcmp(argv[1], "test12") == 0) {
		SPStatus = 1;
		SystemCallData->Argument[1] = (long *)test12;
	}
	else if (strcmp(argv[1], "test13") == 0) {
		SPStatus = 1;
		SystemCallData->Argument[1] = (long *)test13;
	}
	else if (strcmp(argv[1], "test14") == 0) {
		SystemCallData->Argument[1] = (long *)test14;
	}
	else if (strcmp(argv[1], "test15") == 0) {
		SystemCallData->Argument[1] = (long *)test15;
	}
	else if (strcmp(argv[1], "test16") == 0) {
		SystemCallData->Argument[1] = (long *)test16;
	}
	else {
		printf("This is not a valid test name!\n");
	}
	osCreateProcess(SystemCallData, &mmio);

	if (isRunningInMultiProcessorMode == 1) {
		ProcessInfomation* pinfo = createProcessInfomation2("os", getCurrentContext(), -1);
		pinfo->pid = getNewPID();
		QueueNode* os = createQueueNode(pinfo, NODE_TYPE_MISSIONCOMPLETE);
		QueueOfferLast(PCBList, os);
		QueueOfferLast(runningList, os);
	}

	if (isRunningInMultiProcessorMode == 0) {
		scheduler();
	}
	else {
		dispatcher(); 
	}
	
}                                            // End of osInit

void suspendProcess(long contextAddr, int* child) {
	int Success_Or_Failure_Returned;
	short i;
	READ_MODIFY(LOCK_OF_SUSPENDLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	int index = searchProcessIDInQueue(runningList, contextAddr);
	QueueNode* re = QueueRemove(runningList, index);
	QueueOfferLast(suspendList, re);

	memcpy(re->info->childPID, child, MAX_NUMBER_OF_USER_PROCESSES);

	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_SUSPENDLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	suspendContext(contextAddr);
}

void lookForProcessToResume() {
	QueueNode* now = suspendList->head;
	int* child;
	short i;
	short index = 0;
	short flag;
	short idx[MAX_NUMBER_OF_USER_PROCESSES];
	short idxend = 0;
	int Success_Or_Failure_Returned;

	READ_MODIFY(LOCK_OF_SUSPENDLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_READYQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	
	if (QueueIsEmpty(suspendList) == 0) {
		QueueOfferLast(readyQueue, QueuePollLast(suspendList));
	}
	
	//memset(idx, -1, MAX_NUMBER_OF_USER_PROCESSES);

	//while (now != NULL) {
	//	child = now->info->childPID;
	//	flag = 1;
	//	for (i = 0; i < MAX_NUMBER_OF_USER_PROCESSES; i++) {
	//		if (child[i] < 0) {
	//			break;
	//		}
	//		if (QueueContainsPID(PCBList, child[i]) == 1) {
	//			flag = 0;
	//			break;
	//		}
	//	}
	//	if (flag == 1) {
	//		QueueOfferLast(readyQueue, QueueGet(suspendList, index));
	//		idx[idxend] = index;
	//		idxend++;
	//	}
	//	now = now->next;
	//	index++;
	//}

	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_READYQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
}

void doSPrint() {
	int Success_Or_Failure_Returned;
	QueueNode* node;
	QueueNode* now;
	short i;
	short j;
	static int t = 1;

	if (t == 1) {
		t--;
		return;
	}

	READ_MODIFY(LOCK_OF_SP, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_READYQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_TIMERQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	for (i = 0; i < MAX_NUMBER_OF_DISKS; i++) {
		READ_MODIFY(LOCK_OF_DISKQUEUE + i, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	}
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_TERMINATELIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);

	int index = searchProcessIDInQueue(PCBList, getCurrentContext());
	node = QueueGet(PCBList, index);
	memcpy(input->TargetAction, "schedule", 9);
	printf("hello\n");
	if (node == NULL) {
		printf("nothing!\n");
	}
	input->CurrentlyRunningPID = node->info->pid;
	
	input->TargetPID = readyQueue->head->info->pid;

	input->NumberOfRunningProcesses = 1;

	input->RunningProcessPIDs[0] = node->info->pid;

	input->NumberOfReadyProcesses = QueueSize(readyQueue);
	
	i = 0;
	now = readyQueue->head;
	while (now != NULL) {
		input->ReadyProcessPIDs[i] = now->info->pid;
		i++;
		now = now->next;
	}

	input->NumberOfProcSuspendedProcesses = 0;
	input->NumberOfMessageSuspendedProcesses = 0;
	input->NumberOfTimerSuspendedProcesses = QueueSize(timerQueue);

	i = 0;
	now = timerQueue->head;
	while (now != NULL) {
		input->TimerSuspendedProcessPIDs[i] = now->info->pid;
		i++;
		now = now->next;
	}

	input->NumberOfDiskSuspendedProcesses = 0;
	for (i = 0; i < MAX_NUMBER_OF_DISKS; i++) {
		input->NumberOfDiskSuspendedProcesses += QueueSize(diskQueueList[i]);
	}

	j = 0;
	for (i = 0; i < MAX_NUMBER_OF_DISKS; i++) {
		now = diskQueueList[i]->head;
		while (now != NULL) {
			input->DiskSuspendedProcessPIDs[j] = now->info->pid;
			j++;
			now = now->next;
		}
	}

	input->NumberOfTerminatedProcesses = QueueSize(terminatedList);

	i = 0;
	now = terminatedList->head;
	while (now != NULL) {
		input->TerminatedProcessPIDs[i] = now->info->pid;
		i++;
		now = now->next;
	}

	SPPrintLine(input);

	READ_MODIFY(LOCK_OF_SP, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_READYQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_TIMERQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	for (i = 0; i < MAX_NUMBER_OF_DISKS; i++) {
		READ_MODIFY(LOCK_OF_DISKQUEUE + i, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	}
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_TERMINATELIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
}

void scheduler() {
	int Success_Or_Failure_Returned;
	QueueNode* curr;
	if (isRunningInMultiProcessorMode == 0) {
		//doSPrint();
		READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
		if (QueueIsEmpty(PCBList) == TRUE) {
			halt();
		}
		READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
		while (QueueIsEmpty(readyQueue) == TRUE) {
			idle();
			//CALL(1);
		}
		dispatcher();//there is some task on readyQueue	
	}
	else {
		printQueueStatus();
		int pids[MAX_NUMBER_OF_USER_PROCESSES];
		short i = 0;

		memset(pids, -1, MAX_NUMBER_OF_USER_PROCESSES);

		READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
		if (QueueIsEmpty(PCBList) == TRUE) {
			halt();
		}
		READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);

		lookForProcessToResume();

		while (QueueIsEmpty(readyQueue) == TRUE) {
			idle();
		}

		//Dispatch all the tasks on Ready Queue
		READ_MODIFY(LOCK_OF_READYQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
		READ_MODIFY(LOCK_OF_RUNNINGLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
		while (QueueIsEmpty(readyQueue) == FALSE) {
			curr = QueuePollFirst(readyQueue);
			startContext(curr->info->contextAddr);
			QueueOfferLast(runningList, curr);
			pids[i] = curr->info->pid;
		}
		READ_MODIFY(LOCK_OF_RUNNINGLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
		READ_MODIFY(LOCK_OF_READYQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);

		//add the current process to Suspend List
		//then suspend current process

		long currentContext = getCurrentContext();
		suspendProcess(currentContext, pids);
	}	
}

void printQueueStatus() {
	int Success_Or_Failure_Returned;
	READ_MODIFY(LOCK_OF_TIMERQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_READYQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	for (int i = 0; i < MAX_NUMBER_OF_DISKS; i++) {
		READ_MODIFY(LOCK_OF_DISKQUEUE + i, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	}
	READ_MODIFY(LOCK_OF_PCBLIST, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	//printf("-----------------------------------------\n");
	printf("readyQueue:\n");
	printQueue(readyQueue);
	//printf("timerQueue:\n");
	//printQueue(timerQueue);
	//printf("diskQueue[0]:\n");
	//printQueue(diskQueueList[0]);
	//printf("diskQueue[1]:\n");
	//printQueue(diskQueueList[1]);
	//printf("diskQueue[1]:\n");
	//printQueue(diskQueueList[1]);
	//printf("diskQueue[3]:\n");
	//printQueue(diskQueueList[3]);
	//printf("diskQueue[4]:\n");
	//printQueue(diskQueueList[4]);
	printf("runningList:\n");
	printQueue(runningList);
	printf("suspendList:\n");
	printQueue(suspendList);
	printf("PCBList:\n");
	printQueue(PCBList);
	//printf("-----------------------------------------\n");
	READ_MODIFY(LOCK_OF_TIMERQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	READ_MODIFY(LOCK_OF_READYQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	for (int i = 0; i < MAX_NUMBER_OF_DISKS; i++) {
		READ_MODIFY(LOCK_OF_DISKQUEUE + i, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	}
	READ_MODIFY(LOCK_OF_PCBLIST, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
}


void dispatcher() {
	int Success_Or_Failure_Returned;

	QueueNode* curr;
	
	//printf("---------------------------%d\n", QueueSize(readyQueue));
	//printQueueStatus();
	READ_MODIFY(LOCK_OF_READYQUEUE, DO_LOCK, TRUE, &Success_Or_Failure_Returned);
	curr = QueuePollFirst(readyQueue);
	READ_MODIFY(LOCK_OF_READYQUEUE, DO_UNLOCK, TRUE, &Success_Or_Failure_Returned);
	startContext(curr->info->contextAddr);
	//printf("Running %s, PID %d\n", curr->info->name, curr->info->pid);
	if (isRunningInMultiProcessorMode == 0) {
		destroyQueueNode(curr);
	}
	else {
		QueueOfferLast(runningList, curr);
	}
	
	
}

//int indexFull() {
//	char header[PGSIZE];
//	osDiskRead(currentDir->diskID, currentDir->sector, header);
//	int index_location = (unsigned)header[13] * 256 + (unsigned)header[12];
//	int index_level = ((unsigned)(header[8] >> 1)) % 4;
//	printf("%d, %d\n", index_location, index_level);
//	char name[7];
//	char level1[PGSIZE];
//	char level2[PGSIZE];
//	char level3[PGSIZE];
//	char level4[PGSIZE];
//
//	if (index_level == 0) {
//		//osDiskRead(currentDir->diskID, index_location, level1);
//		if (index_location != 0 && isOccupied(index_location) == TRUE) {
//			return -1;
//		}
//		else {
//			return index_location;
//		}
//	}
//
//	if (index_level == 1) {
//		osDiskRead(currentDir->diskID, index_location, level1);
//		for (int i = 0; i < PGSIZE; i += 2) {
//			//osDiskRead(currentDir->diskID, (unsigned)level1[i] + (unsigned)level1[i + 1] * 256, level2);
//			if (() && isOccupied((unsigned)level1[i] + (unsigned)level1[i + 1] * 256) == FALSE) {
//				return (unsigned)level1[i] + (unsigned)level1[i + 1] * 256;
//			}
//		}
//		return -1;
//	}
//
//	if (index_level == 2) {
//		osDiskRead(currentDir->diskID, index_location, level1);
//		for (int i = 0; i < PGSIZE; i += 2) {
//			osDiskRead(currentDir->diskID, (unsigned)level1[i] + (unsigned)level1[i + 1] * 256, level2);
//			for (int j = 0; j < PGSIZE; j += 2) {
//				//osDiskRead(currentDir->diskID, (unsigned)level2[j] + (unsigned)level2[j + 1] * 256, level3);
//				if (isOccupied((unsigned)level2[j] + (unsigned)level2[j + 1] * 256) == FALSE) {
//					return (unsigned)level2[j] + (unsigned)level2[j + 1] * 256;
//				}
//			}
//		}
//		return -1;
//	}
//
//	if (index_level == 3) {
//		osDiskRead(currentDir->diskID, index_location, level1);
//		for (int i = 0; i < PGSIZE; i += 2) {
//			osDiskRead(currentDir->diskID, (unsigned)level1[i] + (unsigned)level1[i + 1] * 256, level2);
//			for (int j = 0; j < PGSIZE; j += 2) {
//				osDiskRead(currentDir->diskID, (unsigned)level2[j] + (unsigned)level2[j + 1] * 256, level3);
//				for (int k = 0; k < PGSIZE; k += 2) {
//					//osDiskRead(currentDir->diskID, (unsigned)level3[k] + (unsigned)level3[k + 1] * 256, level4);
//					if (isOccupied((unsigned)level3[k] + (unsigned)level3[k + 1] * 256) {
//						return (unsigned)level3[k] + (unsigned)level3[k + 1] * 256;
//					}
//				}
//			}
//		}
//		return -1;
//	}
//
//	return -1;
//
//}

//int findDir(char* dirName) {
//	char header[PGSIZE];
//	osDiskRead(currentDir->diskID, currentDir->sector, header);
//	int index_location = (unsigned)header[13] * 256 + (unsigned)header[12];
//	int index_level = ((unsigned)(header[8] >> 1)) % 4;
//	printf("%d, %d\n", index_location, index_level);
//	char name[7];
//	char level1[PGSIZE];
//	char level2[PGSIZE];
//	char level3[PGSIZE];
//	char level4[PGSIZE];
//	if (index_level == 0) {
//		osDiskRead(currentDir->diskID, index_location, level1);
//		if (stringcmp(level1[1], dirName) == 0 && (unsigned)level1[8] % 2 == 1) {
//			return index_location;
//		}
//		else {
//			return -1;
//		}
//	}
//
//	if (index_level == 1) {
//		osDiskRead(currentDir->diskID, index_location, level1);
//		for (int i = 0; i < PGSIZE; i += 2) {
//				osDiskRead(currentDir->diskID, (unsigned)level1[i] + (unsigned)level1[i + 1] * 256, level2);
//				if (stringcmp(level2[1], dirName) == 0 && (unsigned)level2[8] % 2 == 1) {
//					return (unsigned)level2[i] + (unsigned)level2[i + 1] * 256;
//				}		
//		}
//		return -1;
//	}
//
//	if (index_level == 2) {
//		osDiskRead(currentDir->diskID, index_location, level1);
//		for (int i = 0; i < PGSIZE; i += 2) {
//			osDiskRead(currentDir->diskID, (unsigned)level1[i] + (unsigned)level1[i + 1] * 256, level2);
//			for (int j = 0; j < PGSIZE; j += 2) {
//				osDiskRead(currentDir->diskID, (unsigned)level2[j] + (unsigned)level2[j + 1] * 256, level3);
//				if (stringcmp(level3[1], dirName) == 0 && (unsigned)level3[8] % 2 == 1) {
//					return (unsigned)level3[j] + (unsigned)level3[j + 1] * 256;
//				}
//			}
//			
//		}
//		return -1;
//	}
//
//	if (index_level == 3) {
//		osDiskRead(currentDir->diskID, index_location, level1);
//		for (int i = 0; i < PGSIZE; i += 2) {
//			osDiskRead(currentDir->diskID, (unsigned)level1[i] + (unsigned)level1[i + 1] * 256, level2);
//			for (int j = 0; j < PGSIZE; j += 2) {
//				osDiskRead(currentDir->diskID, (unsigned)level2[j] + (unsigned)level2[j + 1] * 256, level3);
//				for (int k = 0; k < PGSIZE; k += 2) {
//					osDiskRead(currentDir->diskID, (unsigned)level3[k] + (unsigned)level3[k + 1] * 256, level4);
//					if (stringcmp(level4[1], dirName) == 0) {
//						return (unsigned)level4[k] + (unsigned)level4[k + 1] * 256;
//					}
//				}
//			}
//		}
//		return -1;
//	}
//
//	return -1;
//}