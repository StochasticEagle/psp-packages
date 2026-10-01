#include <pspkernel.h>
#include <pspdisplay.h>
#include <oslib/oslib.h>
#include <psptest.h>

#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("PSPTEST OSLib", 0, 1, 0);

#define TEST_PNG "psptest-oslib.png"
#define TEST_WAV "psptest-oslib.wav"
#define TEST_MOD "psptest-oslib.mod"
#define OSLIB_HEAP_KB (8u * 1024u)
#define AUDIO_SHUTDOWN_TIMEOUT_US 2000000u

unsigned int sce_newlib_heap_kb_size = OSLIB_HEAP_KB;

extern void __libcglue_init(int argc, char *argv[]);
extern void __libcglue_deinit(void);

void __libpthreadglue_init(void) {
}

PSPTEST_COVERS(oslInit);
PSPTEST_COVERS(oslSin);
PSPTEST_COVERS(oslCos);
PSPTEST_COVERS(VirtualFileOpen);
PSPTEST_COVERS(VirtualFileRead);
PSPTEST_COVERS(VirtualFileWrite);
PSPTEST_COVERS(VirtualFileSeek);
PSPTEST_COVERS(VirtualFileTell);
PSPTEST_COVERS(VirtualFileClose);
PSPTEST_COVERS(oslCreateImage);
PSPTEST_COVERS(oslClearImage);
PSPTEST_COVERS(oslDeleteImage);
PSPTEST_COVERS(oslSetImagePixel);
PSPTEST_COVERS(oslGetImagePixel);
PSPTEST_COVERS(oslCreateImageCopy);
PSPTEST_COVERS(oslConvertImageTo);
PSPTEST_COVERS(oslScaleImageCreate);
PSPTEST_COVERS(oslWriteImageFilePNG);
PSPTEST_COVERS(oslLoadImageFilePNG);
PSPTEST_COVERS(oslCreatePaletteEx);
PSPTEST_COVERS(oslGetPaletteColor);
PSPTEST_COVERS(oslDeletePalette);
PSPTEST_COVERS(oslCreateMap);
PSPTEST_COVERS(oslDeleteMap);
PSPTEST_COVERS(oslSetModSampleRate);
PSPTEST_COVERS(oslLoadSoundFileMOD);
PSPTEST_COVERS(oslLoadSoundFileWAV);
PSPTEST_COVERS(oslInitAudio);
PSPTEST_COVERS(oslPlaySound);
PSPTEST_COVERS(oslStopSound);
PSPTEST_COVERS(oslDeleteSound);
PSPTEST_COVERS(oslDeinitAudio);
PSPTEST_COVERS(oslInitGfx);
PSPTEST_COVERS(oslInitConsole);
PSPTEST_COVERS(oslClearScreen);
PSPTEST_COVERS(oslSetTextColor);
PSPTEST_COVERS(oslSetBkColor);
PSPTEST_COVERS(oslStartDrawing);
PSPTEST_COVERS(oslDrawGradientRect);
PSPTEST_COVERS(oslDrawLine);
PSPTEST_COVERS(oslDrawRect);
PSPTEST_COVERS(oslDrawFillRect);
PSPTEST_COVERS(oslDrawImage);
PSPTEST_COVERS(oslDrawMap);
PSPTEST_COVERS(oslDrawString);
PSPTEST_COVERS(oslGetStringWidth);
PSPTEST_COVERS(oslReadKeys);
PSPTEST_COVERS(oslEndDrawing);
PSPTEST_COVERS(oslEndFrame);
PSPTEST_COVERS(oslSwapBuffers);
PSPTEST_COVERS(oslEndGfx);
PSPTEST_COVERS(oslIntraFontInit);
PSPTEST_COVERS(oslLoadFontFile);
PSPTEST_COVERS(oslIntraFontSetStyle);
PSPTEST_COVERS(oslDeleteFont);
PSPTEST_COVERS(oslIntraFontShutdown);
PSPTEST_COVERS(oslIsWlanPowerOn);
PSPTEST_COVERS(oslIsRemoteExist);

static int runtime_initialized;
static int osl_initialized;
static int gfx_initialized;
static int audio_initialized;
static int intrafont_initialized;
static OSL_SOUND *active_sound;

#define OSLIB_REQUIRE(test, expression, label) do { \
    (test)->assertions++; \
    if (!(expression)) { \
        psptest_fail((test), __FILE__, __LINE__, "assertion failed: " #expression); \
        goto label; \
    } \
} while (0)

#define OSLIB_REQUIRE_EQ_INT(test, expected_value, actual_value, label) do { \
    long long oslib_expected = (long long)(expected_value); \
    long long oslib_actual = (long long)(actual_value); \
    (test)->assertions++; \
    if (oslib_expected != oslib_actual) { \
        psptest_fail_eq_int((test), __FILE__, __LINE__, oslib_expected, oslib_actual); \
        goto label; \
    } \
} while (0)

static int audio_is_idle(void) {
    int channel;

    for (channel = 0; channel < OSL_NUM_AUDIO_CHANNELS; channel++) {
        if (osl_audioActive[channel] != 0) return 0;
    }
    return 1;
}

static int wait_audio_idle(unsigned int timeout_us) {
    uint64_t deadline = sceKernelGetSystemTimeWide() + timeout_us;

    while (!audio_is_idle()) {
        if (sceKernelGetSystemTimeWide() >= deadline) return 0;
        sceKernelDelayThread(1000);
    }
    return 1;
}

static void cleanup_graphics(void) {
    if (intrafont_initialized) {
        oslIntraFontShutdown();
        intrafont_initialized = 0;
    }
    if (gfx_initialized) {
        oslEndGfx();
        gfx_initialized = 0;
    }
}

static int cleanup_audio(void) {
    if (active_sound != NULL) {
        oslStopSound(active_sound);
        if (!wait_audio_idle(AUDIO_SHUTDOWN_TIMEOUT_US)) return 0;
        oslDeleteSound(active_sound);
        active_sound = NULL;
    }

    if (audio_initialized) {
        if (!wait_audio_idle(AUDIO_SHUTDOWN_TIMEOUT_US)) return 0;
        oslDeinitAudio();
        audio_initialized = 0;
    }
    return 1;
}

static int oslib_suite_setup(const PspTestEnvironment *environment) {
    char *argv[2];

    if (environment == NULL || environment->version != PSPTEST_ABI_VERSION || environment->program_path == NULL || environment->program_path[0] == '\0') return -1;

    argv[0] = (char *)environment->program_path;
    argv[1] = NULL;
    __libcglue_init(1, argv);
    runtime_initialized = 1;

    oslInit(OSL_IF_USEOWNCALLBACKS | OSL_IF_NOVBLANKIRQ);
    oslSetQuitOnLoadFailure(0);
    osl_initialized = 1;
    return 0;
}

static int oslib_suite_teardown(const PspTestEnvironment *environment) {
    (void)environment;

    if (!cleanup_audio()) return -1;
    cleanup_graphics();

    remove(TEST_PNG);
    remove(TEST_WAV);
    remove(TEST_MOD);

    osl_initialized = 0;
    if (runtime_initialized) {
        __libcglue_deinit();
        runtime_initialized = 0;
    }
    return 0;
}

static void ensure_osl(PspTestContext *test) {
    PSPTEST_ASSERT_TRUE(test, runtime_initialized != 0);
    PSPTEST_ASSERT_TRUE(test, osl_initialized != 0);
}

static int nearly_equal(float a, float b, float tolerance) {
    return fabsf(a - b) <= tolerance;
}

static int write_minimal_mod(const char *path) {
    unsigned char header[1084];
    unsigned char pattern[1024];
    FILE *file;

    memset(header, 0, sizeof(header));
    memset(pattern, 0, sizeof(pattern));
    memcpy(header, "PSPTEST", 7);
    header[950] = 1;
    memcpy(&header[1080], "M.K.", 4);

    file = fopen(path, "wb");
    if (file == NULL) return 0;
    if (fwrite(header, 1, sizeof(header), file) != sizeof(header) || fwrite(pattern, 1, sizeof(pattern), file) != sizeof(pattern)) {
        fclose(file);
        remove(path);
        return 0;
    }
    return fclose(file) == 0;
}

static int write_le16(FILE *file, unsigned int value) {
    unsigned char bytes[2] = { (unsigned char)(value & 0xffu), (unsigned char)((value >> 8) & 0xffu) };
    return fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
}

static int write_le32(FILE *file, unsigned int value) {
    unsigned char bytes[4] = {
        (unsigned char)(value & 0xffu),
        (unsigned char)((value >> 8) & 0xffu),
        (unsigned char)((value >> 16) & 0xffu),
        (unsigned char)((value >> 24) & 0xffu)
    };
    return fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
}

static int write_test_wav(const char *path) {
    const int sample_rate = 44100;
    const int sample_count = sample_rate / 2;
    const int data_size = sample_count * 2;
    FILE *file = fopen(path, "wb");
    int ok = 1;
    int i;

    if (file == NULL) return 0;

    ok = ok && fwrite("RIFF", 1, 4, file) == 4;
    ok = ok && write_le32(file, 36u + (unsigned int)data_size);
    ok = ok && fwrite("WAVEfmt ", 1, 8, file) == 8;
    ok = ok && write_le32(file, 16);
    ok = ok && write_le16(file, 1);
    ok = ok && write_le16(file, 1);
    ok = ok && write_le32(file, sample_rate);
    ok = ok && write_le32(file, sample_rate * 2);
    ok = ok && write_le16(file, 2);
    ok = ok && write_le16(file, 16);
    ok = ok && fwrite("data", 1, 4, file) == 4;
    ok = ok && write_le32(file, data_size);

    for (i = 0; ok && i < sample_count; i++) {
        float phase = 2.0f * 3.14159265358979323846f * 440.0f * (float)i / (float)sample_rate;
        int16_t sample = (int16_t)(sinf(phase) * 12000.0f);
        ok = write_le16(file, (unsigned int)(uint16_t)sample);
    }

    if (fclose(file) != 0) ok = 0;
    if (!ok) remove(path);
    return ok;
}

PSPTEST_TEST(initialize_osl) {
    ensure_osl(test);
}

PSPTEST_TEST(core_math) {
    ensure_osl(test);
    if (test->status != PSPTEST_STATUS_PASS) return;
    PSPTEST_ASSERT_TRUE(test, nearly_equal(oslSin(90.0f, 1.0f), 1.0f, 0.02f));
    PSPTEST_ASSERT_TRUE(test, nearly_equal(oslCos(0.0f, 1.0f), 1.0f, 0.02f));
}

PSPTEST_TEST(aligned_memory) {
    void *aligned = NULL;
    const size_t size = 16 * 1024;

    ensure_osl(test);
    if (test->status != PSPTEST_STATUS_PASS) return;

    aligned = memalign(64, size);
    OSLIB_REQUIRE(test, aligned != NULL, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, 0, (uintptr_t)aligned & 63u, cleanup);
    memset(aligned, 0x5a, size);
    OSLIB_REQUIRE_EQ_INT(test, 0x5a, ((unsigned char *)aligned)[size - 1], cleanup);

cleanup:
    free(aligned);
}

PSPTEST_TEST(virtual_file_memory) {
    unsigned char storage[64];
    char readback[16];
    VIRTUAL_FILE *file = NULL;

    ensure_osl(test);
    if (test->status != PSPTEST_STATUS_PASS) return;
    memset(storage, 0, sizeof(storage));

    file = VirtualFileOpen(storage, sizeof(storage), VF_MEMORY, VF_O_READWRITE);
    OSLIB_REQUIRE(test, file != NULL, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, 5, VirtualFileWrite("hello", 1, 5, file), cleanup);
    OSLIB_REQUIRE_EQ_INT(test, 5, VirtualFileTell(file), cleanup);
    VirtualFileSeek(file, 0, SEEK_SET);
    memset(readback, 0, sizeof(readback));
    OSLIB_REQUIRE_EQ_INT(test, 5, VirtualFileRead(readback, 1, 5, file), cleanup);
    OSLIB_REQUIRE(test, memcmp(readback, "hello", 5) == 0, cleanup);
    VirtualFileSeek(file, -1, SEEK_END);
    OSLIB_REQUIRE_EQ_INT(test, (int)sizeof(storage) - 1, VirtualFileTell(file), cleanup);

cleanup:
    if (file != NULL && !VirtualFileClose(file) && test->status == PSPTEST_STATUS_PASS) psptest_fail(test, __FILE__, __LINE__, "VirtualFileClose failed");
}

PSPTEST_TEST(image_palette_png) {
    OSL_IMAGE *image = NULL;
    OSL_IMAGE *copy = NULL;
    OSL_IMAGE *converted = NULL;
    OSL_IMAGE *scaled = NULL;
    OSL_IMAGE *loaded = NULL;
    OSL_PALETTE *palette = NULL;
    unsigned long *palette_data;
    int red = RGBA(255, 0, 0, 255);
    int green = RGBA(0, 255, 0, 255);

    ensure_osl(test);
    if (test->status != PSPTEST_STATUS_PASS) return;

    image = oslCreateImage(16, 16, OSL_IN_RAM, OSL_PF_8888);
    OSLIB_REQUIRE(test, image != NULL, cleanup);
    oslClearImage(image, RGBA(0, 0, 0, 255));
    oslSetImagePixel(image, 3, 4, red);
    oslSetImagePixel(image, 5, 6, green);
    OSLIB_REQUIRE_EQ_INT(test, red, oslGetImagePixel(image, 3, 4), cleanup);
    OSLIB_REQUIRE_EQ_INT(test, green, oslGetImagePixel(image, 5, 6), cleanup);

    copy = oslCreateImageCopy(image, OSL_IN_RAM);
    OSLIB_REQUIRE(test, copy != NULL, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, red, oslGetImagePixel(copy, 3, 4), cleanup);

    converted = oslConvertImageTo(image, OSL_IN_RAM, OSL_PF_5650);
    OSLIB_REQUIRE(test, converted != NULL, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, 16, converted->sizeX, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, 16, converted->sizeY, cleanup);

    scaled = oslScaleImageCreate(image, OSL_IN_RAM, 8, 8, OSL_PF_8888);
    OSLIB_REQUIRE(test, scaled != NULL, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, 8, scaled->sizeX, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, 8, scaled->sizeY, cleanup);

    palette = oslCreatePaletteEx(16, OSL_IN_RAM, OSL_PF_8888);
    OSLIB_REQUIRE(test, palette != NULL, cleanup);
    palette_data = (unsigned long *)palette->data;
    palette_data[2] = (unsigned long)green;
    OSLIB_REQUIRE_EQ_INT(test, green, oslGetPaletteColor(palette, 2), cleanup);

    OSLIB_REQUIRE(test, oslWriteImageFilePNG(image, TEST_PNG, OSL_WRI_ALPHA) != 0, cleanup);
    loaded = oslLoadImageFilePNG(TEST_PNG, OSL_IN_RAM, OSL_PF_8888);
    OSLIB_REQUIRE(test, loaded != NULL, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, 16, loaded->sizeX, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, 16, loaded->sizeY, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, red, oslGetImagePixel(loaded, 3, 4), cleanup);

cleanup:
    if (loaded != NULL) oslDeleteImage(loaded);
    if (palette != NULL) oslDeletePalette(palette);
    if (scaled != NULL) oslDeleteImage(scaled);
    if (converted != NULL) oslDeleteImage(converted);
    if (copy != NULL) oslDeleteImage(copy);
    if (image != NULL) oslDeleteImage(image);
    remove(TEST_PNG);
}

PSPTEST_TEST(map_creation) {
    OSL_IMAGE *tiles = NULL;
    OSL_MAP *map = NULL;
    unsigned short map_data[4] = { 0, 1, 1, 0 };

    ensure_osl(test);
    if (test->status != PSPTEST_STATUS_PASS) return;

    tiles = oslCreateImage(16, 8, OSL_IN_RAM, OSL_PF_8888);
    OSLIB_REQUIRE(test, tiles != NULL, cleanup);
    oslClearImage(tiles, RGBA(255, 255, 255, 255));

    map = oslCreateMap(tiles, map_data, 8, 8, 2, 2, OSL_MF_U16);
    OSLIB_REQUIRE(test, map != NULL, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, 8, map->tileX, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, 8, map->tileY, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, 2, map->mapSizeX, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, 2, map->mapSizeY, cleanup);
    OSLIB_REQUIRE(test, map->map == map_data, cleanup);

cleanup:
    if (map != NULL) oslDeleteMap(map);
    if (tiles != NULL) oslDeleteImage(tiles);
}

PSPTEST_TEST(mod_loader_uses_xmp_backend) {
    OSL_SOUND *sound = NULL;

    ensure_osl(test);
    if (test->status != PSPTEST_STATUS_PASS) return;

    OSLIB_REQUIRE(test, write_minimal_mod(TEST_MOD), cleanup);
    oslSetModSampleRate(22050, 0, 1);
    sound = oslLoadSoundFileMOD(TEST_MOD, OSL_FMT_NONE);
    OSLIB_REQUIRE(test, sound != NULL, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, 0, sound->isStreamed, cleanup);
    OSLIB_REQUIRE_EQ_INT(test, 0x10, sound->mono, cleanup);
    OSLIB_REQUIRE(test, sound->data != NULL, cleanup);
    OSLIB_REQUIRE(test, sound->audioCallback != NULL, cleanup);
    OSLIB_REQUIRE(test, sound->playSound != NULL, cleanup);
    OSLIB_REQUIRE(test, sound->stopSound != NULL, cleanup);
    OSLIB_REQUIRE(test, sound->deleteSound != NULL, cleanup);

cleanup:
    if (sound != NULL) oslDeleteSound(sound);
    remove(TEST_MOD);
}

PSPTEST_TEST(platform_state_queries) {
    int wlan;
    int remote;

    ensure_osl(test);
    if (test->status != PSPTEST_STATUS_PASS) return;

    wlan = oslIsWlanPowerOn();
    remote = oslIsRemoteExist();
    PSPTEST_ASSERT_TRUE(test, wlan == 0 || wlan == 1);
    PSPTEST_ASSERT_TRUE(test, remote == 0 || remote == 1);
}

PSPTEST_TEST(graphics_text_map_controller) {
    OSL_IMAGE *image = NULL;
    OSL_IMAGE *tiles = NULL;
    OSL_MAP *map = NULL;
    OSL_FONT *font = NULL;
    unsigned short map_data[4] = { 0, 1, 1, 0 };
    int text_width = 0;
    int frame;

    ensure_osl(test);
    if (test->status != PSPTEST_STATUS_PASS) return;

    oslInitGfx(OSL_PF_8888, 1);
    gfx_initialized = 1;
    oslInitConsole();

    image = oslCreateImage(48, 48, OSL_IN_RAM, OSL_PF_8888);
    tiles = oslCreateImage(16, 8, OSL_IN_RAM, OSL_PF_8888);
    OSLIB_REQUIRE(test, image != NULL && tiles != NULL, cleanup);

    oslClearImage(image, RGBA(32, 96, 220, 255));
    oslSetImagePixel(image, 0, 0, RGBA(255, 255, 255, 255));
    oslClearImage(tiles, RGBA(255, 255, 0, 255));
    for (int y = 0; y < 8; y++) {
        for (int x = 8; x < 16; x++) oslSetImagePixel(tiles, (unsigned int)x, (unsigned int)y, RGBA(0, 255, 255, 255));
    }

    map = oslCreateMap(tiles, map_data, 8, 8, 2, 2, OSL_MF_U16);
    OSLIB_REQUIRE(test, map != NULL, cleanup);
    map->drawSizeX = 16;
    map->drawSizeY = 16;

    if (oslIntraFontInit(INTRAFONT_CACHE_MED) == 0) {
        intrafont_initialized = 1;
        font = oslLoadFontFile("flash0:/font/ltn0.pgf");
        if (font != NULL) {
            oslIntraFontSetStyle(font, 0.8f, RGBA(255, 255, 255, 255), RGBA(0, 0, 0, 255), 0.0f, INTRAFONT_ALIGN_LEFT);
            oslSetFont(font);
        }
    }

    oslSetKeyAutorepeatInit(20);
    oslSetKeyAutorepeatInterval(5);
    text_width = oslGetStringWidth("PSPTEST");

    for (frame = 0; frame < 4; frame++) {
        oslStartDrawing();
        oslDrawGradientRect(0, 0, 480, 272, RGB(20, 20, 40), RGB(40, 20, 20), RGB(20, 40, 20), RGB(20, 20, 40));
        oslDrawLine(20, 45, 200, 45, RGB(255, 255, 255));
        oslDrawRect(20, 60, 120, 110, RGB(255, 190, 0));
        oslDrawFillRect(140, 60, 240, 110, RGB(180, 0, 0));
        image->x = 270;
        image->y = 60;
        oslDrawImage(image);
        oslDrawMap(map);
        oslSetTextColor(RGBA(255, 255, 255, 255));
        oslSetBkColor(RGBA(0, 0, 0, 128));
        oslDrawString(16, 10, "PSPTEST OSLib automated graphics smoke test");
        oslDrawString(16, 125, "Rendering graphics, text, map and controller state.");
        oslEndDrawing();
        (void)oslReadKeys();
        oslEndFrame();
        sceDisplayWaitVblankStart();
        oslSwapBuffers();
    }

    OSLIB_REQUIRE(test, text_width > 0, cleanup);

cleanup:
    if (font != NULL) {
        oslSetFont(osl_sceFont);
        oslDeleteFont(font);
    }
    if (intrafont_initialized) {
        oslIntraFontShutdown();
        intrafont_initialized = 0;
    }
    if (map != NULL) oslDeleteMap(map);
    if (tiles != NULL) oslDeleteImage(tiles);
    if (image != NULL) oslDeleteImage(image);
    if (gfx_initialized) {
        oslEndGfx();
        gfx_initialized = 0;
    }
}

PSPTEST_TEST(audio_wav_playback) {
    int idle = 1;

    ensure_osl(test);
    if (test->status != PSPTEST_STATUS_PASS) return;

    OSLIB_REQUIRE(test, write_test_wav(TEST_WAV), cleanup);

    oslInitGfx(OSL_PF_8888, 1);
    gfx_initialized = 1;
    oslInitConsole();
    OSLIB_REQUIRE_EQ_INT(test, 0, oslInitAudio(), cleanup);
    audio_initialized = 1;

    active_sound = oslLoadSoundFileWAV(TEST_WAV, OSL_FMT_NONE);
    OSLIB_REQUIRE(test, active_sound != NULL, cleanup);
    OSLIB_REQUIRE(test, active_sound->data != NULL, cleanup);
    OSLIB_REQUIRE(test, active_sound->playSound != NULL, cleanup);
    OSLIB_REQUIRE(test, active_sound->stopSound != NULL, cleanup);
    OSLIB_REQUIRE(test, active_sound->deleteSound != NULL, cleanup);

    oslPlaySound(active_sound, 0);
    sceKernelDelayThread(500000);
    oslStopSound(active_sound);
    idle = wait_audio_idle(AUDIO_SHUTDOWN_TIMEOUT_US);
    OSLIB_REQUIRE(test, idle, cleanup);

cleanup:
    if (active_sound != NULL && idle) {
        oslDeleteSound(active_sound);
        active_sound = NULL;
    }
    if (audio_initialized && idle) {
        oslDeinitAudio();
        audio_initialized = 0;
    }
    if (!idle && test->status == PSPTEST_STATUS_PASS) psptest_fail(test, __FILE__, __LINE__, "OSLib audio worker did not stop");
    if (gfx_initialized) {
        oslEndGfx();
        gfx_initialized = 0;
    }
    remove(TEST_WAV);
}

static const PspTestCase cases[] = {
    PSPTEST_CASE(initialize_osl),
    PSPTEST_CASE(core_math),
    PSPTEST_CASE(aligned_memory),
    PSPTEST_CASE(virtual_file_memory),
    PSPTEST_CASE(image_palette_png),
    PSPTEST_CASE(map_creation),
    PSPTEST_CASE(mod_loader_uses_xmp_backend),
    PSPTEST_CASE(platform_state_queries),
    PSPTEST_CASE(graphics_text_map_controller),
    PSPTEST_CASE(audio_wav_playback)
};

PSPTEST_MODULE("packages/oslib", cases, PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, oslib_suite_setup, oslib_suite_teardown)
