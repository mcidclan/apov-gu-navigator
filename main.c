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
#include <pspdisplay.h>

#define BUFFER_WIDTH 512
#define TEXTURE_SIZE 256
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272

#define COLOR_BYTES_COUNT 4
#define MAX_PROJECTION_DEPTH 800.0f
#define ATOMIC_POV_COUNT 36
#define SPACE_SIZE 256
#define RAY_STEP 8

PSP_MODULE_INFO("APoV", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

typedef struct Vertex {
	u16 u, v;
	u32 color;
	u16 x, y, z;
} Vertex __attribute__((aligned(16)));

static const Vertex __attribute__((aligned(16))) quad[6] = {
    {0, TEXTURE_SIZE, 0xFFFFFFFF, 0, TEXTURE_SIZE, 0},
    {0, 0, 0xFFFFFFFF, 0, 0, 0},
    {TEXTURE_SIZE, 0, 0xFFFFFFFF, TEXTURE_SIZE, 0, 0},
    {TEXTURE_SIZE, 0, 0xFFFFFFFF, TEXTURE_SIZE, 0, 0},
    {TEXTURE_SIZE, TEXTURE_SIZE, 0xFFFFFFFF, TEXTURE_SIZE, TEXTURE_SIZE, 0},
    {0, TEXTURE_SIZE, 0xFFFFFFFF, 0, TEXTURE_SIZE, 0}
};

static const u32 BASE_BYTES_COUNT = TEXTURE_SIZE * SCREEN_HEIGHT * sizeof(u32);

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

// Pre-calculation Processes
static float _FACTORS[255] = {0.0f};

void getProjectionFactors() {
    u8 depth = 255;
    while(depth--) {
        _FACTORS[depth] = 1.0f - ((float)depth * PROJECTION_FACTOR);
    }
}

static u32 _Y_OFFSETS[SPACE_SIZE];

void getYOffsets() {
    u16 y = SPACE_SIZE;
    while(y--) {
        _Y_OFFSETS[y] = y * WIN_WIDTH;
    }
}
//

static void initGuContext(void* list) {
    sceGuStart(GU_DIRECT, list);
    
    sceGuDrawBuffer(GU_PSM_8888, (void*)0, BUFFER_WIDTH);
    sceGuDispBuffer(SCREEN_WIDTH, SCREEN_HEIGHT, (void*)(sizeof(u32) *
    BUFFER_WIDTH * SCREEN_HEIGHT) , BUFFER_WIDTH);
    
    sceGuClearColor(0xFF404040);
    sceGuDisable(GU_SCISSOR_TEST);
    sceGuEnable(GU_CULL_FACE);
    sceGuFrontFace(GU_CW);
    
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexMode(GU_PSM_8888, 0, 1, 0);
    sceGuEnable(GU_TEXTURE_2D);
    
    sceGuTexFilter(GU_NEAREST,GU_NEAREST);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
    
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
            const u32 _frame = frame[x + _Y_OFFSETS[y]];
            const u8 depth = (u8)(_frame & 0x000000FF);
            
            const float s = _FACTORS[depth];
            const int _x = (x - WIN_WIDTH_D2) * s;
            const int _y = (y - WIN_HEIGHT_D2) * s;
        
            if(_x >= -WIN_WIDTH_D2 && _x < WIN_WIDTH_D2 && _y >= -WIN_HEIGHT_D2 && _y < WIN_HEIGHT_D2) {                
                const u32 __x = (_x + WIN_WIDTH_D2 - 2);
                const u32 __y = (_y + WIN_HEIGHT_D2 - 2);
                const u32 offset = __x + _Y_OFFSETS[__y];
                
                u32* const px = &base[offset];
                
                if(_frame && (!*px || (depth < zpos[offset]))) {
                    *px = 0xFF000000 | (_frame & 0xFF000000) >> 24 |
                    (_frame & 0x00FF0000) >> 8 | (_frame & 0x0000FF00) << 8;
                    zpos[offset] = depth;
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

void preCalculate() {
    getProjectionFactors();
    getYOffsets();
}

int main() {
    sceKernelDcacheWritebackInvalidateAll();
    scePowerSetClockFrequency(333, 333, 166);
    SceCtrlData pad;
    
    WIN_WIDTH_D2 = WIN_WIDTH / 2;
    WIN_HEIGHT_D2 = WIN_HEIGHT / 2;
    WIN_PIXELS_COUNT = WIN_WIDTH * WIN_HEIGHT;
    WIN_BYTES_COUNT = WIN_PIXELS_COUNT * COLOR_BYTES_COUNT;
    VIEW_BYTES_COUNT = WIN_PIXELS_COUNT * sizeof(u32);
    SPACE_BYTES_COUNT = (SPACE_SIZE / RAY_STEP) * VIEW_BYTES_COUNT;
    
    const u32 lsize = 330000;
    u32* frame = memalign(16, VIEW_BYTES_COUNT);    
    void* list = memalign(16, lsize);
    
    preCalculate();
    sceGuInit();
    initGuContext(list);
    pspDebugScreenInitEx(NULL, PSP_DISPLAY_PIXEL_FORMAT_8888, 0);
    pspDebugScreenEnableBackColor(0);
    openCloseIo(1);
    
    int dbuff = 0;
    u64 size, prev, now, fps;
    const u64 tickResolution = sceRtcGetTickResolution();

    do {
        sceRtcGetCurrentTick(&prev);
        
        memset(list, 0, lsize);
        sceGuStart(GU_DIRECT, list);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        
        u32* base = (u32*)sceGuGetMemory(VIEW_BYTES_COUNT);
        u8* zpos = (u8*)sceGuGetMemory(WIN_PIXELS_COUNT);
        
        const u64 offset = controls(&pad);
        
        readIo(frame, offset);
        getView(frame, zpos, base);
        
        sceGuTexImage(0, TEXTURE_SIZE, TEXTURE_SIZE, TEXTURE_SIZE, base);
        sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_16BIT|GU_COLOR_8888|
		GU_TRANSFORM_2D|GU_VERTEX_16BIT, 6, 0, quad);
        
        size = sceGuFinish();
        sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
        
        pspDebugScreenSetOffset(dbuff);
        pspDebugScreenSetXY(0, 0);
        pspDebugScreenSetTextColor(0xFF00A0FF);
        pspDebugScreenPrintf("Fps: %llu, List size: %llu bytes.\n", fps, size);
        dbuff = (int)sceGuSwapBuffers();
        
        sceRtcGetCurrentTick(&now);
        fps = tickResolution / (now - prev);
    } while(!(pad.Buttons & PSP_CTRL_SELECT));
    
    free(list);
    free(frame);
    openCloseIo(0);
    sceKernelExitGame();
    return 0;
}
