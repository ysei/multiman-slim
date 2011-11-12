#ifndef _ADEC_H_
#define _ADEC_H_

#include "util.h"
#include "common.h"
#include "avidmux.h"
#include "apost.h"

typedef union{
	CellAdecParamAc3	ac3;
	CellAdecParamMP3	mp3;
	CellAdecParamMpmc	mp2;
}UAdecParam;

typedef struct{
	bool		bAuFound;
	bool		bReady;
	bool		bStreamEnd;
	uint32_t	auDoneNum;
}SAdecStatus;

typedef struct{
	SCommonCtlInfo*		pCommonCtlInfo;
	SApostCtlInfo*		pApostCtlInfo;

	sys_ppu_thread_t	threadId;
	int					errorCode;
	UtilMonitor			umInput;

	AviDmuxEsHandle		dmuxEsHandle;

	uint16_t			prxModuleId;
	CellAdecType		decType;
	CellAdecHandle		decHandle;
	UAdecParam			decParam;
	uint32_t			chNum;
	uint32_t			bitSample;
	size_t				memSize;
	uint8_t*			pWorkMemory;
	SAdecStatus			recvStatus;
}SAdecCtlInfo;

CellAdecHandle adecGetAdecHandle(SAdecCtlInfo* pAdecCtlInfo);

void adecPcmOutCb(void* arg);

void adecDmuxCb(uint32_t msg, void* arg);

int adecSetParam(SAdecCtlInfo* pAdecCtlInfo, SCommonCtlInfo* pCommonCtlInfo,
	SApostCtlInfo* pApostCtlInfo, uint32_t chNum);

int adecOpen(SAdecCtlInfo* pAdecCtlInfo, const SAviDmuxStreamInfo* pStreamInfo,
	AviDmuxEsHandle esHandle, int32_t ppuThreadPriority, size_t ppuThreadStackSize,
	int32_t spuThreadPriority);

int adecClose(SAdecCtlInfo* pAdecCtlInfo);

int adecStart(SAdecCtlInfo* pAdecCtlInfo, int prio, size_t stacksize);

int adecEnd(SAdecCtlInfo* pAdecCtlInfo);

#endif /* _ADEC_H_ */
