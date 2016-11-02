#include <stdlib.h>
#include "supportingDataStructures.h"

ProcessInfomation* createProcessInfomation3(char* name, long Addr, char* content, long diskID, long diskSector) {
	ProcessInfomation* pinfo = malloc(sizeof(ProcessInfomation));
	char* eachName = (char *)malloc(strlen(name));//note we need to always allocate new char array for the name instead of just transferring the pointer
	strcpy(eachName, name);
	//char* charp = (char *)malloc(strlen(content));
	//strcpy(charp, content);
	//pinfo->content = charp;
	pinfo->content = content;
	pinfo->name = eachName;
	pinfo->contextAddr = Addr;
	pinfo->diskID = diskID;
	pinfo->diskSector = diskSector;

	pinfo->dirStack = createStack();
	pinfo->currentDir = (dir*)malloc(sizeof(dir));
	pinfo->currentFile = (File*)malloc(sizeof(File));
	pinfo->currentDir->isSet = 0;
	pinfo->currentFile->isSet = 0;

	short i;
	for (i = 0; i < 15; i++) {
		pinfo->childPID[i] = -1;
	}

	return pinfo;
}

ProcessInfomation* createProcessInfomation2(char* name, long Addr, long timeToGoOut) {
	ProcessInfomation* pinfo = malloc(sizeof(ProcessInfomation));
	char* eachName = (char *)malloc(strlen(name));//note we need to always allocate new char array for the name instead of just transferring the pointer
	strcpy(eachName, name);
	pinfo->name = eachName;
	pinfo->contextAddr = Addr;
	pinfo->timeToGoOut = timeToGoOut;

	pinfo->dirStack = createStack();
	pinfo->currentDir = (dir*)malloc(sizeof(dir));
	pinfo->currentFile = (File*)malloc(sizeof(File));
	pinfo->currentDir->isSet = 0;
	pinfo->currentFile->isSet = 0;

	short i;
	for (i = 0; i < 15; i++) {
		pinfo->childPID[i] = -1;
	}

	return pinfo;
}
ProcessInfomation* createProcessInfomation(long Addr, long timeToGoOut) {
	ProcessInfomation* pinfo = malloc(sizeof(ProcessInfomation));
	pinfo->contextAddr = Addr;
	pinfo->timeToGoOut = timeToGoOut;

	pinfo->dirStack = createStack();
	pinfo->currentDir = (dir*)malloc(sizeof(dir));
	pinfo->currentFile = (File*)malloc(sizeof(File));
	pinfo->currentDir->isSet = 0;
	pinfo->currentFile->isSet = 0;

	short i;
	for (i = 0; i < 15; i++) {
		pinfo->childPID[i] = -1;
	}

	return pinfo;
}
void destroyProcessInfomation(ProcessInfomation* pinfo) {
	free(pinfo->currentDir);
	free(pinfo->currentFile);
	free(pinfo->dirStack);
	free(pinfo);
}
QueueNode* createQueueNode(void *info, int nodeType) {
	QueueNode* node = malloc(sizeof(QueueNode));
	node->info = info;
	node->next = NULL;
	node->nodeType = nodeType;
	return node;
}
void destroyQueueNode(QueueNode* node) {
	destroyProcessInfomation(node->info);
	free(node);
}
Queue* createQueue() {
	Queue* tq = malloc(sizeof(Queue));
	tq->head = NULL;
	tq->tail = NULL;
	return tq;
}
void destroyQueue(Queue* tq) {
	QueueNode* now = tq->head;
	if (now == NULL) {
		free(tq);
		return;
	}
	QueueNode* next = now->next;
	while (now != NULL) {
		destroyQueueNode(now);
		now = next;
		if (next == NULL) {
			break;
		}
		next = next->next;
	}
	free(tq);
}
int QueueSize(Queue *tq) {
	QueueNode* now = tq->head;
	int n = 0;
	while (now != NULL) {
		now = now->next;
		n++;
	}
	return n;
}
int QueueIsEmpty(Queue *tq) {
	if (tq->head == NULL) {
		return 1;
	}
	return 0;
}
void QueueOfferLast(Queue *tq, QueueNode *node) {
	if (tq->head == NULL) {
		tq->head = node;
		tq->tail = node;
		return;
	}
	tq->tail->next = node;
	tq->tail = tq->tail->next;
	tq->tail->next = NULL;
}
void QueueOfferFirst(Queue *tq, QueueNode *node) {
	if (tq->head == NULL) {
		tq->head = node;
		tq->tail = node;
		return;
	}
	QueueNode *tmpnode = tq->head;
	tq->head = node;
	node->next = tmpnode;
}

void QueueOfferSecond(Queue *tq, QueueNode *node) {
	if (tq->head == NULL) {
		tq->head = node;
		tq->tail = node;
		return;
	}
	QueueNode* second = tq->head->next;
	node->next = second;
	tq->head->next = node;
}

QueueNode* ReturnTimerQueueFailureInfo() {
	QueueNode *failure = malloc(sizeof(QueueNode));
	failure->next = NULL;
	ProcessInfomation *failureinfo = malloc(sizeof(ProcessInfomation));
	failureinfo->contextAddr = -1;
	failureinfo->timeToGoOut = -1;
	failure->info = (void *)failureinfo;
	return failure;
}
QueueNode* QueuePollFirst(Queue* tq) {
	if (QueueIsEmpty(tq) == 1) {
		return ReturnTimerQueueFailureInfo();
	}
	if (QueueSize(tq) == 1) {
		QueueNode *only = tq->head;
		tq->head = NULL;
		tq->tail = NULL;
		return only;
	}
	QueueNode *node = tq->head;
	tq->head = node->next;
	node->next = NULL;
	return node;
}
QueueNode* QueuePollLast(Queue* tq) {
	if (QueueIsEmpty(tq) == 1) {
		return ReturnTimerQueueFailureInfo();;
	}
	if (QueueSize(tq) == 1) {
		QueueNode* only = tq->head;
		tq->head = NULL;
		tq->tail = NULL;
		return only;
	}
	QueueNode *now = tq->head;
	QueueNode *prev = NULL;
	while (now->next != NULL) {
		prev = now;
		now = now->next;
	}
	tq->tail = prev;
	prev->next = NULL;
	return now;
}
QueueNode* QueuePeekFirst(Queue* tq) {
	return tq->head;
}
QueueNode* QueuePeekLast(Queue* tq) {
	return tq->tail;
}

int NodeCompare(QueueNode* node1, QueueNode* node2, int nodeType) {
	if (nodeType == NODE_TYPE_TIMER) {
		ProcessInfomation* pinfo1 = (ProcessInfomation*)node1->info;
		ProcessInfomation* pinfo2 = (ProcessInfomation*)node2->info;
		if (pinfo1->timeToGoOut > pinfo2->timeToGoOut) {
			return 1;
		}
		else if (pinfo1->timeToGoOut < pinfo2->timeToGoOut) {
			return -1;
		}
	}
	return 0;
}
void QueueInsert(Queue* tq, QueueNode* node, int nodeType) {
	if (QueueIsEmpty(tq)) {
		tq->head = node;
		tq->tail = node;
		return;
	}
	if (NodeCompare(node, tq->head, nodeType) <= 0) {
		node->next = tq->head;
		tq->head = node;
		return;
	}
	if (NodeCompare(node, tq->tail, nodeType) >= 0) {
		tq->tail->next = node;
		tq->tail = node;
		return;
	}
	QueueNode* prev = NULL;
	QueueNode* now = tq->head;
	while (NodeCompare(node, now, nodeType) > 0) {
		prev = now;
		now = now->next;
	}
	prev->next = node;
	node->next = now;
}
void printQueueNode(QueueNode* node, int nodeType) {
	//if (nodeType == NODE_TYPE_TIMER) {
		ProcessInfomation* pinfo = (ProcessInfomation*)node->info;
		if (pinfo->name == NULL || strlen(pinfo->name) == 0) {
			printf("         %ld    %ld\n", pinfo->pid, pinfo->timeToGoOut);
		}
		else {
			printf("%s    %ld    %ld\n", pinfo->name, pinfo->pid, pinfo->timeToGoOut);
		}	
	//}
}
void printQueue(Queue* tq) {
	if (QueueIsEmpty(tq)) {
		printf("Empty queue!\n");
		return;
	}
	QueueNode* now = tq->head;
	while (now != NULL) {
		printQueueNode(now, now->nodeType);
		now = now->next;
	}
}
int searchNameInQueue(Queue* tq, char* name) {
	QueueNode* node = tq->head;
	char* s;
	ProcessInfomation* pcb;
	int pos = 0;
	while (node != NULL) {
		pcb = (ProcessInfomation*)node->info;
		s = pcb->name;
		if (strcmp(name, s) == 0) {
			return pos;
		}
		node = node->next;
		pos++;
	}
	return -1;
}

int searchProcessIDInQueue(Queue* tq, long id) {
	QueueNode* node = tq->head;
	long addr;
	int pos = 0;
	ProcessInfomation* pcb;
	while (node != NULL) {
		pcb = (ProcessInfomation*)node->info;
		addr = pcb->contextAddr;
		if (addr == id) {
			return pos;
		}
		node = node->next;
		pos++;
	}
	return -1;
}

int stillInDiskQueue(Queue* diskQueue, int sector) {
	int isIn = 0;
	QueueNode* now = diskQueue->head;
	while (now != NULL) {
		if (now->info->diskSector == sector) {
			isIn = 1;
			break;
		}
		now = now->next;
	}
	return isIn;
}

QueueNode* QueueRemove(Queue* tq, int index) {
	QueueNode* node;
	int len = QueueSize(tq);
	if (QueueIsEmpty(tq) == 1 || index >= len || index < 0) {
		return NULL;
	}

	if (len == 1) {
		node = tq->head;
		tq->head = NULL;
		tq->tail = NULL;
		return node;
	}

	if (index == 0) {
		Queue* node = tq->head;
		tq->head = tq->head->next;
		if (tq->head == NULL) {
			tq->tail = NULL;
		}
		return node;
	}

	node = tq->head;
	QueueNode* prev = NULL;
	int pos = 0;
	while (pos != index) {
		prev = node;
		node = node->next;
		if (node == NULL) {
			return NULL;
		}
		pos++;
	}
	if (node == tq->tail) {
		tq->tail = prev;
	}
	prev->next = node->next;	
	return node;
}

QueueNode* QueueGet(Queue* tq, int index) {
	QueueNode* node = tq->head;
	int pos = 0;
	while (pos != index) {
		node = node->next;
		if (node == NULL) {
			return NULL;
		}
		pos++;
	}
	return node;
}

void QueueRemoveAndDestory(Queue* tq, QueueNode* node) {
	if (node == NULL) {
		return;
	}
	long id = node->info->contextAddr;
	if (tq->head == NULL) {
		return;
	}
	if (tq->head == tq->tail) {
		if (tq->head->info->contextAddr == id) {
			tq->head = NULL;
			tq->tail = NULL;
			return;
		}
		else {
			return;
		}
	}

	QueueNode* prev = NULL;
	QueueNode* now = tq->head;
	while (now->info->contextAddr != id) {
		prev = now;
		now = now->next;
		if (now == NULL) {
			return;
		}
	}
	prev->next = now->next;
	destroyQueueNode(now);
}

int QueueContainsPID(Queue* tq, int pid) {
	QueueNode* now = tq->head;
	while (now != NULL) {
		if (now->info->pid == pid) {
			return 1;
		}
		now = now->next;
	}
	return 0;
}
/*
Stack (implementation in directory ) 
FIFO using deque
*/


dir* createDir(int diskID, int sector, char* dirName, int isSet) {
	dir* curr = (dir*)malloc(sizeof(dir));
	memset(curr->dirName, '\0', 7);
	memcpy(curr->dirName, dirName, strlen(dirName) + 1);
	curr->diskID = diskID;
	curr->sector = sector;
	curr->isSet = isSet;
	return curr;
}

stackItem* createStackItem(dir* Dir) {
	stackItem* item = (stackItem*)malloc(sizeof(stackItem));
	item->dir = Dir;
	item->next = NULL;
	return item;
}

stack* createStack() {
	stack* Stack = (stack*)malloc(sizeof(stack));
	Stack->head = NULL;
	Stack->tail = NULL;
	return Stack;
}

void destroyDir(dir* Dir) {
	free(Dir);
}

void destoryItem(stackItem* StackItem) {
	destroyDir(StackItem->dir);
	free(StackItem);
}

void destoryStack(stack* Stack) {
	stackItem* now = Stack->head;
	stackItem* next;
	while (now != NULL) {
		next = now->next;
		destoryItem(now);
		now = next;
	}
}

int stackIsEmpty(stack* Stack) {
	if (Stack->head == NULL) {
		return 1;
	}
	else {
		return 0;
	}
}

int stackSize(stack* Stack) {
	stackItem* now = Stack->head;
	int count = 0;
	while (now != NULL) {
		count++;
		now = now->next;
	}
	return count;
}

void push(stack* Stack, stackItem* Item) {
	if (stackIsEmpty(Stack) == 1) {
		Stack->head = Item;
		Stack->tail = Item;
	}
	else {
		Stack->tail->next = Item;
		Stack->tail = Stack->tail->next;
	}
}

stackItem* pop(stack* Stack) {
	if (stackIsEmpty(Stack) == 1) {
		return NULL;
	}
	if (stackSize(Stack) == 1) {
		stackItem* only = Stack->head;
		Stack->head = NULL;
		Stack->tail = NULL;
		return only;
	}
	stackItem* prev = NULL;
	stackItem* now = Stack->head;
	while (now != Stack->tail) {
		prev = now;
		now = now->next;
	}
	prev->next = NULL;
	Stack->tail = prev;
	return now;
}

