#ifndef _VPOST_H_
#define _VPOST_H_

#include "util.h"
#include "common.h"
#include "vdisp.h"

typedef void (*vpostCbFunc)(void* arg);

typedef struct{
	bool		bPicOut;
	bool		bSequence;
}SVpostStatus;

typedef struct{
	SCommonCtlInfo*		pCommonCtlInfo;
	SVdispCtlInfo*		pVdispCtlInfo;

	sys_ppu_thread_t	threadId;
	int					errorCode;
	UtilMonitor			umInput;

	CellVdecHandle		decHandle;
	vpostCbFunc			cbVdecFunc;
	void*				cbVdecArg;

	CellVpostHandle		postHandle;
	size_t				memSize;
	uint8_t*			pWorkMemory;
	uint8_t*			pDecodedFrame;

	uint32_t			width;
	uint32_t			height;
	uint32_t			dispWidth;
	uint32_t			dispHeight;
	uint32_t			frameNum;
	SVFrameBuffer		frameBuf;
	SVFrameBuffer*		pFrameBuf;
	uint32_t			frameRate;

	SVpostStatus		recvStatus;
}SVpostCtlInfo;

SVFrameBuffer* vpostGetFrameBuffer(SVpostCtlInfo* pVpostCtlInfo);

void vpostSeqEndCb(SVpostCtlInfo* pVpostCtlInfo);

void vpostPictureOutCb(SVpostCtlInfo* pVpostCtlInfo);

int vpostSetParam(SVpostCtlInfo* pVpostCtlInfo, SCommonCtlInfo* pCommonCtlInfo,
	SVdispCtlInfo* pVdispCtlInfo, uint32_t width, uint32_t height,
	uint32_t dispWidth, uint32_t dispHeight, uint32_t frameNum);

int vpostOpen(SVpostCtlInfo* pVpostCtlInfo, int32_t ppuThreadPriority,
	size_t ppuThreadStackSize, int32_t spuThreadPriority, uint32_t numOfSpus);

int vpostClose(SVpostCtlInfo* pVpostCtlInfo);

int vpostStart(SVpostCtlInfo* pVpostCtlInfo, int prio, size_t stacksize,
	CellVdecHandle decHandle, vpostCbFunc vdecPicOutFunc, void* vdecPicOutArg);

int vpostEnd(SVpostCtlInfo* pVpostCtlInfo);

#endif /* _VPOST_H_ */
