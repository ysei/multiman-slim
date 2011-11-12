#ifndef _VDEC_H_
#define _VDEC_H_

#include "util.h"
#include "common.h"
#include "avidmux.h"
#include "vpost.h"

typedef struct{
	bool		bAuFound;
	bool		bReady;
	bool		bStreamEnd;
	uint32_t	auDoneNum;
}SVdecStatus;

typedef struct{
	SCommonCtlInfo*		pCommonCtlInfo;
	SVpostCtlInfo*		pVpostCtlInfo;

	sys_ppu_thread_t	threadId;
	int					errorCode;
	UtilMonitor			umInput;

	AviDmuxEsHandle		dmuxEsHandle;

	uint16_t			prxModuleId;
	CellVdecType		decType;
	CellVdecHandle		decHandle;
	size_t				memSize;
	uint8_t*			pWorkMemory;
	CellVdecFrameRate	frameRate;

	SVdecStatus			recvStatus;
}SVdecCtlInfo;

CellVdecHandle vdecGetVdecHandle(SVdecCtlInfo* pVdecCtlInfo);

void vdecPictureOutCb(void* arg);

void vdecDmuxCb(uint32_t msg, void* arg);

int vdecSetParam(SVdecCtlInfo* pVdecCtlInfo, SCommonCtlInfo* pCommonCtlInfo,
	SVpostCtlInfo* pVpostCtlInfo);

int vdecOpen(SVdecCtlInfo* pVdecCtlInfo, const SAviDmuxStreamInfo* pStreamInfo,
	AviDmuxEsHandle esHandle, int32_t ppuThreadPriority, size_t ppuThreadStackSize,
	int32_t spuThreadPriority, uint32_t numOfSpus);

int vdecClose(SVdecCtlInfo* pVdecCtlInfo);

int vdecStart(SVdecCtlInfo* pVdecCtlInfo, int prio, size_t stacksize);

int vdecEnd(SVdecCtlInfo* pVdecCtlInfo);

#endif /* _VDEC_H_ */
