#ifndef _AMIXER_H_
#define _AMIXER_H_

#include "util.h"
#include "common.h"
#include "avsync.h"
#include <cell/audio.h>
#include <cell/mixer.h>

typedef struct{
	UtilQueue			queue;
	UtilMemPool			pool;
}SAPcmBuffer;

typedef struct{
	void*				pPcm;
	CellCodecTimeStamp	pts;
	uint32_t			index;
}SAPcmInfo;

typedef struct{
	SCommonCtlInfo*		pCommonCtlInfo;
	SAVsyncCtlInfo*		pAVsyncCtlInfo;

	sys_ppu_thread_t	threadId;
	int					errorCode;
	UtilMonitor			umInput;

	bool				bSequence;
	bool				bReqStop;
	bool				bReady;

	uint32_t			chNum;
	uint32_t			auSample;
	uint32_t			pcmNum;
	SAPcmBuffer*		pPcmBuf;
	float*				pMixerBuffer;
	CellCodecTimeStamp	lastPts;

	CellAANHandle		mixer;
	uint32_t			portNo;
}SAmixerCtlInfo;

int amixerSetParam(SAmixerCtlInfo* pAmixerCtlInfo,
	SCommonCtlInfo* pCommonCtlInfo, SAVsyncCtlInfo* pAVsyncCtlInfo,
	uint32_t chNum, uint32_t auSample, uint32_t pcmNum);

int amixerOpen(SAmixerCtlInfo* pAmixerCtlInfo, uint32_t prio);

int amixerClose(SAmixerCtlInfo* pAmixerCtlInfo);

int amixerStart(SAmixerCtlInfo* pAmixerCtlInfo, SAPcmBuffer* pPcmBuf);

int amixerEnd(SAmixerCtlInfo* pAmixerCtlInfo);

#endif /* _AMIXER_H_ */
