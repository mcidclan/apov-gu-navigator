/*
 * APoV Project
 * By m-c/d in 2020
 */

#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <pspctrl.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <psprtc.h>
#include <psppower.h>

#define BUFFER_WIDTH 512
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272

#define COLOR_BYTES_COUNT 4
#define MAX_PROJECTION_DEPTH 300.0f
#define ATOMIC_POV_COUNT 36
#define SPACE_SIZE 256
#define RAY_STEP 8

PSP_MODULE_INFO("APoV", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(-1024);

typedef struct Vertex {
	u16 u, v;
	u32 color;
	u16 x, y, z;
} Vertex __attribute__((aligned(16)));

Vertex __attribute__((aligned(16))) quad[2] = {
	{0, 0, 0xFFFFFFFF, 0, 0, 0},
	{512, 272, 0xFFFFFFFF, 512, 272, 0},
};

static const u32 BASE_BYTES_COUNT = BUFFER_WIDTH * SCREEN_HEIGHT * sizeof(u32);

static float PROJECTION_FACTOR = 1.0f / MAX_PROJECTION_DEPTH;
static const float ATOMIC_POV_STEP = 360.0f / ATOMIC_POV_COUNT;
static const u16 WIN_WIDTH = SPACE_SIZE;
static const u16 WIN_HEIGHT = SPACE_SIZE;

static u16 WIN_WIDTH_D2;
static u16 WIN_HEIGHT_D2;
static u32 WIN_PIXELS_COUNT;
static u32 WIN_BYTES_COUNT;
static u32 VIEW_BYTES_COUNT;
static u32 SPACE_BYTES_COUNT;

static void initGuContext(void* list) {
    sceGuStart(GU_DIRECT, list);
    
    sceGuDrawBuffer(GU_PSM_8888, (void*)0, BUFFER_WIDTH);
    sceGuDispBuffer(SCREEN_WIDTH, SCREEN_HEIGHT, (void*)(sizeof(u32) *
    BUFFER_WIDTH * SCREEN_HEIGHT) , BUFFER_WIDTH);
    
    sceGuClearColor(0xFF404040);
    sceGuDisable(GU_SCISSOR_TEST);    
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexMode(GU_PSM_8888, 0, 1, 0);
    sceGuEnable(GU_TEXTURE_2D);
    
    sceGuEnable(GU_BLEND);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    
    sceGuDisplay(GU_TRUE);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}

static SceUID f;
static void openCloseIo(const u8 open) {
    if(open) {
        f = sceIoOpen("atoms.bin", PSP_O_RDONLY, 0777);
    } else sceIoClose(f);
}

static void readIo(u32* const frame, const u64 offset) {
    static u64 loffset = -1;
    if(offset != loffset) {
        sceIoLseek(f, offset, SEEK_SET);
        sceIoRead(f, frame, WIN_PIXELS_COUNT * sizeof(u32));
        loffset = offset;
    }
}

void getView(u32* const frame, u8* const zpos, u32* const base) {
    int x = WIN_WIDTH;
    while(x--) {
        int y = WIN_HEIGHT;
        while(y--) {    
            const u32 step = x + y * WIN_WIDTH;
            
            const u32 _frame = frame[step];
            const u8 depth = (u8)(_frame & 0x000000FF);
            const float s = 1.0f - ((float)depth * PROJECTION_FACTOR);
            const int _x = (x - WIN_WIDTH_D2) * s;
            const int _y = (y - WIN_HEIGHT_D2) * s;
            
            if(_x >= -WIN_WIDTH_D2 && _x < WIN_WIDTH_D2 && _y >= -WIN_HEIGHT_D2 && _y < WIN_HEIGHT_D2) {
                const u32 __x = (_x + WIN_WIDTH_D2);
                const u32 __y = (_y + WIN_HEIGHT_D2);
                const u32 pstep = __x + __y * WIN_WIDTH;
                const u32 poffset = __x + __y * BUFFER_WIDTH;
                
                u32* const px = &base[poffset];
                
                if(_frame && (!*px || (depth < zpos[pstep]))) {
                    *px = 0xFF000000 | (_frame & 0xFF000000) >> 24 |
                    (_frame & 0x00FF0000) >> 8 | (_frame & 0x0000FF00) << 8;
                    zpos[pstep] = depth;
                }
            }
        }
    }
}

static u64 controls(SceCtrlData* const pad) {
    static int move = 0;
    static int rotate = 0;

    sceCtrlReadBufferPositive(pad, 1);
    if(pad->Buttons & PSP_CTRL_LEFT) { rotate--; }
    if(pad->Buttons & PSP_CTRL_RIGHT) { rotate++; }
    if(rotate < 0) {
        rotate = ATOMIC_POV_COUNT - 1;
    } else if(rotate >= ATOMIC_POV_COUNT) {
        rotate = 0;
    }
        
    if(pad->Buttons & PSP_CTRL_UP) {
        if(move < (SPACE_SIZE / RAY_STEP)) { move++; }
    }
    if(pad->Buttons & PSP_CTRL_DOWN) {
        if(move > 0) { move--; }
    }
    return VIEW_BYTES_COUNT * move + SPACE_BYTES_COUNT * rotate;
}

int main() {
    scePowerSetClockFrequency(333, 333, 166);
    SceCtrlData pad;
    
    WIN_WIDTH_D2 = WIN_WIDTH / 2;
    WIN_HEIGHT_D2 = WIN_HEIGHT / 2;
    WIN_PIXELS_COUNT = WIN_WIDTH * WIN_HEIGHT;
    WIN_BYTES_COUNT = WIN_PIXELS_COUNT * COLOR_BYTES_COUNT;
    VIEW_BYTES_COUNT = WIN_PIXELS_COUNT * sizeof(u32);
    SPACE_BYTES_COUNT = (SPACE_SIZE / RAY_STEP) * VIEW_BYTES_COUNT;
    
    const u32 lsize = 630000;
    u32* frame = memalign(16, VIEW_BYTES_COUNT);
    void* list = memalign(16, lsize);
    
    sceGuInit();
    pspDebugScreenInit();
    initGuContext(list);
    openCloseIo(1);
    u64 size = 0;
    
    u64 prev, now, fps;
    const u64 tickResolution = sceRtcGetTickResolution();
    do {
        sceRtcGetCurrentTick(&prev);
        
        memset(list, 0, lsize);
        sceGuStart(GU_DIRECT, list);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        
        u32* base = (u32*)sceGuGetMemory(BASE_BYTES_COUNT);
        u8* zpos = (u8*)sceGuGetMemory(WIN_PIXELS_COUNT);
        
        // Fills the texture with debug
        pspDebugScreenSetBase(base);
        pspDebugScreenSetXY(0,0);
        pspDebugScreenSetTextColor(0xFF00A0FF);
        pspDebugScreenPrintf("Fps: %llu, List size: %llu bytes.\n", fps, size);
        
        const u64 offset = controls(&pad);
        
        readIo(frame, offset);
        getView(frame, zpos, base);
        
        sceGuTexImage(0, BUFFER_WIDTH, SCREEN_HEIGHT, BUFFER_WIDTH, base);
        sceGumDrawArray(GU_SPRITES, GU_TEXTURE_16BIT|GU_COLOR_8888|
		GU_TRANSFORM_2D|GU_VERTEX_16BIT, 2, 0, quad);
        
        size = sceGuFinish();
        sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
        sceGuSwapBuffers();
       
        sceRtcGetCurrentTick(&now); 
        fps = tickResolution / (now - prev);
    } while(!(pad.Buttons & PSP_CTRL_SELECT));
    
    free(list);
    free(frame);
    openCloseIo(0);
    sceKernelExitGame();
    return 0;
}
