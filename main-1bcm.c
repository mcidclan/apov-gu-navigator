/*
 * APoV Project
 * pspgu 1 bit color mapping version
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

#define HEADER_BYTES_COUNT 80
#define TEXTURE_BLOCK_SIZE 256
#define BUFFER_WIDTH 512
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272
    
PSP_MODULE_INFO("APoV", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

void sceDmacMemcpy(void *dst, const void *src, int size);

typedef struct Cached {
    u32 mask, moff, midx;
    float fa, fb, fc;
    u32 hcka, hckb, vcka, vckb;
    u32 hoa, hob, voa, vob;
} Cached __attribute__((aligned(16)));

typedef struct Vertex {
	u16 u, v;
	u16 x, y, z;
} Vertex;

typedef struct Options {
    u32 SPACE_BLOCK_SIZE;
    u32 HORIZONTAL_POV_COUNT;
    u32 VERTICAL_POV_COUNT;
    u32 RAY_STEP;
    u32 WIDTH_BLOCK_COUNT;
    u32 DEPTH_BLOCK_COUNT;
    u32 COLOR_MAP_SIZE;
} Options;

#define S TEXTURE_BLOCK_SIZE
#define P (S / 16)
#define T (S / 16)
    
static u16 VERTICES_COUNT;
static u16 TEXTURE_WIDTH;
static Vertex* surface;
static Options options;

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

static u8 MODE = 0;

static u16 WIN_WIDTH;
static u16 WIN_HEIGHT;
static u32 WIN_PIXELS_COUNT;
static u32 SPACE_VOXELS_COUNT;
static u32 WIN_BYTES_COUNT;
static u32 SPACE_BYTES_COUNT;
static u32 BASE_BYTES_COUNT;

static u16 MAP_WIDTH;
static u16 MAP_HEIGHT;
static u16 MAP_WIDTH_SCALE;
static u16 MAP_HEIGHT_SCALE;
static u32 MAP_PIXELS_COUNT;
static u32 MAP_BYTES_COUNT;
static u32 MAP_VOXELS_COUNT;
static u32 MAP_VOLUME_BYTES_COUNT;

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
    sceGuTexMode(GU_PSM_8888, 0, 1, 0);
    sceGuEnable(GU_TEXTURE_2D);

    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    
    sceGuDisplay(GU_TRUE);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}

static SceUID f;
static void openData() {
    f = sceIoOpen("atoms.apov", PSP_O_RDONLY, 0777);
}
static void closeData() {
    sceIoClose(f);
}
static u64 loffset = -1;
static u8 readData(u8* const frame, const u64 offset) {
    if(offset != loffset) {
        const u64 nbytes = WIN_BYTES_COUNT + MAP_BYTES_COUNT;
        sceIoLseek(f, offset + HEADER_BYTES_COUNT, SEEK_SET);
        if(nbytes != sceIoRead(f, frame, nbytes)) {
            openData();
            return 0;
        }
        loffset = offset;
        return 1;
    }
    return 0;
}

Cached* cached = NULL;
void cache() {
    Cached c;
    cached = memalign(16, WIN_PIXELS_COUNT * sizeof(Cached));
    u16 x = 0;
    while(x < WIN_WIDTH) {
        u16 y = 0;
        while(y < WIN_HEIGHT) {
            const u32 i = x + y * WIN_WIDTH;
            const float fx = ((float)x) / MAP_WIDTH_SCALE;
            const float fy = ((float)y) / MAP_HEIGHT_SCALE;
            const u32 ux = fx;
            const u32 uy = fy;
            const u32 uyb = uy * MAP_WIDTH;
            const float hc = fx - ux - 0.5f;
            const float vc = fy - uy - 0.5f;
            c.mask = (0b1 << (i % 8));
            c.moff = (i / 8);
            c.midx = (ux + uyb);
            c.fb = (hc < 0.0f ? -hc : hc);
            c.fc = (vc < 0.0f ? -vc : vc);
            c.fa = 1.0f - (c.fb + c.fc);
            c.hoa = ux - 1 + uyb;
            c.hob = ux + 1 + uyb;
            c.voa = ux + uyb - MAP_WIDTH;
            c.vob = ux + uyb + MAP_WIDTH;
            c.hcka = hc < 0.0f && ux > 0 ? 1 : 0;
            c.hckb = hc >= 0.0f && ux < (MAP_WIDTH - 1) ? 1 : 0;
            c.vcka = vc < 0.0f && uy > 0 ? 1 : 0;
            c.vckb = vc >= 0.0f && uy < (MAP_HEIGHT - 1 ) ? 1:0;
            cached[i] = c;
            y++;
        }
        x++;
    }
}

void updateView(u8* const frame, u32* const map, u32* const base) {
    if(MODE == 0) {
        u32 i = 0;
        while(i < WIN_PIXELS_COUNT) {
            Cached* const cache = &(cached[i]);
            if(frame[cache->moff] & cache->mask) {
                base[i] = map[cache->midx] | 0xFF << 24;
            } else base[i] = 0x00;
            i++;
        }
    } else if(MODE == 1) {
        u32 i = 0;
        while(i < WIN_PIXELS_COUNT) {
            Cached* const cache = &(cached[i]);
            if(frame[cache->moff] & cache->mask) {
                u32 b, c;
                const u32 a = map[cache->midx];
                
                if(cache->hcka) {
                    b = map[cache->hoa];
                } else if(cache->hckb) {
                    b = map[cache->hob];
                } else b = 0;
                
                if(cache->vcka) {
                    c = map[cache->voa];
                } else if(cache->vckb) {
                    c = map[cache->vob];
                } else c = 0;
               
                const u8 R = (u8)(
                    ((a & 0xFF) * cache->fa) +
                    ((b & 0xFF) * cache->fb) +
                    ((c & 0xFF) * cache->fc));
                
                const u8 G = (u8)(
                    (((a >> 8) & 0xFF) * cache->fa) +
                    (((b >> 8) & 0xFF) * cache->fb) +
                    (((c >> 8) & 0xFF) * cache->fc));
                
                const u8 B = (u8)(
                    (((a >> 16) & 0xFF) * cache->fa) +
                    (((b >> 16) & 0xFF) * cache->fb) +
                    (((c >> 16) & 0xFF) * cache->fc));

                base[i] = R | G << 8 | B << 16 | 0xFF << 24;
            } else base[i] = 0x00;
            i++;
        }
    }
}

static int ajustCursor(const int value, const u8 mode) {
    if(!mode) {
        u16 max;
        if(value < 0) {
            return 0;
        } else if(value >= (max = (options.SPACE_BLOCK_SIZE *
            options.DEPTH_BLOCK_COUNT) / options.RAY_STEP)) {
            return max - 1;
        }
    } else if(mode == 1) {   
        if(value < 0) {
            return options.HORIZONTAL_POV_COUNT - 1;
        } else if(value >= options.HORIZONTAL_POV_COUNT) {
            return 0;
        }
    } else if(mode == 2) {   
        if(value < 0) {
            return options.VERTICAL_POV_COUNT - 1;
        } else if(value >= options.VERTICAL_POV_COUNT) {
            return 0;
        }
    }
    return value;
}

static u64 getOffset(const int move, const int hrotate, const int vrotate) {
    const u32 pov = (hrotate * options.VERTICAL_POV_COUNT + vrotate);
    return (WIN_BYTES_COUNT + MAP_BYTES_COUNT) * move +
        pov * (SPACE_BYTES_COUNT + MAP_VOLUME_BYTES_COUNT);
}

SceCtrlData pad;
static u64 controls() {
    static int move = 0;
    static int hrotate = 0;
    static int vrotate = 0;
    static SceCtrlData lpad;
    
    sceCtrlReadBufferPositive(&pad, 1);
    
    if(pad.Buttons & PSP_CTRL_TRIANGLE) { move++; }
    if(pad.Buttons & PSP_CTRL_CROSS) { move--; }
    if(pad.Buttons & PSP_CTRL_RIGHT) { hrotate--; }
    if(pad.Buttons & PSP_CTRL_LEFT) { hrotate++; }
    if(pad.Buttons & PSP_CTRL_UP) { vrotate--; }
    if(pad.Buttons & PSP_CTRL_DOWN) { vrotate++; }
    
    move = ajustCursor(move, 0);
    hrotate = ajustCursor(hrotate, 1);
    vrotate = ajustCursor(vrotate, 2);
    
    if((pad.Buttons & PSP_CTRL_SQUARE) &&
        !(lpad.Buttons & PSP_CTRL_SQUARE)) {
        MODE = (MODE + 1) % 2;
        loffset = -1;
    }
    lpad = pad;
    
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
    FILE* f = fopen("atoms.apov", "rb");
    if(f != NULL) {
        fread(&options, sizeof(Options), 1, f);
        fclose(f);
    }
}
   
int main() {
    scePowerSetClockFrequency(333, 333, 166);    
    getOptions();
    
    const u16 DEPTH_FRAME_COUNT = ((options.DEPTH_BLOCK_COUNT *
        options.SPACE_BLOCK_SIZE) / options.RAY_STEP);
    
    WIN_WIDTH = options.SPACE_BLOCK_SIZE * options.WIDTH_BLOCK_COUNT;
    WIN_HEIGHT = options.SPACE_BLOCK_SIZE;
    WIN_PIXELS_COUNT = WIN_WIDTH * WIN_HEIGHT;
    SPACE_VOXELS_COUNT = DEPTH_FRAME_COUNT * WIN_PIXELS_COUNT;
    
    BASE_BYTES_COUNT = WIN_PIXELS_COUNT * 8;
    WIN_BYTES_COUNT = WIN_PIXELS_COUNT / 8;
    SPACE_BYTES_COUNT = SPACE_VOXELS_COUNT / 8;
    
    MAP_WIDTH = options.COLOR_MAP_SIZE * options.WIDTH_BLOCK_COUNT;
    MAP_HEIGHT = options.COLOR_MAP_SIZE;
    MAP_PIXELS_COUNT = MAP_WIDTH * MAP_HEIGHT;
    MAP_BYTES_COUNT = MAP_PIXELS_COUNT * sizeof(u32);
    MAP_VOXELS_COUNT = DEPTH_FRAME_COUNT * MAP_PIXELS_COUNT;

    MAP_VOLUME_BYTES_COUNT = MAP_VOXELS_COUNT * sizeof(u32);
    MAP_WIDTH_SCALE = WIN_WIDTH / MAP_WIDTH;
    MAP_HEIGHT_SCALE = WIN_HEIGHT / MAP_HEIGHT;
    
    TEXTURE_WIDTH = TEXTURE_BLOCK_SIZE * options.WIDTH_BLOCK_COUNT;
    
    cache();
    
    u32* base = memalign(16, BASE_BYTES_COUNT);
    u8* frame = memalign(16, WIN_BYTES_COUNT + MAP_BYTES_COUNT);
    
    void* list = memalign(16, 256);
    
    generateRenderSurface();
    sceGuInit();
    initGuContext(list);
    pspDebugScreenInitEx(NULL, PSP_DISPLAY_PIXEL_FORMAT_8888, 0);
    pspDebugScreenEnableBackColor(0);
    
    openData();
    
    int dbuff = 0;
    u64 prev, now, fps = 0;
    const u64 tickResolution = sceRtcGetTickResolution();

    do {
        sceRtcGetCurrentTick(&prev);
        sceGuStart(GU_DIRECT, list);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        
        if(readData(frame, controls())) {
            updateView(frame, (u32*)&frame[WIN_BYTES_COUNT], base);
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
    free(cached);
    closeData();
    sceKernelExitGame();
    return 0;
}
