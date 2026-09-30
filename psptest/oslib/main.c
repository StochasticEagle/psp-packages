#include <pspkernel.h>
#include <oslib/oslib.h>
#include <psptest.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("PSPTEST OSLib", 0, 1, 0);

#define TEST_PNG "psptest-oslib.png"
#define TEST_WAV "psptest-oslib.wav"
#define TEST_MOD "psptest-oslib.mod"

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
PSPTEST_COVERS(oslSyncFrame);
PSPTEST_COVERS(oslEndGfx);
PSPTEST_COVERS(oslIntraFontInit);
PSPTEST_COVERS(oslLoadFontFile);
PSPTEST_COVERS(oslIntraFontSetStyle);
PSPTEST_COVERS(oslDeleteFont);
PSPTEST_COVERS(oslIntraFontShutdown);
PSPTEST_COVERS(oslIsWlanPowerOn);
PSPTEST_COVERS(oslIsRemoteExist);

static int osl_initialized;

static void ensure_osl(void) {
    if (!osl_initialized) {
        oslInit(0);
        oslSetQuitOnLoadFailure(0);
        osl_initialized = 1;
    }
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

static void write_le16(FILE *file, unsigned int value) {
    unsigned char bytes[2] = { (unsigned char)(value & 0xffu), (unsigned char)((value >> 8) & 0xffu) };
    fwrite(bytes, 1, sizeof(bytes), file);
}

static void write_le32(FILE *file, unsigned int value) {
    unsigned char bytes[4] = {
        (unsigned char)(value & 0xffu),
        (unsigned char)((value >> 8) & 0xffu),
        (unsigned char)((value >> 16) & 0xffu),
        (unsigned char)((value >> 24) & 0xffu)
    };
    fwrite(bytes, 1, sizeof(bytes), file);
}

static int write_test_wav(const char *path) {
    const int sample_rate = 44100;
    const int sample_count = sample_rate / 2;
    const int data_size = sample_count * 2;
    FILE *file = fopen(path, "wb");

    if (file == NULL) return 0;

    fwrite("RIFF", 1, 4, file);
    write_le32(file, 36u + (unsigned int)data_size);
    fwrite("WAVEfmt ", 1, 8, file);
    write_le32(file, 16);
    write_le16(file, 1);
    write_le16(file, 1);
    write_le32(file, sample_rate);
    write_le32(file, sample_rate * 2);
    write_le16(file, 2);
    write_le16(file, 16);
    fwrite("data", 1, 4, file);
    write_le32(file, data_size);

    for (int i = 0; i < sample_count; i++) {
        float phase = 2.0f * 3.14159265358979323846f * 440.0f * (float)i / (float)sample_rate;
        int16_t sample = (int16_t)(sinf(phase) * 12000.0f);
        write_le16(file, (unsigned int)(uint16_t)sample);
    }

    return fclose(file) == 0;
}

PSPTEST_TEST(initialize_osl) {
    ensure_osl();
    PSPTEST_ASSERT_TRUE(test, osl_initialized != 0);
}

PSPTEST_TEST(core_math) {
    ensure_osl();
    PSPTEST_ASSERT_TRUE(test, nearly_equal(oslSin(90.0f, 1.0f), 1.0f, 0.02f));
    PSPTEST_ASSERT_TRUE(test, nearly_equal(oslCos(0.0f, 1.0f), 1.0f, 0.02f));
}

PSPTEST_TEST(aligned_memory) {
    void *aligned;
    const size_t size = 16 * 1024;

    ensure_osl();
    aligned = memalign(64, size);
    PSPTEST_ASSERT_NOT_NULL(test, aligned);
    PSPTEST_ASSERT_EQ_INT(test, 0, (uintptr_t)aligned & 63u);
    memset(aligned, 0x5a, size);
    PSPTEST_ASSERT_EQ_INT(test, 0x5a, ((unsigned char *)aligned)[size - 1]);
    free(aligned);
}

PSPTEST_TEST(virtual_file_memory) {
    unsigned char storage[64];
    char readback[16];
    VIRTUAL_FILE *file;

    ensure_osl();
    memset(storage, 0, sizeof(storage));

    file = VirtualFileOpen(storage, sizeof(storage), VF_MEMORY, VF_O_READWRITE);
    PSPTEST_ASSERT_NOT_NULL(test, file);
    PSPTEST_ASSERT_EQ_INT(test, 5, VirtualFileWrite("hello", 1, 5, file));
    PSPTEST_ASSERT_EQ_INT(test, 5, VirtualFileTell(file));
    VirtualFileSeek(file, 0, SEEK_SET);
    memset(readback, 0, sizeof(readback));
    PSPTEST_ASSERT_EQ_INT(test, 5, VirtualFileRead(readback, 1, 5, file));
    PSPTEST_ASSERT_TRUE(test, memcmp(readback, "hello", 5) == 0);
    VirtualFileSeek(file, -1, SEEK_END);
    PSPTEST_ASSERT_EQ_INT(test, (int)sizeof(storage) - 1, VirtualFileTell(file));
    PSPTEST_ASSERT_TRUE(test, VirtualFileClose(file) != 0);
}

PSPTEST_TEST(image_palette_png) {
    OSL_IMAGE *image;
    OSL_IMAGE *copy;
    OSL_IMAGE *converted;
    OSL_IMAGE *scaled;
    OSL_IMAGE *loaded;
    OSL_PALETTE *palette;
    unsigned long *palette_data;
    int red = RGBA(255, 0, 0, 255);
    int green = RGBA(0, 255, 0, 255);

    ensure_osl();

    image = oslCreateImage(16, 16, OSL_IN_RAM, OSL_PF_8888);
    PSPTEST_ASSERT_NOT_NULL(test, image);
    oslClearImage(image, RGBA(0, 0, 0, 255));
    oslSetImagePixel(image, 3, 4, red);
    oslSetImagePixel(image, 5, 6, green);
    PSPTEST_ASSERT_EQ_INT(test, red, oslGetImagePixel(image, 3, 4));
    PSPTEST_ASSERT_EQ_INT(test, green, oslGetImagePixel(image, 5, 6));

    copy = oslCreateImageCopy(image, OSL_IN_RAM);
    PSPTEST_ASSERT_NOT_NULL(test, copy);
    PSPTEST_ASSERT_EQ_INT(test, red, oslGetImagePixel(copy, 3, 4));

    converted = oslConvertImageTo(image, OSL_IN_RAM, OSL_PF_5650);
    PSPTEST_ASSERT_NOT_NULL(test, converted);
    PSPTEST_ASSERT_EQ_INT(test, 16, converted->sizeX);
    PSPTEST_ASSERT_EQ_INT(test, 16, converted->sizeY);

    scaled = oslScaleImageCreate(image, OSL_IN_RAM, 8, 8, OSL_PF_8888);
    PSPTEST_ASSERT_NOT_NULL(test, scaled);
    PSPTEST_ASSERT_EQ_INT(test, 8, scaled->sizeX);
    PSPTEST_ASSERT_EQ_INT(test, 8, scaled->sizeY);

    palette = oslCreatePaletteEx(16, OSL_IN_RAM, OSL_PF_8888);
    PSPTEST_ASSERT_NOT_NULL(test, palette);
    palette_data = (unsigned long *)palette->data;
    palette_data[2] = (unsigned long)green;
    PSPTEST_ASSERT_EQ_INT(test, green, oslGetPaletteColor(palette, 2));

    PSPTEST_ASSERT_TRUE(test, oslWriteImageFilePNG(image, TEST_PNG, OSL_WRI_ALPHA) != 0);
    loaded = oslLoadImageFilePNG(TEST_PNG, OSL_IN_RAM, OSL_PF_8888);
    PSPTEST_ASSERT_NOT_NULL(test, loaded);
    PSPTEST_ASSERT_EQ_INT(test, 16, loaded->sizeX);
    PSPTEST_ASSERT_EQ_INT(test, 16, loaded->sizeY);
    PSPTEST_ASSERT_EQ_INT(test, red, oslGetImagePixel(loaded, 3, 4));

    oslDeleteImage(loaded);
    oslDeletePalette(palette);
    oslDeleteImage(scaled);
    oslDeleteImage(converted);
    oslDeleteImage(copy);
    oslDeleteImage(image);
    remove(TEST_PNG);
}

PSPTEST_TEST(map_creation) {
    OSL_IMAGE *tiles;
    OSL_MAP *map;
    unsigned short map_data[4] = { 0, 1, 1, 0 };

    ensure_osl();

    tiles = oslCreateImage(16, 8, OSL_IN_RAM, OSL_PF_8888);
    PSPTEST_ASSERT_NOT_NULL(test, tiles);
    oslClearImage(tiles, RGBA(255, 255, 255, 255));

    map = oslCreateMap(tiles, map_data, 8, 8, 2, 2, OSL_MF_U16);
    PSPTEST_ASSERT_NOT_NULL(test, map);
    PSPTEST_ASSERT_EQ_INT(test, 8, map->tileX);
    PSPTEST_ASSERT_EQ_INT(test, 8, map->tileY);
    PSPTEST_ASSERT_EQ_INT(test, 2, map->mapSizeX);
    PSPTEST_ASSERT_EQ_INT(test, 2, map->mapSizeY);
    PSPTEST_ASSERT_TRUE(test, map->map == map_data);

    oslDeleteMap(map);
    oslDeleteImage(tiles);
}

PSPTEST_TEST(mod_loader_uses_xmp_backend) {
    OSL_SOUND *sound;

    ensure_osl();
    PSPTEST_ASSERT_TRUE(test, write_minimal_mod(TEST_MOD));
    oslSetModSampleRate(22050, 0, 1);
    sound = oslLoadSoundFileMOD(TEST_MOD, OSL_FMT_NONE);
    remove(TEST_MOD);

    PSPTEST_ASSERT_NOT_NULL(test, sound);
    PSPTEST_ASSERT_EQ_INT(test, 0, sound->isStreamed);
    PSPTEST_ASSERT_EQ_INT(test, 0x10, sound->mono);
    PSPTEST_ASSERT_NOT_NULL(test, sound->data);
    PSPTEST_ASSERT_NOT_NULL(test, sound->audioCallback);
    PSPTEST_ASSERT_NOT_NULL(test, sound->playSound);
    PSPTEST_ASSERT_NOT_NULL(test, sound->stopSound);
    PSPTEST_ASSERT_NOT_NULL(test, sound->deleteSound);
    oslDeleteSound(sound);
}

PSPTEST_TEST(platform_state_queries) {
    int wlan;
    int remote;

    ensure_osl();
    wlan = oslIsWlanPowerOn();
    remote = oslIsRemoteExist();
    PSPTEST_ASSERT_TRUE(test, wlan == 0 || wlan == 1);
    PSPTEST_ASSERT_TRUE(test, remote == 0 || remote == 1);
}

PSPTEST_TEST(graphics_text_map_controller) {
    OSL_IMAGE *image;
    OSL_IMAGE *tiles;
    OSL_MAP *map;
    OSL_FONT *font = NULL;
    unsigned short map_data[4] = { 0, 1, 1, 0 };
    int text_width;

    ensure_osl();
    oslInitGfx(OSL_PF_8888, 1);
    oslInitConsole();

    image = oslCreateImage(48, 48, OSL_IN_RAM, OSL_PF_8888);
    tiles = oslCreateImage(16, 8, OSL_IN_RAM, OSL_PF_8888);
    if (image == NULL || tiles == NULL) {
        if (image != NULL) oslDeleteImage(image);
        if (tiles != NULL) oslDeleteImage(tiles);
        oslEndGfx();
        psptest_fail(test, __FILE__, __LINE__, "unable to allocate graphics test images");
        return;
    }

    oslClearImage(image, RGBA(32, 96, 220, 255));
    oslSetImagePixel(image, 0, 0, RGBA(255, 255, 255, 255));
    oslClearImage(tiles, RGBA(255, 255, 0, 255));
    for (int y = 0; y < 8; y++) {
        for (int x = 8; x < 16; x++) {
            oslSetImagePixel(tiles, (unsigned int)x, (unsigned int)y, RGBA(0, 255, 255, 255));
        }
    }

    map = oslCreateMap(tiles, map_data, 8, 8, 2, 2, OSL_MF_U16);
    if (map == NULL) {
        oslDeleteImage(tiles);
        oslDeleteImage(image);
        oslEndGfx();
        psptest_fail(test, __FILE__, __LINE__, "unable to create graphics test map");
        return;
    }
    map->drawSizeX = 16;
    map->drawSizeY = 16;

    if (oslIntraFontInit(INTRAFONT_CACHE_MED) == 0) {
        font = oslLoadFontFile("flash0:/font/ltn0.pgf");
        if (font != NULL) {
            oslIntraFontSetStyle(font, 0.8f, RGBA(255, 255, 255, 255), RGBA(0, 0, 0, 255), 0.0f, INTRAFONT_ALIGN_LEFT);
            oslSetFont(font);
        }
    }

    oslSetKeyAutorepeatInit(20);
    oslSetKeyAutorepeatInterval(5);
    text_width = oslGetStringWidth("PSPTEST");

    for (int frame = 0; frame < 4; frame++) {
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
        oslSyncFrame();
    }

    if (font != NULL) {
        oslSetFont(osl_sceFont);
        oslDeleteFont(font);
    }
    oslIntraFontShutdown();
    oslDeleteMap(map);
    oslDeleteImage(tiles);
    oslDeleteImage(image);
    oslEndGfx();

    PSPTEST_ASSERT_TRUE(test, text_width > 0);
}

PSPTEST_TEST(audio_wav_playback) {
    OSL_SOUND *sound;

    ensure_osl();
    PSPTEST_ASSERT_TRUE(test, write_test_wav(TEST_WAV));

    oslInitGfx(OSL_PF_8888, 1);
    oslInitConsole();
    oslInitAudio();

    sound = oslLoadSoundFileWAV(TEST_WAV, OSL_FMT_NONE);
    if (sound == NULL) {
        remove(TEST_WAV);
        oslDeinitAudio();
        oslEndGfx();
        psptest_fail(test, __FILE__, __LINE__, "generated WAV could not be loaded");
        return;
    }

    PSPTEST_ASSERT_NOT_NULL(test, sound->data);
    PSPTEST_ASSERT_NOT_NULL(test, sound->playSound);
    PSPTEST_ASSERT_NOT_NULL(test, sound->stopSound);
    PSPTEST_ASSERT_NOT_NULL(test, sound->deleteSound);

    oslPlaySound(sound, 0);
    sceKernelDelayThread(500000);
    oslStopSound(sound);
    oslDeleteSound(sound);
    oslDeinitAudio();
    oslEndGfx();
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

PSPTEST_MODULE("packages/oslib", cases, PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU)
