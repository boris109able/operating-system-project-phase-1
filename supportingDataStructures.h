#ifndef SUPPORTINGDATASTRUCTURES_H_
#define SUPPORTINGDATASTRUCTURES_H_

#define			NODE_TYPE_MISSIONCOMPLETE       3
#define         NODE_TYPE_TIMER                 0
#define         NODE_TYPE_DISKWRITE             1
#define			NODE_TYPE_DISKREAD				2

typedef struct dir {
	int diskID;
	int sector;
	char dirName[7];
	int isSet;
}dir;

typedef struct stackItem {
	dir* dir;
	dir* next;
}stackItem;

typedef struct stack {
	struct stackItem* head;
	struct stackItem* tail;
}stack;

typedef struct File {
	char name[7];
	int fileSector;
	int Inode;
	int isSet;
}File;

typedef struct {
	int   pid;
	char* name;
	long  contextAddr;//serves as ID
	long  timeToGoOut;//only valid for PCB in timer queue
	char* content;//the pointer for reading and writing disk content
	long  diskID;
	long  diskSector;
	dir*  currentDir;
	stack* dirStack;
	File* currentFile;
	int childPID[15];
}ProcessInfomation;

typedef struct {
	long processId;
	char* processName;
}ProcessNameId;

typedef struct QueueNode
{
	struct QueueNode        *next;
	ProcessInfomation       *info;
	int nodeType;
}QueueNode;

typedef struct
{
	QueueNode               *head;
	QueueNode               *tail;
}Queue;

ProcessInfomation* createProcessInfomation3(char* name, long Addr, char* content, long diskID, long diskSector);
ProcessInfomation* createProcessInfomation2(char* name, long Addr, long timeToGoOut);
ProcessInfomation* createProcessInfomation(long Addr, long timeToGoOut);
void               destroyProcessInfomation(ProcessInfomation* pinfo);
QueueNode*         createQueueNode(void *info, int nodeType);
void               destroyQueueNode(QueueNode* node);
Queue*             createQueue();
void               destroyQueue(Queue* tq);
int                QueueSize(Queue *tq);
int                QueueIsEmpty(Queue *tq);
void               QueueOfferLast(Queue *tq, QueueNode *node);
void               QueueOfferFirst(Queue *tq, QueueNode *node);
QueueNode*         ReturnTimerQueueFailureInfo();
QueueNode*         QueuePollFirst(Queue* tq);
QueueNode*         QueuePollLast(Queue* tq);
QueueNode*		   QueuePeekFirst(Queue* tq);
QueueNode*		   QueuePeekLast(Queue* tq);
int                NodeCompare(QueueNode* node1, QueueNode* node2, int nodeType);
void               QueueInsert(Queue* tq, QueueNode* node, int nodeType);
void               printQueueNode(QueueNode* node, int nodeType);
void               printQueue(Queue* tq);
int                searchNameInQueue(Queue* tq, char* name);
int                searchProcessIDInQueue(Queue* tq, long id);
int                stillInDiskQueue(Queue* diskQueue, int sector);
QueueNode*         QueueRemove(Queue* tq, int index);
QueueNode*         QueueGet(Queue* tq, int index);



dir*       createDir(int diskID, int sector, char* dirName, int isSet);;
stackItem* createStackItem(dir* Dir);
stack*     createStack();
void       destroyDir(dir* Dir);
void       destoryItem(stackItem* StackItem);
void       destoryStack(stack* Stack);
int        stackIsEmpty(stack* Stack);
int        stackSize(stack* Stack);
void       push(stack* Stack, stackItem* Item);
stackItem* pop(stack* Stack);

#endif