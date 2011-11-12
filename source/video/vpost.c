#include "vpost.h"

static void vpostWaitEvent(SVpostCtlInfo* pVpostCtlInfo, SVpostStatus* pVpostThreadStatus);
static void vpostThread(uint64_t arg);
static void vpostMakeCtrlParam(
	const CellVdecPicItem* pPicItem, bool* pProgressive,
	bool* pTopFieldFirst, uint32_t* pFieldNum,
	uint32_t* pPrsnNum, uint32_t* pRepeatCounter,
	CellVpostCtrlParam* pCtrlParam);

SVFrameBuffer* vpostGetFrameBuffer(SVpostCtlInfo* pVpostCtlInfo){
	return pVpostCtlInfo->pFrameBuf;
}

void vpostSeqEndCb(SVpostCtlInfo* pVpostCtlInfo){
	utilMonitorLock(&pVpostCtlInfo->umInput, 0);
	pVpostCtlInfo->recvStatus.bSequence	= false;
	pVpostCtlInfo->recvStatus.bPicOut	= true;
	utilMonitorSignal(&pVpostCtlInfo->umInput);
	utilMonitorUnlock(&pVpostCtlInfo->umInput);
}

void vpostPictureOutCb(SVpostCtlInfo* pVpostCtlInfo){
	utilMonitorLock(&pVpostCtlInfo->umInput, 0);
	pVpostCtlInfo->recvStatus.bPicOut = true;
	utilMonitorSignal(&pVpostCtlInfo->umInput);
	utilMonitorUnlock(&pVpostCtlInfo->umInput);
}

int vpostSetParam(SVpostCtlInfo* pVpostCtlInfo, SCommonCtlInfo* pCommonCtlInfo,
	SVdispCtlInfo* pVdispCtlInfo, uint32_t width, uint32_t height,
	uint32_t dispWidth, uint32_t dispHeight, uint32_t frameNum)
{
	pVpostCtlInfo->pCommonCtlInfo	= pCommonCtlInfo;
	pVpostCtlInfo->pVdispCtlInfo	= pVdispCtlInfo;

	pVpostCtlInfo->width			= width;
	pVpostCtlInfo->height			= height;
	pVpostCtlInfo->dispWidth		= dispWidth;
	pVpostCtlInfo->dispHeight		= dispHeight;
	pVpostCtlInfo->frameNum			= frameNum;

	pVpostCtlInfo->pFrameBuf		= &pVpostCtlInfo->frameBuf;

	return CELL_OK;
}

int vpostOpen(SVpostCtlInfo* pVpostCtlInfo, int32_t ppuThreadPriority,
	size_t ppuThreadStackSize, int32_t spuThreadPriority, uint32_t numOfSpus)
{
	int32_t				ret;
	CellVpostCfgParam	cfgParam = {
		pVpostCtlInfo->width, pVpostCtlInfo->height,
		CELL_VPOST_PIC_DEPTH_8, CELL_VPOST_PIC_FMT_IN_YUV420_PLANAR,
		pVpostCtlInfo->dispWidth, pVpostCtlInfo->dispHeight,
		CELL_VPOST_PIC_DEPTH_8, CELL_VPOST_PIC_FMT_OUT_RGBA_ILV,
		0, 0
	};
	CellVpostAttr		postAttr;
	CellVpostResource	res;

	ret = cellSysmoduleLoadModule(CELL_SYSMODULE_VPOST);
	if(ret < CELL_OK){	return ret;	}

	ret = cellVpostQueryAttr(&cfgParam, &postAttr);
	if(ret < CELL_OK){
		goto L_VPOST_OPEN_ERR;
	}

	pVpostCtlInfo->memSize = postAttr.memSize;

	if(utilMonitorInit(&pVpostCtlInfo->umInput)
	|| utilMemPoolInit(&pVpostCtlInfo->pFrameBuf->pool,
		pVpostCtlInfo->dispWidth * pVpostCtlInfo->dispHeight * 4,
		pVpostCtlInfo->frameNum, ALIGN_1MBYTE)
	|| utilQueueInit(&pVpostCtlInfo->pFrameBuf->queue, &pVpostCtlInfo->umInput,
		sizeof(SVFrameInfo), pVpostCtlInfo->frameNum)
	|| vdispMapVframe(pVpostCtlInfo->pVdispCtlInfo, &pVpostCtlInfo->pFrameBuf->pool) ){
		ret = RET_CODE_ERR_FATAL;
		goto L_VPOST_OPEN_ERR;
	}

	pVpostCtlInfo->pDecodedFrame = AlignedAlloc(
		pVpostCtlInfo->width * (pVpostCtlInfo->height + 2) * 3 / 2,
		ALIGN_128BYTE
	);
	if(pVpostCtlInfo->pDecodedFrame == NULL){
		ret = RET_CODE_ERR_FATAL;
		goto L_VPOST_OPEN_ERR;
	}

	pVpostCtlInfo->pWorkMemory = memalign(128, pVpostCtlInfo->memSize);
	if(pVpostCtlInfo->pWorkMemory == NULL){
		AlignedFree(pVpostCtlInfo->pDecodedFrame);
		ret = RET_CODE_ERR_FATAL;
		goto L_VPOST_OPEN_ERR;
	}

	res.memAddr				= pVpostCtlInfo->pWorkMemory;
	res.memSize				= pVpostCtlInfo->memSize;
	res.ppuThreadPriority	= ppuThreadPriority;
	res.ppuThreadStackSize	= ppuThreadStackSize;
	res.spuThreadPriority	= spuThreadPriority;
	res.numOfSpus			= numOfSpus;

	ret = cellVpostOpen(&cfgParam, &res, &pVpostCtlInfo->postHandle);
	if(ret < CELL_OK){
		free(pVpostCtlInfo->pWorkMemory);
		AlignedFree(pVpostCtlInfo->pDecodedFrame);
		goto L_VPOST_OPEN_ERR;
	}

	return CELL_OK;

L_VPOST_OPEN_ERR:
	cellSysmoduleUnloadModule(CELL_SYSMODULE_VPOST);
	return ret;
}

int vpostClose(SVpostCtlInfo* pVpostCtlInfo){
	utilMemPoolFin(&pVpostCtlInfo->pFrameBuf->pool);
	utilQueueFin(&pVpostCtlInfo->pFrameBuf->queue);
	if(utilMonitorFin(&pVpostCtlInfo->umInput)){
		return RET_CODE_ERR_FATAL;
	}

	int32_t		ret;
	ret = cellVpostClose(pVpostCtlInfo->postHandle);
	if(ret < CELL_OK){
		return ret;
	}
	free(pVpostCtlInfo->pWorkMemory);
	AlignedFree(pVpostCtlInfo->pDecodedFrame);
	ret = cellSysmoduleUnloadModule(CELL_SYSMODULE_VPOST);

	return CELL_OK;//ret;
}

int vpostStart(SVpostCtlInfo* pVpostCtlInfo, int prio, size_t stacksize,
	CellVdecHandle decHandle, vpostCbFunc vdecPicOutFunc, void* vdecPicOutArg)
{
	pVpostCtlInfo->errorCode			= RET_CODE_ERR_INVALID;
	pVpostCtlInfo->decHandle			= decHandle;
	pVpostCtlInfo->cbVdecFunc			= vdecPicOutFunc;
	pVpostCtlInfo->cbVdecArg			= vdecPicOutArg;
	pVpostCtlInfo->recvStatus.bPicOut	= false;
	pVpostCtlInfo->recvStatus.bSequence	= true;

	int	ret = sys_ppu_thread_create(&pVpostCtlInfo->threadId, vpostThread, (uintptr_t)pVpostCtlInfo,
		prio, stacksize, SYS_PPU_THREAD_CREATE_JOINABLE, __func__);

	return ret;
}

int vpostEnd(SVpostCtlInfo* pVpostCtlInfo){
	uint64_t thrStat;
	int	ret = sys_ppu_thread_join(pVpostCtlInfo->threadId, &thrStat);
	if(ret < CELL_OK){
		return ret;
	}

	return pVpostCtlInfo->errorCode;
}

static void vpostThread(uint64_t arg){
	SVpostCtlInfo*			pVpostCtlInfo = (SVpostCtlInfo*)(uintptr_t)arg;

	int32_t					vdecRet;
	const CellVdecPicFormat	format = { CELL_VDEC_PICFMT_YUV420_PLANAR,
		CELL_VDEC_COLOR_MATRIX_TYPE_BT709, 0 };

	int32_t					vpostRet;
	void*					pInPicBuff = pVpostCtlInfo->pDecodedFrame;
	const bool				bDecimation = (pVpostCtlInfo->dispWidth < pVpostCtlInfo->width
		&& pVpostCtlInfo->dispHeight < pVpostCtlInfo->height)? true: false;
	CellVpostCtrlParam		ctrlParam = {
		CELL_VPOST_EXEC_TYPE_PFRM_PFRM,
		(bDecimation)? CELL_VPOST_SCALER_TYPE_2X4TAP: CELL_VPOST_SCALER_TYPE_BILINEAR,
		CELL_VPOST_IPC_TYPE_LINEAR,
		pVpostCtlInfo->width, pVpostCtlInfo->height,
		CELL_VPOST_CHROMA_POS_TYPE_B,
		CELL_VPOST_QUANT_RANGE_BROADCAST,
		CELL_VPOST_COLOR_MATRIX_BT709,
		{ 0, 0, pVpostCtlInfo->width, pVpostCtlInfo->height },
		pVpostCtlInfo->dispWidth, pVpostCtlInfo->dispHeight,
		{ 0, 0, pVpostCtlInfo->dispWidth, pVpostCtlInfo->dispHeight },
		0xFF, 0, 0, 0
	};
	CellVpostPictureInfo	picInfo;
	SVFrameInfo				frameInfo;
	const CellVdecPicItem*	picItem;
	CellCodecTimeStamp		pts[2];
	uint32_t				auNum;

	bool					progressive = true;
	bool					topFieldFirst = false;
	uint32_t				fieldNum = 0;
	uint32_t				prsnNum[3] = { 0, 0, 0 };
	uint32_t				repeatCounter = 0;

	SVpostStatus			threadStatus = pVpostCtlInfo->recvStatus;

	DP("start...\n");

	while(1){
		vpostWaitEvent(pVpostCtlInfo, &threadStatus);

		if(pVpostCtlInfo->errorCode != RET_CODE_ERR_INVALID){
			break;
		}
		if(!( commonGetMode( pVpostCtlInfo->pCommonCtlInfo ) & MODE_EXEC ) ){
			break;
		}

		bool		bBreakLoop = false;

		if(threadStatus.bPicOut){
			vdecRet = cellVdecGetPicItem(pVpostCtlInfo->decHandle, &picItem);
			switch(vdecRet){
			case CELL_OK:
				/* convert picture information. */
				pts[0] = picItem->auPts[0];
				pts[1] = picItem->auPts[1];
				auNum = picItem->auNum;
				vpostMakeCtrlParam(picItem, &progressive, &topFieldFirst,
					&fieldNum, prsnNum, &repeatCounter, &ctrlParam);

				/* get picture. */
				vdecRet = cellVdecGetPicture(pVpostCtlInfo->decHandle, &format, pInPicBuff);
				if(vdecRet < CELL_OK){
					pVpostCtlInfo->errorCode = vdecRet;
					bBreakLoop = true;
					break;
				}
				(*pVpostCtlInfo->cbVdecFunc)(pVpostCtlInfo->cbVdecArg);

				/* progressive */
				if(progressive){
					utilMemPoolPop(&pVpostCtlInfo->pFrameBuf->pool, &frameInfo.pFrame, true);
					ctrlParam.execType = CELL_VPOST_EXEC_TYPE_PFRM_PFRM;
					vpostRet = cellVpostExec(pVpostCtlInfo->postHandle,
						pInPicBuff, &ctrlParam, frameInfo.pFrame, &picInfo);
					if(vpostRet < CELL_OK){
						utilMemPoolPush(&pVpostCtlInfo->pFrameBuf->pool, frameInfo.pFrame);
						pVpostCtlInfo->errorCode = vpostRet;
						bBreakLoop = true;
						break;
					}
					frameInfo.prsnNum = prsnNum[0];
					frameInfo.pts = pts[0];
					frameInfo.bPtsValid = true;
					utilQueuePush(&pVpostCtlInfo->pFrameBuf->queue, &frameInfo, true);
					vdispPictureOutCb(pVpostCtlInfo->pVdispCtlInfo);
				/* interlaced frame */
				}else{
					/* I/P conversion. */
					for(uint32_t index = 0; index < fieldNum; ++index){
						utilMemPoolPop(&pVpostCtlInfo->pFrameBuf->pool, &frameInfo.pFrame, true);
						ctrlParam.execType = ((index & 1) ^ topFieldFirst)
							? CELL_VPOST_EXEC_TYPE_ITOP_PFRM: CELL_VPOST_EXEC_TYPE_IBTM_PFRM;
						vpostRet = cellVpostExec(pVpostCtlInfo->postHandle,
							pInPicBuff, &ctrlParam, frameInfo.pFrame, &picInfo);
						if(vpostRet < CELL_OK){
							utilMemPoolPush(&pVpostCtlInfo->pFrameBuf->pool, frameInfo.pFrame);
							pVpostCtlInfo->errorCode = vpostRet;
							bBreakLoop = true;
							break;
						}
						frameInfo.prsnNum = prsnNum[index];
						if(index < auNum
						&& pts[index].upper != CELL_VDEC_PTS_INVALID){
							frameInfo.pts = pts[index];
							frameInfo.bPtsValid = true;
						}else{
							frameInfo.bPtsValid = false;
						}
						utilQueuePush(&pVpostCtlInfo->pFrameBuf->queue, &frameInfo, true);
						vdispPictureOutCb(pVpostCtlInfo->pVdispCtlInfo);
					}
				}
				break;
			case CELL_VDEC_ERROR_BUSY:
			case CELL_VDEC_ERROR_EMPTY:
				threadStatus.bPicOut = false;
				if(!threadStatus.bSequence){
					vdispSeqEndCb(pVpostCtlInfo->pVdispCtlInfo);
					pVpostCtlInfo->errorCode = CELL_OK;
					bBreakLoop = true;
				}
				break;
			default:
				pVpostCtlInfo->errorCode = vdecRet;
				bBreakLoop = true;
				break;
			}
			if(bBreakLoop){
				break;
			}
		}

	}

	if(pVpostCtlInfo->errorCode < CELL_OK
	&& pVpostCtlInfo->errorCode != RET_CODE_ERR_INVALID){
		commonErrorExit(pVpostCtlInfo->pCommonCtlInfo, pVpostCtlInfo->errorCode);
		EINFO(pVpostCtlInfo->errorCode);
	}else{
		pVpostCtlInfo->errorCode = CELL_OK;
	}

	DP("end...\n");

	sys_ppu_thread_exit(0);
}

static void vpostWaitEvent(SVpostCtlInfo* pVpostCtlInfo, SVpostStatus* pVpostThreadStatus){
	SVpostStatus* pRecvStatus = &pVpostCtlInfo->recvStatus;

	utilMonitorLock(&pVpostCtlInfo->umInput, 0);

	// update receiving status with thread status
	if(!pRecvStatus->bPicOut){
		pRecvStatus->bPicOut = pVpostThreadStatus->bPicOut;
	}

	// wait condition
	while(	( commonGetMode( pVpostCtlInfo->pCommonCtlInfo ) & MODE_EXEC ) &&
			pVpostCtlInfo->errorCode == RET_CODE_ERR_INVALID &&
			!pRecvStatus->bPicOut ){
		utilMonitorWait(&pVpostCtlInfo->umInput, 0);
	}

	// copy from receiving status to thread status
	*pVpostThreadStatus = *pRecvStatus;

	// clear receiving status
	pRecvStatus->bPicOut = false;

	utilMonitorUnlock(&pVpostCtlInfo->umInput);
}

static void vpostMakeCtrlParam(
	const CellVdecPicItem* pPicItem, bool* pProgressive,
	bool* pTopFieldFirst, uint32_t* pFieldNum,
	uint32_t* pPrsnNum, uint32_t* pRepeatCounter,
	CellVpostCtrlParam* pCtrlParam)
{
	CellVpostChromaPositionType	chromaPositionType = CELL_VPOST_CHROMA_POS_TYPE_B;
	CellVpostQuantRange			quantRange = CELL_VPOST_QUANT_RANGE_BROADCAST;
	CellVpostColorMatrix		colorMatrixType = CELL_VPOST_COLOR_MATRIX_BT709;

	if(pPicItem->codecType == CELL_VDEC_CODEC_TYPE_DIVX){
		const CellVdecDivxInfo*	pDivxInfo = (const CellVdecDivxInfo*)pPicItem->picInfo;

		switch(pDivxInfo->pictureStruct){
		case CELL_VDEC_DIVX_PSTR_TOP_BTM:
			*pProgressive = false;
			*pTopFieldFirst = true;
			*pFieldNum = 2;
			break;
		case CELL_VDEC_DIVX_PSTR_BTM_TOP:
			*pProgressive = false;
			*pTopFieldFirst = false;
			*pFieldNum = 2;
			break;
		case CELL_VDEC_DIVX_PSTR_FRAME:
		default:
			*pProgressive = true;
			pPrsnNum[0] = 1;
			break;
		}
		if(!*pProgressive){
			switch(pDivxInfo->frameRateCode){
			case CELL_VDEC_DIVX_FRC_24000DIV1001:
			case CELL_VDEC_DIVX_FRC_24:
				for(uint32_t index = 0; index < *pFieldNum; ++index){
					pPrsnNum[index] = 3 - *pRepeatCounter;
					*pRepeatCounter = (*pRepeatCounter + 1) % 2;
				}
				break;
			case CELL_VDEC_DIVX_FRC_30000DIV1001:
			case CELL_VDEC_DIVX_FRC_30:
			default:
				pPrsnNum[0] = 1;
				pPrsnNum[1] = 1;
				pPrsnNum[2] = 1;
				break;
			}
		}else{
			switch(pDivxInfo->frameRateCode){
			case CELL_VDEC_DIVX_FRC_24000DIV1001:
			case CELL_VDEC_DIVX_FRC_24:
				pPrsnNum[0] *= 3 - *pRepeatCounter;
				*pRepeatCounter = (*pRepeatCounter + 1) % 2;
				break;
			case CELL_VDEC_DIVX_FRC_30000DIV1001:
			case CELL_VDEC_DIVX_FRC_30:
				pPrsnNum[0] *= 2;
				break;
			case CELL_VDEC_DIVX_FRC_60000DIV1001:
			case CELL_VDEC_DIVX_FRC_60:
			default:
				pPrsnNum[0] *= 1;
				break;
			}
		}

		if(pDivxInfo->colourDescription){
			switch(pDivxInfo->matrixCoefficients){
			case CELL_VDEC_DIVX_MXC_ITU_R_BT_470_SYS_BG:
			case CELL_VDEC_DIVX_MXC_SMPTE_170_M:
			case CELL_VDEC_DIVX_MXC_FCC:
				colorMatrixType = CELL_VPOST_COLOR_MATRIX_BT601;
				break;
			}
		}else{
			colorMatrixType = CELL_VPOST_COLOR_MATRIX_BT601;
		}
	}

	pCtrlParam->inChromaPosType = chromaPositionType;
	pCtrlParam->inQuantRange = quantRange;
	pCtrlParam->inColorMatrix = colorMatrixType;
}
