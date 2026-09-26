#include <pspkernel.h>
#include <oslib/oslib.h>
#include <psptest.h>

#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("PSPTEST OSLib", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

PSPTEST_COVERS(oslSetModSampleRate);
PSPTEST_COVERS(oslLoadSoundFileMOD);
PSPTEST_COVERS(oslDeleteSound);

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
    if (file == NULL) {
        return 0;
    }

    if (fwrite(header, 1, sizeof(header), file) != sizeof(header) ||
        fwrite(pattern, 1, sizeof(pattern), file) != sizeof(pattern)) {
        fclose(file);
        remove(path);
        return 0;
    }

    if (fclose(file) != 0) {
        remove(path);
        return 0;
    }

    return 1;
}

PSPTEST_TEST(mod_loader_uses_xmp_backend) {
    const char *path = "psptest-oslib.mod";
    OSL_SOUND *sound;

    PSPTEST_ASSERT_TRUE(test, write_minimal_mod(path));

    oslSetModSampleRate(22050, 0, 1);
    sound = oslLoadSoundFileMOD(path, OSL_FMT_NONE);
    remove(path);

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

static const PspTestCase cases[] = {
    PSPTEST_CASE(mod_loader_uses_xmp_backend)
};

PSPTEST_MAIN("packages/oslib", cases)
