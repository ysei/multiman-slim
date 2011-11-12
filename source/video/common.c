#include "util.h"
#include "common.h"

#include "avidmux.h"
#include "vdec.h"
#include "adec.h"
#include "avsync.h"

extern bool mm_shutdown;
extern u8 is_bg_video;

static inline void	UM_LOCK(SCommonCtlInfo* pCommonCtlInfo)
{
	utilLWMutexLock(&pCommonCtlInfo->umInput, 0);
}
static inline void  UM_UNLOCK(SCommonCtlInfo* pCommonCtlInfo)
{
	utilLWMutexUnlock(&pCommonCtlInfo->umInput);
}

int commonSetParam( SCommonCtlInfo* pCommonCtlInfo,
	void*	pAviDmuxCtlInfo,

	void*	pVdecCtlInfo,
	void*	pVpostCtlInfo,
	void*	pVdispCtlInfo,

	void*	pAdecCtlInfo,
	void*	pApostCtlInfo,
	void*	pAmixerCtlInfo )
{
	pCommonCtlInfo->pAviDmuxCtlInfo	= pAviDmuxCtlInfo;

	pCommonCtlInfo->pVdecCtlInfo	= pVdecCtlInfo;
	pCommonCtlInfo->pVpostCtlInfo	= pVpostCtlInfo;
	pCommonCtlInfo->pVdispCtlInfo	= pVdispCtlInfo;

	pCommonCtlInfo->pAdecCtlInfo	= pAdecCtlInfo;
	pCommonCtlInfo->pApostCtlInfo	= pApostCtlInfo;
	pCommonCtlInfo->pAmixerCtlInfo	= pAmixerCtlInfo;

	return( CELL_OK );
}

int commonOpen( SCommonCtlInfo* pCommonCtlInfo )
{
	int ret = cellSysmoduleInitialize();
	if(ret < CELL_OK){
		return ret;
	}

	ret = cellSysutilRegisterCallback(COMMON_SYSUTIL_CB_SLOT,
		commonSysutilCb, pCommonCtlInfo);
	if(ret < CELL_OK){
		return ret;
	}

	if(utilLWMutexInit(&pCommonCtlInfo->umInput) < CELL_OK){
		return( RET_CODE_ERR_FATAL );
	}

	return( CELL_OK );
}

int commonClose( SCommonCtlInfo* pCommonCtlInfo )
{

	if(utilLWMutexFin(&pCommonCtlInfo->umInput) < CELL_OK){
		return( RET_CODE_ERR_FATAL );
	}

	int ret = cellSysutilUnregisterCallback(COMMON_SYSUTIL_CB_SLOT);
	if(ret < CELL_OK){
		return ret;
	}

	ret = cellSysmoduleFinalize();
	if(ret < CELL_OK){
		return ret;
	}

	return( CELL_OK );
}

int commonStart( SCommonCtlInfo* pCommonCtlInfo )
{
	UM_LOCK(pCommonCtlInfo);

	pCommonCtlInfo->mode		= MODE_EXEC;
	pCommonCtlInfo->errorCode	= RET_CODE_ERR_INVALID;
	pCommonCtlInfo->videoStatus	= STATUS_CLEAN;
	pCommonCtlInfo->audioStatus	= STATUS_CLEAN;

	UM_UNLOCK(pCommonCtlInfo);
	return( CELL_OK );
}

int commonEnd( SCommonCtlInfo* pCommonCtlInfo )
{

	(void)pCommonCtlInfo;
	return( CELL_OK );
}

int commonGetMode( SCommonCtlInfo *pCommonCtlInfo)
{
	int mode;
	UM_LOCK(pCommonCtlInfo);
	mode = pCommonCtlInfo->mode;
	UM_UNLOCK(pCommonCtlInfo);
	return( mode );
}

int commonSetMode( SCommonCtlInfo *pCommonCtlInfo, int mode, int flag )
{
	UM_LOCK(pCommonCtlInfo);
	if( flag == BIT_CLEAN ) {
		pCommonCtlInfo->mode &= ~mode;
	} else {
		pCommonCtlInfo->mode |= mode;
	}
	UM_UNLOCK(pCommonCtlInfo);
	return( CELL_OK );
}

int commonGetVideoStatus( SCommonCtlInfo *pCommonCtlInfo )
{
	int status;
	UM_LOCK(pCommonCtlInfo);
	status = pCommonCtlInfo->videoStatus;
	UM_UNLOCK(pCommonCtlInfo);
	return( status );
}

int commonSetVideoStatus( SCommonCtlInfo *pCommonCtlInfo, int status, int flag )
{
	UM_LOCK(pCommonCtlInfo);
	if( flag == BIT_CLEAN ) {
		pCommonCtlInfo->videoStatus &= ~status;
	} else {
		pCommonCtlInfo->videoStatus |= status;
	}
	UM_UNLOCK(pCommonCtlInfo);
	return( CELL_OK );
}

int commonGetAudioStatus( SCommonCtlInfo *pCommonCtlInfo )
{
	int status;
	UM_LOCK(pCommonCtlInfo);
	status = pCommonCtlInfo->audioStatus;
	UM_UNLOCK(pCommonCtlInfo);
	return( status );
}

int commonSetAudioStatus( SCommonCtlInfo *pCommonCtlInfo, int status, int flag )
{
	UM_LOCK(pCommonCtlInfo);
	if( flag == BIT_CLEAN ) {
		pCommonCtlInfo->audioStatus &= ~status;
	} else {
		pCommonCtlInfo->audioStatus |= status;
	}
	UM_UNLOCK(pCommonCtlInfo);
	return( CELL_OK );
}

int commonGetErrorCode( SCommonCtlInfo *pCommonCtlInfo )
{
	int errorCode;
	UM_LOCK(pCommonCtlInfo);
	errorCode = pCommonCtlInfo->errorCode;
	UM_UNLOCK(pCommonCtlInfo);
	return( errorCode );
}

int commonErrorExit(SCommonCtlInfo* pCommonCtlInfo, int retCode)
{
	SAviDmuxCtlInfo*	pAviDmuxCtlInfo =
		(SAviDmuxCtlInfo*)pCommonCtlInfo->pAviDmuxCtlInfo;

	SVdecCtlInfo*	pVdecCtlInfo	= (SVdecCtlInfo*)	pCommonCtlInfo->pVdecCtlInfo;
	SVpostCtlInfo*	pVpostCtlInfo	= (SVpostCtlInfo*)	pCommonCtlInfo->pVpostCtlInfo;
	SVdispCtlInfo*	pVdispCtlInfo	= (SVdispCtlInfo*)	pCommonCtlInfo->pVdispCtlInfo;

	SAdecCtlInfo*	pAdecCtlInfo	= (SAdecCtlInfo*)	pCommonCtlInfo->pAdecCtlInfo;
	SApostCtlInfo*	pApostCtlInfo	= (SApostCtlInfo*)	pCommonCtlInfo->pApostCtlInfo;
	SAmixerCtlInfo*	pAmixerCtlInfo	= (SAmixerCtlInfo*)	pCommonCtlInfo->pAmixerCtlInfo;


	if( commonGetMode(pCommonCtlInfo) & MODE_EXEC ){
		commonSetMode(pCommonCtlInfo, MODE_EXEC, BIT_CLEAN );
		UM_LOCK(pCommonCtlInfo);
		pCommonCtlInfo->errorCode	= retCode;
		UM_UNLOCK(pCommonCtlInfo);

		utilMonitorSignal( &pAviDmuxCtlInfo->umInput );

		utilMonitorSignal( &pVdecCtlInfo->umInput );
		utilMonitorSignal( &pVpostCtlInfo->umInput );
		utilMonitorSignal( &pVdispCtlInfo->umInput );

		utilMonitorSignal( &pAdecCtlInfo->umInput );
		utilMonitorSignal( &pApostCtlInfo->umInput );
		utilMonitorSignal( &pAmixerCtlInfo->umInput );
	}
	return( CELL_OK );
}

void commonSysutilCb(uint64_t status, uint64_t param, void * userdata)
{
	(void)param;
	SCommonCtlInfo*	pCommonCtlInfo = (SCommonCtlInfo*)userdata;
	if(mm_shutdown || !is_bg_video) commonErrorExit(pCommonCtlInfo, RET_CODE_ERR_FORCE_EXIT);

	switch(status){
	case CELL_SYSUTIL_REQUEST_EXITGAME:
		DP("receive CELL_SYSUTIL_REQUEST_EXITGAME.\n");
		commonErrorExit(pCommonCtlInfo, RET_CODE_ERR_FORCE_EXIT);
		break;
	default:
		break;
	}
}

