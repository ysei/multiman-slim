#ifndef _APOST_H_
#define _APOST_H_

#include "util.h"
#include "common.h"
#include "amixer.h"

typedef void (*apostCbFunc)(void* arg);

typedef struct{
	bool		bPcmOut;
	bool		bSequence;
}SApostStatus;

typedef struct{
	SCommonCtlInfo*		pCommonCtlInfo;
	SAmixerCtlInfo*		pAmixerCtlInfo;

	sys_ppu_thread_t	threadId;
	int					errorCode;
	UtilMonitor			umInput;

	CellAdecHandle		decHandle;
	apostCbFunc			cbAdecFunc;
	void*				cbAdecArg;

	uint32_t			chNum;
	uint32_t			auSample;
	uint32_t			pcmNum;
	SAPcmBuffer			pcmBuf;
	SAPcmBuffer*		pPcmBuf;

	SApostStatus		recvStatus;
}SApostCtlInfo;

SAPcmBuffer* apostGetPcmBuffer(SApostCtlInfo* pApostCtlInfo);

void apostSeqEndCb(SApostCtlInfo* pApostCtlInfo);

void apostPcmOutCb(SApostCtlInfo* pApostCtlInfo);

int apostSetParam(SApostCtlInfo* pApostCtlInfo, SCommonCtlInfo* pCommonCtlInfo,
	SAmixerCtlInfo* pAmixerCtlInfo, uint32_t chNum, uint32_t auSample, uint32_t pcmNum);

int apostOpen(SApostCtlInfo* pApostCtlInfo);

int apostClose(SApostCtlInfo* pApostCtlInfo);

int apostStart(SApostCtlInfo* pApostCtlInfo, int prio, size_t stacksize,
	CellAdecHandle decHandle, apostCbFunc adecPcmOutFunc, void* adecPcmOutArg);

int apostEnd(SApostCtlInfo* pApostCtlInfo);

#endif /* _APOST_H_ */
