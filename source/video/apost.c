#include "apost.h"

static void apostWaitEvent(SApostCtlInfo* pApostCtlInfo, SApostStatus* pApostThreadStatus);
static void apostThread(uint64_t arg);

SAPcmBuffer* apostGetPcmBuffer(SApostCtlInfo* pApostCtlInfo){
	return pApostCtlInfo->pPcmBuf;
}

void apostSeqEndCb(SApostCtlInfo* pApostCtlInfo){
	utilMonitorLock(&pApostCtlInfo->umInput, 0);
	pApostCtlInfo->recvStatus.bSequence	= false;
	pApostCtlInfo->recvStatus.bPcmOut	= true;
	utilMonitorSignal(&pApostCtlInfo->umInput);
	utilMonitorUnlock(&pApostCtlInfo->umInput);
}

void apostPcmOutCb(SApostCtlInfo* pApostCtlInfo){
	utilMonitorLock(&pApostCtlInfo->umInput, 0);
	pApostCtlInfo->recvStatus.bPcmOut = true;
	utilMonitorSignal(&pApostCtlInfo->umInput);
	utilMonitorUnlock(&pApostCtlInfo->umInput);
}

int apostSetParam(SApostCtlInfo* pApostCtlInfo, SCommonCtlInfo* pCommonCtlInfo,
	SAmixerCtlInfo* pAmixerCtlInfo, uint32_t chNum, uint32_t auSample, uint32_t pcmNum)
{
	pApostCtlInfo->pCommonCtlInfo	= pCommonCtlInfo;
	pApostCtlInfo->pAmixerCtlInfo	= pAmixerCtlInfo;

	pApostCtlInfo->chNum			= chNum;
	pApostCtlInfo->auSample			= auSample;
	pApostCtlInfo->pcmNum 			= pcmNum;

	pApostCtlInfo->pPcmBuf 			= &pApostCtlInfo->pcmBuf;

	return CELL_OK;
}

int apostOpen(SApostCtlInfo* pApostCtlInfo)
{
	if(utilMonitorInit(&pApostCtlInfo->umInput)
	|| utilMemPoolInit(&pApostCtlInfo->pPcmBuf->pool,
		pApostCtlInfo->chNum*pApostCtlInfo->auSample*sizeof(float),
		pApostCtlInfo->pcmNum, ALIGN_128BYTE)
	|| utilQueueInit(&pApostCtlInfo->pPcmBuf->queue, &pApostCtlInfo->umInput,
		sizeof(SAPcmInfo), pApostCtlInfo->pcmNum) ){
		return RET_CODE_ERR_FATAL;
	}

	return CELL_OK;
}

int apostClose(SApostCtlInfo* pApostCtlInfo){
	utilMemPoolFin(&pApostCtlInfo->pPcmBuf->pool);
	utilQueueFin(&pApostCtlInfo->pPcmBuf->queue);
	if(utilMonitorFin(&pApostCtlInfo->umInput)){
		return RET_CODE_ERR_FATAL;
	}

	return CELL_OK;
}

int apostStart(SApostCtlInfo* pApostCtlInfo, int prio, size_t stacksize,
	CellAdecHandle decHandle, apostCbFunc adecPcmOutFunc, void* adecPcmOutArg)
{
	int	ret;
	pApostCtlInfo->errorCode 			= RET_CODE_ERR_INVALID;
	pApostCtlInfo->decHandle 			= decHandle;
	pApostCtlInfo->cbAdecFunc			= adecPcmOutFunc;
	pApostCtlInfo->cbAdecArg			= adecPcmOutArg;
	pApostCtlInfo->recvStatus.bPcmOut	= false;
	pApostCtlInfo->recvStatus.bSequence = true;

	ret = sys_ppu_thread_create(&pApostCtlInfo->threadId, apostThread, (uintptr_t)pApostCtlInfo,
		prio, stacksize, SYS_PPU_THREAD_CREATE_JOINABLE, __func__);

	return ret;
}

int apostEnd(SApostCtlInfo* pApostCtlInfo){
	uint64_t thrStat;
	int	ret = sys_ppu_thread_join(pApostCtlInfo->threadId, &thrStat);
	if(ret < CELL_OK){
		return ret;
	}

	return pApostCtlInfo->errorCode;
}

static void apostThread(uint64_t arg){
	SApostCtlInfo*	pApostCtlInfo = (SApostCtlInfo*)(uintptr_t)arg;
	int				adecRet;
	SApostStatus	threadStatus = pApostCtlInfo->recvStatus;

	DP("start...\n");

	while(1){
		apostWaitEvent(pApostCtlInfo, &threadStatus);

		if(pApostCtlInfo->errorCode != RET_CODE_ERR_INVALID){
			break;
		}
		if( !( commonGetMode( pApostCtlInfo->pCommonCtlInfo ) & MODE_EXEC ) ){
			break;
		}

		bool		bBreakLoop = false;

		if(threadStatus.bPcmOut){
			SAPcmInfo				pcmInfo;
			const CellAdecPcmItem*	pcmItem;
			adecRet = cellAdecGetPcmItem(pApostCtlInfo->decHandle, &pcmItem);
			switch(adecRet){
			case CELL_OK:
				if(pcmItem->size == 0){
					adecRet = cellAdecGetPcm(pApostCtlInfo->decHandle, NULL);
					if(adecRet < CELL_OK){
						pApostCtlInfo->errorCode = adecRet;
						bBreakLoop = true;
					}
					break;
				}
				pcmInfo.pts = pcmItem->auInfo.pts;
				pcmInfo.index = 0;
				utilMemPoolPop(&pApostCtlInfo->pPcmBuf->pool, &pcmInfo.pPcm, true);
				adecRet = cellAdecGetPcm(pApostCtlInfo->decHandle, pcmInfo.pPcm);
				if(adecRet == CELL_OK){
					(*pApostCtlInfo->cbAdecFunc)(pApostCtlInfo->cbAdecArg);
					utilQueuePush(&pApostCtlInfo->pPcmBuf->queue, &pcmInfo, true);
				}else{
					utilMemPoolPush(&pApostCtlInfo->pPcmBuf->pool, pcmInfo.pPcm);
					pApostCtlInfo->errorCode = adecRet;
					bBreakLoop = true;
				}
				break;
			case CELL_ADEC_ERROR_BUSY:
			case CELL_ADEC_ERROR_AC3_BUSY:
			case CELL_ADEC_ERROR_MP3_BUSY:
			case CELL_ADEC_ERROR_EMPTY:
			case CELL_ADEC_ERROR_AC3_EMPTY:
			case CELL_ADEC_ERROR_MP3_EMPTY:
				threadStatus.bPcmOut = false;
				if(!threadStatus.bSequence){
					pApostCtlInfo->errorCode = CELL_OK;
					bBreakLoop = true;
				}
				break;
			default:
				pApostCtlInfo->errorCode = adecRet;
				bBreakLoop = true;
				break;
			}
			if(bBreakLoop){
				break;
			}
		}

	}

	if(pApostCtlInfo->errorCode < CELL_OK
	&& pApostCtlInfo->errorCode != RET_CODE_ERR_INVALID){
		commonErrorExit(pApostCtlInfo->pCommonCtlInfo, pApostCtlInfo->errorCode);
		EINFO(pApostCtlInfo->errorCode);
	}else{
		pApostCtlInfo->errorCode = CELL_OK;
	}

	DP("end...\n");

	sys_ppu_thread_exit(0);
}

static void apostWaitEvent(SApostCtlInfo* pApostCtlInfo, SApostStatus* pApostThreadStatus){
	SApostStatus* pRecvStatus = &pApostCtlInfo->recvStatus;

	utilMonitorLock(&pApostCtlInfo->umInput, 0);

	//E update receiving status with thread status
	if(!pRecvStatus->bPcmOut){
		pRecvStatus->bPcmOut = pApostThreadStatus->bPcmOut;
	}

	//E wait condition
	while(	( commonGetMode( pApostCtlInfo->pCommonCtlInfo ) & MODE_EXEC ) &&
			pApostCtlInfo->errorCode == RET_CODE_ERR_INVALID &&
			!pRecvStatus->bPcmOut ){
		utilMonitorWait(&pApostCtlInfo->umInput, 0);
	}

	//E copy from receiving status to thread status
	*pApostThreadStatus = *pRecvStatus;

	//E clear receiving status
	pRecvStatus->bPcmOut = false;

	utilMonitorUnlock(&pApostCtlInfo->umInput);
}

