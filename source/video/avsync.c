#include "avsync.h"

static int _avsyncGetApts(SAVsyncCtlInfo* pAVsyncCtlInfo, int *pPtsAudio);
static int _avsyncGetVpts(SAVsyncCtlInfo* pAVsyncCtlInfo, int *pPtsVideo);
static bool _avsyncGetValidAudio(SAVsyncCtlInfo *pAVsyncCtlInfo);
static bool _avsyncGetValidVideo(SAVsyncCtlInfo *pAVsyncCtlInfo);

int avsyncSetParam(SAVsyncCtlInfo* pAVsyncCtlInfo, 	SCommonCtlInfo*	pCommonCtlInfo)
{
	pAVsyncCtlInfo->pCommonCtlInfo = pCommonCtlInfo;
	return CELL_OK;
}

int avsyncOpen(SAVsyncCtlInfo *pAVsyncCtlInfo)
{
	if(utilLWMutexInit(&pAVsyncCtlInfo->umInput) < CELL_OK){
		return RET_CODE_ERR_FATAL;
	}
	return CELL_OK;
}

int avsyncClose(SAVsyncCtlInfo* pAVsyncCtlInfo)
{
	if(utilLWMutexFin(&pAVsyncCtlInfo->umInput) < CELL_OK){
		return RET_CODE_ERR_FATAL;
	}
	return CELL_OK;
}

int avsyncStart(SAVsyncCtlInfo* pAVsyncCtlInfo)
{
	CellCodecTimeStamp ptsAudio = { 0, 0};
	CellCodecTimeStamp ptsVideo = { 0, 0};

	avsyncSetApts(pAVsyncCtlInfo, ptsAudio);
	avsyncSetVpts(pAVsyncCtlInfo, ptsVideo);
	avsyncSetValidAudio(pAVsyncCtlInfo, false);
	avsyncSetValidVideo(pAVsyncCtlInfo, false);

	return CELL_OK;
}

int avsyncEnd(SAVsyncCtlInfo* pAVsyncCtlInfo)
{
	(void)pAVsyncCtlInfo;
	return CELL_OK;
}


static int _avsyncGetApts(SAVsyncCtlInfo* pAVsyncCtlInfo, int *pPtsAudio)
{
	utilLWMutexLock(&pAVsyncCtlInfo->umInput, 0);
	*pPtsAudio = pAVsyncCtlInfo->ptsAudio.lower;
	utilLWMutexUnlock(&pAVsyncCtlInfo->umInput);

	return CELL_OK;

}

static int _avsyncGetVpts(SAVsyncCtlInfo* pAVsyncCtlInfo, int *pPtsVideo)
{
	utilLWMutexLock(&pAVsyncCtlInfo->umInput, 0);
	*pPtsVideo = pAVsyncCtlInfo->ptsVideo.lower;
	utilLWMutexUnlock(&pAVsyncCtlInfo->umInput);
	return CELL_OK;

}

static bool _avsyncGetValidAudio(SAVsyncCtlInfo *pAVsyncCtlInfo)
{
	bool bValid;
	utilLWMutexLock(&pAVsyncCtlInfo->umInput, 0);
	bValid = pAVsyncCtlInfo->bAudioValid;
	utilLWMutexUnlock(&pAVsyncCtlInfo->umInput);
	return (bValid);
}

static bool _avsyncGetValidVideo(SAVsyncCtlInfo *pAVsyncCtlInfo)
{
	bool bValid;
	utilLWMutexLock(&pAVsyncCtlInfo->umInput, 0);
	bValid = pAVsyncCtlInfo->bVideoValid;
	utilLWMutexUnlock(&pAVsyncCtlInfo->umInput);
	return (bValid);
}

int avsyncSetValidAudio(SAVsyncCtlInfo *pAVsyncCtlInfo, bool bValid )
{
	utilLWMutexLock(&pAVsyncCtlInfo->umInput, 0);
	pAVsyncCtlInfo->bAudioValid = bValid;
	utilLWMutexUnlock(&pAVsyncCtlInfo->umInput);
	return CELL_OK;
}

int avsyncSetValidVideo(SAVsyncCtlInfo *pAVsyncCtlInfo, bool bValid )
{
	utilLWMutexLock(&pAVsyncCtlInfo->umInput, 0);
	pAVsyncCtlInfo->bVideoValid = bValid;
	utilLWMutexUnlock(&pAVsyncCtlInfo->umInput);
	return CELL_OK;
}


int avsyncSetApts(SAVsyncCtlInfo* pAVsyncCtlInfo, CellCodecTimeStamp ptsAudio)
{
	utilLWMutexLock(&pAVsyncCtlInfo->umInput, 0);
	pAVsyncCtlInfo->bAudioValid = true;
	pAVsyncCtlInfo->ptsAudio	= ptsAudio;
	utilLWMutexUnlock(&pAVsyncCtlInfo->umInput);

	return CELL_OK;
}
int avsyncSetVpts(SAVsyncCtlInfo* pAVsyncCtlInfo, CellCodecTimeStamp ptsVideo)
{
	utilLWMutexLock(&pAVsyncCtlInfo->umInput, 0);
	pAVsyncCtlInfo->bVideoValid	= true;
	pAVsyncCtlInfo->ptsVideo	= ptsVideo;
	utilLWMutexUnlock(&pAVsyncCtlInfo->umInput);
	return CELL_OK;
}

int avsyncCompare(SAVsyncCtlInfo* pAVsyncCtlInfo)
{
	int ret, ptsAudio, ptsVideo, ptsDiff;
	SCommonCtlInfo*	pCommonCtlInfo;

	pCommonCtlInfo = pAVsyncCtlInfo->pCommonCtlInfo;

	if( !( commonGetVideoStatus( pCommonCtlInfo ) & STATUS_READY ) ||
		!( commonGetAudioStatus( pCommonCtlInfo ) & STATUS_READY ) ) {

		ret = AVSYNC_VDISP_MODE_NORMAL;
		goto _term;
	}

	if( !_avsyncGetValidAudio(pAVsyncCtlInfo) ||
		!_avsyncGetValidVideo(pAVsyncCtlInfo)  ) {
		ret = AVSYNC_VDISP_MODE_NORMAL;
		goto _term;
	}

	 _avsyncGetApts(pAVsyncCtlInfo, &ptsAudio);
	 _avsyncGetVpts(pAVsyncCtlInfo, &ptsVideo);

	ptsDiff = ptsAudio - ptsVideo;

	if (ptsDiff >= (-1)* AVSYNC_VDISP_INTERVAL && ptsDiff <= AVSYNC_VDISP_INTERVAL) {
		ret = AVSYNC_VDISP_MODE_NORMAL;

	} else if (ptsDiff > AVSYNC_VDISP_INTERVAL){
		ret = AVSYNC_VDISP_MODE_SKIP;

	} else {
		ret = AVSYNC_VDISP_MODE_WAIT;

	}

_term:
	return ret;

}

