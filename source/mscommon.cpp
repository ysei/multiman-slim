#include "mscommon.h"

static int audioInitCell(void);

// SPURS information
#define				SPURS_SPU_NUM	1
#define				SPU_THREAD_GROUP_PRIORITY		128
CellSpurs			spurs __attribute__((aligned (128)));

sys_ppu_thread_t     s_MultiStreamPuThread   = 0;
void *               s_pMultiStreamMemory    = NULL;

#define   CHANNEL   CELL_AUDIO_PORT_8CH
#define   BLOCK     CELL_AUDIO_BLOCK_8

#define EXIT_CODE               (0xbee)


/**********************************************************************************

audioInitCell

	Returns: audio port number returned from cellAudioPortOpen(..)

**********************************************************************************/
static int audioInitCell(void)
{
	int ret = 0;
	unsigned int portNum = -1;


	//	cellMSSystemConfigureSysUtil returns the following data:
	//	Bits 0-3:	Number of available output channels
	//	Bit    4:	Dolby On status
	//	unsigned int retSysUtil = cellMSSystemConfigureSysUtil();
	//	unsigned int numChans = (retSysUtil & 0x0000000F);
	//	unsigned int dolbyOn = (retSysUtil & 0x00000010) >> 4;
	//	printf("Number Of Channels: %u\n", numChans);
	//	printf("Dolby enabled: %u\n", dolbyOn);

	ret = cellAudioInit();
	if (ret !=CELL_OK)	return -1;

	audioParam.nChannel = CHANNEL;
	audioParam.nBlock   = BLOCK;

	ret = cellAudioPortOpen(&audioParam, &portNum);
	if (ret != CELL_OK)
	{
		cellAudioQuit();
		return -1;
	}

	ret = cellAudioGetPortConfig(portNum, &portConfig);
	if (ret != CELL_OK)
	{
		cellAudioQuit();
		return -1;
	}

	cellMSSystemConfigureLibAudio(&audioParam, &portConfig);

	return portNum;
}


int InitSPURS(void)
{
	int ret = -1;
	sys_ppu_thread_t	thread_id;
	int					ppu_thr_prio __attribute__((aligned (128)));  // information for the reverb

	sys_ppu_thread_get_id(&thread_id);
	ret = sys_ppu_thread_get_priority(thread_id, &ppu_thr_prio);
	if(ret == CELL_OK)
		ret = cellSpursInitialize(&spurs, SPURS_SPU_NUM, SPU_THREAD_GROUP_PRIORITY, ppu_thr_prio-1, 1);
	if(ret != CELL_OK) return -1;
	return 1;
}


/**********************************************************************************
InitialiseAudio

	This function sets up the audio system.

	Requires:	nStreams		Maximum number of streams to be active at any time
				nmaxSubs		Maximum number of sub channels to init in MultiStream
				_nPortNumber	Reference to int - Returns port number from CELL audio init
				_audioParam		Reference to CellAudioPortParam - Returns audio params from CELL audio init
				_portConfig		Reference to CellAudioPortConfig - Returns port configuration from CELL audio init

	Returns:	0	OK
				-1	Error

**********************************************************************************/
long InitialiseAudio( const long nStreams, const long nmaxSubs, int &_nPortNumber , CellAudioPortParam &_audioParam, CellAudioPortConfig &_portConfig)
{
	CellMSSystemConfig cfg;


	uint8_t prios[8] = {13, 13, 13, 13, 13, 13, 13, 13};
	cfg.channelCount=nStreams;
	cfg.subCount=nmaxSubs;
	cfg.dspPageCount=0;
	cfg.flags=CELL_MS_DISABLE_SPU_PRINTF_SERVER | CELL_MS_TD_ONLY_128;


    _nPortNumber = audioInitCell();
	if(_nPortNumber < 0)
	{
		return -1;
	}

	_audioParam = audioParam;
	_portConfig = portConfig;

	// Initialise SPURS MultiStream version
	int nMemoryNeeded = cellMSSystemGetNeededMemorySize(&cfg);
	s_pMultiStreamMemory = memalign(128, nMemoryNeeded);
	if(InitSPURS()==1)
		cellMSSystemInitSPURS(s_pMultiStreamMemory, &cfg, &spurs, &prios[0]);
	else return -1;

    return 0;
}

void ShutdownMultiStream()
{
	void* pMemory = cellMSSystemClose();
	assert(pMemory == s_pMultiStreamMemory);

	free( s_pMultiStreamMemory);

	s_pMultiStreamMemory = NULL;
	pMemory = NULL;
}

