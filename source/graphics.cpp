#include <cell/error.h>
#include <cell/sysmodule.h>
#include <sysutil/sysutil_sysparam.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <sys/timer.h>
#include <cell/cell_fs.h>

#include <cell/gcm.h>

#include <cell/control_console.h>

#include <sysutil/sysutil_sysparam.h>
#include <netex/libnetctl.h>

#include "graphics.h"
#include "language.h"

#define ROUNDUP(x, a) (((x)+((a)-1))&(~((a)-1)))

CellDbgFontConsoleId consoleID = CELL_DBGFONT_STDOUT_ID;

extern u32 frame_index;
extern int V_WIDTH;
extern int V_HEIGHT;
extern float overscan;
extern int cover_mode;
extern bool key_repeat;
extern int xmb_slide_y;
extern bool is_remoteplay;
extern u8 video_mode;

extern bool th_device_list;
extern bool th_device_separator;
extern u16 th_device_separator_y;
extern bool th_legend;
extern u16 th_legend_y;
extern bool th_drive_icon;
extern u16 th_drive_icon_x;
extern u16 th_drive_icon_y;
extern bool use_depth;
extern u8 hide_bd;
typedef struct
{
	float x, y, z;
	u32 color;
} vtx_color;

typedef struct {
	float x, y, z;
	float tx, ty;
} vtx_texture;

u32 screen_width;
u32 screen_height;

u32 color_pitch;
u32 depth_pitch;
u32 color_offset[V_BUFFERS];
u32 depth_offset;

extern u32 _binary_vpshader_vpo_start;
extern u32 _binary_vpshader_vpo_end;
extern u32 _binary_fpshader_fpo_start;
extern u32 _binary_fpshader_fpo_end;
extern u32 video_buffer;

static unsigned char *vertex_program_ptr =
(unsigned char *)&_binary_vpshader_vpo_start;
static unsigned char *fragment_program_ptr =
(unsigned char *)&_binary_fpshader_fpo_start;

static CGprogram vertex_program;
static CGprogram fragment_program;

extern struct _CGprogram _binary_vpshader2_vpo_start;
extern struct _CGprogram _binary_fpshader2_fpo_start;

extern void *color_base_addr;

extern void put_label(uint8_t *buffer, uint32_t width, uint32_t height, char *str1p, char *str2p, char *str3p, uint32_t color);
extern void put_texture( uint8_t *buffer_to, uint8_t *buffer_from, uint32_t width, uint32_t height, int from_width, int x, int y, int border, uint32_t border_color);
extern void print_label(float x, float y, float scale, uint32_t color, char *str1p);
extern void print_label_ex(float x, float y, float scale, uint32_t color, char *str1p, float weight, float slant, int font, float hscale, float vscale, int centered);
extern void flush_ttf(uint8_t *buffer, uint32_t _V_WIDTH, uint32_t _V_HEIGHT);
extern void draw_box( uint8_t *buffer_to, uint32_t width, uint32_t height, int x, int y, uint32_t border_color);
extern void put_texture_Galpha( uint8_t *buffer_to, uint32_t Twidth, uint32_t Theight, uint8_t *buffer_from, uint32_t _width, uint32_t _height, int from_width, int x, int y, int border, uint32_t border_color);
extern void put_texture_with_alpha( uint8_t *buffer_to, uint8_t *buffer_from, uint32_t _width, uint32_t _height, int from_width, int x, int y, int border, uint32_t border_color);
extern int max_ttf_label;
extern u8 *text_bmp;
extern u8 *text_USB;
extern u8 *text_HDD;
extern u8 *text_BLU_1;
extern u8 *text_legend;
extern u8 *text_FONT;
extern int legend_y, legend_h;
extern int last_selected;
extern int b_box_opaq;
extern int b_box_step;
extern int draw_legend;

static void *vertex_program_ucode;
static void *fragment_program_ucode;
static u32 fragment_offset;

static void *text_vertex_prg_ucode;
static void *text_fragment_prg_ucode;
static u32 text_fragment_offset;

static u32 vertex_offset[2];
static u32 color_index;
static u32 position_index;

static vtx_color *vertex_color;
extern int vert_indx;
extern int vert_texture_indx;

static vtx_texture *vertex_text;
static u32 vertex_text_offset;

static u32 text_obj_coord_indx;
static u32 text_tex_coord_indx;

static CGresource tindex;
static CGprogram vertex_prg;
static CGprogram fragment_prg;
static CellGcmTexture text_param;

static u32 local_heap = 0;

static void *localAlloc(const u32 size)
{
	u32 align = (size + 1023) & (~1023);
	u32 base = local_heap;

	local_heap += align;
	return (void*)base;
}

static void *localAllocAlign(const u32 alignment, const u32 size)
{
	local_heap = (local_heap + alignment-1) & (~(alignment-1));

	return (void*)localAlloc(size);
}


void setRenderTarget(void)
{
	CellGcmSurface surface;

	surface.colorFormat 	 = CELL_GCM_SURFACE_A8R8G8B8;
	surface.colorTarget		 = CELL_GCM_SURFACE_TARGET_0;
	surface.colorLocation[0] = CELL_GCM_LOCATION_LOCAL;
	surface.colorOffset[0] 	 = color_offset[frame_index];
	surface.colorPitch[0] 	 = color_pitch;

	surface.colorLocation[1] = CELL_GCM_LOCATION_LOCAL;
	surface.colorLocation[2] = CELL_GCM_LOCATION_LOCAL;
	surface.colorLocation[3] = CELL_GCM_LOCATION_LOCAL;

	surface.colorOffset[1] 	 = 0;
	surface.colorOffset[2] 	 = 0;
	surface.colorOffset[3] 	 = 0;
	surface.colorPitch[1]	 = 64;
	surface.colorPitch[2]	 = 64;
	surface.colorPitch[3]	 = 64;

	surface.depthFormat 	 = CELL_GCM_SURFACE_Z24S8;
	surface.depthLocation	 = CELL_GCM_LOCATION_LOCAL;
	surface.depthOffset	     = depth_offset;
	surface.depthPitch 	     = depth_pitch;

	surface.type		     = CELL_GCM_SURFACE_PITCH;
	surface.antialias	     = CELL_GCM_SURFACE_CENTER_1;//CELL_GCM_SURFACE_SQUARE_ROTATED_4;//

	surface.width 		     = screen_width;
	surface.height 	 	     = screen_height;
	surface.x 		         = 0;
	surface.y 		         = 0;

	cellGcmSetSurface(gCellGcmCurrentContext, &surface);
}


void initShader(void)
{
	vertex_program   = (CGprogram)vertex_program_ptr;
	fragment_program = (CGprogram)fragment_program_ptr;

	cellGcmCgInitProgram(vertex_program);
	cellGcmCgInitProgram(fragment_program);

	u32 ucode_size;
	void *ucode;
	cellGcmCgGetUCode(fragment_program, &ucode, &ucode_size);

	void *ret = localAllocAlign(64, ucode_size);
	fragment_program_ucode = ret;
	memcpy(fragment_program_ucode, ucode, ucode_size);

	cellGcmCgGetUCode(vertex_program, &ucode, &ucode_size);
	vertex_program_ucode = ucode;
}


int text_create();

int initDisplay(void)
{
	int ret, i;
	u32 color_size, depth_size, color_depth= 4, z_depth= 4;

	color_base_addr=NULL;
	void *depth_base_addr, *color_addr[V_BUFFERS];

	CellVideoOutResolution resolution;
	CellVideoOutState videoState;

	ret = cellVideoOutGetState(CELL_VIDEO_OUT_PRIMARY, 0, &videoState);
	if (ret != CELL_OK)	return -1;

	is_remoteplay = ( videoState.displayMode.conversion == CELL_VIDEO_OUT_DISPLAY_CONVERSION_TO_REMOTEPLAY );

	cellVideoOutGetResolution(videoState.displayMode.resolutionId, &resolution);

	screen_width = resolution.width;
	screen_height = resolution.height;
	V_WIDTH  = screen_width;
	V_HEIGHT = screen_height;

	color_pitch = screen_width*color_depth;
	depth_pitch = screen_width*z_depth;
	color_size  = color_pitch*screen_height;
	depth_size  = depth_pitch*screen_height;

	CellVideoOutConfiguration videocfg;
	memset(&videocfg, 0, sizeof(CellVideoOutConfiguration));
	videocfg.resolutionId = videoState.displayMode.resolutionId;
	videocfg.format = CELL_VIDEO_OUT_BUFFER_COLOR_FORMAT_X8R8G8B8;
	videocfg.pitch = color_pitch;
	videocfg.aspect = CELL_VIDEO_OUT_ASPECT_AUTO;

	ret = cellVideoOutConfigure(CELL_VIDEO_OUT_PRIMARY, &videocfg, NULL, 0);
	if (ret != CELL_OK) return -1;

if(cover_mode==8 || !video_mode)
	cellGcmSetFlipMode(CELL_GCM_DISPLAY_VSYNC);
else
	cellGcmSetFlipMode(CELL_GCM_DISPLAY_HSYNC);
//	cellGcmSetFlipMode(CELL_GCM_DISPLAY_HSYNC_WITH_NOISE);

	CellGcmConfig config;
	cellGcmGetConfiguration(&config);

	local_heap = (u32) config.localAddress;

	color_base_addr = localAllocAlign(16, V_BUFFERS * color_size);
	video_buffer=color_size;

	for (i = 0; i < V_BUFFERS; i++)
		{
		color_addr[i]= (void *)((u32)color_base_addr+ (i*color_size));
		ret = cellGcmAddressToOffset(color_addr[i], &color_offset[i]);
		if(ret != CELL_OK) return -1;
		ret = cellGcmSetDisplayBuffer(i, color_offset[i], color_pitch, screen_width, screen_height);
		if(ret != CELL_OK) return -1;
		}

	depth_base_addr = localAllocAlign(16, depth_size);
	ret = cellGcmAddressToOffset(depth_base_addr, &depth_offset);
	if(ret != CELL_OK) return -1;

	cellGcmSetZcull(0, depth_offset,
					ROUNDUP(screen_width, 64), ROUNDUP(screen_height, 64), 0,
					CELL_GCM_ZCULL_Z24S8, CELL_GCM_SURFACE_CENTER_1,
					CELL_GCM_ZCULL_LESS, CELL_GCM_ZCULL_LONES,
					CELL_GCM_ZCULL_LESS, 0x80, 0xff);

	text_create();

	return 0;
}

void setDrawEnv(void)
{
	u16 x,y,w,h;
	float min, max;
	float scale[4],offset[4];

	w = (u16)((float)screen_width*(1.f-overscan/2.f));
	h = (u16)((float)screen_height*(1.f-overscan/2.f));

	x = (u16)((float)screen_width*overscan/2.f);
	y = 0;

	min = 0.0f;
	max = 1.0f;
	scale[0] = w * 0.5f * (1.f-overscan/2.f);
	scale[1] = h * -0.5f * (1.f-overscan/2.f);
	scale[2] = (max - min) * 0.5f;
	scale[3] = 0.0f;
	offset[0] = x + scale[0];
	offset[1] = h - y + scale[1];
	offset[2] = (max + min) * 0.5f;
	offset[3] = 0.0f;

	cellGcmSetColorMask(gCellGcmCurrentContext, CELL_GCM_COLOR_MASK_B | CELL_GCM_COLOR_MASK_G | CELL_GCM_COLOR_MASK_R | CELL_GCM_COLOR_MASK_A);
	cellGcmSetColorMaskMrt(gCellGcmCurrentContext, 0); //CELL_GCM_COLOR_MASK_B | CELL_GCM_COLOR_MASK_G | CELL_GCM_COLOR_MASK_R | CELL_GCM_COLOR_MASK_A

	cellGcmSetViewport(gCellGcmCurrentContext, x, y, w, h, min, max, scale, offset);
	cellGcmSetClearColor(gCellGcmCurrentContext, 0xff000000);

	cellGcmSetDepthTestEnable(gCellGcmCurrentContext, CELL_GCM_TRUE);
	cellGcmSetDepthFunc(gCellGcmCurrentContext, CELL_GCM_LESS);

	cellGcmSetBlendFunc(gCellGcmCurrentContext,CELL_GCM_SRC_ALPHA, CELL_GCM_ONE_MINUS_SRC_ALPHA,CELL_GCM_SRC_ALPHA, CELL_GCM_ONE_MINUS_SRC_ALPHA);
	cellGcmSetBlendEquation(gCellGcmCurrentContext,CELL_GCM_FUNC_ADD, CELL_GCM_FUNC_ADD);
	cellGcmSetBlendEnable(gCellGcmCurrentContext,CELL_GCM_TRUE);

	cellGcmSetShadeMode(gCellGcmCurrentContext, CELL_GCM_SMOOTH);

	cellGcmSetAlphaFunc(gCellGcmCurrentContext, CELL_GCM_GREATER, 0x01);

	cellGcmSetAntiAliasingControl(gCellGcmCurrentContext, CELL_GCM_TRUE, CELL_GCM_FALSE, CELL_GCM_FALSE, 0xffff);

}

void setRenderColor(void)
{
	cellGcmSetVertexProgram(gCellGcmCurrentContext, vertex_program, vertex_program_ucode);
	cellGcmSetVertexDataArray(gCellGcmCurrentContext, position_index, 0, sizeof(vtx_color), 3, CELL_GCM_VERTEX_F, CELL_GCM_LOCATION_LOCAL, (u32)vertex_offset[0]);
	cellGcmSetVertexDataArray(gCellGcmCurrentContext, color_index, 0, sizeof(vtx_color), 4,	CELL_GCM_VERTEX_UB, CELL_GCM_LOCATION_LOCAL, (u32)vertex_offset[1]);

	cellGcmSetFragmentProgram(gCellGcmCurrentContext, fragment_program, fragment_offset);

}

int setRenderObject(void)
{

	vertex_color = (vtx_color*) localAllocAlign(131072/*128*1024*/, 1536*sizeof(vtx_color)); // 384 quad polygons/textures (384x4 vertices)

	CGparameter position = cellGcmCgGetNamedParameter(vertex_program, "position");
	CGparameter color = cellGcmCgGetNamedParameter(vertex_program, "color");

	position_index = cellGcmCgGetParameterResource(vertex_program, position) - CG_ATTR0;
	color_index = cellGcmCgGetParameterResource(vertex_program, color) - CG_ATTR0;


	if(cellGcmAddressToOffset(fragment_program_ucode, &fragment_offset) != CELL_OK) return -1;

	if (cellGcmAddressToOffset(&vertex_color->x, &vertex_offset[0]) != CELL_OK)	return -1;
	if (cellGcmAddressToOffset(&vertex_color->color, &vertex_offset[1]) != CELL_OK)	return -1;

	return 0;
}

int initFont()
{
	CellDbgFontConfigGcm config;

	int size = CELL_DBGFONT_FRAGMENT_SIZE + CELL_DBGFONT_VERTEX_SIZE * CONSOLE_WIDTH * CONSOLE_HEIGHT + CELL_DBGFONT_TEXTURE_SIZE;

	int ret = 0;

	void*localmem = localAllocAlign(128, size);
	if( localmem == NULL ) return -1;

	memset(&config, 0, sizeof(CellDbgFontConfigGcm));

	config.localBufAddr = (sys_addr_t)localmem;
	config.localBufSize = size;
	config.mainBufAddr = NULL;
	config.mainBufSize  = 0;
	config.option = CELL_DBGFONT_VERTEX_LOCAL;
	config.option |= CELL_DBGFONT_TEXTURE_LOCAL;
	config.option |= CELL_DBGFONT_SYNC_ON;
	config.option |= CELL_DBGFONT_MAGFILTER_LINEAR | CELL_DBGFONT_MINFILTER_LINEAR;

	ret = cellDbgFontInitGcm(&config);
	if(ret < 0) return ret;

	return 0;
}

int initConsole()
{
	CellDbgFontConsoleConfig config;
	config.posLeft     = 0.086f;
	config.posTop      = 0.16f;
	config.cnsWidth    = CONSOLE_WIDTH;
	config.cnsHeight   = CONSOLE_HEIGHT;
	config.scale       = 0.72f;
	config.color       = 0xffA0A0A0;
	consoleID = cellDbgFontConsoleOpen(&config);

	if (consoleID < 0) return -1;

return 0;
}

/*void DbgEnable()
{
	cellDbgFontConsoleEnable(consoleID);
}

void DbgDisable()
{
	cellDbgFontConsoleDisable(consoleID);
}
*/

int termConsole()
{
	int ret;
	ret = cellDbgFontConsoleClose(consoleID);

	if(ret) return -1;

	consoleID = CELL_DBGFONT_STDOUT_ID;

	return ret;
}

int termFont()
{
	int ret;

	ret = cellDbgFontExitGcm();

	if(ret) return -1;

	return ret;
}


int DPrintf( const char *string, ... )
{
	int ret=0;
	va_list argp;

	va_start(argp, string);
	if(consoleID != CELL_DBGFONT_STDOUT_ID)
		ret = cellDbgFontConsoleVprintf(consoleID, string, argp);
	va_end(argp);

	return ret;
}

void utf8_to_ansi(char *utf8, char *ansi, int len)
{
u8 *ch= (u8 *) utf8;
u8 c;

	while(*ch!=0 && len>0){

	// 3, 4 bytes utf-8 code
	if(((*ch & 0xF1)==0xF0 || (*ch & 0xF0)==0xe0) && (*(ch+1) & 0xc0)==0x80){

	*ansi++=' '; // ignore
	len--;
	ch+=2+1*((*ch & 0xF1)==0xF0);

	}
	else
	// 2 bytes utf-8 code
	if((*ch & 0xE0)==0xc0 && (*(ch+1) & 0xc0)==0x80){

	c= (((*ch & 3)<<6) | (*(ch+1) & 63));

	if(c>=0xC0 && c<=0xC5) c='A';
	else if(c==0xc7) c='C';
	else if(c>=0xc8 && c<=0xcb) c='E';
	else if(c>=0xcc && c<=0xcf) c='I';
	else if(c==0xd1) c='N';
	else if(c>=0xd2 && c<=0xd6) c='O';
	else if(c>=0xd9 && c<=0xdc) c='U';
	else if(c==0xdd) c='Y';
	else if(c>=0xe0 && c<=0xe5) c='a';
	else if(c==0xe7) c='c';
	else if(c>=0xe8 && c<=0xeb) c='e';
	else if(c>=0xec && c<=0xef) c='i';
	else if(c==0xf1) c='n';
	else if(c>=0xf2 && c<=0xf6) c='o';
	else if(c>=0xf9 && c<=0xfc) c='u';
	else if(c==0xfd || c==0xff) c='y';
	else if(c>127) c=*(++ch+1); //' ';

	if(c=='%') c='#';

	*ansi++=c;
	len--;
	ch++;

	}
	else {

	if(*ch<32) *ch=32;
	if(*ch=='%') *ch='#';
	*ansi++=*ch;

	len--;

	}

	ch++;
	}
	while(len>0) {
	*ansi++=0;
	len--;
	}
}

void draw_text_stroke(float x, float y, float size, u32 color, const char *str)
{
//	print_label(x, y, size, color, (char*)str); return;
/*		cellDbgFontPrintf( x-.001f, y+.001f, size, 0xc0000000, str);
		cellDbgFontPrintf( x-.001f, y-.001f, size, 0xc0000000, str);
		cellDbgFontPrintf( x+.001f, y+.001f, size, 0xc0000000, str);
		cellDbgFontPrintf( x+.001f, y-.001f, size, 0xc0000000, str);
		cellDbgFontPrintf( x+.0015f, y+.0015f, size, 0x90202020, str);
*/
		cellDbgFontPrintf( x-.0015f, y-0.0015, size+0.0030f, 0xE0101010, str);
		cellDbgFontPrintf( x-.0015f, y+0.0015, size+0.0030f, 0xD0101010, str);
		cellDbgFontPrintf( x, y, size, color, str);
}


void draw_device_list(u32 flags, int _cover_mode, int opaq, char *content)
{
	(void)_cover_mode;
	if(cover_mode==5 || opaq<0x30 || cover_mode==8) return;

//	float y = 0.15f + 0.05f*15.0f;
	float y = (float)th_device_separator_y/1080.0f + 0.015;
	char str[256];

	union CellNetCtlInfo net_info;

	int n,ok=0;
	float len;
	float x=0.08;

	char sizer[255];
	char path[255];

	if( (cover_mode<3 || cover_mode==6 || cover_mode==7 || cover_mode==4) ) goto just_legend;

	for(n=0;n<16;n++)
		{
		if(th_device_list==0) break;
		if(n==11 && hide_bd==1) continue;
		str[0]=0;
		ok=0;
		if((flags>>n) & 1) ok=1;

		if(ok || n==12)

			{
			switch(n)
				{
				case 0:
					sprintf(str, "%s", " HDD ");
					sprintf(path, "/dev_hdd0/");
					break;

				case 7:
					sprintf(str, "USB#%i", n-5);
					sprintf(path, "/dev_usb00%d/", n-1);
					break;

				case 11:
					sprintf(str, "%s", "Game disc");
					if(strstr(content,"AVCHD")!=NULL) sprintf(str, "%s", "AVCHD disc");
					else if(strstr(content,"BDMV")!=NULL) sprintf(str, "%s", "Movie disc");
					else if(strstr(content,"PS2")!=NULL) sprintf(str, "%s", "PS2 disc");
					else if(strstr(content,"DVD")!=NULL) sprintf(str, "%s", "DVD disc");
					sprintf(path, "/dev_bdvd/");
					break;

				case 12:
					sprintf(str, "PS3 IP");
					sprintf(path, "/ftp_service");
					break;

				case 13:
					sprintf(str, " USB ");
					sprintf(path, "/pvd_usb000");
					break;

				case 14:
					sprintf(str, "SDHC");
					sprintf(path, "/dev_sd");
					break;

				case 15:
					sprintf(str, " MS ");
					sprintf(path, "/dev_ms");
					break;

				default:
					sprintf(str, "USB#%i", n);
					sprintf(path, "/dev_usb00%d/", n-1);
					break;
				}

			len=0.025f*(float)(strlen(str));

			if(n!=12){
				draw_square((x-0.5f)*2.0f-0.02f, (0.5f-y+0.01)*2.0f, len+0.04f, 0.095f, -0.9f, ((flags>>(n+16)) & 1) ? 0x0080ffc0 : 0x101010c0);
				if(n!=11 && n!=13)
				{
					if(str[0])
					{
						cellFsGetFreeSize(path, &blockSize, &freeSize);
						freeSpace = (uint64_t)(blockSize * freeSize);
						//last_refreshD=time(NULL);
						//dev_free_space[n].freespace=freeSpace;
					}
					else
					{
						//freeSpace = dev_free_space[n].freespace;
						freeSpace = 0;
					}
					sprintf(sizer, "%.2fGB", (double) (freeSpace / 1073741824.00f));
					draw_text_stroke( (x+(len+0.024f)/4.0f)-(float)((0.009f*(float)(strlen(sizer)))/2), y+0.045f, 0.70f, COL_LEGEND, sizer);
//					draw_text_stroke( x+overscan, y+0.045-overscan, 0.70f-overscan,((flags>>(n+16)) & 1) ? 0xd0c0c0c0 : 0xc0c0c0a0, sizer);
				}

				if(n==13) {sprintf(sizer, "PFS Drive"); draw_text_stroke( (x+(len+0.024f)/4.0f)-(float)((0.009f*(float)(strlen(sizer)))/2), y+0.045f, 0.70f, COL_LEGEND, sizer);}
				draw_text_stroke( x, y-0.005, 1.0f, ((flags>>(n+16)) & 1) ? 0xd0c0c0c0 : COL_LEGEND, str);

			}
			else
			{
				if(cellNetCtlGetInfo(16, &net_info) < 0) str[0]=0;
				else
				{
					sprintf(sizer, "%s", net_info.ip_address);
					draw_square((x-0.5f)*2.0f-0.02f, (0.5f-y+0.01)*2.0f, len+0.04f, 0.095f, -0.9f, 0x101060c0);
					draw_text_stroke( (x+(len+0.02f)/4.0f)-(float)((0.009f*(float)(strlen(sizer)))/2), y+0.045, 0.65f,((flags>>(n+16)) & 1) ? 0xc0c0c0ff : COL_LEGEND, sizer);
					draw_text_stroke( x, y-0.005, 1.0f, ((flags>>(n+16)) & 1) ? 0xc0c0c0ff : COL_LEGEND, str);
				}
			}

			len=0.025f*(float)(strlen(str));
			x+=len;

			}
		}



just_legend:
		if( (cover_mode==4 && th_legend==1) )
		{
//				float legend_font=0.9f;
//				if(overscan>0.05) legend_font=(float)(legend_font-(legend_font*overscan));
//				draw_text_stroke( 0.08f+(overscan/4.0f), (((float)th_legend_y/1080.f)+0.02f)-overscan,   legend_font, COL_LEGEND, "X - Load   [] - Game settings  [R1] - Next mode\nO - Exit   /\\ - System menu    [L1] - Prev mode" );
				set_texture( text_legend, 1665, 96);
				display_img(127, th_legend_y+15, 1665, 96, 1665, 96, -0.5f, 1665, 96);
		}




		if( (cover_mode<3 || cover_mode==6 || cover_mode==7) && draw_legend )
		{
			if(th_legend==1)
			{
				if( cover_mode==1 || cover_mode==6 || cover_mode==7)
						put_texture_with_alpha( text_bmp, text_legend, 1665, 96, 1665, 127, th_legend_y, 0, 0);
				else 	put_texture_with_alpha( text_bmp, text_legend, 1665, 96, 1665, 127, th_legend_y+15, 0, 0);
			}
		if(th_device_separator==1)	draw_box( text_bmp, 1920, 2, 0, th_device_separator_y, 0x80808080);

			u32 info_color=0xffc0c0c0;
			u32 info_color2=0xff808080;

		if(th_device_list==1)
			{

			x=0.1f;
			max_ttf_label=0;
			for(n=0;n<16;n++)
				{
				str[0]=0;
				ok=0;
				if(n==11 && hide_bd==1) continue;
				if((flags>>n) & 1) ok=1;

				if(ok || n==12)

					{
					switch(n)
						{
						case 0:
							sprintf(str, "%s", " HDD ");
							sprintf(path, "/dev_hdd0/");
							break;

						case 7:
							sprintf(str, "USB#%i", n-5);
							sprintf(path, "/dev_usb00%d/", n-1);
							break;

						case 11:
							sprintf(str, "%s", "Game disc");
							if(strstr(content,"AVCHD")!=NULL) sprintf(str, "%s", "AVCHD disc");
							else if(strstr(content,"BDMV")!=NULL) sprintf(str, "%s", "Movie disc");
							else if(strstr(content,"PS2")!=NULL) sprintf(str, "%s", "PS2 disc");
							else if(strstr(content,"DVD")!=NULL) sprintf(str, "%s", "DVD disc");
							sprintf(path, "/dev_bdvd/");
							break;

						case 12:
							sprintf(str, "PS3 IP");
							sprintf(path, "/ftp_service");
							break;

						case 13:
							sprintf(str, " USB ");
							sprintf(path, "/pvd_usb000");
							break;

						case 14:
							sprintf(str, "SDHC");
							sprintf(path, "/dev_sd");
							break;

						case 15:
							sprintf(str, " MS ");
							sprintf(path, "/dev_ms");
							break;

						default:
							sprintf(str, "USB#%i", n);
							sprintf(path, "/dev_usb00%d/", n-1);
							break;
						}

					len=0.025f*(float)(strlen(str));
					sizer[0]=0;
					if(n!=12){
						if(n!=11 && n!=13)
						{
							if(str[0])
							{
								cellFsGetFreeSize(path, &blockSize, &freeSize);
								freeSpace = (uint64_t)(blockSize * freeSize);
							}
							else
							{
								freeSpace = 0;
							}
							sprintf(sizer, "%.2fGB", (double) (freeSpace / 1073741824.00f));
							//draw_text_stroke( (x+overscan+(len+0.024f)/4.0f)-(float)((0.009f*(float)(strlen(sizer)))/2), y+0.045f-overscan, 0.70f-overscan, COL_LEGEND, sizer);

						}
						if(n==13) {
							sprintf(sizer, "PFS Drive"); //draw_text_stroke( (x+overscan+(len+0.024f)/4.0f)-(float)((0.009f*(float)(strlen(sizer)))/2), y+0.045f-overscan, 0.70f-overscan, COL_LEGEND, sizer);
							}
						//draw_text_stroke( x+overscan, y-0.005-overscan, 1.0f-overscan, ((flags>>(n+16)) & 1) ? 0xd0c0c0c0 : COL_LEGEND, str);
						print_label_ex( x, (((float)(th_device_separator_y+15))/1080.f), 1.5f, info_color, str, 1.04f, 0.00f, 15, 0.6f, 0.6f, 1);
						print_label_ex( x, (((float)(th_device_separator_y+44))/1080.f), 1.5f, info_color2, sizer, 1.00f, 0.00f, 15, 0.5f, 0.5f, 1);
					}
					else
					{
						if(cellNetCtlGetInfo(16, &net_info) < 0) str[0]=0;
						else
						{
							sprintf(sizer, "%s", net_info.ip_address);
							//draw_square((x+overscan-0.5f)*2.0f-0.02f, (0.5f-y+0.01+overscan)*2.0f, len+0.04f, 0.095f, -0.9f, 0x101060c0);
							//draw_text_stroke( (x+overscan+(len+0.02f)/4.0f)-(float)((0.009f*(float)(strlen(sizer)))/2), y+0.045-overscan, 0.65f-overscan,((flags>>(n+16)) & 1) ? 0xc0c0c0ff : COL_LEGEND, sizer);
							//draw_text_stroke( x+overscan, y-0.005-overscan, 1.0f-overscan, ((flags>>(n+16)) & 1) ? 0xc0c0c0ff : COL_LEGEND, str);
							print_label_ex( x, (((float)(th_device_separator_y+15))/1080.f), 1.5f, info_color, str, 1.04f, 0.00f, 15, 0.6f, 0.6f, 1);
							print_label_ex( x, (((float)(th_device_separator_y+44))/1080.f), 1.5f, info_color2, sizer, 1.00f, 0.00f, 15, 0.5f, 0.5f, 1);
						}
					}

					len=0.11f;
					x+=len;

					}
				}
				flush_ttf(text_bmp, 1920, 1080);

			}
			draw_legend=0;
		}

}



void draw_list( t_menu_list *menu, int menu_size, int selected, int dir_mode, int display_mode, int _cover_mode, int game_sel_last, int opaq )
{
	(void)_cover_mode;
	if(cover_mode==5 || cover_mode==0 || cover_mode==8) return;

	float y = 0.1f;
	int i = 0, c=0;
	char str[256];
	char ansi[256];
	float len=0;
	u32 color, color2;
	game_sel_last+=0;
	int flagb= selected & 0x10000;
	int max_entries=14;

	if(cover_mode==1) max_entries=8;
	if(cover_mode==7) max_entries=32;

	selected&= 0xffff;

	if(cover_mode==4 || cover_mode==1 || cover_mode==6 || cover_mode==7)
	{
		int grey=(menu[selected].title[0]=='_' || menu[selected].split);
		if(grey && menu[selected].title[0]=='_')
			{utf8_to_ansi(menu[selected].title+1, ansi, 60);}
		else
			{utf8_to_ansi(menu[selected].title, ansi, 60);}
		ansi[60]=0;
		color= (flagb && selected==0)? COL_PS3DISC : ((grey==0) ?  COL_PS3 : COL_SPLIT);
		if(strstr(menu[selected].content,"AVCHD")!=NULL) color=COL_AVCHD;
		if(strstr(menu[selected].content,"BDMV")!=NULL) color=COL_BDMV;
		if(strstr(menu[selected].content,"PS2")!=NULL) color=COL_PS2;
		if(strstr(menu[selected].content,"DVD")!=NULL) color=COL_DVD;

		if(grey)
			sprintf(str, "%s (Split)", ansi);
		else
			sprintf(str, "%s", ansi);
		len=(0.023f*(float)(strlen(str)))/2;
		sprintf(str, (const char*)(STR_POP_1OF1)+4, selected+1, menu_size); //"%i of %i"

		if(cover_mode==4)
//			draw_text_stroke( 0.5f-(float)(len/2), 0.71f, 0.88f, color, str);
//		else
		{
//			draw_text_stroke( 0.5f-(float)(len/2), 0.6f, 0.88f, color, str);

			/*if(selected!=last_selected)
			{
				last_selected=selected;
				int tmp_legend_y=legend_y;
				legend_y=0;
				memset(text_bmp, 0, 737280);
				if(dir_mode==1)
					put_label(text_bmp, 1920, 1080, (char*)menu[selected].title, (char*)str, (char*)menu[selected].path, color);
				else
					put_label(text_bmp, 1920, 1080, (char*)menu[selected].title, (char*)str, (char*)menu[selected].title_id, color);
				legend_y=tmp_legend_y;
			} */
			set_texture( text_bmp, 1920, legend_h);
			display_img(0, 728, 1920, legend_h, 1920, legend_h, -0.5f, 1920, legend_h);

		}


//		len=(0.023f*(float)(strlen(str)))/2;

		// bottom device icon
		if(cover_mode==1 || cover_mode==6 || cover_mode==7)
		{
//			draw_text_stroke( 0.5f-(float)(len/2), 0.75f, 0.88f, color, str);
			if(selected!=last_selected)
			{
				last_selected=selected;
				memcpy(text_bmp + (1920*4*legend_y), text_FONT, (1920*4*legend_h));
				if(dir_mode==1)
					put_label(text_bmp, 1920, 1080, (char*)menu[selected].title, (char*)str, (char*)menu[selected].path, color);
				else
					put_label(text_bmp, 1920, 1080, (char*)menu[selected].title, (char*)str, (char*)menu[selected].title_id, color);

				if(th_drive_icon==1)
				{
					if(strstr(menu[selected].path, "/dev_usb")!=NULL || strstr(menu[selected].path, "/pvd_usb")!=NULL)
					{
						put_texture( text_bmp, text_USB, 96, 96, 320, th_drive_icon_x, th_drive_icon_y, 0, 0xff800080);
					}
					else if(strstr(menu[selected].path, "/dev_hdd")!=NULL)
					{
						put_texture( text_bmp, text_HDD, 96, 96, 320, th_drive_icon_x, th_drive_icon_y, 0, 0xff800080);
					}
					else if(strstr(menu[selected].path, "/dev_bdvd")!=NULL)
					{
						put_texture( text_bmp, text_BLU_1, 96, 96, 320, th_drive_icon_x, th_drive_icon_y, 0, 0xff800080);
					}
				}
			}
			if(cover_mode==6) return;

			float s_x, s_y, s_h, s_w, b_size;

			int game_rel=0, c_x=0, c_y=0, c_game=0, c_w=0, c_h=0;
			if(cover_mode==1)
			{
				game_rel=int(selected/8)*8;
				c_game= selected-(game_rel);
				if(c_game<4)
				{
					c_y=64;
					c_x= 150 + (433*c_game);

				}
				else
				{
					c_y=430;
					c_x= 150 + (433*(c_game-4));
				}
				if(menu[selected].cover!=1)
				{
					c_y+=124;
					c_w=320;
					c_h=176;
				}
				else
				{
					c_x+=30;
					c_w=260;
					c_h=300;
				}
			}

			if(cover_mode==7)
			{
				game_rel=int(selected/32)*32;
				c_game= selected-(game_rel);
				if(c_game<8)
				{
					c_y=62;
					c_x= 118 + (int)(216.5f*c_game);
				}

				if(c_game>7 && c_game<16)
				{
					c_y=240;
					c_x= 118 + (int)(216.5f*(c_game-8));
				}

				if(c_game>15 && c_game<24)
				{
					c_y=418;
					c_x= 118 + (int)(216.5f*(c_game-16));
				}

				if(c_game>23 && c_game<32)
				{
					c_y=596;
					c_x= 118 + (int)(216.5f*(c_game-24));
				}
				if(menu[selected].cover!=1)
				{
					c_y+=62;
					c_w=160;
					c_h=88;
					c_x+=7;
				}
				else
				{
					c_x+=15;
					c_w=130;
					c_h=150;
				}
			}

			b_size = 0.008f;
			s_x=(float) c_x/1920.0f-b_size;
			s_y=(float) c_y/1080.0f-b_size;
			s_h=(float) c_h/1080.0f+2.0f*b_size;
			s_w=(float) c_w/1920.0f+2.0f*b_size;
			uint32_t b_color = 0x0080ff80; if(strstr(menu[selected].path, "/dev_usb")!=NULL || strstr(menu[selected].path, "/pvd_usb")!=NULL) b_color = 0xff800000;
			if(strstr(menu[selected].path, "/dev_bdvd")!=NULL) b_color = 0x39b3f900;
			if(strstr(menu[selected].title_id, "AVCHD")!=NULL) b_color = 0xeeeb1a00;
			if(menu[selected].title[0]=='_' || menu[selected].split) b_color = 0xff000000;

			b_box_opaq+=b_box_step;
			if(b_box_opaq>0xfb) b_box_step=-4;
			if(b_box_opaq<0x20) b_box_step= 8;
			b_color = (b_color & 0xffffff00) | b_box_opaq;

			draw_square((s_x-0.5f)*2.0f, (0.5f-s_y)*2.0f , s_w*2.0f, b_size, -0.9f, b_color);
			draw_square((s_x-0.5f)*2.0f, (0.5f-s_y-s_h)*2.0f+b_size , s_w*2.0f, b_size, -0.9f, b_color);

			draw_square((s_x-0.5f)*2.0f, (0.5f-s_y)*2.0f , b_size, s_h*2.0f, -0.9f, b_color);
			draw_square((s_x+s_w-0.5f)*2.0f-b_size, (0.5f-s_y)*2.0f , b_size, s_h*2.0f, -0.9f, b_color);


		}
//		else
//			draw_text_stroke( 0.5f-(float)(len/2), 0.63f, 0.88f, color, str);

	}
	else
	{



	while( (c<max_entries && i < menu_size) )
	{

		if( (display_mode==1 && strstr(menu[i].content,"AVCHD")!=NULL) || (display_mode==2 && strstr(menu[i].content,"PS3")!=NULL) ) { i++; continue;}

		if( (i >= (int) (selected / max_entries)*max_entries) )
		{

		int grey=0;

		if(i<menu_size)
			{
			grey=(menu[i].title[0]=='_' || menu[i].split);
			if(grey && menu[i].title[0]=='_')
				{ utf8_to_ansi(menu[i].title+1, ansi, 128);	}
			else
				{ utf8_to_ansi(menu[i].title, ansi, 128);	}

			if(dir_mode==0 && (cover_mode==0 || cover_mode==2)) { ansi[40]=0; }
			if(dir_mode==1 && (cover_mode==0 || cover_mode==2)) { ansi[55]=0; }
			if(dir_mode==0 && cover_mode==3) { ansi[47]=0; }
			if(dir_mode==1 && cover_mode==3) { ansi[62]=0; }
			if( (cover_mode==1 || cover_mode==4 || cover_mode==7)) { ansi[128]=0; }
			if(grey)
				sprintf(str, "%s (Split)", ansi);
			else
				sprintf(str, "%s", ansi);
			}
		else
			{
			sprintf(str, " ");
			}

		color= 0xff606060;

		if(i==selected)
			color= (flagb && i==0) ? COL_PS3DISCSEL : ((grey==0) ? COL_SEL : 0xff008080);

		else {
			color= (flagb && i==0)? COL_PS3DISC : ((grey==0) ?  COL_PS3 : COL_SPLIT);
			if(strstr(menu[i].content,"AVCHD")!=NULL) color=COL_AVCHD;
			if(strstr(menu[i].content,"BDMV")!=NULL) color=COL_BDMV;
			if(strstr(menu[i].content,"PS2")!=NULL) color=COL_PS2;
			if(strstr(menu[i].content,"DVD")!=NULL) color=COL_DVD;
			}

		color2=( (color & 0x00ffffff) | (opaq<<24));
		if(dir_mode==1)
		{	len=0.023f*(float)(strlen(str)+2);
			if(i==selected)	// && cover_mode==3
				draw_text_stroke( 0.08f, y-0.005f, 0.88f, color, str);
			else
				if(opaq>0xFD)
					draw_text_stroke( 0.08f, y-0.005f, 0.88f, color2, str);
				else
					cellDbgFontPrintf( 0.08f, y-0.005f, 0.88f, color2, str);
		}

		else
		{	len=0.03f*(float)(strlen(str));

			if(dir_mode==0){
			if(i==selected)
				draw_text_stroke( 0.08f, y-0.005f, 1.2f, color, str);
			else
				if(opaq>0xFD)
					draw_text_stroke( 0.08f, y-0.005f, 1.2f, color2, str);
				else
					cellDbgFontPrintf( 0.08f, y-0.005f, 1.2f, color2, str);
			}
			else //dir2
			{
			len=0.023f*(float)(strlen(str)+2);
			if(i==selected)	// && cover_mode==3
				draw_text_stroke( 0.08f, y+0.001f, 0.88f, color, str);
			else
				if(opaq>0xFD)
					draw_text_stroke( 0.08f, y+0.001f, 0.88f, color2, str);
				else
					cellDbgFontPrintf( 0.08f, y+0.001f, 0.88f, color2, str);
			}
			}



		if(strlen(str)>1 && dir_mode==1)
		{
			sprintf(str, "%s", menu[i].path);
			if(strstr(menu[i].content,"AVCHD")!=NULL || strstr(menu[i].content,"BDMV")!=NULL)
				sprintf(str, "(%s) %s", menu[i].entry, menu[i].details); str[102]=0;

			if(0.01125f*(float)(strlen(str))>len) len=0.01125f*(float)(strlen(str));
		if(i==selected)
			cellDbgFontPrintf( 0.08f, (y+0.022f), 0.45f, color, str);
			else
			cellDbgFontPrintf( 0.08f, (y+0.022f), 0.45f, color2, str);
		}
		if(i==selected)
		{
			u32 b_color=0x0080ffd0;
			b_box_opaq+=b_box_step;
			if(b_box_opaq>0xd0) b_box_step=-2;
			if(b_box_opaq<0x30) b_box_step= 1;
			b_color = (b_color & 0xffffff00) | (b_box_opaq-20);
			draw_square((0.08f-0.5f)*2.0f-0.02f, (0.5f-y+0.01)*2.0f , len+0.04f, 0.1f, -0.9f, b_color);
		}

		y += 0.05f;
		c++;
		}
		i++;
	}
	} // cover_mode==4

	return;
}


static void init_text_shader( void )
{

	void *ucode;
	u32 ucode_size;

	vertex_prg = &_binary_vpshader2_vpo_start;
	fragment_prg = &_binary_fpshader2_fpo_start;

	cellGcmCgInitProgram( vertex_prg );
	cellGcmCgInitProgram( fragment_prg );

	cellGcmCgGetUCode( fragment_prg, &ucode, &ucode_size );

	text_fragment_prg_ucode = localAllocAlign(64, ucode_size );

	cellGcmAddressToOffset( text_fragment_prg_ucode, &text_fragment_offset );

	memcpy( text_fragment_prg_ucode, ucode, ucode_size );

	cellGcmCgGetUCode( vertex_prg, &text_vertex_prg_ucode, &ucode_size );

}


int text_create()
{
	init_text_shader();

	vertex_text = (vtx_texture*) localAllocAlign(128*1024, 1024*sizeof(vtx_texture));

	cellGcmAddressToOffset( (void*)vertex_text,
							&vertex_text_offset );

	text_param.format  = CELL_GCM_TEXTURE_A8R8G8B8;
	text_param.format |= CELL_GCM_TEXTURE_LN;

	text_param.remap = CELL_GCM_TEXTURE_REMAP_REMAP << 14 | CELL_GCM_TEXTURE_REMAP_REMAP << 12 | CELL_GCM_TEXTURE_REMAP_REMAP << 10 |
		CELL_GCM_TEXTURE_REMAP_REMAP <<  8 | CELL_GCM_TEXTURE_REMAP_FROM_G << 6 | CELL_GCM_TEXTURE_REMAP_FROM_R << 4 |
		CELL_GCM_TEXTURE_REMAP_FROM_A << 2 | CELL_GCM_TEXTURE_REMAP_FROM_B;

	text_param.mipmap = 1;
	text_param.cubemap = CELL_GCM_FALSE;
	text_param.dimension = CELL_GCM_TEXTURE_DIMENSION_2;

	CGparameter objCoord = cellGcmCgGetNamedParameter( vertex_prg, "a2v.objCoord" );
	if( objCoord == 0 ) return -1;

	CGparameter texCoord = cellGcmCgGetNamedParameter( vertex_prg, "a2v.texCoord" );
	if( texCoord == 0) return -1;

	CGparameter texture = cellGcmCgGetNamedParameter( fragment_prg, "texture" );

	if( texture == 0 ) return -1;

	text_obj_coord_indx = cellGcmCgGetParameterResource( vertex_prg, objCoord) - CG_ATTR0;
	text_tex_coord_indx = cellGcmCgGetParameterResource( vertex_prg, texCoord) - CG_ATTR0;
	tindex = (CGresource) (cellGcmCgGetParameterResource( fragment_prg, texture ) - CG_TEXUNIT0 );

	return 0;

}

int set_texture( u8 *buffer, u32 x_size, u32 y_size )
{

	int ret;
	u32 buf_offs;

	ret = cellGcmAddressToOffset( buffer, &buf_offs );
	if( CELL_OK != ret ) return ret;

	text_param.depth  = 1;
	text_param.width  = x_size;
	text_param.height = y_size;
	text_param.pitch  = x_size*4;
	text_param.offset = buf_offs;
	text_param.location = CELL_GCM_LOCATION_MAIN;


	cellGcmSetTexture( gCellGcmCurrentContext, tindex, &text_param );

	cellGcmSetTextureControl( gCellGcmCurrentContext, tindex, 1, 0, 15, CELL_GCM_TEXTURE_MAX_ANISO_1 );

	cellGcmSetTextureAddress( gCellGcmCurrentContext, tindex, CELL_GCM_TEXTURE_CLAMP_TO_EDGE, CELL_GCM_TEXTURE_CLAMP_TO_EDGE,
		CELL_GCM_TEXTURE_CLAMP_TO_EDGE, CELL_GCM_TEXTURE_UNSIGNED_REMAP_NORMAL, CELL_GCM_TEXTURE_ZFUNC_LESS, 0 );

//	cellGcmSetTextureFilter( gCellGcmCurrentContext, tindex, 0, CELL_GCM_TEXTURE_LINEAR_LINEAR, CELL_GCM_TEXTURE_LINEAR, CELL_GCM_TEXTURE_CONVOLUTION_QUINCUNX );
	if(V_WIDTH<1280)
		cellGcmSetTextureFilter( gCellGcmCurrentContext, tindex, 0, CELL_GCM_TEXTURE_CONVOLUTION_MIN, CELL_GCM_TEXTURE_LINEAR, CELL_GCM_TEXTURE_CONVOLUTION_GAUSSIAN );
	else
		cellGcmSetTextureFilter( gCellGcmCurrentContext, tindex, 0, CELL_GCM_TEXTURE_LINEAR_LINEAR, CELL_GCM_TEXTURE_LINEAR, CELL_GCM_TEXTURE_CONVOLUTION_QUINCUNX );

	cellGcmSetVertexProgram( gCellGcmCurrentContext, vertex_prg, text_vertex_prg_ucode );
	cellGcmSetFragmentProgram( gCellGcmCurrentContext, fragment_prg, text_fragment_offset);
	cellGcmSetInvalidateTextureCache( gCellGcmCurrentContext, CELL_GCM_INVALIDATE_TEXTURE );
	cellGcmSetVertexDataArray( gCellGcmCurrentContext, text_obj_coord_indx, 0, sizeof(vtx_texture), 3, CELL_GCM_VERTEX_F,
							   CELL_GCM_LOCATION_LOCAL, vertex_text_offset );
	cellGcmSetVertexDataArray( gCellGcmCurrentContext, text_tex_coord_indx, 0, sizeof(vtx_texture), 2, CELL_GCM_VERTEX_F,
	                           CELL_GCM_LOCATION_LOCAL, ( vertex_text_offset + sizeof(float)*3 ) );

	return ret;

}

void display_img_persp(int x, int y, int width, int height, int tx, int ty, float z, int Dtx, int Dty, int keystoneL, int keystoneR)
{
    vertex_text[vert_texture_indx].x= ((float) ((x)*2))/((float) 1920)-1.0f;
    vertex_text[vert_texture_indx].y= ((float) ((y-keystoneL)*-2))/((float) 1080)+1.0f;
    vertex_text[vert_texture_indx].z= z;
    vertex_text[vert_texture_indx].tx= 0.0f;
    vertex_text[vert_texture_indx].ty= 0.0f;

    vertex_text[vert_texture_indx+1].x= ((float) ((x+width)*2))/((float) 1920)-1.0f;
    vertex_text[vert_texture_indx+1].y= ((float) ((y-keystoneR)*-2))/((float) 1080)+1.0f;
    vertex_text[vert_texture_indx+1].z= z;
    vertex_text[vert_texture_indx+1].tx= ((float) tx)/Dtx;
    vertex_text[vert_texture_indx+1].ty= 0.0f;

    vertex_text[vert_texture_indx+2].x= ((float) ((x+width)*2))/((float) 1920)-1.0f;
    vertex_text[vert_texture_indx+2].y= ((float) ((y+height+keystoneR)*-2))/((float) 1080)+1.0f;
    vertex_text[vert_texture_indx+2].z= z;
    vertex_text[vert_texture_indx+2].tx= ((float) tx)/Dtx;
    vertex_text[vert_texture_indx+2].ty=((float) ty)/Dty;

    vertex_text[vert_texture_indx+3].x= ((float) ((x)*2))/((float) 1920)-1.0f;
    vertex_text[vert_texture_indx+3].y= ((float) ((y+height+keystoneL)*-2))/((float) 1080)+1.0f;
    vertex_text[vert_texture_indx+3].z= z;
    vertex_text[vert_texture_indx+3].tx= 0.0f;
    vertex_text[vert_texture_indx+3].ty= ((float) ty)/Dty;

    cellGcmSetDrawArrays( gCellGcmCurrentContext, CELL_GCM_PRIMITIVE_QUADS, vert_texture_indx, 4 ); //CELL_GCM_PRIMITIVE_TRIANGLE_STRIP
    vert_texture_indx+=4;
}

void display_img(int x, int y, int width, int height, int tx, int ty, float z, int Dtx, int Dty)
{
	/*if((cover_mode==8) && V_WIDTH==720)
	{
		x-=(int)((float)width*0.04f);
		y-=(int)((float)height*0.06f);
		width+=(int)((float)width*0.08f);
		height+=(int)((float)height*0.12f);

	}*/
    vertex_text[vert_texture_indx].x= ((float) ((x)*2))/((float) 1920)-1.0f;
    vertex_text[vert_texture_indx].y= ((float) ((y)*-2))/((float) 1080)+1.0f;
    vertex_text[vert_texture_indx].z= z;
    vertex_text[vert_texture_indx].tx= 0.0f;
    vertex_text[vert_texture_indx].ty= 0.0f;

    vertex_text[vert_texture_indx+1].x= ((float) ((x+width)*2))/((float) 1920)-1.0f;
    vertex_text[vert_texture_indx+1].y= ((float) ((y)*-2))/((float) 1080)+1.0f;
    vertex_text[vert_texture_indx+1].z= z;
    vertex_text[vert_texture_indx+1].tx= ((float) tx)/Dtx;
    vertex_text[vert_texture_indx+1].ty= 0.0f;

    vertex_text[vert_texture_indx+2].x= ((float) ((x+width)*2))/((float) 1920)-1.0f;
    vertex_text[vert_texture_indx+2].y= ((float) ((y+height)*-2))/((float) 1080)+1.0f;
    vertex_text[vert_texture_indx+2].z= z;
    vertex_text[vert_texture_indx+2].tx= ((float) tx)/Dtx;
    vertex_text[vert_texture_indx+2].ty=((float) ty)/Dty;

    vertex_text[vert_texture_indx+3].x= ((float) ((x)*2))/((float) 1920)-1.0f;
    vertex_text[vert_texture_indx+3].y= ((float) ((y+height)*-2))/((float) 1080)+1.0f;
    vertex_text[vert_texture_indx+3].z= z;
    vertex_text[vert_texture_indx+3].tx= 0.0f;
    vertex_text[vert_texture_indx+3].ty= ((float) ty)/Dty;

    cellGcmSetDrawArrays( gCellGcmCurrentContext, CELL_GCM_PRIMITIVE_QUADS, vert_texture_indx, 4 ); //CELL_GCM_PRIMITIVE_TRIANGLE_STRIP
    vert_texture_indx+=4;
}

void display_img_nr(int x, int y, int width, int height, int tx, int ty, float z, int Dtx, int Dty)
{
    vertex_text[vert_texture_indx].x= ((float) ((x)*2))/((float) V_WIDTH)-1.0f;
    vertex_text[vert_texture_indx].y= ((float) ((y)*-2))/((float) V_HEIGHT)+1.0f;
    vertex_text[vert_texture_indx].z= z;
    vertex_text[vert_texture_indx].tx= 0.0f;
    vertex_text[vert_texture_indx].ty= 0.0f;

    vertex_text[vert_texture_indx+1].x= ((float) ((x+width)*2))/((float) V_WIDTH)-1.0f;
    vertex_text[vert_texture_indx+1].y= ((float) ((y)*-2))/((float) V_HEIGHT)+1.0f;
    vertex_text[vert_texture_indx+1].z= z;
    vertex_text[vert_texture_indx+1].tx= ((float) tx)/Dtx;
    vertex_text[vert_texture_indx+1].ty= 0.0f;

    vertex_text[vert_texture_indx+2].x= ((float) ((x)*2))/((float) V_WIDTH)-1.0f;
    vertex_text[vert_texture_indx+2].y= ((float) ((y+height)*-2))/((float) V_HEIGHT)+1.0f;
    vertex_text[vert_texture_indx+2].z= z;
    vertex_text[vert_texture_indx+2].tx= 0.0f;
    vertex_text[vert_texture_indx+2].ty= ((float) ty)/Dty;

    vertex_text[vert_texture_indx+3].x= ((float) ((x+width)*2))/((float) V_WIDTH)-1.0f;
    vertex_text[vert_texture_indx+3].y= ((float) ((y+height)*-2))/((float) V_HEIGHT)+1.0f;
    vertex_text[vert_texture_indx+3].z= z;
    vertex_text[vert_texture_indx+3].tx= ((float) tx)/Dtx;
    vertex_text[vert_texture_indx+3].ty=((float) ty)/Dty;

    cellGcmSetDrawArrays( gCellGcmCurrentContext, CELL_GCM_PRIMITIVE_TRIANGLE_STRIP, vert_texture_indx, 4 ); //CELL_GCM_PRIMITIVE_TRIANGLE_STRIP
    vert_texture_indx+=4;
}

int angle_coord_x(int radius, float __angle)
{
	float _angle=__angle;
	if(_angle>=360.f) _angle= __angle - 360.f;
	if(_angle<0.f) _angle=360.f+__angle;
	return (int) ((float) radius * cos((_angle/360.f * 6.283185307179586476925286766559f)));
}

int angle_coord_y(int radius, float __angle)
{
	float _angle=__angle;
	if(_angle>=360.f) _angle= __angle - 360.f;
	if(_angle<0.f) _angle=360.f+__angle;
	return (int) ((float) radius * sin((_angle/360.f * 6.283185307179586476925286766559f)));
}

void display_img_angle(int x, int y, int width, int height, int tx, int ty, float z, int Dtx, int Dty, float _angle)
{
	int _radius;
	/*if((cover_mode==8) && V_WIDTH==720)
	{
		x-=(int)((float)width*0.04f);
		y-=(int)((float)height*0.06f);
		width+=(int)((float)width*0.08f);
		height+=(int)((float)height*0.12f);

	}*/

	_radius = (int)(((float)width*sqrt(2.f))/2.f); // diagonal/2 -> works for square textures at the moment

	// center of rotation
	int xC= x+width/2;
	int yC= y+height/2;

    vertex_text[vert_texture_indx].x= ((float) ((xC+angle_coord_x(_radius, _angle))*2))/((float) 1920)-1.0f;
    vertex_text[vert_texture_indx].y= ((float) ((yC+angle_coord_y(_radius, _angle))*-2))/((float) 1080)+1.0f;
    vertex_text[vert_texture_indx].z= z;
    vertex_text[vert_texture_indx].tx= 0.0f;
    vertex_text[vert_texture_indx].ty= 0.0f;

    vertex_text[vert_texture_indx+1].x= ((float) ((xC+angle_coord_x(_radius, _angle+90.f))*2))/((float) 1920)-1.0f;
    vertex_text[vert_texture_indx+1].y= ((float) ((yC+angle_coord_y(_radius, _angle+90.f))*-2))/((float) 1080)+1.0f;
    vertex_text[vert_texture_indx+1].z= z;
    vertex_text[vert_texture_indx+1].tx= ((float) tx)/Dtx;
    vertex_text[vert_texture_indx+1].ty= 0.0f;

    vertex_text[vert_texture_indx+2].x= ((float) ((xC+angle_coord_x(_radius, _angle-90.f))*2))/((float) 1920)-1.0f;
    vertex_text[vert_texture_indx+2].y= ((float) ((yC+angle_coord_y(_radius, _angle-90.f))*-2))/((float) 1080)+1.0f;
    vertex_text[vert_texture_indx+2].z= z;
    vertex_text[vert_texture_indx+2].tx= 0.0f;
    vertex_text[vert_texture_indx+2].ty= ((float) ty)/Dty;

    vertex_text[vert_texture_indx+3].x= ((float) ((xC+angle_coord_x(_radius, _angle-180.f))*2))/((float) 1920)-1.0f;
    vertex_text[vert_texture_indx+3].y= ((float) ((yC+angle_coord_y(_radius, _angle-180.f))*-2))/((float) 1080)+1.0f;
    vertex_text[vert_texture_indx+3].z= z;
    vertex_text[vert_texture_indx+3].tx= ((float) tx)/Dtx;
    vertex_text[vert_texture_indx+3].ty=((float) ty)/Dty;

    cellGcmSetDrawArrays( gCellGcmCurrentContext, CELL_GCM_PRIMITIVE_TRIANGLE_STRIP, vert_texture_indx, 4 ); //CELL_GCM_PRIMITIVE_TRIANGLE_STRIP
    vert_texture_indx+=4;
}

void draw_square(float x, float y, float w, float h, float z, u32 color)
{

	vertex_color[vert_indx].x = x;
	vertex_color[vert_indx].y = y;
	vertex_color[vert_indx].z = z;
	vertex_color[vert_indx].color=color;

	vertex_color[vert_indx+1].x = x+w;
	vertex_color[vert_indx+1].y = y;
	vertex_color[vert_indx+1].z = z;
	vertex_color[vert_indx+1].color=color;

	vertex_color[vert_indx+2].x = x+w;
	vertex_color[vert_indx+2].y = y-h;
	vertex_color[vert_indx+2].z = z;
	vertex_color[vert_indx+2].color=color;

	vertex_color[vert_indx+3].x = x;
	vertex_color[vert_indx+3].y = y-h;
	vertex_color[vert_indx+3].z = z;
	vertex_color[vert_indx+3].color=color;

	cellGcmSetDrawArrays( gCellGcmCurrentContext, CELL_GCM_PRIMITIVE_QUADS, vert_indx, 4);
	vert_indx+=4;

}

/*void draw_square_angle(float _x, float _y, float w, float h, float z, u32 color, float _angle)
{
	int _radius;
	(void) w;
	(void) h;

	_radius = (int)(((float) 540.f * sqrt(2.f))/2.f); // diagonal/2 -> works for square textures at the moment

	float x=_x+(float)angle_coord_x(_radius, _angle)/1920.f;
	float y=_y+(float)angle_coord_y(_radius, _angle)/1080.f;

	vertex_color[vert_indx].x = x;
	vertex_color[vert_indx].y = y;
	vertex_color[vert_indx].z = z;
	vertex_color[vert_indx].color=color;

	vertex_color[vert_indx+1].x = x+w;
	vertex_color[vert_indx+1].y = y;
	vertex_color[vert_indx+1].z = z;
	vertex_color[vert_indx+1].color=color;

	vertex_color[vert_indx+2].x = x+w;
	vertex_color[vert_indx+2].y = y-h;
	vertex_color[vert_indx+2].z = z;
	vertex_color[vert_indx+2].color=color;

	vertex_color[vert_indx+3].x = x;
	vertex_color[vert_indx+3].y = y-h;
	vertex_color[vert_indx+3].z = z;
	vertex_color[vert_indx+3].color=color;

	cellGcmSetDrawArrays( gCellGcmCurrentContext, CELL_GCM_PRIMITIVE_QUADS, vert_indx, 4);
	vert_indx+=4;
}*/

