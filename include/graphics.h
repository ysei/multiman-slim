#ifdef __cplusplus
extern "C" {
#endif

#include <cell/gcm.h>
#include <cell/dbgfont.h>

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define CONSOLE_WIDTH		(76+16)
#define CONSOLE_HEIGHT		(31)

#define DISPLAY_WIDTH  1920
#define DISPLAY_HEIGHT 1080

#define V_BUFFERS 2

typedef struct
{
	unsigned flags;
	char title[64];
	char title_id[64];
	char path[768];
	char entry[64];
	char content[8]; //PS2 PS3 AVCHD
	char details[128]; //load AVCHD details from details.txt
	int split;
	int plevel;
	int cover;
	u32 user; //user options flag
//	u8 selected;
}
t_menu_list;

extern u32 COL_PS3DISC;
extern u32 COL_PS3DISCSEL;

extern u32 COL_SEL;
extern u32 COL_PS3;
extern u32 COL_PS2;
extern u32 COL_DVD;
extern u32 COL_BDMV;
extern u32 COL_AVCHD;

extern u32 COL_LEGEND;

extern u32 COL_FMFILE;
extern u32 COL_FMDIR;
extern u32 COL_FMJPG;
extern u32 COL_FMMP3;
extern u32 COL_FMEXE;

extern u32 COL_HEXVIEW;
extern u32 COL_SPLIT;

extern float c_firmware;
extern uint32_t blockSize;
extern uint64_t freeSize;
extern uint64_t freeSpace;

extern char bluray_game[64];

extern float angle;
//extern int cover_mode;
//extern unsigned icon_raw[8192];

void put_vertex(float x, float y, float z, u32 color);
void put_texture_vertex(float x, float y, float z, float tx, float ty);

void draw_square(float x, float y, float w, float h, float z, u32 rgba);
void draw_square_angle(float x, float y, float w, float h, float z, u32 color, float angle);
void draw_triangle(float x, float y, float w, float h, float z, u32 rgba);
void utf8_to_ansi(char *utf8, char *ansi, int len);

int set_texture( u8 *buffer, u32 x_size, u32 y_size );

void display_img(int x, int y, int width, int height, int tx, int ty, float z, int Dtx, int Dty);
void display_img_nr(int x, int y, int width, int height, int tx, int ty, float z, int Dtx, int Dty);
void display_img_angle(int x, int y, int width, int height, int tx, int ty, float z, int Dtx, int Dty, float angle);
void display_img_persp(int x, int y, int width, int height, int tx, int ty, float z, int Dtx, int Dty, int keystoneL, int keystoneR);
void display_img_rotate(int x, int y, int width, int height, int tx, int ty, float z, int Dtx, int Dty, int step);

void draw_device_list(u32 flags, int cover_mode, int opaq, char *content);

int initConsole(void);
int termConsole(void);
int initFont(void);
int termFont(void);

int initDisplay(void);
int setRenderObject(void);

void setRenderColor(void);
void setRenderTarget(void);
void initShader(void);
void setDrawEnv(void);

void draw_list( t_menu_list *menu, int menu_size, int selected, int dir_mode, int display_mode, int cover_mode, int game_sel_last, int opaq);
void drawResultWindow( int result, int busy );

int DPrintf( const char *string, ... );

#include <types.h>
#include <sys/synchronization.h>

#ifdef __cplusplus
}
#endif