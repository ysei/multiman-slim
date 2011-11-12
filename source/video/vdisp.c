
#include "vdisp.h"
#include "../include/graphics.h"

extern struct _CGprogram _binary_vpshader_vpo_start;
extern struct _CGprogram _binary_fpshader_fpo_start;

extern u8 *cFrame;
extern bool canDraw;
extern bool mm_flip_done;


static SVdispCtlInfo* g_pVdispCtlInfo = NULL;


static void flipv(SVdispCtlInfo* pVdispCtlInfo);
static void vdispWaitEvent(SVdispCtlInfo* pVdispCtlInfo, SVdispStatus* pVdispThreadStatus);
static void vdispThread(uint64_t arg);
static void vdispDrawTexture(SVdispCtlInfo* pVdispCtlInfo, void* pTexture);

void vdispSeqEndCb(SVdispCtlInfo* pVdispCtlInfo){
	utilMonitorLock(&pVdispCtlInfo->umInput, 0);
	pVdispCtlInfo->recvStatus.bSequence	= false;
	pVdispCtlInfo->recvStatus.bPicOut	= true;
	utilMonitorSignal(&pVdispCtlInfo->umInput);
	utilMonitorUnlock(&pVdispCtlInfo->umInput);
}

void vdispPictureOutCb(SVdispCtlInfo* pVdispCtlInfo){
	utilMonitorLock(&pVdispCtlInfo->umInput, 0);
	pVdispCtlInfo->recvStatus.bPicOut = true;
	utilMonitorSignal(&pVdispCtlInfo->umInput);
	utilMonitorUnlock(&pVdispCtlInfo->umInput);
}

int vdispGetResolution(uint32_t* pResolution,
	uint32_t* pDispWidth, uint32_t* pDispHeight)
{
	int					ret;
	CellVideoOutState	state;
	//CellVideoOutResolution	resolution;

	ret = cellVideoOutGetState(CELL_VIDEO_OUT_PRIMARY, 0, &state);
	if(ret < CELL_OK){
		return ret;
	}

//	ret = cellVideoOutGetResolution(state.displayMode.resolutionId, &resolution);
//	if(ret < CELL_OK){	return ret;	}

	*pResolution = 1;//state.displayMode.resolutionId;
	*pDispWidth = 1920;//resolution.width;
	*pDispHeight = 1080;//resolution.height;

	return ret;
}

int vdispSetParam(SVdispCtlInfo* pVdispCtlInfo,
	SCommonCtlInfo* pCommonCtlInfo, SAVsyncCtlInfo* pAVsyncCtlInfo,
	uint32_t dispWidth, uint32_t dispHeight, uint32_t width, uint32_t height,
	uint32_t frameNum, uint32_t resolution)
{
	pVdispCtlInfo->pCommonCtlInfo	= pCommonCtlInfo;
	pVdispCtlInfo->pAVsyncCtlInfo	= pAVsyncCtlInfo;

	(void)width;
	(void)height;
	pVdispCtlInfo->dispWidth		= dispWidth;
	pVdispCtlInfo->dispHeight		= dispHeight;
	pVdispCtlInfo->width			= dispWidth;
	pVdispCtlInfo->height			= dispHeight;
	pVdispCtlInfo->frameNum			= frameNum;
	pVdispCtlInfo->resolution		= resolution;

	g_pVdispCtlInfo = pVdispCtlInfo;

	return CELL_OK;
}

int vdispMapVframe(SVdispCtlInfo* pVdispCtlInfo, UtilMemPool* pMemPool){
	(void)pVdispCtlInfo;
	uint32_t	sMainBaseOffset;
	return cellGcmMapMainMemory(pMemPool->pTop,
		ROUNDUP(pMemPool->totalByte, ALIGN_1MBYTE), &sMainBaseOffset);
}

int vdispOpen(SVdispCtlInfo* pVdispCtlInfo)
{
	int		ret;

	if(utilMonitorInit(&pVdispCtlInfo->umInput)){
		ret = RET_CODE_ERR_FATAL;
		return ret;
	}

	return CELL_OK;
}

int vdispClose(SVdispCtlInfo* pVdispCtlInfo){
	if(utilMonitorFin(&pVdispCtlInfo->umInput)){
		return RET_CODE_ERR_FATAL;
	}

	//finish();

	return CELL_OK;
}

int vdispStart(SVdispCtlInfo* pVdispCtlInfo, int prio, size_t stacksize, SVFrameBuffer* pFrameBuf){
	pVdispCtlInfo->errorCode			= RET_CODE_ERR_INVALID;
	pVdispCtlInfo->pFrameBuf			= pFrameBuf;
	pVdispCtlInfo->recvStatus.bPicOut	= false;
	pVdispCtlInfo->recvStatus.bSequence	= true;

	int	ret = sys_ppu_thread_create(&pVdispCtlInfo->threadId, vdispThread, (uintptr_t)pVdispCtlInfo,
		prio, stacksize, SYS_PPU_THREAD_CREATE_JOINABLE, __func__);

	return ret;
}

int vdispEnd(SVdispCtlInfo* pVdispCtlInfo){
	uint64_t thrStat;
	int	ret = sys_ppu_thread_join(pVdispCtlInfo->threadId, &thrStat);
	if(ret < CELL_OK){
		return ret;
	}

	return pVdispCtlInfo->errorCode;
}

static void vdispThread(uint64_t arg){
	SVdispCtlInfo*			pVdispCtlInfo = (SVdispCtlInfo*)(uintptr_t)arg;
	uint32_t				queueFrameNum;
	SVFrameInfo*			pFrameInfo;
	SVFrameInfo				lastFrameInfo;
	SVdispStatus			threadStatus = pVdispCtlInfo->recvStatus;
	int						ret;

	DP("start...\n");

	while(!utilQueuePop(&pVdispCtlInfo->pFrameBuf->queue, &lastFrameInfo, false)){
		utilMemPoolPush(&pVdispCtlInfo->pFrameBuf->pool, lastFrameInfo.pFrame);
	}
	lastFrameInfo.pFrame = NULL;

	//vdispClearDisplay(pVdispCtlInfo);

	while(1){
		vdispWaitEvent(pVdispCtlInfo, &threadStatus);

		if(pVdispCtlInfo->errorCode != RET_CODE_ERR_INVALID){
			break;
		}
		if( !(commonGetMode( pVdispCtlInfo->pCommonCtlInfo ) & MODE_EXEC ) ){
			break;
		}

		SCommonCtlInfo*	pCommonCtlInfo = pVdispCtlInfo->pCommonCtlInfo;
		if( !( commonGetVideoStatus( pCommonCtlInfo ) & STATUS_READY ) ){

			utilQueuePeek(&pVdispCtlInfo->pFrameBuf->queue, (void*)&pFrameInfo, &queueFrameNum);
			if(queueFrameNum >= pVdispCtlInfo->frameNum){
				commonSetVideoStatus( pCommonCtlInfo, STATUS_READY, BIT_SET );
			}else{
				threadStatus.bPicOut = false;
				//vdispClearDisplay(pVdispCtlInfo);
			}
		}

		if(	threadStatus.bPicOut &&
			( commonGetVideoStatus( pCommonCtlInfo ) & STATUS_READY ) &&
			( commonGetAudioStatus( pCommonCtlInfo ) & STATUS_READY ) ) {

			utilQueuePeek(&pVdispCtlInfo->pFrameBuf->queue, (void*)&pFrameInfo, &queueFrameNum);
			if(queueFrameNum == 0) {
				threadStatus.bPicOut = false;
				if(lastFrameInfo.pFrame){
					vdispDrawTexture(pVdispCtlInfo, lastFrameInfo.pFrame);
				}else{
					//vdispClearDisplay(pVdispCtlInfo);
				}
				if(!threadStatus.bSequence){
					pVdispCtlInfo->errorCode = CELL_OK;
					commonSetVideoStatus( pCommonCtlInfo, STATUS_END, BIT_SET );
					break;
				}
			}else{
				int avsyncVdispMode;
				if(pFrameInfo->bPtsValid){
					avsyncSetVpts(pVdispCtlInfo->pAVsyncCtlInfo, pFrameInfo->pts);
					avsyncVdispMode = avsyncCompare(pVdispCtlInfo->pAVsyncCtlInfo);
				}else{
					avsyncVdispMode = AVSYNC_VDISP_MODE_NORMAL;
				}

				switch(avsyncVdispMode) {
				case AVSYNC_VDISP_MODE_NORMAL:
					vdispDrawTexture(pVdispCtlInfo, pFrameInfo->pFrame);
					pFrameInfo->bPtsValid = false;
					if(--pFrameInfo->prsnNum < 1){
						if(lastFrameInfo.pFrame){
							utilMemPoolPush(&pVdispCtlInfo->pFrameBuf->pool, lastFrameInfo.pFrame);
						}
						utilQueuePop(&pVdispCtlInfo->pFrameBuf->queue, &lastFrameInfo, false);
					}
					break;

				case AVSYNC_VDISP_MODE_WAIT:
					if(lastFrameInfo.pFrame){
						vdispDrawTexture(pVdispCtlInfo, lastFrameInfo.pFrame);
					}else{
						//vdispClearDisplay(pVdispCtlInfo);
					}
					break;

				case AVSYNC_VDISP_MODE_SKIP:
					if(lastFrameInfo.pFrame){
						utilMemPoolPush(&pVdispCtlInfo->pFrameBuf->pool, lastFrameInfo.pFrame);
					}
					utilQueuePop(&pVdispCtlInfo->pFrameBuf->queue, &lastFrameInfo, false);
					break;
				}
			}
		}

		// check sysutil event
		ret = cellSysutilCheckCallback();
		if(ret < CELL_OK){
			EMSG("cellSysutilCheckCallback() = 0x%x\n", ret);
		}
	}

	if(pVdispCtlInfo->errorCode < CELL_OK
	&& pVdispCtlInfo->errorCode != RET_CODE_ERR_INVALID){
		commonErrorExit(pVdispCtlInfo->pCommonCtlInfo, pVdispCtlInfo->errorCode);
		EINFO(pVdispCtlInfo->errorCode);
	}else{
		pVdispCtlInfo->errorCode = CELL_OK;
	}

	if(lastFrameInfo.pFrame){
		utilMemPoolPush(&pVdispCtlInfo->pFrameBuf->pool, lastFrameInfo.pFrame);
	}
	while(!utilQueuePop(&pVdispCtlInfo->pFrameBuf->queue, &lastFrameInfo, false)){
		utilMemPoolPush(&pVdispCtlInfo->pFrameBuf->pool, lastFrameInfo.pFrame);
	}

	//vdispClearDisplay(pVdispCtlInfo);

	DP("end...\n");

	sys_ppu_thread_exit(0);
}

static void vdispWaitEvent(SVdispCtlInfo* pVdispCtlInfo, SVdispStatus* pVdispThreadStatus){
	SVdispStatus* pRecvStatus = &pVdispCtlInfo->recvStatus;

	utilMonitorLock(&pVdispCtlInfo->umInput, 0);

	// update receiving status with thread status
	if(!pRecvStatus->bPicOut){
		pRecvStatus->bPicOut = pVdispThreadStatus->bPicOut;
	}

	// wait condition
	while(	( commonGetMode( pVdispCtlInfo->pCommonCtlInfo ) & MODE_EXEC ) &&
			pVdispCtlInfo->errorCode == RET_CODE_ERR_INVALID &&
			!pRecvStatus->bPicOut ) {
		utilMonitorWait(&pVdispCtlInfo->umInput, 0);
	}

	// copy from receiving status to thread status
	*pVdispThreadStatus = *pRecvStatus;

	// clear receiving status
	pRecvStatus->bPicOut = false;

	utilMonitorUnlock(&pVdispCtlInfo->umInput);
}

static void vdispDrawTexture(SVdispCtlInfo* pVdispCtlInfo, void* pTexture){
	cFrame=pTexture;
	flipv(pVdispCtlInfo);
}


static void flipv(SVdispCtlInfo* pVdispCtlInfo){
	utilMonitorLock(&pVdispCtlInfo->umInput, 0);
	pVdispCtlInfo->bFlipDone = false;
	utilMonitorSignal(&pVdispCtlInfo->umInput);
	mm_flip_done=false;
	while(1){
		pVdispCtlInfo->bFlipDone=mm_flip_done;
		sys_timer_usleep(10427);
		break;

	}
	pVdispCtlInfo->bFlipDone = true;
	utilMonitorSignal(&pVdispCtlInfo->umInput);
	utilMonitorUnlock(&pVdispCtlInfo->umInput);
}

