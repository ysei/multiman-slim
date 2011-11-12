#include "amixer.h"

#define AMIXER_MAX_SAMPLE		256
#define AMIXER_CB_INCR_CLOCKS	(90000 * 256 / 48000)

static void amixerIncrementAudioPts(SAmixerCtlInfo* pAmixerCtlInfo);
static int amixerCb(void *pArg, uint32_t index, uint32_t samples);

int amixerSetParam(SAmixerCtlInfo* pAmixerCtlInfo,
	SCommonCtlInfo* pCommonCtlInfo, SAVsyncCtlInfo* pAVsyncCtlInfo,
	uint32_t chNum, uint32_t auSample, uint32_t pcmNum)
{
	pAmixerCtlInfo->pCommonCtlInfo	= pCommonCtlInfo;
	pAmixerCtlInfo->pAVsyncCtlInfo	= pAVsyncCtlInfo;

	pAmixerCtlInfo->chNum			= chNum;
	pAmixerCtlInfo->auSample		= auSample;
	pAmixerCtlInfo->pcmNum			= pcmNum;

	int32_t	ret;
	CellAudioOutConfiguration	audioConfig;
	memset(&audioConfig, 0, sizeof(audioConfig));
	switch(chNum){
	case 1:
		++chNum;
	case 2:
	case 6:
	case 8:
		audioConfig.encoder   = CELL_AUDIO_OUT_CODING_TYPE_AC3;//CELL_AUDIO_OUT_CODING_TYPE_LPCM;
		audioConfig.channel   = 6;//chNum;
		audioConfig.downMixer = CELL_AUDIO_OUT_DOWNMIXER_TYPE_B;//CELL_AUDIO_OUT_DOWNMIXER_NONE;
		break;
	default:
		return RET_CODE_ERR_ARG;
	}

	ret = cellAudioOutConfigure(CELL_AUDIO_OUT_PRIMARY, &audioConfig, NULL, 0);
	if(ret < CELL_OK){
		audioConfig.encoder   = CELL_AUDIO_OUT_CODING_TYPE_LPCM;
		audioConfig.channel   = chNum;
		audioConfig.downMixer = CELL_AUDIO_OUT_DOWNMIXER_NONE;
		ret = cellAudioOutConfigure(CELL_AUDIO_OUT_PRIMARY, &audioConfig, NULL, 0);
	}
	if(ret < CELL_OK) return ret;

	return CELL_OK;
}

int amixerOpen(SAmixerCtlInfo* pAmixerCtlInfo, uint32_t prio)
{
	pAmixerCtlInfo->bSequence = false;
	pAmixerCtlInfo->bReqStop = false;

	int	ret;
	//ret = cellAudioInit();	if(ret < CELL_OK){	return ret;	}

	CellSurMixerConfig	config = { prio, 0, 0, 0, 0 };
	uint32_t			csType;
	switch(pAmixerCtlInfo->chNum){
	case 1:
		config.chStrips1 = 1;
		csType = CELL_SURMIXER_CHSTRIP_TYPE1A;
		break;
	case 2:
		config.chStrips2 = 1;
		csType = CELL_SURMIXER_CHSTRIP_TYPE2A;
		break;
	case 6:
		config.chStrips6 = 1;
		csType = CELL_SURMIXER_CHSTRIP_TYPE6A;
		break;
	case 8:
		config.chStrips8 = 1;
		csType = CELL_SURMIXER_CHSTRIP_TYPE8A;
		break;
	default:
		ret = RET_CODE_ERR_ARG;
		return ret;
	}

	pAmixerCtlInfo->pMixerBuffer = malloc(sizeof(float) * AMIXER_MAX_SAMPLE
		* pAmixerCtlInfo->chNum);
	if(pAmixerCtlInfo->pMixerBuffer == NULL){
		ret = RET_CODE_ERR_ARG;
		return ret;
	}

	if(utilMonitorInit(&pAmixerCtlInfo->umInput)){
		ret = RET_CODE_ERR_ARG;
		return ret;
	}

	if ((ret = cellSurMixerCreate(&config)) < 0) {
		EMSG("cellSurMixerCreate() failed. (%d)\n", ret);
		return ret;
	}

	if ((ret = cellSurMixerGetAANHandle(&pAmixerCtlInfo->mixer)) < 0) {
		EMSG("cellSurMixerGetAANHandle() failed. (%d)\n", ret);
		return ret;
	}

	if ((ret = cellSurMixerChStripGetAANPortNo(&pAmixerCtlInfo->portNo, csType, 0)) < 0) {
		EMSG("cellSurMixerChStripGetAANPortNo() failed. (%d)\n", ret);
		return ret;
	}

	if ((ret = cellSurMixerSetNotifyCallback(amixerCb, pAmixerCtlInfo)) < 0) {
		EMSG("cellSurMixerSetNotifyCallback() failed. (%d)\n", ret);
		return ret;
	}

	if ((ret = cellSurMixerSetParameter(CELL_SURMIXER_PARAM_TOTALLEVEL, -3.0)) < 0) {
		EMSG("cellSurMixerSetParameter() failed.\n");
		return ret;
	}

	if ((ret = cellSurMixerStart()) < 0) {
		EMSG("cellSurMixerStart() failed\n");
		return ret;
	}

	return CELL_OK;
}

int amixerClose(SAmixerCtlInfo* pAmixerCtlInfo){
	(void)pAmixerCtlInfo;
	int	ret;
	ret = cellSurMixerFinalize();
	if (ret < 0) {
		EMSG("cellSurMixerFinalize() failed\n");
		return ret;
	}

	//ret = cellAudioQuit();	if (ret < 0) {	EMSG("cellAudioQuit() failed\n");	return ret;	}

	free(pAmixerCtlInfo->pMixerBuffer);

	if(utilMonitorFin(&pAmixerCtlInfo->umInput)){
		return RET_CODE_ERR_FATAL;
	}

	return ret;
}

int amixerStart(SAmixerCtlInfo* pAmixerCtlInfo, SAPcmBuffer* pPcmBuf){
	utilMonitorLock(&pAmixerCtlInfo->umInput, 0);
	pAmixerCtlInfo->pPcmBuf		= pPcmBuf;
	pAmixerCtlInfo->bSequence	= true;
	pAmixerCtlInfo->bReqStop	= false;
	utilMonitorUnlock(&pAmixerCtlInfo->umInput);

	return CELL_OK;
}

int amixerEnd(SAmixerCtlInfo* pAmixerCtlInfo){
	utilMonitorLock(&pAmixerCtlInfo->umInput, 0);
	pAmixerCtlInfo->bReqStop = true;
	while(pAmixerCtlInfo->bReqStop){
		utilMonitorWait(&pAmixerCtlInfo->umInput, 0);
	}
	utilMonitorUnlock(&pAmixerCtlInfo->umInput);

	return CELL_OK;
}

static void amixerIncrementAudioPts(SAmixerCtlInfo* pAmixerCtlInfo){
	CellCodecTimeStamp	pts = pAmixerCtlInfo->lastPts;
	pts.lower += AMIXER_CB_INCR_CLOCKS;
	if(pts.lower < pAmixerCtlInfo->lastPts.lower){
		pts.upper = (pts.upper + 1) & 1;
	}
	avsyncSetApts(pAmixerCtlInfo->pAVsyncCtlInfo, pts);
	pAmixerCtlInfo->lastPts = pts;
}

static int amixerCb(void *pArg, uint32_t index, uint32_t samples){
	(void)index;
	bool			bSequence;
	bool			bReqStop;
	uint32_t		auSample;
	uint32_t		chNum;
	uint32_t		pcmNum;
	SAmixerCtlInfo*	pAmixerCtlInfo = (SAmixerCtlInfo*)pArg;
	SCommonCtlInfo*	pCommonCtlInfo = pAmixerCtlInfo->pCommonCtlInfo;

	utilMonitorLock(&pAmixerCtlInfo->umInput, 0);
	bSequence	= pAmixerCtlInfo->bSequence;
	bReqStop	= pAmixerCtlInfo->bReqStop;
	auSample	= pAmixerCtlInfo->auSample;
	chNum		= pAmixerCtlInfo->chNum;
	pcmNum		= pAmixerCtlInfo->pcmNum;
	utilMonitorUnlock(&pAmixerCtlInfo->umInput);

	if(!bSequence){
		if(commonGetAudioStatus(pCommonCtlInfo) & STATUS_END){
			amixerIncrementAudioPts(pAmixerCtlInfo);
		}
		return 1;
	}

	SAPcmInfo*	pPcmInfo;
	uint32_t	queuePcmNum;
	utilQueuePeek(&pAmixerCtlInfo->pPcmBuf->queue, (void*)&pPcmInfo, &queuePcmNum);

	if(	!( commonGetAudioStatus( pCommonCtlInfo) & STATUS_READY ) &&
		queuePcmNum >= pcmNum ){
		commonSetAudioStatus( pCommonCtlInfo, STATUS_READY, BIT_SET );
	}
	if( !( commonGetVideoStatus( pCommonCtlInfo ) & STATUS_READY ) ||
		!( commonGetAudioStatus( pCommonCtlInfo ) & STATUS_READY ) ) {
		return 1;
	}

	if(queuePcmNum == 0){
		if(bReqStop){
			utilMonitorLock(&pAmixerCtlInfo->umInput, 0);
			pAmixerCtlInfo->bSequence	= false;
			pAmixerCtlInfo->bReqStop	= false;
			commonSetAudioStatus( pCommonCtlInfo, STATUS_END, BIT_SET );
			utilMonitorSignal(&pAmixerCtlInfo->umInput);
			utilMonitorUnlock(&pAmixerCtlInfo->umInput);
			amixerIncrementAudioPts(pAmixerCtlInfo);
		}
		return 1;
	}

	uint32_t	bufferIndex = 0;
	uint32_t	reqSample = samples;
	uint32_t	sampleByte = chNum * sizeof(float);
	float*		buffer = pAmixerCtlInfo->pMixerBuffer;
	avsyncSetApts(pAmixerCtlInfo->pAVsyncCtlInfo, pPcmInfo->pts);
	pAmixerCtlInfo->lastPts = pPcmInfo->pts;
	while(reqSample && queuePcmNum){
		uint32_t	addSample = auSample - pPcmInfo->index;
		if(addSample > reqSample){
			addSample = reqSample;
		}
		memcpy((uint8_t*)buffer + bufferIndex * sampleByte,
			(uint8_t*)pPcmInfo->pPcm + pPcmInfo->index * sampleByte,
			addSample * sampleByte);
		bufferIndex		+= addSample;
		pPcmInfo->index += addSample;
		reqSample		-= addSample;
		if(pPcmInfo->index >= auSample){
			utilQueuePop(&pAmixerCtlInfo->pPcmBuf->queue, NULL, false);
			utilMemPoolPush(&pAmixerCtlInfo->pPcmBuf->pool, pPcmInfo->pPcm);
			utilQueuePeek(&pAmixerCtlInfo->pPcmBuf->queue, (void*)&pPcmInfo, &queuePcmNum);
		}
	}
	if(reqSample){
		memset((uint8_t*)buffer + bufferIndex * sampleByte, 0, reqSample * sampleByte);
	}

	cellAANAddData(pAmixerCtlInfo->mixer, pAmixerCtlInfo->portNo, 0, buffer, samples);

	return 0;
}
