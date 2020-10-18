/*
 * APoV Project pspgu clut version
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

#define TEXTURE_BLOCK_SIZE 256
#define BUFFER_WIDTH 512
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272

PSP_MODULE_INFO("APoV", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

void sceDmacMemcpy(void *dst, const void *src, int size);

typedef struct Vertex {
	u16 u, v;
	u16 x, y, z;
} Vertex;

#define S TEXTURE_BLOCK_SIZE
#define P (S / 16)
#define T (S / 16)

#define CLUT_COLOR_COUNT 256
static u32 __attribute__((aligned(16))) clut[CLUT_COLOR_COUNT] = {0};
 
static u16 VERTICES_COUNT;
static u16 TEXTURE_WIDTH;
static Vertex* surface;

void generateRenderSurface() {
    const u8 VERTICES_BY_BLOCK = 2;
    VERTICES_COUNT = VERTICES_BY_BLOCK * (TEXTURE_BLOCK_SIZE / P) * (TEXTURE_WIDTH / P);
    surface = memalign(16, sizeof(Vertex) * VERTICES_COUNT);
    const u16 X = (SCREEN_WIDTH - TEXTURE_WIDTH) / 2;
    const u16 Y = (SCREEN_HEIGHT - TEXTURE_BLOCK_SIZE) / 2;
    u16 x = 0;
    u16 offset = 0;
    while(x < TEXTURE_WIDTH) {
        u16 y = 0;
        while(y < TEXTURE_BLOCK_SIZE) {
            const Vertex a = {x,   y,   X+x,   Y+y,   0};
            const Vertex b = {x+T, y+T, X+x+P, Y+y+P, 0};
            surface[offset + 0] = a;
            surface[offset + 1] = b;
            offset += VERTICES_BY_BLOCK;
            y += P;
        }
        x += P;
    }
}

#define SPACE_BLOCK_SIZE 256
static u32 WIDTH_BLOCK_COUNT = 1;
static u32 DEPTH_BLOCK_COUNT = 1;
static u32 RAY_STEP = 1;
static u32 HORIZONTAL_POV_COUNT = 4;
static u32 VERTICAL_POV_COUNT = 1;
static u16 WIN_WIDTH;
static u16 WIN_HEIGHT = SPACE_BLOCK_SIZE;
static u32 WIN_PIXELS_COUNT;
static u32 FRAME_INDICES_COUNT;
static u32 SPACE_INDICES_COUNT;

static void initGuContext(void* list) {
    sceGuStart(GU_DIRECT, list);
    
    sceGuDrawBuffer(GU_PSM_8888, (void*)0, BUFFER_WIDTH);
    sceGuDispBuffer(SCREEN_WIDTH, SCREEN_HEIGHT, (void*)(sizeof(u32) *
    BUFFER_WIDTH * SCREEN_HEIGHT) , BUFFER_WIDTH);
    
    sceGuClearColor(0xFF000000);
    sceGuDisable(GU_SCISSOR_TEST);
    sceGuEnable(GU_CULL_FACE);
    sceGuFrontFace(GU_CW);
    sceGuEnable(GU_CLIP_PLANES);

    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuEnable(GU_TEXTURE_2D);
    
    sceGuClutLoad(CLUT_COLOR_COUNT / 8, clut);
    sceGuClutMode(GU_PSM_8888, 0, CLUT_COLOR_COUNT - 1, 0); 
    sceGuTexMode(GU_PSM_T8, 0, 0, 0);

    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    
    sceGuDisplay(GU_TRUE);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}

static SceUID f;
static void openCloseIo(const u8 open) {
    if(open) {
        f = sceIoOpen("clut-indexes.bin", PSP_O_RDONLY, 0777);
    } else sceIoClose(f);
}


static u8 readIo(u8* const frame, const u64 offset) {
    static u64 loffset = -1;
    if(offset != loffset) {
        sceIoLseek(f, offset, SEEK_SET);
        const u64 nbytes = WIN_PIXELS_COUNT * sizeof(u8);
        if(nbytes != sceIoRead(f, frame, nbytes)) {
            openCloseIo(1);
            return 0;
        }
        loffset = offset;
        return 1;
    }
    return 0;
}

void updateView(u8* const frame, u8* const base) {
    sceKernelDcacheWritebackAll();
    sceDmacMemcpy(base, frame, FRAME_INDICES_COUNT);
}

static int ajustCursor(const int value, const u8 mode) {
    if(!mode) {
        u16 max;
        if(value < 0) {
            return 0;
        } else if(value >= (max = (SPACE_BLOCK_SIZE * DEPTH_BLOCK_COUNT) / RAY_STEP)) {
            return max - 1;
        }
    } else if(mode == 1) {   
        if(value < 0) {
            return HORIZONTAL_POV_COUNT - 1;
        } else if(value >= HORIZONTAL_POV_COUNT) {
            return 0;
        }
    } else if(mode == 2) {   
        if(value < 0) {
            return VERTICAL_POV_COUNT - 1;
        } else if(value >= VERTICAL_POV_COUNT) {
            return 0;
        }
    }
    return value;
}

static u64 getOffset(const int move, const int hrotate, const int vrotate) {
    return FRAME_INDICES_COUNT * move + (hrotate * VERTICAL_POV_COUNT + vrotate) * SPACE_INDICES_COUNT;
}

SceCtrlData pad;
static u64 controls() {
    static int move = 0;
    static int hrotate = 0;
    static int vrotate = 0;
    
    sceCtrlReadBufferPositive(&pad, 1);
    
    if(pad.Buttons & PSP_CTRL_TRIANGLE) { move++; }
    if(pad.Buttons & PSP_CTRL_CROSS) { move--; }
    if(pad.Buttons & PSP_CTRL_LEFT) { hrotate--; }
    if(pad.Buttons & PSP_CTRL_RIGHT) { hrotate++; }
    if(pad.Buttons & PSP_CTRL_DOWN) { vrotate--; }
    if(pad.Buttons & PSP_CTRL_UP) { vrotate++; }
    
    move = ajustCursor(move, 0);
    hrotate = ajustCursor(hrotate, 1);
    vrotate = ajustCursor(vrotate, 2);
    
    return getOffset(move, hrotate, vrotate);
}

u8 getPower(u16 value) {
    u8 power = 0;
    while(value > 1) {
        if((value & 1) != 0) {
            return 0;
        }
        power++;
        value >>= 1;
    }
    return power;
}
    
void getOptions() {
    FILE* f = fopen("options.txt", "r");
    if(f != NULL) {
        char* options = (char*)memalign(16, 128);
        fgets(options, 128, f);
        sscanf(options, "HPOV:%u VPOV:%u RAYSTEP:%u WBCOUNT:%u DBCOUNT:%u",
            &HORIZONTAL_POV_COUNT,
            &VERTICAL_POV_COUNT,
            &RAY_STEP,
            &WIDTH_BLOCK_COUNT,
            &DEPTH_BLOCK_COUNT);
        fclose(f);
        free(options);
    }
    
    f = fopen("clut.bin", "rb");
    if(f != NULL) {
        fread(clut, sizeof(u32), CLUT_COLOR_COUNT, f);
        fclose(f);
    }
}

int main() {
    scePowerSetClockFrequency(333, 333, 166);    
    getOptions();
    
    WIN_WIDTH = SPACE_BLOCK_SIZE * WIDTH_BLOCK_COUNT;
    WIN_PIXELS_COUNT = WIN_WIDTH * WIN_HEIGHT;
    FRAME_INDICES_COUNT = WIN_PIXELS_COUNT * sizeof(u8);
    SPACE_INDICES_COUNT = ((DEPTH_BLOCK_COUNT * SPACE_BLOCK_SIZE) / RAY_STEP) * FRAME_INDICES_COUNT;    
    TEXTURE_WIDTH = TEXTURE_BLOCK_SIZE * WIDTH_BLOCK_COUNT;

    u8* base = memalign(16, FRAME_INDICES_COUNT);
    u8* frame = memalign(16, FRAME_INDICES_COUNT);
    void* list = memalign(16, 262144);
    
    generateRenderSurface();
    
    sceGuInit();
    initGuContext(list);
    pspDebugScreenInitEx(NULL, PSP_DISPLAY_PIXEL_FORMAT_8888, 0);
    pspDebugScreenEnableBackColor(0);
    
    openCloseIo(1);
    
    int dbuff = 0;
    u64 prev, now, fps;
    const u64 tickResolution = sceRtcGetTickResolution();

    do {
        sceRtcGetCurrentTick(&prev);
        
        sceGuStart(GU_DIRECT, list);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        
        if(readIo(frame, controls())) {
            updateView(frame, base);
        }
        
        sceGuTexImage(0, TEXTURE_WIDTH, TEXTURE_BLOCK_SIZE, TEXTURE_WIDTH, base);        
        sceGumDrawArray(GU_SPRITES, GU_TEXTURE_16BIT|GU_TRANSFORM_2D|GU_VERTEX_16BIT,
        VERTICES_COUNT, 0, surface);
        
        sceGuFinish();
        sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
        
        pspDebugScreenSetOffset(dbuff);
        pspDebugScreenSetXY(0, 0);
        pspDebugScreenSetTextColor(0xFF00A0FF);
        pspDebugScreenPrintf("Fps: %llu\n", fps);
        
        sceDisplayWaitVblankStart(); 
        dbuff = (int)sceGuSwapBuffers();
        
        sceRtcGetCurrentTick(&now);
        fps = tickResolution / (now - prev);
    } while(!(pad.Buttons & PSP_CTRL_SELECT));
    
    sceGuTerm();
    free(surface);
    free(list);
    free(base);
    free(frame);
    
    openCloseIo(0);
    sceKernelExitGame();
    return 0;
}
