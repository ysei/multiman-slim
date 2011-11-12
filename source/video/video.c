#include "avidmux.h"
#include "vdec.h"
#include "adec.h"
#include "avsync.h"
#include "../include/graphics.h"
#include "video.h"

/* definition of control information of each modules. */
static SCommonCtlInfo	commonCtlInfo;
static SAVsyncCtlInfo	avsyncCtlInfo;

static SAviDmuxCtlInfo	aviDmuxCtlInfo;

static SVdecCtlInfo		vdecCtlInfo;
static SVpostCtlInfo	vpostCtlInfo;
static SVdispCtlInfo	vdispCtlInfo;

static SAdecCtlInfo		adecCtlInfo;
static SApostCtlInfo	apostCtlInfo;
static SAmixerCtlInfo	amixerCtlInfo;

extern bool mm_shutdown;
extern u8 is_bg_video;
extern int video_status;

/* definition of static functions. */
static int openAvi(const SAviDmuxFileInfo** ppFileInfo,
	const SAviDmuxStreamInfo** ppVideoInfo, const SAviDmuxStreamInfo** ppAudioInfo);

static const char* getAudioTypsString(const SAviDmuxWaveFormatEx* pWaveFormatEx);

//static int printAviInfo(SAviDmuxCtlInfo* pAviDmuxCtlInfo, const SAviDmuxFileInfo* pAviFileInfo);

static int closeAvi(void);

static int setParamModules(uint32_t resolution, uint32_t dispWidth, uint32_t dispHeight,
	const SAviDmuxStreamInfo* pVideoInfo, const SAviDmuxStreamInfo* pAudioInfo);

static int openModules(
	const SAviDmuxStreamInfo* pVideoInfo, const SAviDmuxStreamInfo* pAudioInfo,
	AviDmuxEsHandle* pVideoEsHandle, AviDmuxEsHandle* pAudioEsHandle);

static int closeModules(
	AviDmuxEsHandle videoEsHandle, AviDmuxEsHandle audioEsHandle);

static int playSequence(void);


/* primary PPU thread entry. */

static void video_thread_entry( uint64_t arg );
sys_ppu_thread_t video_thr_id;

char *bg_video_stream;

int main_video(char *path){


	if(!is_bg_video)
	{
		is_bg_video=1;
		bg_video_stream=path;
		sys_ppu_thread_create( &video_thr_id, video_thread_entry,
						   NULL, 1001, 32768,
						   0, "multiMAN_video" );
	}
	else
	{
		is_bg_video=0;
	}

	return 0;
}

void video_thread_entry( uint64_t arg )
{
	int		ret;
	(void)arg;

	/* set parameter of common module. */
	ret = commonSetParam(
		&commonCtlInfo, &aviDmuxCtlInfo,
		&vdecCtlInfo, &vpostCtlInfo, &vdispCtlInfo,
		&adecCtlInfo, &apostCtlInfo, &amixerCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		ret=-1;
		goto end_video;
	}

	/* open of common module. */
	ret = commonOpen(&commonCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		goto end_video;
	}

	uint32_t					resolution, dispWidth, dispHeight;
	const SAviDmuxFileInfo*		pFileInfo;
	const SAviDmuxStreamInfo*	pVideoInfo, * pAudioInfo;
	AviDmuxEsHandle				videoEsHandle, audioEsHandle;

	ret = vdispGetResolution(&resolution, &dispWidth, &dispHeight);
	if(ret < CELL_OK){
		EINFO(ret);
		ret=-2;
		goto end_video;
	}

	/* investigate attribute of input stream. */
	ret = openAvi(&pFileInfo, &pVideoInfo, &pAudioInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		//ret=-3;
		goto end_video;
	}

	const SAviDmuxBmpInfoHeader* pBmpInfoHeader = &pVideoInfo->specInfo.bmpInfoHeader;
	DP("video format: DivX %ux%u, scaling to %ux%u\n",
		pBmpInfoHeader->biWidth, pBmpInfoHeader->biHeight,
		dispWidth, dispHeight);
	DP("audio format: %s  %u ch\n", getAudioTypsString(&pAudioInfo->specInfo.waveFormatEx),
		pAudioInfo->specInfo.waveFormatEx.nChannels);

	/* set parameter of each modules. */
	ret = setParamModules(resolution, dispWidth, dispHeight, pVideoInfo, pAudioInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		ret=-4;
		goto end_video;
	}

	/* open of each modules. */
	ret = openModules(pVideoInfo, pAudioInfo, &videoEsHandle, &audioEsHandle);
	if(ret != CELL_OK){
		EINFO(ret);
		ret=-5;
		goto end_video;
	}

	/* play stream. */
	while(ret == CELL_OK) {
		ret = playSequence();
		if(commonGetErrorCode(&commonCtlInfo) != RET_CODE_ERR_INVALID){
			break;
		}
		//if(mm_shutdown || !is_bg_video) break;
	}

	if(ret < CELL_OK && ret != RET_CODE_ERR_FORCE_EXIT){
		EINFO(-7);
	}

	/* close of each modules. */
	ret = closeModules(videoEsHandle, audioEsHandle);
	if(ret < CELL_OK){
		EINFO(ret);
		ret=-6;
//		goto end_video;
	}

	ret = closeAvi();
	if(ret < CELL_OK){
		EINFO(ret);
		ret=-7;
//		goto end_video;
	}

	/* close of common module. */
	ret = commonClose(&commonCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		ret=-8;
//		goto end_video;
	}


end_video:
	is_bg_video=0;
	video_status=ret;
	sys_ppu_thread_exit( 0 );

}

static int setParamModules(uint32_t resolution, uint32_t dispWidth, uint32_t dispHeight,
	const SAviDmuxStreamInfo* pVideoInfo, const SAviDmuxStreamInfo* pAudioInfo)
{
	int			ret;

	if(pVideoInfo->dwScale == 0){
		EMSG("pVideoInfo->dwScale == 0\n");
		return RET_CODE_ERR_FATAL;
	}

	ret = avsyncSetParam(&avsyncCtlInfo, &commonCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	const SAviDmuxBmpInfoHeader* pBmpInfoHeader = &pVideoInfo->specInfo.bmpInfoHeader;
	const uint32_t	videoFps = pVideoInfo->dwRate / pVideoInfo->dwScale;
	const uint32_t	videoBufferNum = (videoFps > 30)
		? VIDEO_FULL_RATE_BUFFER_NUM: VIDEO_HALF_RATE_BUFFER_NUM;
	const uint32_t	videoWidth = pBmpInfoHeader->biWidth;
	const uint32_t	videoHeight = pBmpInfoHeader->biHeight;

	const SAviDmuxWaveFormatEx* pWaveFormatEx = &pAudioInfo->specInfo.waveFormatEx;
	const uint32_t	audioAuSample =
		(pWaveFormatEx->wFormatTag == AVIDMUX_WAVE_FORMAT_AC3)? 1536: 1152;
	const uint32_t	audioBufferNum = (AUDIO_BUFFER_SAMPLES+audioAuSample-1)/audioAuSample;
	const uint32_t	audioChNum = (pWaveFormatEx->nChannels == 5)
		? pWaveFormatEx->nChannels + 1: pWaveFormatEx->nChannels;

	ret = vdispSetParam(&vdispCtlInfo, &commonCtlInfo, &avsyncCtlInfo,
		dispWidth, dispHeight, videoWidth, videoHeight,
		videoBufferNum, resolution);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = amixerSetParam(&amixerCtlInfo, &commonCtlInfo, &avsyncCtlInfo,
		audioChNum, audioAuSample, audioBufferNum);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = vpostSetParam(&vpostCtlInfo, &commonCtlInfo, &vdispCtlInfo,
		videoWidth, videoHeight, dispWidth, dispHeight, videoBufferNum);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = apostSetParam(&apostCtlInfo, &commonCtlInfo, &amixerCtlInfo,
		audioChNum, audioAuSample, audioBufferNum);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = vdecSetParam(&vdecCtlInfo, &commonCtlInfo, &vpostCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = adecSetParam(&adecCtlInfo, &commonCtlInfo, &apostCtlInfo, audioChNum);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	return CELL_OK;
}

static int openModules(
	const SAviDmuxStreamInfo* pVideoInfo, const SAviDmuxStreamInfo* pAudioInfo,
	AviDmuxEsHandle* pVideoEsHandle, AviDmuxEsHandle* pAudioEsHandle)
{
	int		ret;

	ret = aviDmuxEnableEs(&aviDmuxCtlInfo, pVideoInfo,
		vdecDmuxCb, &vdecCtlInfo, pVideoEsHandle);
	if(ret < CELL_OK){
		EINFO(ret);
		return 20;
	}

	ret = aviDmuxEnableEs(&aviDmuxCtlInfo, pAudioInfo,
		adecDmuxCb, &adecCtlInfo, pAudioEsHandle);
	if(ret < CELL_OK){
		EINFO(ret);
		return 21;
	}

	ret = avsyncOpen(&avsyncCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return 22;
	}

	ret = vdispOpen(&vdispCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return 23;
	}

	ret = amixerOpen(&amixerCtlInfo, AMIXER_LIB_PPU_THREAD_PRIO);
	if(ret < CELL_OK){
		EINFO(ret);
		return 24;
	}

	ret = vpostOpen(&vpostCtlInfo, VPOST_LIB_PPU_THREAD_PRIO, VPOST_LIB_PPU_STACK_SIZE,
		VPOST_LIB_SPU_THREAD_PRIO, VPOST_LIB_SPU_NUM);
	if(ret < CELL_OK){
		EINFO(ret);
		return 25;
	}

	ret = apostOpen(&apostCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return 26;
	}

	ret = vdecOpen(&vdecCtlInfo, pVideoInfo, *pVideoEsHandle, VDEC_LIB_PPU_THREAD_PRIO,
		VDEC_LIB_PPU_STACK_SIZE, VDEC_LIB_SPU_THREAD_PRIO, VDEC_LIB_DIVX_SPU_NUM);
	if(ret < CELL_OK){
		EINFO(ret);
		return 27;
	}

	ret = adecOpen(&adecCtlInfo, pAudioInfo, *pAudioEsHandle, ADEC_LIB_PPU_THREAD_PRIO,
		ADEC_LIB_PPU_STACK_SIZE, ADEC_LIB_SPU_THREAD_PRIO);
	if(ret < CELL_OK){
		EINFO(ret);
		return 28;
	}

	return CELL_OK;
}

static int closeModules(
	AviDmuxEsHandle videoEsHandle, AviDmuxEsHandle audioEsHandle)
{
	int		ret;

	ret = adecClose(&adecCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = vdecClose(&vdecCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = apostClose(&apostCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = vpostClose(&vpostCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = amixerClose(&amixerCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = vdispClose(&vdispCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = avsyncClose(&avsyncCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = aviDmuxDisableEs(audioEsHandle);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = aviDmuxDisableEs(videoEsHandle);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	return CELL_OK;
}

static int playSequence(void){
	int		ret;

	ret = commonStart(&commonCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = avsyncStart(&avsyncCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = vdispStart(&vdispCtlInfo, VDISP_PPU_THREAD_PRIO, VDISP_PPU_STACK_SIZE,
		vpostGetFrameBuffer(&vpostCtlInfo));
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = vpostStart(&vpostCtlInfo, VPOST_PPU_THREAD_PRIO, VPOST_PPU_STACK_SIZE,
		vdecGetVdecHandle(&vdecCtlInfo), vdecPictureOutCb, &vdecCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = vdecStart(&vdecCtlInfo, VDEC_PPU_THREAD_PRIO, VDEC_PPU_STACK_SIZE);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = amixerStart(&amixerCtlInfo, apostGetPcmBuffer(&apostCtlInfo));
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = apostStart(&apostCtlInfo, APOST_PPU_THREAD_PRIO, APOST_PPU_STACK_SIZE,
		adecGetAdecHandle(&adecCtlInfo), adecPcmOutCb, &adecCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = adecStart(&adecCtlInfo, ADEC_PPU_THREAD_PRIO, ADEC_PPU_STACK_SIZE);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = aviDmuxStart(&aviDmuxCtlInfo, DMUX_PPU_THREAD_PRIO, DMUX_PPU_STACK_SIZE);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}


	ret = aviDmuxEnd(&aviDmuxCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = adecEnd(&adecCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = apostEnd(&apostCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = amixerEnd(&amixerCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = vdecEnd(&vdecCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = vpostEnd(&vpostCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = vdispEnd(&vdispCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = avsyncEnd(&avsyncCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	ret = commonEnd(&commonCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	return CELL_OK;
}


static int openAvi(const SAviDmuxFileInfo** ppFileInfo,
	const SAviDmuxStreamInfo** ppVideoInfo, const SAviDmuxStreamInfo** ppAudioInfo)
{
	int				ret;

	ret = aviDmuxSetParam(&aviDmuxCtlInfo, &commonCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		ret=-10;
		return ret;
	}

	ret = aviDmuxOpen(&aviDmuxCtlInfo, bg_video_stream);
	if(ret < CELL_OK){
		EINFO(ret);
		ret=-11;
		return ret;
	}

	ret = aviDmuxGetFileInfo(&aviDmuxCtlInfo, ppFileInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		ret=-12;
		return ret;
	}

	//ret = printAviInfo(&aviDmuxCtlInfo, *ppFileInfo); if(ret < CELL_OK){	EINFO(ret);	ret=-13;	return ret;	}

	uint32_t	videoIndex;
	ret = aviDmuxFindVideoStream(&aviDmuxCtlInfo, AVIDMUX_CODEC_TYPE_ANY,
		AVIDMUX_CODEC_TYPE_ANY, 0, &videoIndex);
	if(ret < CELL_OK){
		EINFO(ret);
		ret=-14;
		return ret;
	}

	ret = aviDmuxGetStreamInfo(&aviDmuxCtlInfo, videoIndex, ppVideoInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		ret=-15;
		return ret;
	}

	uint32_t	audioIndex;
	do{
		audioIndex = 0;
		while(1){
			ret = aviDmuxFindAudioStream(&aviDmuxCtlInfo,
				AVIDMUX_WAVE_FORMAT_MPEGLAYER3, audioIndex, &audioIndex);
			if(ret < CELL_OK){
				break;
			}
			ret = aviDmuxGetStreamInfo(&aviDmuxCtlInfo, audioIndex, ppAudioInfo);
			if(ret < CELL_OK){
				break;
			}
			break; //if((*ppAudioInfo)->specInfo.waveFormatEx.nSamplesPerSec == 48000){	break;	}
			++audioIndex;
		}
		if(ret == CELL_OK || ret != RET_CODE_ERR_NOT_FOUND){
			break;
		}

		audioIndex = 0;
		while(1){
			ret = aviDmuxFindAudioStream(&aviDmuxCtlInfo,
				AVIDMUX_WAVE_FORMAT_MPEG, audioIndex, &audioIndex);
			if(ret < CELL_OK){
				break;
			}
			ret = aviDmuxGetStreamInfo(&aviDmuxCtlInfo, audioIndex, ppAudioInfo);
			if(ret < CELL_OK){
				break;
			}
			break; //if((*ppAudioInfo)->specInfo.waveFormatEx.nSamplesPerSec == 48000){	break;	}
			++audioIndex;
		}
		if(ret == CELL_OK || ret != RET_CODE_ERR_NOT_FOUND){
			break;
		}

		audioIndex = 0;
		while(1){
			ret = aviDmuxFindAudioStream(&aviDmuxCtlInfo,
				AVIDMUX_WAVE_FORMAT_AC3, audioIndex, &audioIndex);
			if(ret < CELL_OK){
				break;
			}
			ret = aviDmuxGetStreamInfo(&aviDmuxCtlInfo, audioIndex, ppAudioInfo);
			if(ret < CELL_OK){
				break;
			}
			break; //if((*ppAudioInfo)->specInfo.waveFormatEx.nSamplesPerSec == 48000){	break;	}
			++audioIndex;
		}
		if(ret == CELL_OK || ret != RET_CODE_ERR_NOT_FOUND){
			break;
		}

	}while(0);
	if(ret < CELL_OK){
		EINFO(ret);
		ret=-16;
		return ret;
	}

	DP("stream select, videoIndex: %u, audioIndex: %u\n", videoIndex, audioIndex);

	return CELL_OK;
}

static int closeAvi(void){
	int				ret;

	ret = aviDmuxClose(&aviDmuxCtlInfo);
	if(ret < CELL_OK){
		EINFO(ret);
		return ret;
	}

	return CELL_OK;
}

static const char* getAudioTypsString(const SAviDmuxWaveFormatEx* pWaveFormatEx){
	const char*	wFormatTagString = "unknown";
	switch(pWaveFormatEx->wFormatTag){
	case AVIDMUX_WAVE_FORMAT_MPEG:
		wFormatTagString = "MPEG";
		break;
	case AVIDMUX_WAVE_FORMAT_MPEGLAYER3:
		wFormatTagString = "MP3";
		break;
	case AVIDMUX_WAVE_FORMAT_AC3:
		wFormatTagString = "AC3";
		break;
	}
	return wFormatTagString;
}

/*
static int printAviInfo(SAviDmuxCtlInfo* pAviDmuxCtlInfo, const SAviDmuxFileInfo* pAviFileInfo){
	int	ret;
	DP("\n");
	DP("==== AviFileInfo ====\n");
	DP("dwMicroSecPerFrame: %u\n", pAviFileInfo->dwMicroSecPerFrame);
	DP("dwMaxBytesPerSec: %u\n", pAviFileInfo->dwMaxBytesPerSec);
	DP("dwPaddingGranularity: %u\n", pAviFileInfo->dwPaddingGranularity);
	DP("dwFlags: 0x%08x\n", pAviFileInfo->dwFlags);
	DP("dwTotalFrames: %u\n", pAviFileInfo->dwTotalFrames);
	DP("dwInitialFrames: %u\n", pAviFileInfo->dwInitialFrames);
	DP("dwStreams: %u\n", pAviFileInfo->dwStreams);
	DP("dwSuggestedBufferSize: %u\n", pAviFileInfo->dwSuggestedBufferSize);
	DP("dwWidth: %u\n", pAviFileInfo->dwWidth);
	DP("dwHeight: %u\n", pAviFileInfo->dwHeight);
	DP("odmlIndexValid: %u\n", pAviFileInfo->odmlIndexValid);
	DP("odmlTotalFrames: %u\n", pAviFileInfo->odmlTotalFrames);
	DP("validStreamNum: %u\n", pAviFileInfo->validStreamNum);
	DP("\n");

	const SAviDmuxStreamInfo*	pAviStreamInfo;
	for(uint32_t streamIndex = 0; streamIndex < pAviFileInfo->validStreamNum; ++streamIndex){
		ret = aviDmuxGetStreamInfo(pAviDmuxCtlInfo, streamIndex, &pAviStreamInfo);
		if(ret < CELL_OK){
			EINFO(ret);
			return -1;
		}
		const unsigned int	fccTypeString[] = { pAviStreamInfo->fccType, 0 };
		const unsigned int	fccHandlerString[] = { pAviStreamInfo->fccHandler, 0 };
		DP("==== AviStreamInfo: %u ====\n", streamIndex);
		switch(pAviStreamInfo->fccType){
		case AVIDMUX_FCC_vids:
			DP("fccType: %s\n", (const char*)fccTypeString);
			DP("fccHandler: %s\n", (const char*)fccHandlerString);
			DP("dwFlags: 0x%08x\n", pAviStreamInfo->dwFlags);
			DP("dwScale: %u\n", pAviStreamInfo->dwScale);
			DP("dwRate: %u\n", pAviStreamInfo->dwRate);
			DP("dwLength: %u\n", pAviStreamInfo->dwLength);
			{
				const SAviDmuxBmpInfoHeader*	pBmpInfoHeader =
					&pAviStreamInfo->specInfo.bmpInfoHeader;
				const unsigned int	biCompressionString[] =
					{ pBmpInfoHeader->biCompression, 0 };
				DP("biWidth: %d\n", pBmpInfoHeader->biWidth);
				DP("biHeight: %d\n", pBmpInfoHeader->biHeight);
				DP("biBitCount: %u\n", pBmpInfoHeader->biBitCount);
				DP("biCompression: %s\n", (const char*)biCompressionString);
				DP("biSizeImage: %u\n", pBmpInfoHeader->biSizeImage);
			}
			DP("indexNum: %u\n", pAviStreamInfo->indexNum);
			DP("maxDataSize: %u\n", pAviStreamInfo->maxDataSize);
			DP("\n");
			break;
		case AVIDMUX_FCC_auds:
			DP("fccType: %s\n", (const char*)fccTypeString);
			DP("dwFlags: 0x%08x\n", pAviStreamInfo->dwFlags);
			DP("dwScale: %u\n", pAviStreamInfo->dwScale);
			DP("dwRate: %u\n", pAviStreamInfo->dwRate);
			DP("dwLength: %u\n", pAviStreamInfo->dwLength);
			DP("dwSampleSize: %u\n", pAviStreamInfo->dwSampleSize);
			{
				const SAviDmuxWaveFormatEx*	pWaveFormatEx =
					&pAviStreamInfo->specInfo.waveFormatEx;
				const char*	wFormatTagString = getAudioTypsString(pWaveFormatEx);
				DP("wFormatTag: 0x%04x(%s)\n", pWaveFormatEx->wFormatTag, wFormatTagString);
				DP("nChannels: %u\n", pWaveFormatEx->nChannels);
				DP("nSamplesPerSec: %u\n", pWaveFormatEx->nSamplesPerSec);
				DP("nBlockAlign: %u\n", pWaveFormatEx->nBlockAlign);
				switch(pWaveFormatEx->wFormatTag){
				case AVIDMUX_WAVE_FORMAT_MPEG:
					{
						const SAviDmuxMpeg1WaveFormat*	pMpeg1WaveFormat =
							&pAviStreamInfo->specInfo.mpeg1WaveFormat;
						DP("fwHeadLayer: %u\n", pMpeg1WaveFormat->fwHeadLayer);
						DP("dwHeadBitrate: %u\n", pMpeg1WaveFormat->dwHeadBitrate);
						DP("fwHeadMode: %u\n", pMpeg1WaveFormat->fwHeadMode);
						DP("fwHeadModeExt: %u\n", pMpeg1WaveFormat->fwHeadModeExt);
						DP("wHeadEmphasis: %u\n", pMpeg1WaveFormat->wHeadEmphasis);
						DP("fwHeadFlags: %u\n", pMpeg1WaveFormat->fwHeadFlags);
						DP("dwPTSLow: %u\n", pMpeg1WaveFormat->dwPTSLow);
						DP("dwPTSHigh: %u\n", pMpeg1WaveFormat->dwPTSHigh);
					}
					break;
				case AVIDMUX_WAVE_FORMAT_MPEGLAYER3:
					{
						const SAviDmuxMpegLayer3WaveFormat*	pMpegLayer3WaveFormat =
							&pAviStreamInfo->specInfo.mpegLayer3WaveFormat;
						DP("wID: %u\n", pMpegLayer3WaveFormat->wID);
						DP("fdwFlags: %u\n", pMpegLayer3WaveFormat->fdwFlags);
						DP("nBlockSize: %u\n", pMpegLayer3WaveFormat->nBlockSize);
						DP("nFramesPerBlock: %u\n", pMpegLayer3WaveFormat->nFramesPerBlock);
						DP("nCodecDelay: %u\n", pMpegLayer3WaveFormat->nCodecDelay);
					}
					break;
				}
			}
			DP("indexNum: %u\n", pAviStreamInfo->indexNum);
			DP("maxDataSize: %u\n", pAviStreamInfo->maxDataSize);
			DP("\n");
			break;
		default:
			DP("fccType: %s\n", (const char*)fccTypeString);
		}
	}
	return CELL_OK;
}
*/
