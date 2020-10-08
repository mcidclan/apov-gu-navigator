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

void sceDmacMemcpy(void *dst, const void *src, int size);

typedef struct Vertex {
	u16 u, v;
	u32 color;
	u16 x, y, z;
} Vertex __attribute__((aligned(16)));

#define S TEXTURE_SIZE / 2
#define T TEXTURE_SIZE / 2
#define X 112
#define Y 8
static const Vertex __attribute__((aligned(16))) quad[24] = {
    {0, T, 0xFFFFFFFF, X+0, Y+S, 0},
    {0, 0, 0xFFFFFFFF, X+0, Y+0, 0},
    {T, 0, 0xFFFFFFFF, X+S, Y+0, 0},
    {T, 0, 0xFFFFFFFF, X+S, Y+0, 0},
    {T, T, 0xFFFFFFFF, X+S, Y+S, 0},
    {0, T, 0xFFFFFFFF, X+0, Y+S, 0},
    //
    {T,   T,   0xFFFFFFFF, X+S,   Y+S, 0},
    {T,   0,   0xFFFFFFFF, X+S,   Y+0, 0},
    {T+T, 0,   0xFFFFFFFF, X+S+S, Y+0, 0},
    {T+T, 0,   0xFFFFFFFF, X+S+S, Y+0, 0},
    {T+T, T,   0xFFFFFFFF, X+S+S, Y+S, 0},
    {T,   T,   0xFFFFFFFF, X+S,   Y+S, 0},
    //
    {0, T+T, 0xFFFFFFFF, X+0, Y+S+S, 0},
    {0, T,   0xFFFFFFFF, X+0, Y+S,   0},
    {T, T,   0xFFFFFFFF, X+S, Y+S,   0},
    {T, T,   0xFFFFFFFF, X+S, Y+S,   0},
    {T, T+T, 0xFFFFFFFF, X+S, Y+S+S, 0},
    {0, T+T, 0xFFFFFFFF, X+0, Y+S+S, 0},
    //
    {T,   T+T, 0xFFFFFFFF, X+S,   Y+S+S, 0},
    {T,   T,   0xFFFFFFFF, X+S,   Y+S,   0},
    {T+T, T,   0xFFFFFFFF, X+S+S, Y+S,   0},
    {T+T, T,   0xFFFFFFFF, X+S+S, Y+S,   0},
    {T+T, T+T, 0xFFFFFFFF, X+S+S, Y+S+S, 0},
    {T,   T+T, 0xFFFFFFFF, X+S,   Y+S+S, 0},
};

static u8 DEPTH_OF_FIELD = 0;

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
    
    sceGuClearColor(0xFF000000);
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
        u32 i = WIN_PIXELS_COUNT;
        while(i--) {
            const u32 _frame = frame[i];
            const u8 depth = (u8)(_frame & 0x000000FF);
            if(_frame) {
                const float s = _FACTORS[depth];
                const int _x = _COORDINATES[i].x * s;
                const int _y = _COORDINATES[i].y * s;
            
                if(_x >= -WIN_WIDTH_D2 && _x < WIN_WIDTH_D2 && _y >= -WIN_HEIGHT_D2 && _y < WIN_HEIGHT_D2) {
                    const u16 __x = (_x + WIN_WIDTH_D2 - 2);
                    const u16 __y = (_y + WIN_HEIGHT_D2 - 2);
                    const u32 offset = __x | (__y << 8);                    
                    u32* const px = &base[offset];
                    
                    if(_frame && (!*px || (depth < zpos[offset]))) {
                        *px = 0xFF000000 | _frame;
                        zpos[offset] = depth;
                    }
                }
            }
        }
    } else {
        if(DEPTH_OF_FIELD) {
            memset(base, 0, VIEW_BYTES_COUNT);
            u32 x = WIN_WIDTH - 3;
            while(--x > 2) {
                u32 y = WIN_HEIGHT - 3;
                while(--y > 2) {
                        u32 const o = frame[x | y << 8];
                        u32 const a = frame[(x + 3) | y << 8];
                        u32 const b = frame[(x - 3) | y << 8];
                        u32 const c = frame[x | (y + 3) << 8];
                        u32 const d = frame[x | (y - 3) << 8];
                        
                        u32 const e = frame[(x + 2) | (y + 2) << 8];
                        u32 const f = frame[(x - 2) | (y + 2) << 8];
                        u32 const g = frame[(x + 2) | (y + 2) << 8];
                        u32 const h = frame[(x - 2) | (y - 2) << 8];
                            
                    if(o || a || b || c || d || e || f || g || h) {
                        
                        u8 n =
                            (a ? 1 : 0) +
                            (b ? 1 : 0) +
                            (c ? 1 : 0) +
                            (d ? 1 : 0) +
                            (e ? 1 : 0) +
                            (f ? 1 : 0) +
                            (g ? 1 : 0) +
                            (h ? 1 : 0);
                        
                        const int dd = n ? ((
                            ((a & 0xFF000000) >> 24) +
                            ((b & 0xFF000000) >> 24) +
                            ((c & 0xFF000000) >> 24) +
                            ((d & 0xFF000000) >> 24) +
                            ((e & 0xFF000000) >> 24) +
                            ((f & 0xFF000000) >> 24) +
                            ((g & 0xFF000000) >> 24) +
                            ((h & 0xFF000000) >> 24)
                        ) / n) - ((o & 0xFF000000) >> 24) : 0;
                        
                        if(dd >= -20 && dd <= 20) {                            
                            const u8 _R = (
                                (o & 0x000000FF) + (a & 0x000000FF) + (b & 0x000000FF) + (c & 0x000000FF) + (d & 0x000000FF) +
                                (e & 0x000000FF) + (f & 0x000000FF) + (g & 0x000000FF) + (h & 0x000000FF)) / 9;
                                
                            const u8 _G = (
                                ((o & 0x0000FF00) >> 8) + ((a & 0x0000FF00) >> 8) + ((b & 0x0000FF00) >> 8) + ((c & 0x0000FF00) >> 8) + ((d & 0x0000FF00) >> 8) +
                                ((e & 0x0000FF00) >> 8) + ((f & 0x0000FF00) >> 8) + ((g & 0x0000FF00) >> 8) + ((h & 0x0000FF00) >> 8)) / 9;
                                
                            const u8 _B = (
                                ((o & 0x00FF0000) >> 16) + ((a & 0x00FF0000) >> 16) + ((b & 0x00FF0000) >> 16) + ((c & 0x00FF0000) >> 16) + ((d & 0x00FF0000) >> 16) +
                                ((e & 0x00FF0000) >> 16) + ((f & 0x00FF0000) >> 16) + ((g & 0x00FF0000) >> 16) + ((h & 0x00FF0000) >> 16)) / 9;
                        
                            const float maxdof = 127.0f;
                            float depth = ((o & 0xFF000000) >> 24);
                            if(depth > maxdof) { depth = maxdof; }
                            
                            const float m = (maxdof - depth)/maxdof;                            
                            const u8 R = m * (o & 0x000000FF) +  (1 - m) * _R;
                            const u8 G = m * ((o & 0x0000FF00) >> 8) + (1 - m) * _G;
                            const u8 B = m * ((o & 0x00FF0000) >> 16) + (1 - m) * _B;
                            
                            base[x | y << 8] = 0xFF000000 | (B << 16) | (G << 8) | R;
                        } else base[x | y << 8] = 0xFF000000 | o;
                    }
                }
            }
        } else {
            sceKernelDcacheWritebackAll();
            sceDmacMemcpy(base, frame, WIN_BYTES_COUNT);
        }
    }
}

static int ajustCursor(const int value, const u8 mode) {
    if(!mode) {
        u16 max;
        if(value < 0) {
            return 0;
        } else if(value >= (max = SPACE_SIZE / RAY_STEP)) {
            return max - 1;
        }
    } else {    
        if(value < 0) {
            return ATOMIC_POV_COUNT - 1;
        } else if(value >= ATOMIC_POV_COUNT) {
            return 0;
        }
    }
    return value;
}

static u64 getOffset(const int move, const int rotate) {
    return VIEW_BYTES_COUNT * move + SPACE_BYTES_COUNT * rotate;
}

SceCtrlData pad;
static u64 controls() {
    static int move = 0;
    static int rotate = 0;
    static SceCtrlData lpad;
    
    sceCtrlReadBufferPositive(&pad, 1);
    
    if(pad.Buttons & PSP_CTRL_UP) { move++; }
    if(pad.Buttons & PSP_CTRL_DOWN) { move--; }
    if(pad.Buttons & PSP_CTRL_LEFT) { rotate--; }
    if(pad.Buttons & PSP_CTRL_RIGHT) { rotate++; }
    
    move = ajustCursor(move, 0);
    rotate = ajustCursor(rotate, 1);
    
    if((pad.Buttons & PSP_CTRL_TRIANGLE) &&
        !(lpad.Buttons & PSP_CTRL_TRIANGLE)) {
        DEPTH_OF_FIELD = !DEPTH_OF_FIELD;
    }
    
    lpad = pad;
    return getOffset(move, rotate);
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

static u64 currentOffset = 0;
static int keys(unsigned int args, void *argp) {
    do {
        if(DEPTH_OF_FIELD) {
            currentOffset = controls();
        }
        sceKernelDelayThread(33333);
    } while(!(pad.Buttons & PSP_CTRL_SELECT));
    return 0;
}

int main() {
    scePowerSetClockFrequency(333, 333, 166);    
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
    
    u8* zpos = memalign(16, WIN_PIXELS_COUNT);
    u32* base = memalign(16, VIEW_BYTES_COUNT);
    u32* frame = memalign(16, VIEW_BYTES_COUNT);
    
    void* list = memalign(16, 256);
    
    if(MAX_PROJECTION_DEPTH > 0.0f) {
        preCalculate();
    }
    sceGuInit();
    initGuContext(list);
    
    pspDebugScreenInitEx(NULL, PSP_DISPLAY_PIXEL_FORMAT_8888, 0);
    pspDebugScreenEnableBackColor(0);
    
    SceUID kid = sceKernelCreateThread("apov_keys", keys, 0x10, 0x1000, 0, 0);
    if (kid >= 0){
        sceKernelStartThread(kid, 0, 0);
    }

    openCloseIo(1);
    
    int dbuff = 0;
    u64 size, prev, now, fps;
    const u64 tickResolution = sceRtcGetTickResolution();

    do {
        sceRtcGetCurrentTick(&prev);
        
        if(MAX_PROJECTION_DEPTH > 0.0f) {
            memset(zpos, 0, WIN_PIXELS_COUNT);
        }
        
        sceGuStart(GU_DIRECT, list);
        sceGuClear(GU_COLOR_BUFFER_BIT);

        if(!DEPTH_OF_FIELD) {
            currentOffset = controls();
        }
        
        readIo(frame, currentOffset);
        getView(frame, zpos, base);
        
        sceGuTexImage(0, TEXTURE_SIZE, TEXTURE_SIZE, TEXTURE_SIZE, base);
        sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_16BIT|GU_COLOR_8888|
		GU_TRANSFORM_2D|GU_VERTEX_16BIT, 24, 0, quad);
        
        size = sceGuFinish();
        sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
        
        pspDebugScreenSetOffset(dbuff);
        pspDebugScreenSetXY(0, 0);
        pspDebugScreenSetTextColor(0xFF00A0FF);
        pspDebugScreenPrintf("Fps: %llu, DOF: %s\n", fps, DEPTH_OF_FIELD ? "on" : "off");
        pspDebugScreenPrintf("List size: %llu bytes.\n", size);
        
        dbuff = (int)sceGuSwapBuffers();
        
        sceRtcGetCurrentTick(&now);
        fps = tickResolution / (now - prev);
    } while(!(pad.Buttons & PSP_CTRL_SELECT));
    
    free(list);
    free(zpos);
    free(base);
    free(frame);
    free(_FACTORS);
    free(_COORDINATES);
    
    openCloseIo(0);
    sceKernelExitGame();
    return 0;
}
