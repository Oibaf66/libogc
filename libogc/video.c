#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "asm.h"
#include "processor.h"
#include "ogcsys.h"
#include "irq.h"
#include "exi.h"
#include "gx.h"
#include "lwp.h"
#include "system.h"
#include "video.h"
#include "video_types.h"

//#define _VIDEO_DEBUG

#define VIDEO_MQ					1

#define _SHIFTL(v, s, w)	\
    ((u32) (((u32)(v) & ((0x01 << (w)) - 1)) << (s)))
#define _SHIFTR(v, s, w)	\
    ((u32)(((u32)(v) >> (s)) & ((0x01 << (w)) - 1)))

/*+----------------------------------------------------------------------------------------------+*/
#define MEM_VIDEO_BASE               0xCC002000           ///< Memory address of Video Interface
#define MEM_VIDEO_BASE_PTR           (u32*)MEM_VIDEO_BASE   ///< Pointer to Video Interface
/*+----------------------------------------------------------------------------------------------+*/
#define R_VIDEO_STATUS1				 *((vu16*)(MEM_VIDEO_BASE+0x02))   ///< Status? register location. Includes typecasting for direct c writes.
#define R_VIDEO_FRAMEBUFFER_1        *((vu32*)(MEM_VIDEO_BASE+0x1C))   ///< Framebuffer1 register location. Includes typecasting for direct c writes.
#define R_VIDEO_FRAMEBUFFER_2        *((vu32*)(MEM_VIDEO_BASE+0x24))   ///< Framebuffer2 register location. Includes typecasting for direct c writes.
#define R_VIDEO_HALFLINE_1           *((vu16*)(MEM_VIDEO_BASE+0x2C))   ///< HalfLine1 register location. Includes typecasting for direct c writes.
#define R_VIDEO_HALFLINE_2           *((vu16*)(MEM_VIDEO_BASE+0x2E))   ///< HalfLine2 register location. Includes typecasting for direct c writes.
#define R_VIDEO_STATUS               *((vu16*)(MEM_VIDEO_BASE+0x6C))   ///< VideoStatus register location. Includes typecasting for direct c writes.

static const u32 VIDEO_Mode640X480YUV16[3][32] = {
	// NTSC 60Hz
	{
		0x0F060001, 0x476901AD, 0x02EA5140, 0x00030018,
		0x00020019, 0x410C410C, 0x40ED40ED, 0x00435A4E,
		0x00000000, 0x00435A4E, 0x00000000, 0x00000000,
		0x110701AE, 0x10010001, 0x00010001, 0x00010001,
		0x00000000, 0x00000000, 0x28500100, 0x1AE771F0,
		0x0DB4A574, 0x00C1188E, 0xC4C0CBE2, 0xFCECDECF,
		0x13130F08, 0x00080C0F, 0x00FF0000, 0x00000000,
		0x02800000, 0x000000FF, 0x00FF00FF, 0x00FF00FF
	},
	//PAL 50Hz
	{
		0x11F50105, 0x4B6A01B0, 0x02F85640, 0x00010023,
		0x00000024, 0x4D2B4D6D, 0x4D8A4D4C, 0x00435A4E,
		0x00000000, 0x00435A4E, 0x00000000, 0x013C0144,
		0x113901B1, 0x10010001, 0x00010001, 0x00010001,
		0x00000000, 0x00000000, 0x28500100, 0x1AE771F0,
		0x0DB4A574, 0x00C1188E, 0xC4C0CBE2, 0xFCECDECF,
		0x13130F08, 0x00080C0F, 0x00FF0000, 0x00000000,
		0x02800000, 0x000000FF, 0x00FF00FF, 0x00FF00FF	
	},
	//PAL 60Hz
	{
		0x0F060001, 0x476901AD, 0x02EA5140, 0x00030018,
		0x00020019, 0x410C410C, 0x40ED40ED, 0x00435A4E,
		0x00000000, 0x00435A4E, 0x00000000, 0x00050176,
		0x110701AE, 0x10010001, 0x00010001, 0x00010001,
		0x00000000, 0x00000000, 0x28500100, 0x1AE771F0,
		0x0DB4A574, 0x00C1188E, 0xC4C0CBE2, 0xFCECDECF,
		0x13130F08, 0x00080C0F, 0x00FF0000, 0x00000000,
		0x02800000, 0x000000FF, 0x00FF00FF, 0x00FF00FF
	}
};

static const u8 video_timing[6][38] = {
	{
		0x06,0x00,0x00,0xF0,0x00,0x18,0x00,0x19,
		0x00,0x03,0x00,0x02,0x0C,0x0D,0x0C,0x0D,
		0x02,0x08,0x02,0x07,0x02,0x08,0x02,0x07,
		0x02,0x0D,0x01,0xAD,0x40,0x47,0x69,0xA2,
		0x01,0x75,0x7A,0x00,0x01,0x9C
	},
	{
		0x06,0x00,0x00,0xF0,0x00,0x18,0x00,0x18,
		0x00,0x04,0x00,0x04,0x0C,0x0C,0x0C,0x0C,
		0x02,0x08,0x02,0x08,0x02,0x08,0x02,0x08,
		0x02,0x0E,0x01,0xAD,0x40,0x47,0x69,0xA2,
		0x01,0x75,0x7A,0x00,0x01,0x9C
	},
	{
		0x05,0x00,0x01,0x1F,0x00,0x23,0x00,0x24,
		0x00,0x01,0x00,0x00,0x0D,0x0C,0x0B,0x0A,
		0x02,0x6B,0x02,0x6A,0x02,0x69,0x02,0x6C,
		0x02,0x71,0x01,0xB0,0x40,0x4B,0x6A,0xAC,
		0x01,0x7C,0x85,0x00,0x01,0xA4	
	},
	{
		0x05,0x00,0x01,0x1F,0x00,0x21,0x00,0x21,
		0x00,0x02,0x00,0x02,0x0D,0x0B,0x0D,0x0B,
		0x02,0x6B,0x02,0x6D,0x02,0x6B,0x02,0x6D,
		0x02,0x70,0x01,0xB0,0x40,0x4B,0x6A,0xAC,
		0x01,0x7C,0x85,0x00,0x01,0xA4
	},
	{
		0x06,0x00,0x00,0xF0,0x00,0x18,0x00,0x19,
		0x00,0x03,0x00,0x02,0x10,0x0F,0x0E,0x0D,
		0x02,0x06,0x02,0x05,0x02,0x04,0x02,0x07,
		0x02,0x0D,0x01,0xAD,0x40,0x4E,0x70,0xA2,
		0x01,0x75,0x7A,0x00,0x01,0x9C
	},
	{
		0x06,0x00,0x00,0xF0,0x00,0x18,0x00,0x18,
		0x00,0x04,0x00,0x04,0x10,0x0E,0x10,0x0E,
		0x02,0x06,0x02,0x08,0x02,0x06,0x02,0x08,
		0x02,0x0E,0x01,0xAD,0x40,0x4E,0x70,0xA2,
		0x01,0x75,0x7A,0x00,0x01,0x9C
	}
};

static const u16 taps[26] = {
	0x01F0,0x01DC,0x01AE,0x0174,0x0129,0x00DB,
	0x008E,0x0046,0x000C,0x00E2,0x00CB,0x00C0,
	0x00C4,0x00CF,0x00DE,0x00EC,0x00FC,0x0008,
	0x000F,0x0013,0x0013,0x000F,0x000C,0x0008,
	0x0001,0x0000                              
};

GXRModeObj TVPal524IntAa = 
{
	VI_TVMODE_PAL_INT,
	640,
	264,
	524,
	(VI_MAX_WIDTH_PAL-640)/2,
	(VI_MAX_HEIGHT_PAL-528)/2,
	640,
	524,
	VI_XFBMODE_DF,
	GX_FALSE,
	GX_TRUE,
	
    // sample points arranged in increasing Y order
	{
		{3,2},{9,6},{3,10},				// pix 0, 3 sample points, 1/12 units, 4 bits each
		{3,2},{9,6},{3,10},				// pix 1
		{9,2},{3,6},{9,10},				// pix 2
		{9,2},{3,6},{9,10}				// pix 3
	},
 
	 // vertical filter[7], 1/64 units, 6 bits each
	{
		4,         // line n-1
		8,         // line n-1
		12,        // line n
		16,        // line n
		12,        // line n
		8,         // line n+1
		4          // line n+1
	}
};

GXRModeObj TVPal528IntDf = 
{
    VI_TVMODE_PAL_INT,      // viDisplayMode
    640,             // fbWidth
    528,             // efbHeight
    528,             // xfbHeight
    (VI_MAX_WIDTH_PAL - 640)/2,         // viXOrigin
    (VI_MAX_HEIGHT_PAL - 528)/2,        // viYOrigin
    640,             // viWidth
    528,             // viHeight
    VI_XFBMODE_DF,   // xFBmode
    GX_FALSE,        // field_rendering
    GX_FALSE,        // aa

    // sample points arranged in increasing Y order
	{
		{6,6},{6,6},{6,6},  // pix 0, 3 sample points, 1/12 units, 4 bits each
		{6,6},{6,6},{6,6},  // pix 1
		{6,6},{6,6},{6,6},  // pix 2
		{6,6},{6,6},{6,6}   // pix 3
	},
    // vertical filter[7], 1/64 units, 6 bits each
	{
		 8,         // line n-1
		 8,         // line n-1
		10,         // line n
		12,         // line n
		10,         // line n
		 8,         // line n+1
		 8          // line n+1
	}
};

GXRModeObj TVPal574IntDfScale = 
{
    VI_TVMODE_PAL_INT,      // viDisplayMode
    640,             // fbWidth
    480,             // efbHeight
    574,             // xfbHeight
    (VI_MAX_WIDTH_PAL - 640)/2,         // viXOrigin
    (VI_MAX_HEIGHT_PAL - 574)/2,        // viYOrigin
    640,             // viWidth
    574,             // viHeight
    VI_XFBMODE_DF,   // xFBmode
    GX_FALSE,        // field_rendering
    GX_FALSE,        // aa

    // sample points arranged in increasing Y order
	{
		{6,6},{6,6},{6,6},  // pix 0, 3 sample points, 1/12 units, 4 bits each
		{6,6},{6,6},{6,6},  // pix 1
		{6,6},{6,6},{6,6},  // pix 2
		{6,6},{6,6},{6,6}   // pix 3
	},
    // vertical filter[7], 1/64 units, 6 bits each
	{
		 8,         // line n-1
		 8,         // line n-1
		10,         // line n
		12,         // line n
		10,         // line n
		 8,         // line n+1
		 8          // line n+1
	}
};

static lwpq_t video_queue;

static u32 curFB = 0;
static u32 videoMode = -1;
static vu32 retraceCount = 0;
static void *nextFramebuffer = NULL;
static void *nextFramebufferA = NULL;
static void *nextFramebufferB = NULL;
static const u32 *currTiming = NULL;
static VIRetraceCallback preRetraceCB = NULL;
static VIRetraceCallback postRetraceCB = NULL;

u8 *curr_timing;
u16 viWidth,fbWidth,xfbWidth;
u16 viXOrigin,viYOrigin;
u32 viMode,viFmt;

static s16 displayOffsetH;
static s16 displayOffsetV;
static u32 currTvMode;
static u64 changed;
static const u8 *curr_Timing;
static vu16* const _viReg = (u16*)0xCC002000;

extern void __UnmaskIrq(u32);
extern void __MaskIrq(u32);

extern u32 __SYS_LockSram();
extern u32 __SYS_UnlockSram(u32 write);


#ifdef _VIDEO_DEBUG
extern int printf(const char *fmt,...);
#endif

static __inline__ void __clear_interrupt()
{
	_viReg[24] &= ~0x8000;
	_viReg[26] &= ~0x8000;
	_viReg[28] &= ~0x8000;
	_viReg[30] &= ~0x8000;
}

static __inline__ u32 __checkclear_interrupt()
{
	u32 ret = 0;

	if(_viReg[24]&0x8000) {
		_viReg[24] &= ~0x8000;
		ret |= 0x01;
	}
	if(_viReg[26]&0x8000) {
		_viReg[26] &= ~0x8000;
		ret |= 0x02;
	}
	if(_viReg[28]&0x8000) {
		_viReg[28] &= ~0x8000;
		ret |= 0x04;
	}
	if(_viReg[30]&0x8000) {
		_viReg[30] &= ~0x8000;
		ret |= 0x08;
	}
	return ret;
}

static const u8* __gettiming(u32 vimode)
{
	if(vimode>0x0015) return NULL;

	switch(vimode) {
		case VI_TVMODE_NTSC_INT:
			return video_timing[0];
			break;
		case VI_TVMODE_NTSC_DS:
			return video_timing[1];
			break;
		case VI_TVMODE_PAL_INT:
			return video_timing[2];
			break;
		case VI_TVMODE_PAL_DS:
			return video_timing[3];
			break;
	}
	return NULL;
}

static void __VIInit(u32 vimode)
{
	u32 cnt;
	u32 vi_mode,interlace,prog;
	const u8 *cur_timing = NULL;

	vi_mode = vimode>>2;
	interlace = vimode&0x01;
	prog = vimode&0x02;
	
	cur_timing = __gettiming(vimode);

	//reset the interface
	cnt = 0;
	_viReg[1] = 0x02;
	while(cnt<1000) cnt++;
	_viReg[1] = 0x00;

	// now begin to setup the interface
	_viReg[2] = ((cur_timing[29]<<8)|cur_timing[30]);		//set HCS & HCE
	_viReg[3] = ((u16*)cur_timing)[13];						//set Half Line Width

	_viReg[4] = ((((u16*)cur_timing)[16]<<1)&0xFFFE);			//set HBS
	_viReg[5] = ((cur_timing[31]<<7)|cur_timing[28]);		//set HBE & HSY
	
	_viReg[0] = cur_timing[0];

	_viReg[6] = (((u16*)cur_timing)[4]+2);					//set PSB odd field
	_viReg[7] = (((u16*)cur_timing)[2]+((((u16*)cur_timing)[1]<<1)-2));		//set PRB odd field

	_viReg[8] = (((u16*)cur_timing)[5]+2);					//set PSB even field
	_viReg[9] = (((u16*)cur_timing)[3]+((((u16*)cur_timing)[1]<<1)-2));		//set PRB even field

	_viReg[10] = ((((u16*)cur_timing)[10]<<5)|cur_timing[14]);	//set BE3 & BS3
	_viReg[11] = ((((u16*)cur_timing)[8]<<5)|cur_timing[12]);	//set BE1 & BS1
	
	_viReg[12] = ((((u16*)cur_timing)[11]<<5)|cur_timing[15]);	//set BE4 & BS4
	_viReg[13] = ((((u16*)cur_timing)[9]<<5)|cur_timing[13]);	//set BE2 & BS2

	_viReg[24] = (((((u16*)cur_timing)[12]/2)+1)|0x1000);
	_viReg[25] = (((u16*)cur_timing)[13]+1);
	
	_viReg[26] = 0x1001;		//set DI1
	_viReg[27] = 0x0001;		//set DI1
	_viReg[36] = 0x2828;		//set HSR
	
	if(vimode==0x0002 || vimode==0x0003) {
		_viReg[1] = ((vi_mode<<8)|0x0005);		//set MODE & INT & enable
		_viReg[54] = 0x0001;
	} else {
		_viReg[1] = ((vi_mode<<8)|(prog<<2)|0x0001);
		_viReg[54] = 0x0000;
	}
}

static u32 __getCurrentHalfLine()
{
	u32 tmp;
	u32 vpos = 0;
	u32 hpos = 0;

	vpos = _viReg[22]&0x07FF;
	do {
		tmp = vpos;
		hpos = _viReg[23]&0x07FF;
		vpos = _viReg[22]&0x07FF;
	} while(tmp!=vpos);
	
	hpos--;
	vpos--;
	vpos <<= 1;
	tmp = ((u16*)currTiming)[3];

	return vpos+(hpos/tmp);	
}

static u32 __getCurrFieldEvenOdd()
{
	u32 hline;
	u32 nhline;

	hline = __getCurrentHalfLine();
	nhline = ((((u16*)currTiming)[24]&0x03FF)*2)-1;
	if(hline<nhline) return 1;

	return 0;
}

static u32 __VISetRegs()
{
	void *tmp = nextFramebuffer;

	if(!__getCurrFieldEvenOdd()) return 0;

	nextFramebufferA = tmp;
	if(curFB==1) {
		tmp = (void*)(u8*)tmp+1280;
	}
	nextFramebufferB = tmp;
	
	((u32*)_viReg)[7] = (0x10000000|(MEM_VIRTUAL_TO_PHYSICAL(nextFramebufferA)>>5));
	((u32*)_viReg)[9] = (0x10000000|(MEM_VIRTUAL_TO_PHYSICAL(nextFramebufferB)>>5));

	curFB ^= 1;
	return 1;
}

static void __VIRetraceHandler(u32 nIrq,void *pCtx)
{
	u32 ret;
	if(!(ret=__checkclear_interrupt()) || ret&0xc) return;

	retraceCount++;
	if(preRetraceCB)
		preRetraceCB(retraceCount);

	__VISetRegs();
	
	if(postRetraceCB)
		postRetraceCB(retraceCount);
#ifdef _VIDEO_DEBUG
	printf("__VIRetraceHandler(%d)\n",retraceCount);
#endif
	LWP_WakeThread(video_queue);
}

void __vi_init()
{
	retraceCount = 0;
	
	__clear_interrupt();

	LWP_InitQueue(&video_queue);

	IRQ_Request(IRQ_PI_VI,__VIRetraceHandler,NULL);
	__UnmaskIrq(IRQMASK(IRQ_PI_VI));
}

void VI_Init()
{
	u32 vimode = 0;
	syssram *sram;

	if(!(_viReg[1]&0x0001))
		__VIInit(VI_TVMODE_NTSC_INT);

	retraceCount = 0;
	changed = 0;
	
	_viReg[38] = ((taps[1]>>6)|(taps[2]<<4));
	_viReg[39] = (taps[0]|_SHIFTL(taps[1],10,6));
	_viReg[40] = ((taps[4]>>6)|(taps[5]<<4));
	_viReg[41] = (taps[3]|_SHIFTL(taps[4],10,6));
	_viReg[42] = ((taps[7]>>6)|(taps[8]<<4));
	_viReg[43] = (taps[6]|_SHIFTL(taps[7],10,6));
	_viReg[44] = (taps[11]|(taps[12]<<8));
	_viReg[45] = (taps[9]|(taps[10]<<8));
	_viReg[46] = (taps[15]|(taps[16]<<8));
	_viReg[47] = (taps[13]|(taps[14]<<8));
	_viReg[48] = (taps[19]|(taps[20]<<8));
	_viReg[49] = (taps[17]|(taps[18]<<8));
	_viReg[50] = (taps[23]|(taps[24]<<8));
	_viReg[51] = (taps[21]|(taps[22]<<8));
	_viReg[56] = 640;

	sram = (syssram*)__SYS_LockSram();
	displayOffsetV = 0;
	displayOffsetH = (s16)sram->display_offsetH;
	__SYS_UnlockSram(0)	;

	viMode = _SHIFTR(_viReg[1],2,1);
	viFmt = _SHIFTR(_viReg[1],8,2);

	vimode = viMode;
	if(viFmt!=VI_DEBUG) vimode += (viFmt<<2);
	curr_timing = (u8*)__gettiming(vimode);
	currTvMode = viFmt;

	viWidth = 640;
	viXOrigin = (VI_MAX_WIDTH_NTSC - fbWidth)/2;
	viYOrigin = 0;
	
}

void VIDEO_Configure(GXRModeObj *rmode)
{
}

void VIDEO_Init(u32 VideoMode)
{
	u32 *pDstAddr=MEM_VIDEO_BASE_PTR;

	retraceCount = 0;
	videoMode = VideoMode;
	switch(VideoMode) {
		case VIDEO_640X480_NTSC_YUV16:
		case VIDEO_640X480_PAL50_YUV16:
		case VIDEO_640X480_PAL60_YUV16:
			currTiming = VIDEO_Mode640X480YUV16[videoMode];
			break;
		default:
			videoMode = VIDEO_GetCurrentMode();
			currTiming = VIDEO_Mode640X480YUV16[videoMode];
			break;
	}
	memcpy(pDstAddr,currTiming,sizeof(u32)*32);
}

void VIDEO_SetFrameBuffer(u32 which, void *fb)
{
   switch(which)
   {
      // Framebuffer 1 
      case VIDEO_FRAMEBUFFER_1:
               ((u32*)_viReg)[7] = (0x10000000|(MEM_VIRTUAL_TO_PHYSICAL(fb)>>5));
               break;

      // Framebuffer 2 
      case VIDEO_FRAMEBUFFER_2:
               ((u32*)_viReg)[9] = (0x10000000|(MEM_VIRTUAL_TO_PHYSICAL(fb)>>5));
               break;

      // Set both Framebuffers 
      case VIDEO_FRAMEBUFFER_BOTH:
               ((u32*)_viReg)[7] = (0x10000000|(MEM_VIRTUAL_TO_PHYSICAL(fb)>>5));
               ((u32*)_viReg)[9] = (0x10000000|(MEM_VIRTUAL_TO_PHYSICAL(fb)>>5));
               break;
   }
}

void VIDEO_SetBlack(bool btrue)
{
	if(btrue==TRUE) {
		((vu16*)MEM_VIDEO_BASE)[0] = 0x0006;
		((vu32*)MEM_VIDEO_BASE)[3] = 0x001501E6;
		((vu32*)MEM_VIDEO_BASE)[4] = 0x001401E7;
	} else {
		switch(videoMode) {
			case VIDEO_640X480_NTSC_YUV16:
				((vu16*)MEM_VIDEO_BASE)[0] = 0x0F06;
				((vu32*)MEM_VIDEO_BASE)[3] = 0x00030018;
				((vu32*)MEM_VIDEO_BASE)[4] = 0x00020019;
				break;
			case VIDEO_640X480_PAL50_YUV16:
				((vu16*)MEM_VIDEO_BASE)[0] = 0x11F5;
				((vu32*)MEM_VIDEO_BASE)[3] = 0x00010023;
				((vu32*)MEM_VIDEO_BASE)[4] = 0x00000024;
				break;
			case VIDEO_640X480_PAL60_YUV16:
				((vu16*)MEM_VIDEO_BASE)[0] = 0x0F06;
				((vu32*)MEM_VIDEO_BASE)[3] = 0x00030018;
				((vu32*)MEM_VIDEO_BASE)[4] = 0x00020019;
				break;
		}
	}
}

void VIDEO_WaitVSync(void)
{
	u32 level;
	u32 retcnt;
	
	_CPU_ISR_Disable(level);
	retcnt = retraceCount;
	while(retraceCount==retcnt) {
#ifdef _VIDEO_DEBUG
		printf("VIDEO_WaitVSync(%d,%d)\n",retraceCount,retcnt);
#endif
		LWP_SleepThread(video_queue);
	}
	_CPU_ISR_Restore(level);
}

void VIDEO_SetNextFramebuffer(void *fb)
{
	u32 level;

	_CPU_ISR_Disable(level);
	nextFramebuffer = fb;
	_CPU_ISR_Restore(level);
}

#define CLAMP(x,l,h) ((x>h)?h:((x<l)?l:x))

u32 VIDEO_GetCurrentMode()
{
	u32 mode;
	u32 level;

	_CPU_ISR_Disable(level);
	mode = _SHIFTR(_viReg[1],8,2);
	_CPU_ISR_Restore(level);

	return mode;
}

u32 VIDEO_GetYCbCr(u8 r,u8 g,u8 b)
{
	float Y,Cb,Cr;

	Y  =  0.257 * (float)r + 0.504 * (float)g + 0.098 * (float)b +  16.0 + 0.5;
	Cb = -0.148 * (float)r - 0.291 * (float)g + 0.439 * (float)b + 128.0 + 0.5;
	Cr =  0.439 * (float)r - 0.368 * (float)g - 0.071 * (float)b + 128.0 + 0.5;

	Y  = CLAMP(Y,16,235);
	Cb = CLAMP(Y,16,240);
	Cr = CLAMP(Y,16,240);

	return (u32)((u8)Y<<24|(u8)Cb<<16|(u8)Y<<8|(u8)Cr);
}

VIRetraceCallback VIDEO_SetPreRetraceCallback(VIRetraceCallback callback)
{
	u32 level = 0;
	VIRetraceCallback ret = preRetraceCB;
	_CPU_ISR_Disable(level);
	preRetraceCB = callback;
	_CPU_ISR_Restore(level);
	return ret;
}

VIRetraceCallback VIDEO_SetPostRetraceCallback(VIRetraceCallback callback)
{
	u32 level = 0;
	VIRetraceCallback ret = postRetraceCB;
	_CPU_ISR_Disable(level);
	postRetraceCB = callback;
	_CPU_ISR_Restore(level);
	return ret;
}
