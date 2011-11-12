#ifndef _PFSM_H_
#define _PFSM_H_

/*
 * path to device must be in format '/pvd_usbXXX' where XXX represents volume ID
 * pvd = Playstation Virtual Device
 * example: '/pvd_usb000/DIR/FILE'
 */

#define PFSM_DEVPATH				"/pvd_usb"
#define PFSM_DEVNAME_SIZE			15			/* "PFSM_VVVV:PPPP" */

#define PFS_FILE_INVALID			NULL
#define PFS_FIND_INVALID			NULL

#define PFS_FILE_SEEK_BEGIN			0
#define PFS_FILE_SEEK_CURRENT		1
#define PFS_FILE_SEEK_END			2

#define PFS_FIND_DIR				0x10
#define PFS_FIND_META				0x80000000


typedef struct _PFSM_DEVICE {
	char		Name[PFSM_DEVNAME_SIZE];
} PFSM_DEVICE;


#ifndef PFS_INTERNALS
	typedef void * PFS_HFILE;
	typedef void * PFS_HFIND;
	typedef void * PFS_HSTREAM;
#endif

typedef struct _PFS_INFO_DATA {
	uint32_t	FileAttributes;
	uint64_t	CreationTime;
	uint64_t	LastAccessTime;
	uint64_t	LastWriteTime;
	uint64_t	FileSize;
	uint32_t	NumberOfLinks;
	uint64_t	FileIndex;
} PFS_INFO_DATA;

typedef struct _PFS_FIND_DATA {
	uint32_t	FileAttributes;
	uint64_t	CreationTime;
	uint64_t	LastAccessTime;
	uint64_t	LastWriteTime;
	uint64_t	FileSize;
	char *		FileName;
} PFS_FIND_DATA;

typedef struct _PFS_STREAM_DATA {
	uint64_t	StreamSize;
	char *		StreamName;
} PFS_STREAM_DATA;


#ifdef __cplusplus
extern "C" {
#endif

int32_t PfsmInit(int32_t max_volumes);
void PfsmUninit(void);

int32_t PfsmDevAdd(uint16_t vid, uint16_t pid, PFSM_DEVICE *dev);
int32_t PfsmDevDel(PFSM_DEVICE *dev);

int32_t PfsmVolStat(int32_t vol_id);

PFS_HFILE PfsFileOpen(const char *path);
int32_t PfsFileRead(PFS_HFILE file, void *buffer, uint32_t size, uint32_t *read);
int32_t PfsFileSeek(PFS_HFILE file, int64_t distance, uint32_t move_method);
int32_t PfsFileGetSize(const char *path, uint64_t *size);
int32_t PfsFileGetSizeFromHandle(PFS_HFILE file, uint64_t *size);
int32_t PfsFileGetInfo(PFS_HFILE file, PFS_INFO_DATA *info);
void PfsFileClose(PFS_HFILE file);

PFS_HFIND PfsFileFindFirst(const char *path, PFS_FIND_DATA *find_data);
int32_t PfsFileFindNext(PFS_HFIND find, PFS_FIND_DATA *find_data);
void PfsFileFindClose(PFS_HFIND find);

PFS_HSTREAM PfsStreamFindFirst(const char *path, PFS_STREAM_DATA *stream_data);
int32_t PfsStreamFindNext(PFS_HSTREAM stream, PFS_STREAM_DATA *stream_data);
void PfsStreamClose(PFS_HSTREAM stream);

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

#endif

