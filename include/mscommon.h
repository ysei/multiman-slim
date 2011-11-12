#ifndef MS_COMMON_H
#define MS_COMMON_H

#include <sysutil/sysutil_sysparam.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <types.h>							
#include <sys/ppu_thread.h>
#include <sys/sys_time.h>
#include <sys/timer.h>
#include <sys/process.h>
#include <sys/spu_initialize.h>


#include <fcntl.h>
#include <cell/fs/cell_fs_errno.h>
#include <cell/fs/cell_fs_file_api.h>

#include <cell/mstream.h>

#include <assert.h>

#include <sys/paths.h>
#include <cell/audio.h>
#include <cell/gcm.h>

#include <sys/system_types.h>

#include <cell/spurs/control.h>
#include <cell/spurs/task.h>
#include <cell/spurs/event_flag.h>
#include <cell/sysmodule.h>

extern CellAudioPortParam   audioParam;
extern CellAudioPortConfig  portConfig;

long InitialiseAudio( const long nStreams, const long nmaxSubs, int &_nPortNumber, CellAudioPortParam &_audioParam, CellAudioPortConfig &_portConfig);
int InitSPURS(void);
int InitFile(const char *filename,long *addr, long *size);
void ShutdownMultiStream();

#define SUPPRESS_COMPILER_WARNING(x) (void)x
#endif
