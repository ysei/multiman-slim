#ifndef _VDISP_H_
#define _VDISP_H_

#include <cell/gcm.h>
#include "util.h"
#include "common.h"
#include "avsync.h"

typedef struct{
	float Px, Py, Pz;
	float Tx, Ty;
}Vertex_t;

typedef struct{
	UtilQueue			queue;
	UtilMemPool			pool;
}SVFrameBuffer;

typedef struct{
	void*				pFrame;
	int32_t				prsnNum;
	bool				bPtsValid;
	CellCodecTimeStamp	pts;
}SVFrameInfo;

typedef struct{
	bool				bPicOut;
	bool				bSequence;
}SVdispStatus;

typedef struct{
	SCommonCtlInfo*		pCommonCtlInfo;
	SAVsyncCtlInfo*		pAVsyncCtlInfo;

	sys_ppu_thread_t	threadId;
	int					errorCode;
	UtilMonitor			umInput;

	SVFrameBuffer*		pFrameBuf;
	uint32_t			dispWidth;
	uint32_t			dispHeight;
	uint32_t			width;
	uint32_t			height;
	uint32_t			resolution;
	uint32_t			frameNum;
	SVdispStatus		recvStatus;

	bool				bFlipDone;
	unsigned int		mFrameIndex;
	void*				mFrameAddress[3];
	uint32_t			mFrameOffset[3];
	unsigned int		mFramePitch[3];
	uint32_t			mDefaultCB;
	uint32_t			mFrameSize;
	uint32_t			mDepthSize;

	CGresource			mSampler;
	CGresource			mObjCoordIndex;
	CGresource			mTexCoordIndex;
	CGprogram			mCGVertexProgram;
	CGprogram			mCGFragmentProgram;
	CGparameter 		mObjCoord;
	CGparameter 		mTexCoord;
	void*				mVertexProgramUCode;
	void*				mFragmentProgramUCode;
	uint32_t			mFragmentProgramOffset;
	Vertex_t*			mVertexBuffer;
	uint32_t			mVertexBufferOffset;
	CellGcmTexture		mTexture;
	void*				mTextureAddress;
}SVdispCtlInfo;

void vdispSeqEndCb(SVdispCtlInfo* pVdispCtlInfo);

void vdispPictureOutCb(SVdispCtlInfo* pVdispCtlInfo);

int vdispGetResolution(uint32_t* pResolution,
	uint32_t* pDispWidth, uint32_t* pDispHeight);

int vdispSetParam(SVdispCtlInfo* pVdispCtlInfo,
	SCommonCtlInfo* pCommonCtlInfo, SAVsyncCtlInfo* pAVsyncCtlInfo,
	uint32_t dispWidth, uint32_t dispHeight, uint32_t width, uint32_t height,
	uint32_t frameNum, uint32_t resolution);

int vdispMapVframe(SVdispCtlInfo* pVdispCtlInfo, UtilMemPool* pMemPool);

int vdispOpen(SVdispCtlInfo* pVdispCtlInfo);

int vdispClose(SVdispCtlInfo* pVdispCtlInfo);

int vdispStart(SVdispCtlInfo* pVdispCtlInfo, int prio, size_t stacksize, SVFrameBuffer* pFrameBuf);

int vdispEnd(SVdispCtlInfo* pVdispCtlInfo);

#endif /* _VDISP_H_ */
