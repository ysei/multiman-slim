#ifndef _AVSYNC_H_
#define _AVSYNC_H_

#include "util.h"
#include "common.h"

#define AVSYNC_1VSYNC_ON_TIMESTAMP	(90000/(60 * 1000 / 1001))
#define AVSYNC_VDISP_INTERVAL		(AVSYNC_1VSYNC_ON_TIMESTAMP * 2)

typedef enum {
	AVSYNC_VDISP_MODE_NORMAL,
	AVSYNC_VDISP_MODE_SKIP,
	AVSYNC_VDISP_MODE_WAIT
}AVsyncVdispMode;

typedef struct{
	SCommonCtlInfo*				pCommonCtlInfo;
	volatile CellCodecTimeStamp	ptsAudio;
	volatile CellCodecTimeStamp	ptsVideo;
	volatile bool				bAudioValid;
	volatile bool				bVideoValid;

	UtilLWMutex					umInput;
}SAVsyncCtlInfo;

int avsyncSetParam(SAVsyncCtlInfo* pAVsyncCtlInfo, 	SCommonCtlInfo*	pCommonCtlInfo);
int avsyncOpen(SAVsyncCtlInfo *pAVsyncCtlInfo);
int avsyncClose(SAVsyncCtlInfo* pAVsyncCtlInfo);
int avsyncStart(SAVsyncCtlInfo* pAVsyncCtlInfo);
int avsyncEnd(SAVsyncCtlInfo* pAVsyncCtlInfo);

int avsyncSetApts(SAVsyncCtlInfo* pAVsyncCtlInfo, CellCodecTimeStamp ptsAudio);
int avsyncSetVpts(SAVsyncCtlInfo* pAVsyncCtlInfo, CellCodecTimeStamp ptsVideo);
int avsyncSetValidAudio(SAVsyncCtlInfo *pAVsyncCtlInfo, bool bValid);
int avsyncSetValidVideo(SAVsyncCtlInfo *pAVsyncCtlInfo, bool bValid );

int avsyncCompare(SAVsyncCtlInfo* pAVsyncCtlInfo);

#endif /* _AVSYNC_H_ */
