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
	u32 color;
	u16 x, y, z;
} Vertex __attribute__((aligned(16)));

#define S TEXTURE_BLOCK_SIZE
#define P (S / 8)
#define T (S / 8)

static u16 VERTICES_COUNT;
static u16 TEXTURE_WIDTH;
static Vertex* quad;

void generateRenderSurface() {
    VERTICES_COUNT = 6 * (TEXTURE_BLOCK_SIZE / P) * (TEXTURE_WIDTH / P);
    quad = memalign(16, sizeof(Vertex) * VERTICES_COUNT);
    const u16 X = (SCREEN_WIDTH - TEXTURE_WIDTH) / 2;
    const u16 Y = (SCREEN_HEIGHT - TEXTURE_BLOCK_SIZE) / 2;
    u16 x = 0;
    u16 offset = 0;
    while(x < TEXTURE_WIDTH) {
        u16 y = 0;
        while(y < TEXTURE_BLOCK_SIZE) {
            const Vertex a = {x,   y+T, 0xFFFFFFFF, X+x,   Y+y+P, 0};
            const Vertex b = {x,   y,   0xFFFFFFFF, X+x,   Y+y,   0};
            const Vertex c = {x+T, y,   0xFFFFFFFF, X+x+P, Y+y,   0};
            const Vertex d = {x+T, y,   0xFFFFFFFF, X+x+P, Y+y,   0};
            const Vertex e = {x+T, y+T, 0xFFFFFFFF, X+x+P, Y+y+P, 0};
            const Vertex f = {x,   y+T, 0xFFFFFFFF, X+x,   Y+y+P, 0};
            
            quad[offset + 0] = a;
            quad[offset + 1] = b;
            quad[offset + 2] = c;
            quad[offset + 3] = d;
            quad[offset + 4] = e;
            quad[offset + 5] = f;
            
            offset += 6;
            y += P;
        }
        x += P;
    }
}

#define SPACE_BLOCK_SIZE 256
static u8 DEPTH_OF_FIELD = 0;
static u32 WIDTH_BLOCK_COUNT = 1;
static u32 DEPTH_BLOCK_COUNT = 1;
static u32 RAY_STEP = 1;
static u32 ATOMIC_POV_COUNT = 4;
static float MAX_PROJECTION_DEPTH = 0.0f;
static float PROJECTION_FACTOR;
static u8 SPACE_Y_OFFSET;
static u16 WIN_WIDTH;
static u16 WIN_HEIGHT = SPACE_BLOCK_SIZE;
static u16 WIN_WIDTH_D2;
static u16 WIN_HEIGHT_D2;
static u32 WIN_PIXELS_COUNT;
static u32 FRAME_BYTES_COUNT;
static u32 SPACE_BYTES_COUNT;

// Pre-calculation Processes
typedef struct {
    int x, y;
} Coords __attribute__((aligned(16)));


static float* _FACTORS;
static Coords* _COORDINATES;

#define DOF_MATRIX_UNIT_COUNT 9
typedef struct {
    u32 *o, *a, *b, *c, *d, *e, *f, *g, *h;
} DofMatRef __attribute__((aligned(16)));

static u32* frame;
static float* _DOF;
static DofMatRef* _DOF_MATRIX_REFS;

void preCalcDof() {    
    _DOF_MATRIX_REFS = memalign(16, WIN_PIXELS_COUNT * sizeof(DofMatRef));
    
    const u8 size = 3;
    u32 x = WIN_WIDTH;
    while(x--) {
        u32 y = WIN_HEIGHT;
        while(y--) {
            int xr = x + size >= WIN_WIDTH ? 0 : size;
            int xl = x - size < 0 ? 0 : -size;
            int yd = y + size >= WIN_HEIGHT ? 0 : size;
            int yu = y - size < 0 ? 0 : -size;
            DofMatRef* const m = &_DOF_MATRIX_REFS[x | y << SPACE_Y_OFFSET];
            m->o = &frame[x | y << SPACE_Y_OFFSET];
            m->a = &frame[(x + xr - 1) | y << SPACE_Y_OFFSET];
            m->b = &frame[(x + xl + 1) | y << SPACE_Y_OFFSET];
            m->c = &frame[x | (y + yd - 1) << SPACE_Y_OFFSET];
            m->d = &frame[x | (y + yu + 1) << SPACE_Y_OFFSET];
            m->e = &frame[(x + xr) | (y + yd) << SPACE_Y_OFFSET];
            m->f = &frame[(x + xl) | (y + yd) << SPACE_Y_OFFSET];
            m->g = &frame[(x + xr) | (y + yu) << SPACE_Y_OFFSET];
            m->h = &frame[(x + xl) | (y + yu) << SPACE_Y_OFFSET];
        }
    }
    
    _DOF = memalign(16, 256 * sizeof(float));
    u16 depth = 256;
    const float maxdof = 127.0f;
    while(depth--) {
        if(depth >= maxdof) {
            _DOF[depth] = 0;
        } else {
            _DOF[depth] = (maxdof - depth)/maxdof;
        }
    }
}
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
            memset(base, 0, FRAME_BYTES_COUNT);
            u32 i = WIN_PIXELS_COUNT;
            while(--i) {
                DofMatRef* const m = &_DOF_MATRIX_REFS[i];
                u32 const o = *m->o;
                u32 const a = *m->a;
                u32 const b = *m->b;
                u32 const c = *m->c;
                u32 const d = *m->d;
                u32 const e = *m->e;
                u32 const f = *m->f;
                u32 const g = *m->g;
                u32 const h = *m->h;
                
                if(o || a || b || c || d || e || f || g || h) {
                    const u8 n =
                        (a ? 1 : 0) + (b ? 1 : 0) + (c ? 1 : 0) +
                        (d ? 1 : 0) + (e ? 1 : 0) + (f ? 1 : 0) +
                        (g ? 1 : 0) + (h ? 1 : 0);
                    
                    const int dd = n ? ((
                        (a >> 24) + (b >> 24) + (c >> 24) +
                        (d >> 24) + (e >> 24) + (f >> 24) +
                        (g >> 24) + (h >> 24)
                    ) / n) - (o >> 24) : 0;
                    
                    if(dd >= -10 && dd <= 10) {
                        const u8 _R = (
                            (o & 0x000000FF) + (a & 0x000000FF) + (b & 0x000000FF) +
                            (c & 0x000000FF) + (d & 0x000000FF) + (e & 0x000000FF) +
                            (f & 0x000000FF) + (g & 0x000000FF) + (h & 0x000000FF)) / 9;
                        const u8 _G = (
                            ((o & 0x0000FF00) >> 8) + ((a & 0x0000FF00) >> 8) + ((b & 0x0000FF00) >> 8) +
                            ((c & 0x0000FF00) >> 8) + ((d & 0x0000FF00) >> 8) + ((e & 0x0000FF00) >> 8) +
                            ((f & 0x0000FF00) >> 8) + ((g & 0x0000FF00) >> 8) + ((h & 0x0000FF00) >> 8)) / 9;
                        const u8 _B = (
                            ((o & 0x00FF0000) >> 16) + ((a & 0x00FF0000) >> 16) + ((b & 0x00FF0000) >> 16) +
                            ((c & 0x00FF0000) >> 16) + ((d & 0x00FF0000) >> 16) + ((e & 0x00FF0000) >> 16) +
                            ((f & 0x00FF0000) >> 16) + ((g & 0x00FF0000) >> 16) + ((h & 0x00FF0000) >> 16)) / 9;
                        
                        const float m = _DOF[o >> 24];
                        const u8 R = m * (o & 0x000000FF) +  (1 - m) * _R;
                        const u8 G = m * ((o & 0x0000FF00) >> 8) + (1 - m) * _G;
                        const u8 B = m * ((o & 0x00FF0000) >> 16) + (1 - m) * _B;
                        
                        base[i] = 0xFF000000 | (B << 16) | (G << 8) | R;
                    } else base[i] = 0xFF000000 | o;
                }
            }
        } else {
            sceKernelDcacheWritebackAll();
            sceDmacMemcpy(base, frame, FRAME_BYTES_COUNT);
        }
    }
}

static int ajustCursor(const int value, const u8 mode) {
    if(!mode) {
        u16 max;
        if(value < 0) {
            return 0;
        } else if(value >= (max = (SPACE_BLOCK_SIZE * DEPTH_BLOCK_COUNT) / RAY_STEP)) {
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
    return FRAME_BYTES_COUNT * move + SPACE_BYTES_COUNT * rotate;
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
        char* options = (char*)memalign(16, 32);
        fgets(options, 32, f);
        sscanf(options, "%f %u %u %u %u",
            &MAX_PROJECTION_DEPTH,
            &ATOMIC_POV_COUNT,
            &RAY_STEP,
            &WIDTH_BLOCK_COUNT,
            &DEPTH_BLOCK_COUNT);
        fclose(f);
    }
}

int main() {
    scePowerSetClockFrequency(333, 333, 166);    
    getOptions();
    
    if(MAX_PROJECTION_DEPTH > 0.0f) {
        PROJECTION_FACTOR = 1.0f / MAX_PROJECTION_DEPTH;  
    }
    
    WIN_WIDTH = SPACE_BLOCK_SIZE * WIDTH_BLOCK_COUNT;
    WIN_WIDTH_D2 = WIN_WIDTH / 2;
    WIN_HEIGHT_D2 = WIN_HEIGHT / 2;
    WIN_PIXELS_COUNT = WIN_WIDTH * WIN_HEIGHT;
    FRAME_BYTES_COUNT = WIN_PIXELS_COUNT * sizeof(u32);
    SPACE_BYTES_COUNT = ((DEPTH_BLOCK_COUNT * SPACE_BLOCK_SIZE) / RAY_STEP) * FRAME_BYTES_COUNT;    
    TEXTURE_WIDTH = TEXTURE_BLOCK_SIZE * WIDTH_BLOCK_COUNT;
    SPACE_Y_OFFSET = getPower(TEXTURE_WIDTH);

    u8* zpos = memalign(16, WIN_PIXELS_COUNT);
    u32* base = memalign(16, FRAME_BYTES_COUNT);
    frame = memalign(16, FRAME_BYTES_COUNT);
    
    void* list = memalign(16, 256);
    
    if(MAX_PROJECTION_DEPTH > 0.0f) {
        preCalculate();
    }
    
    generateRenderSurface();
    preCalcDof();
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
        
        if(MAX_PROJECTION_DEPTH > 0.0f) {
            memset(zpos, 0, WIN_PIXELS_COUNT);
        }
        
        sceGuStart(GU_DIRECT, list);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        
        readIo(frame, controls());
        getView(frame, zpos, base);
        
        sceGuTexImage(0, TEXTURE_WIDTH, TEXTURE_BLOCK_SIZE, TEXTURE_WIDTH, base);
        sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_16BIT|GU_COLOR_8888|
		GU_TRANSFORM_2D|GU_VERTEX_16BIT, VERTICES_COUNT, 0, quad);
        
        size = sceGuFinish();
        sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
        
        pspDebugScreenSetOffset(dbuff);
        pspDebugScreenSetXY(0, 0);
        pspDebugScreenSetTextColor(0xFF00A0FF);
        pspDebugScreenPrintf("Fps: %llu, DOF: %s\n", fps, DEPTH_OF_FIELD ? "on" : "off");
        pspDebugScreenPrintf("List size: %llu bytes.\n", size);
        
        sceDisplayWaitVblankStart();
        dbuff = (int)sceGuSwapBuffers();
        
        sceRtcGetCurrentTick(&now);
        fps = tickResolution / (now - prev);
    } while(!(pad.Buttons & PSP_CTRL_SELECT));
    
    free(quad);
    free(list);
    free(zpos);
    free(base);
    free(frame);
    free(_DOF);
    free(_DOF_MATRIX_REFS);
    free(_FACTORS);
    free(_COORDINATES);
    
    openCloseIo(0);
    sceKernelExitGame();
    return 0;
}
