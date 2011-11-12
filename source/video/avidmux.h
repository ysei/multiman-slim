#ifndef _AVIDMUX_H_
#define _AVIDMUX_H_

#include "util.h"
#include "common.h"


/* FourCC */
#define AVIDMUX_MAKE_FCC(a,b,c,d)	((a<<24)|(b<<16)|(c<<8)|d)

#define AVIDMUX_FCC_RIFF	AVIDMUX_MAKE_FCC('R','I','F','F')
#define AVIDMUX_FCC_LIST	AVIDMUX_MAKE_FCC('L','I','S','T')
#define AVIDMUX_FCC_JUNK	AVIDMUX_MAKE_FCC('J','U','N','K')

#define AVIDMUX_FCC_AVI		AVIDMUX_MAKE_FCC('A','V','I',' ')
#define AVIDMUX_FCC_hdrl	AVIDMUX_MAKE_FCC('h','d','r','l')
#define AVIDMUX_FCC_avih	AVIDMUX_MAKE_FCC('a','v','i','h')
#define AVIDMUX_FCC_strl	AVIDMUX_MAKE_FCC('s','t','r','l')
#define AVIDMUX_FCC_strh	AVIDMUX_MAKE_FCC('s','t','r','h')
#define AVIDMUX_FCC_vids	AVIDMUX_MAKE_FCC('v','i','d','s')
#define AVIDMUX_FCC_auds	AVIDMUX_MAKE_FCC('a','u','d','s')
#define AVIDMUX_FCC_strf	AVIDMUX_MAKE_FCC('s','t','r','f')
#define AVIDMUX_FCC_movi	AVIDMUX_MAKE_FCC('m','o','v','i')
#define AVIDMUX_FCC_idx1	AVIDMUX_MAKE_FCC('i','d','x','1')

#define AVIDMUX_FCC_AVIX	AVIDMUX_MAKE_FCC('A','V','I','X')
#define AVIDMUX_FCC_odml	AVIDMUX_MAKE_FCC('o','d','m','l')
#define AVIDMUX_FCC_dmlh	AVIDMUX_MAKE_FCC('d','m','l','h')
#define AVIDMUX_FCC_indx	AVIDMUX_MAKE_FCC('i','n','d','x')

#define AVIDMUX_FCC_divx	AVIDMUX_MAKE_FCC('d','i','v','x')
#define AVIDMUX_FCC_xvid	AVIDMUX_MAKE_FCC('x','v','i','d')

#define AVIDMUX_FCC_DIVX	AVIDMUX_MAKE_FCC('D','I','V','X')
#define AVIDMUX_FCC_DX50	AVIDMUX_MAKE_FCC('D','X','5','0')

/* AVI1.0 */
#define AVIDMUX_AVIF_HASINDEX				0x00000010
#define AVIDMUX_AVIF_MUSTUSEINDEX			0x00000020
#define AVIDMUX_AVIF_ISINTERLEAVED			0x00000100
#define AVIDMUX_AVIF_TRUSTCKTYPE			0x00000800
#define AVIDMUX_AVIF_WASCAPTUREFILE			0x00010000
#define AVIDMUX_AVIF_COPYRIGHTED			0x00020000
#define AVIDMUX_AVISF_DISABLED				0x00000001
#define AVIDMUX_AVISF_VIDEO_PALCHANGES		0x00010000
#define AVIDMUX_AVIIF_LIST					0x0001
#define AVIDMUX_AVIIF_TWOCC					0x0002
#define AVIDMUX_AVIIF_KEYFRAME				0x0010
#define AVIDMUX_AVIIF_FIRSTPART				0x0020
#define AVIDMUX_AVIIF_LASTPART				0x0040
#define AVIDMUX_AVIIF_MIDPART				(AVIDMUX_AVIIF_FIRSTPART|AVIDMUX_AVIIF_LASTPART)
#define AVIDMUX_AVIIF_NOTIME				0x0100

/* OpenDML */
#define AVIDMUX_AVI_INDEX_OF_INDEXES		0x00
#define AVIDMUX_AVI_INDEX_OF_CHUNKS			0x01
#define AVIDMUX_AVI_INDEX_OF_TIMED_CHUNKS	0x02
#define AVIDMUX_AVI_INDEX_OF_SUB_2FIELD		0x03
#define AVIDMUX_AVI_INDEX_IS_DATA			0x80
#define AVIDMUX_AVI_INDEX_SUB_DEFAULT		0x00
#define AVIDMUX_AVI_INDEX_SUB_2FIELD		0x01

/* AVI file information */
typedef struct{
	uint32_t			dwMicroSecPerFrame;
	uint32_t			dwMaxBytesPerSec;
	uint32_t			dwPaddingGranularity;
	uint32_t			dwFlags;
	uint32_t			dwTotalFrames;
	uint32_t			dwInitialFrames;
	uint32_t			dwStreams;
	uint32_t			dwSuggestedBufferSize;
	uint32_t			dwWidth;
	uint32_t			dwHeight;
	uint32_t			dwReserved[4];

	bool				odmlIndexValid;
	uint32_t			odmlTotalFrames;
	uint32_t			validStreamNum;
}SAviDmuxFileInfo;


/* Video stream information */
typedef struct{
	uint32_t			biSize;
	int32_t				biWidth;
	int32_t				biHeight;
	uint16_t			biPlanes;
	uint16_t			biBitCount;
	uint32_t			biCompression;
	uint32_t			biSizeImage;
	int32_t				biXPelsPerMeter;
	int32_t				biYPelsPerMeter;
	uint32_t			biClrUsed;
	uint32_t			biClrImportant;
}SAviDmuxBmpInfoHeader;


/* Audio stream information */
#pragma pack(1)

typedef struct{
	uint16_t			wFormatTag;
	uint16_t			nChannels;
	uint32_t			nSamplesPerSec;
	uint32_t			nAvgBytesPerSec;
	uint16_t			nBlockAlign;
	uint16_t			wBitsPerSample;
	uint16_t			cbSize;
}SAviDmuxWaveFormatEx;

#define AVIDMUX_WAVE_FORMAT_PCM				0x0001
#define AVIDMUX_WAVE_FORMAT_MPEG			0x0050
#define AVIDMUX_WAVE_FORMAT_MPEGLAYER3		0x0055
#define AVIDMUX_WAVE_FORMAT_AC3				0x2000

typedef struct{
	SAviDmuxWaveFormatEx	wfx;
	uint16_t				fwHeadLayer;
	uint32_t				dwHeadBitrate;
	uint16_t				fwHeadMode;
	uint16_t				fwHeadModeExt;
	uint16_t				wHeadEmphasis;
	uint16_t				fwHeadFlags;
	uint32_t				dwPTSLow;
	uint32_t				dwPTSHigh;
}SAviDmuxMpeg1WaveFormat;

#define AVIDMUX_ACM_MPEG_LAYER1             (0x0001)
#define AVIDMUX_ACM_MPEG_LAYER2             (0x0002)
#define AVIDMUX_ACM_MPEG_LAYER3             (0x0004)
#define AVIDMUX_ACM_MPEG_STEREO             (0x0001)
#define AVIDMUX_ACM_MPEG_JOINTSTEREO        (0x0002)
#define AVIDMUX_ACM_MPEG_DUALCHANNEL        (0x0004)
#define AVIDMUX_ACM_MPEG_SINGLECHANNEL      (0x0008)
#define AVIDMUX_ACM_MPEG_PRIVATEBIT         (0x0001)
#define AVIDMUX_ACM_MPEG_COPYRIGHT          (0x0002)
#define AVIDMUX_ACM_MPEG_ORIGINALHOME       (0x0004)
#define AVIDMUX_ACM_MPEG_PROTECTIONBIT      (0x0008)
#define AVIDMUX_ACM_MPEG_ID_MPEG1           (0x0010)

typedef struct{
	SAviDmuxWaveFormatEx	wfx;
	uint16_t				wID;
	uint32_t				fdwFlags;
	uint16_t				nBlockSize;
	uint16_t				nFramesPerBlock;
	uint16_t				nCodecDelay;
}SAviDmuxMpegLayer3WaveFormat;

#define AVIDMUX_MPEGLAYER3_ID_UNKNOWN            0
#define AVIDMUX_MPEGLAYER3_ID_MPEG               1
#define AVIDMUX_MPEGLAYER3_ID_CONSTANTFRAMESIZE  2
#define AVIDMUX_MPEGLAYER3_FLAG_PADDING_ISO      0x00000000
#define AVIDMUX_MPEGLAYER3_FLAG_PADDING_ON       0x00000001
#define AVIDMUX_MPEGLAYER3_FLAG_PADDING_OFF      0x00000002

#pragma pack()

typedef union{
	SAviDmuxBmpInfoHeader			bmpInfoHeader;
	SAviDmuxWaveFormatEx			waveFormatEx;
	SAviDmuxMpeg1WaveFormat			mpeg1WaveFormat;
	SAviDmuxMpegLayer3WaveFormat	mpegLayer3WaveFormat;
}UAviDmuxStreamSpecInfo;


/* Stream information */
typedef struct{
	uint32_t			fccType;
	uint32_t			fccHandler;
	uint32_t			dwFlags;
	uint16_t			wPriority;
	uint16_t			wLanguage;
	uint32_t			dwInitialFrames;
	uint32_t			dwScale;
	uint32_t			dwRate;
	uint32_t			dwStart;
	uint32_t			dwLength;
	uint32_t			dwSuggestedBufferSize;
	uint32_t			dwQuality;
	uint32_t			dwSampleSize;
	struct{
		uint16_t		left;
		uint16_t		top;
		uint16_t		right;
		uint16_t		bottom;
	}rcFrame;

	UAviDmuxStreamSpecInfo	specInfo;

	uint32_t			indexNum;
	uint32_t			maxDataSize;
}SAviDmuxStreamInfo;



/* Max stream number */
#define	AVIDMUX_MAX_STREAM_NUM		18

#define	AVIDMUX_CODEC_TYPE_ANY		0xFFFFFFFF
#define	AVIDMUX_TIME_STAMP_INVALID	0xFFFFFFFF

enum{
	AVIDMUX_MSG_TYPE_AU_FOUND,
	AVIDMUX_MSG_TYPE_DMUX_DONE,
};

typedef void (*aviDmuxCbFunc)(uint32_t msg, void* arg);

typedef struct{
	void*				auAddr;
	size_t				auSize;
	uint32_t			ptsUpper;
	uint32_t			ptsLower;
	uint32_t			dtsUpper;
	uint32_t			dtsLower;
}SAviDmuxAuInfo;

typedef struct{
	uint64_t			offset;
	uint32_t			size;
	uint32_t			flags;
}SAviDmuxPosition;


typedef struct{
	void*				pHandle;
	aviDmuxCbFunc		cbNotifyFunc;
	void*				cbNotifyArg;
	uint32_t			streamIndex;
	uint32_t			positionIndex;
	uint32_t			auBufferNum;
	uint32_t			maxAuSize;

	uint8_t*			pParseBuffer;
	uint32_t			parseBufferSize;
	bool				parseSyncSearch;
	uint32_t			parseStartIndex;
	uint32_t			parseSize;
	uint32_t			parseAuCount;
	int32_t				parseLastError;

	SAviDmuxAuInfo*		pAuInfo;
	uint8_t*			pAuBuffer;
	UtilMonitor			monitor;
	UtilQueue			auWriteQueue;
	UtilQueue			auReadQueue;
	UtilQueue			auReleaseQueue;
}SAviDmuxEsCtlInfo;

typedef SAviDmuxEsCtlInfo*	AviDmuxEsHandle;

typedef int (*aviDmuxEsParseFunc)(UtilBufferedFileReader* pBfr,
	SAviDmuxStreamInfo* pStreamInfo, SAviDmuxEsCtlInfo* pEsCtlInfo,
	SAviDmuxPosition* pPosition);

typedef struct{
	bool					bReleaseDone;
}SAviDmuxStatus;

typedef struct{
	SCommonCtlInfo*			pCommonCtlInfo;

	sys_ppu_thread_t		threadId;
	int						errorCode;
	UtilMonitor				umInput;
	SAviDmuxStatus			recvStatus;

	UtilBufferedFileReader	bfr;

	SAviDmuxFileInfo		fileInfo;
	SAviDmuxStreamInfo		streamInfo[AVIDMUX_MAX_STREAM_NUM];

	SAviDmuxPosition*		streamPosition[AVIDMUX_MAX_STREAM_NUM];
	unsigned long long*		superIndex[AVIDMUX_MAX_STREAM_NUM];

	SAviDmuxEsCtlInfo		esCtlInfo[AVIDMUX_MAX_STREAM_NUM];
	aviDmuxEsParseFunc		esParseFunc[AVIDMUX_MAX_STREAM_NUM];
}SAviDmuxCtlInfo;


int aviDmuxSetParam(SAviDmuxCtlInfo* pAviDmuxCtlInfo,
	SCommonCtlInfo* pCommonCtlInfo);

int aviDmuxOpen(SAviDmuxCtlInfo* pAviDmuxCtlInfo, const char* filePath);

int aviDmuxClose(SAviDmuxCtlInfo* pAviDmuxCtlInfo);

int aviDmuxGetFileInfo(SAviDmuxCtlInfo* pAviDmuxCtlInfo,
	const SAviDmuxFileInfo** ppFileInfo);

int aviDmuxFindVideoStream(SAviDmuxCtlInfo* pAviDmuxCtlInfo, uint32_t fccHandler,
	uint32_t biCompression, uint32_t startIndex, uint32_t* pStreamIndex);

int aviDmuxFindAudioStream(SAviDmuxCtlInfo* pAviDmuxCtlInfo, uint32_t wFormatTag,
	uint32_t startIndex, uint32_t* pStreamIndex);

int aviDmuxGetStreamInfo(SAviDmuxCtlInfo* pAviDmuxCtlInfo, uint32_t streamIndex,
	const SAviDmuxStreamInfo** ppStreamInfo);

int aviDmuxStart(SAviDmuxCtlInfo* pAviDmuxCtlInfo, int prio, size_t stacksize);

int aviDmuxEnd(SAviDmuxCtlInfo* pAviDmuxCtlInfo);

int aviDmuxEnableEs(SAviDmuxCtlInfo* pAviDmuxCtlInfo, const SAviDmuxStreamInfo* pStreamInfo,
	aviDmuxCbFunc cbNotifyFunc, void* cbNotifyArg, AviDmuxEsHandle* pEsHandle);

int aviDmuxDisableEs(AviDmuxEsHandle esHandle);

int aviDmuxGetAu(AviDmuxEsHandle esHandle, const SAviDmuxAuInfo **auInfo, void **auSpecificInfo);

int aviDmuxReleaseAu(AviDmuxEsHandle esHandle);

#endif /* _AVIDMUX_H_ */
