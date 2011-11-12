
#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/timer.h>
#include <sys/process.h>
#include <sys/ppu_thread.h>
#include <sys/spu_initialize.h>
#include <sys/synchronization.h>
#include <sys/paths.h>

#include <sysutil/sysutil_sysparam.h>

#include <cell/error.h>
#include <cell/sysmodule.h>
#include <cell/codec.h>
#include <cell/vpost.h>

/* configuration of audio and video buffer size. */
#define VIDEO_FULL_RATE_BUFFER_NUM	4
#define VIDEO_HALF_RATE_BUFFER_NUM	2
#define AUDIO_BUFFER_SAMPLES		8000

/* API call retry interval on busy status. */
#define RETRY_INTERVAL				500


/* configuration of primary PPU thread. */
#define PRIMARY_PPU_THREAD_PRIO		900
#define PRIMARY_PPU_STACK_SIZE		65536


/* configuration of stream input module. */
#define STMIN_PPU_THREAD_PRIO		820
#define STMIN_PPU_STACK_SIZE		8192
#define STMIN_BANK_SIZE				(1024*512)
#define STMIN_BANK_NUM				4


/* configuration of demuxer module. */
#define DMUX_ES_NUM					2

#define DMUX_PPU_THREAD_PRIO		800
#define DMUX_PPU_STACK_SIZE			16384

#define DMUX_LIB_PPU_THREAD_PRIO	780
#define DMUX_LIB_PPU_STACK_SIZE		16384
#define DMUX_LIB_SPU_THREAD_PRIO	250
#define DMUX_LIB_SPU_NUM			1


/* configuration of video decoder module. */
#define VDEC_PPU_THREAD_PRIO		740
#define VDEC_PPU_STACK_SIZE			16384

#define VDEC_LIB_PPU_THREAD_PRIO	720
#define VDEC_LIB_PPU_STACK_SIZE		16384
#define VDEC_LIB_SPU_THREAD_PRIO	200
#define VDEC_LIB_DIVX_SPU_NUM		2


/* configuration of audio decoder module. */
#define ADEC_PPU_THREAD_PRIO		700
#define ADEC_PPU_STACK_SIZE			16384

#define ADEC_LIB_PPU_THREAD_PRIO	680
#define ADEC_LIB_PPU_STACK_SIZE		16384
#define ADEC_LIB_SPU_THREAD_PRIO	150


/* configuration of video post processing module. */
#define VPOST_PPU_THREAD_PRIO		640
#define VPOST_PPU_STACK_SIZE		16384

#define VPOST_LIB_PPU_THREAD_PRIO	620
#define VPOST_LIB_PPU_STACK_SIZE	16384
#define VPOST_LIB_SPU_THREAD_PRIO	100
#define VPOST_LIB_SPU_NUM			1


/* configuration of audio post processing module. */
#define APOST_PPU_THREAD_PRIO		600
#define APOST_PPU_STACK_SIZE		16384


/* configuration of video display module. */
#define VDISP_PPU_THREAD_PRIO		500
#define VDISP_PPU_STACK_SIZE		16384


/* configuration of audio mixer module. */
#define AMIXER_LIB_PPU_THREAD_PRIO	400


/* error code of sample program. */
#define DIVX_AVI_SAMPLE_ERROR_FACILITY	0x801
#define RET_CODE_ERR_ARG		CELL_ERROR_MAKE_ERROR(DIVX_AVI_SAMPLE_ERROR_FACILITY,0x0001)
#define RET_CODE_ERR_SEQ		CELL_ERROR_MAKE_ERROR(DIVX_AVI_SAMPLE_ERROR_FACILITY,0x0002)
#define RET_CODE_ERR_NOT_FOUND	CELL_ERROR_MAKE_ERROR(DIVX_AVI_SAMPLE_ERROR_FACILITY,0x0003)
#define RET_CODE_ERR_EMPTY		CELL_ERROR_MAKE_ERROR(DIVX_AVI_SAMPLE_ERROR_FACILITY,0x0004)
#define RET_CODE_ERR_FULL		CELL_ERROR_MAKE_ERROR(DIVX_AVI_SAMPLE_ERROR_FACILITY,0x0005)
#define RET_CODE_ERR_FATAL		CELL_ERROR_MAKE_ERROR(DIVX_AVI_SAMPLE_ERROR_FACILITY,0x0081)
#define RET_CODE_ERR_FORCE_EXIT	CELL_ERROR_MAKE_ERROR(DIVX_AVI_SAMPLE_ERROR_FACILITY,0x0082)
#define RET_CODE_ERR_UNKNOWN	CELL_ERROR_MAKE_ERROR(DIVX_AVI_SAMPLE_ERROR_FACILITY,0x00FE)
#define RET_CODE_ERR_INVALID	CELL_ERROR_MAKE_ERROR(DIVX_AVI_SAMPLE_ERROR_FACILITY,0x00FF)


/* definition of common module. */
#define	COMMON_SYSUTIL_CB_SLOT		1

#define	BIT_CLEAN					0x00
#define BIT_SET						0x01

#define MODE_CLEAN					0x00
#define MODE_EXEC					0x01

#define	STATUS_CLEAN				0x00
#define	STATUS_READY				0x01
#define	STATUS_END					0x02

typedef struct{
	volatile int	errorCode;

	volatile int	mode;
	volatile int	videoStatus;
	volatile int	audioStatus;

	UtilLWMutex		umInput;

	void*	pAviDmuxCtlInfo;

	void*	pVdecCtlInfo;
	void*	pVpostCtlInfo;
	void*	pVdispCtlInfo;

	void*	pAdecCtlInfo;
	void*	pApostCtlInfo;
	void*	pAmixerCtlInfo;

} SCommonCtlInfo;

int commonSetParam( SCommonCtlInfo* pCommonCtlInfo,
	void*	pAviDmuxCtlInfo,

	void*	pVdecCtlInfo,
	void*	pVpostCtlInfo,
	void*	pVdispCtlInfo,

	void*	pAdecCtlInfo,
	void*	pApostCtlInf,
	void*	pAmixerCtlInfo
);
int commonOpen( SCommonCtlInfo* pCommonCtlInfo );
int commonClose( SCommonCtlInfo* pCommonCtlInfo );
int commonStart( SCommonCtlInfo* pCommonCtlInfo );
int commonEnd( SCommonCtlInfo* pCommonCtlInfo );

int commonGetMode( SCommonCtlInfo *pCommonCtlInfo);
int commonSetMode( SCommonCtlInfo *pCommonCtlInfo, int mode, int flag );
int commonGetVideoStatus( SCommonCtlInfo *pCommonCtlInfo );
int commonSetVideoStatus( SCommonCtlInfo *pCommonCtlInfo, int status, int flag );
int commonGetAudioStatus( SCommonCtlInfo *pCommonCtlInfo );
int commonSetAudioStatus( SCommonCtlInfo *pCommonCtlInfo, int status, int flag );

int commonGetErrorCode( SCommonCtlInfo *pCommonCtlInfo );
int commonErrorExit( SCommonCtlInfo* pCommonCtlInfo, int retCode );

void commonSysutilCb(uint64_t status, uint64_t param, void * userdata);

#endif /* _COMMON_H_ */
