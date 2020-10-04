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

#define SPACE_SIZE 256
#define COLOR_BYTES_COUNT 4

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

static u32 RAY_STEP = 1;
static u32 ATOMIC_POV_COUNT = 4;
static float MAX_PROJECTION_DEPTH = 0.0f;

static const u32 BASE_BYTES_COUNT = TEXTURE_SIZE * SCREEN_HEIGHT * sizeof(u32);

static float PROJECTION_FACTOR;
static const u16 WIN_WIDTH = SPACE_SIZE;
static const u16 WIN_HEIGHT = SPACE_SIZE;

static u16 WIN_WIDTH_D2;
static u16 WIN_HEIGHT_D2;
static u32 WIN_PIXELS_COUNT;
static u32 WIN_BYTES_COUNT;
static u32 VIEW_BYTES_COUNT;
static u32 SPACE_BYTES_COUNT;

// Pre-calculation Processes
typedef struct {
    int x, y;
} Coords __attribute__((aligned(16)));

static float* _FACTORS;
static Coords* _COORDINATES;

void preCalculate() {
    _FACTORS = memalign(16, 256 * sizeof(float));
    _COORDINATES = memalign(16, WIN_PIXELS_COUNT * sizeof(Coords));
    
    u16 depth = 256;
    while(depth--) {
        _FACTORS[depth] = 1.0f - ((float)depth * PROJECTION_FACTOR);
    }
    
    u16 ux = WIN_WIDTH;
    while(ux--) {
        u16 uy = WIN_HEIGHT;
        while(uy--) {
            const u32 i = ux + uy * WIN_WIDTH;
            _COORDINATES[i].x = ux - WIN_WIDTH_D2;
            _COORDINATES[i].y = uy - WIN_HEIGHT_D2;
        }
    }
}

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
        f = sceIoOpen("atoms-psp.bin", PSP_O_RDONLY, 0777);
    } else sceIoClose(f);
}

static void readIo(u32* const frame, const u64 offset) {
    static u64 loffset = -1;
    if(offset != loffset) {
        sceIoLseek(f, offset, SEEK_SET);
        const u64 nbytes = WIN_PIXELS_COUNT * sizeof(u32);
        if(nbytes != sceIoRead(f, frame, nbytes)) {
            openCloseIo(1);
        }
        loffset = offset;
    }
}

void getView(u32* const frame, u8* const zpos, u32* const base) {
    if(MAX_PROJECTION_DEPTH > 0.0f) {
        int i = WIN_PIXELS_COUNT;
        while(i--) {
            const u32 _frame = frame[i];
            const u8 depth = (u8)(_frame & 0x000000FF);
            if(_frame) {
                const float s = _FACTORS[depth];
                const int _x = _COORDINATES[i].x * s;
                const int _y = _COORDINATES[i].y * s;
            
                if(_x >= -WIN_WIDTH_D2 && _x < WIN_WIDTH_D2 && _y >= -WIN_HEIGHT_D2 && _y < WIN_HEIGHT_D2) {
                    const u16 __x = (_x + WIN_WIDTH_D2 - 1);
                    const u16 __y = (_y + WIN_HEIGHT_D2 - 1);
                    const u32 offset = __x | (__y << 8);                    
                    u32* const px = &base[offset];
                    
                    if(_frame && (!*px || (depth < zpos[offset]))) {
                        *px = 0xFF000000 | (_frame & 0xFF000000) >> 24 |
                        (_frame & 0x00FF0000) >> 8 | (_frame & 0x0000FF00) << 8;
                        zpos[offset] = depth;
                    }
                }
            }
        }
    } else {
        u32 i = WIN_PIXELS_COUNT;
        while(i--) {
            const u32 _frame = frame[i];
            if(_frame) {
                base[i] = 0xFF000000 | (_frame & 0xFF000000) >> 24 |
                    (_frame & 0x00FF0000) >> 8 | (_frame & 0x0000FF00) << 8;
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
        if(move < (SPACE_SIZE / RAY_STEP) - 1) { move++; }
    }
    if(pad->Buttons & PSP_CTRL_DOWN) {
        if(move > 0) { move--; }
    }
    return VIEW_BYTES_COUNT * move + SPACE_BYTES_COUNT * rotate;
}

void getOptions() {
    FILE* f = fopen("options.txt", "r");
    if(f != NULL) {
        char* options = (char*)memalign(16, 32);
        fgets(options, 32, f);
        sscanf(options, "%f %u %u",
        &MAX_PROJECTION_DEPTH, &ATOMIC_POV_COUNT, &RAY_STEP);
        fclose(f);
    }
}

int main() {
    scePowerSetClockFrequency(333, 333, 166);
    SceCtrlData pad;
    
    getOptions();
    if(MAX_PROJECTION_DEPTH > 0.0f) {
        PROJECTION_FACTOR = 1.0f / MAX_PROJECTION_DEPTH;  
    }
    WIN_WIDTH_D2 = WIN_WIDTH / 2;
    WIN_HEIGHT_D2 = WIN_HEIGHT / 2;
    WIN_PIXELS_COUNT = WIN_WIDTH * WIN_HEIGHT;
    WIN_BYTES_COUNT = WIN_PIXELS_COUNT * COLOR_BYTES_COUNT;
    VIEW_BYTES_COUNT = WIN_PIXELS_COUNT * sizeof(u32);
    SPACE_BYTES_COUNT = (SPACE_SIZE / RAY_STEP) * VIEW_BYTES_COUNT;
    
    const u32 lsize = 330000;
    u32* frame = memalign(16, VIEW_BYTES_COUNT);    
    void* list = memalign(16, lsize);
    
    if(MAX_PROJECTION_DEPTH > 0.0f) {
        preCalculate();
    }
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
    free(_FACTORS);
    free(_COORDINATES);
    
    openCloseIo(0);
    sceKernelExitGame();
    return 0;
}
