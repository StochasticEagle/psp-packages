#include <pspctrl.h>
#include <pspdebug.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <psploadexec.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("PSPTEST Launcher", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define COLOR_WHITE 0xFFFFFFFFu
#define COLOR_AMBER 0xFF00BFFFu
#define COLOR_GRAY  0xFFD3D3D3u
#define COLOR_RED   0xFF0000FFu
#define COLOR_GREEN 0xFF00FF00u

#define MAX_TESTS 128
#define PAGE_ROWS 20

typedef enum TestStatus {
    TEST_PENDING = 0,
    TEST_PASS,
    TEST_FAIL,
    TEST_WARNING
} TestStatus;

typedef struct TestEntry {
    char module[64];
    char relative_path[128];
    char kind[16];
    TestStatus status;
    unsigned int passed;
    unsigned int failed;
    unsigned int skipped;
    unsigned int total;
} TestEntry;

typedef struct RunState {
    int running;
    int index;
    char module[64];
    char mode[16];
} RunState;

static TestEntry tests[MAX_TESTS];
static int test_count;
static char root_path[256];
static char launcher_path[320];
static char interrupted_module[64];

static void set_color(unsigned int color) {
    pspDebugScreenSetTextColor(color);
}

static void print_header(const char *title) {
    pspDebugScreenClear();
    set_color(COLOR_AMBER);
    pspDebugScreenPrintf("PSPTEST  %s\n", title);
    pspDebugScreenPrintf("------------------------------------------------------------\n");
    set_color(COLOR_WHITE);
}

static void print_note(const char *text) {
    set_color(COLOR_GRAY);
    pspDebugScreenPrintf("%s\n", text);
    set_color(COLOR_WHITE);
}

static const char *status_name(TestStatus status) {
    switch (status) {
        case TEST_PASS: return "PASS";
        case TEST_FAIL: return "FAIL";
        case TEST_WARNING: return "WARN";
        default: return "PENDING";
    }
}

static unsigned int status_color(TestStatus status) {
    switch (status) {
        case TEST_PASS: return COLOR_GREEN;
        case TEST_FAIL: return COLOR_RED;
        case TEST_WARNING: return COLOR_AMBER;
        default: return COLOR_WHITE;
    }
}

static void make_path(char *destination, size_t destination_size, const char *suffix) {
    snprintf(destination, destination_size, "%s/%s", root_path, suffix);
}

static void derive_root_path(int argc, char **argv) {
    const char *fallback = "ms0:/PSP/GAME/psptest";
    size_t length;

    if (argc <= 0 || argv == NULL || argv[0] == NULL || argv[0][0] == '\0') {
        snprintf(root_path, sizeof(root_path), "%s", fallback);
    } else {
        snprintf(root_path, sizeof(root_path), "%s", argv[0]);
        length = strlen(root_path);
        while (length > 0 && root_path[length - 1] != '/' && root_path[length - 1] != ':') {
            root_path[--length] = '\0';
        }
        if (length > 0 && root_path[length - 1] == '/') {
            root_path[length - 1] = '\0';
        }
        if (root_path[0] == '\0') {
            snprintf(root_path, sizeof(root_path), "%s", fallback);
        }
    }

    snprintf(launcher_path, sizeof(launcher_path), "%s/EBOOT.PBP", root_path);
}

static const char *argument_value(int argc, char **argv, const char *name) {
    int index;
    size_t name_length = strlen(name);

    for (index = 1; index < argc; index++) {
        if (strncmp(argv[index], name, name_length) == 0 && argv[index][name_length] == '=' && argv[index][name_length + 1] != '\0') {
            return argv[index] + name_length + 1;
        }
        if (strcmp(argv[index], name) == 0 && index + 1 < argc) {
            return argv[index + 1];
        }
    }

    return NULL;
}

static unsigned int read_press(void) {
    SceCtrlData pad;

    do {
        sceCtrlReadBufferPositive(&pad, 1);
        sceKernelDelayThread(10000);
    } while (pad.Buttons != 0);

    do {
        sceCtrlReadBufferPositive(&pad, 1);
        sceKernelDelayThread(10000);
    } while (pad.Buttons == 0);

    return pad.Buttons;
}

static int load_manifest(void) {
    char path[320];
    char line[320];
    FILE *file;

    make_path(path, sizeof(path), "manifest.tsv");
    file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }

    test_count = 0;
    while (fgets(line, sizeof(line), file) != NULL && test_count < MAX_TESTS) {
        TestEntry *entry;

        if (strncmp(line, "TEST\t", 5) != 0) {
            continue;
        }

        entry = &tests[test_count];
        memset(entry, 0, sizeof(*entry));
        if (sscanf(line, "TEST\t%63[^\t]\t%127[^\t]\t%15[^\r\n]", entry->module, entry->relative_path, entry->kind) == 3) {
            test_count++;
        }
    }

    fclose(file);
    return test_count;
}

static int load_result(TestEntry *entry) {
    char suffix[160];
    char path[320];
    char line[320];
    FILE *file;

    snprintf(suffix, sizeof(suffix), "results/%s.log", entry->module);
    make_path(path, sizeof(path), suffix);
    file = fopen(path, "r");
    if (file == NULL) {
        entry->status = TEST_PENDING;
        return 0;
    }

    entry->status = TEST_WARNING;
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned int pass;
        unsigned int fail;
        unsigned int skip;
        unsigned int total;

        if (sscanf(line, "SUMMARY\tpass=%u\tfail=%u\tskip=%u\ttotal=%u", &pass, &fail, &skip, &total) == 4) {
            entry->passed = pass;
            entry->failed = fail;
            entry->skipped = skip;
            entry->total = total;
            if (fail != 0) {
                entry->status = TEST_FAIL;
            } else if (skip != 0) {
                entry->status = TEST_WARNING;
            } else {
                entry->status = TEST_PASS;
            }
            fclose(file);
            return 1;
        }
    }

    fclose(file);
    return -1;
}

static void load_results(void) {
    int index;

    for (index = 0; index < test_count; index++) {
        load_result(&tests[index]);
        if (interrupted_module[0] != '\0' && strcmp(tests[index].module, interrupted_module) == 0 && tests[index].status == TEST_PENDING) {
            tests[index].status = TEST_WARNING;
        }
    }
}

static int write_state(const char *mode, int index) {
    char temp_path[320];
    char state_path[320];
    FILE *file;

    make_path(temp_path, sizeof(temp_path), "state.tmp");
    make_path(state_path, sizeof(state_path), "state.tsv");

    file = fopen(temp_path, "w");
    if (file == NULL) {
        return -1;
    }

    if (index >= 0 && index < test_count) {
        fprintf(file, "RUNNING\t%s\t%s\t%d\n", tests[index].module, mode, index);
    } else {
        fprintf(file, "IDLE\n");
    }

    if (fflush(file) != 0 || fclose(file) != 0) {
        remove(temp_path);
        return -1;
    }

    remove(state_path);
    if (rename(temp_path, state_path) != 0) {
        remove(temp_path);
        return -1;
    }

    return 0;
}

static RunState read_state(void) {
    char path[320];
    char line[256];
    FILE *file;
    RunState state;

    memset(&state, 0, sizeof(state));
    state.index = -1;

    make_path(path, sizeof(path), "state.tsv");
    file = fopen(path, "r");
    if (file == NULL) {
        return state;
    }

    if (fgets(line, sizeof(line), file) != NULL &&
        sscanf(line, "RUNNING\t%63[^\t]\t%15[^\t]\t%d", state.module, state.mode, &state.index) == 3) {
        state.running = 1;
    }

    fclose(file);
    return state;
}

static void result_path_for(int index, char *path, size_t path_size) {
    char suffix[160];
    snprintf(suffix, sizeof(suffix), "results/%s.log", tests[index].module);
    make_path(path, path_size, suffix);
}

static int launch_test(int index, const char *mode) {
    SceKernelLoadExecParam parameters;
    char child_path[384];
    char result_path[320];
    char arguments[1024];
    int length;
    int result;

    if (index < 0 || index >= test_count) {
        return -1;
    }

    snprintf(child_path, sizeof(child_path), "%s/%s", root_path, tests[index].relative_path);
    result_path_for(index, result_path, sizeof(result_path));
    remove(result_path);

    if (write_state(mode, index) != 0) {
        return -2;
    }

    length = snprintf(arguments, sizeof(arguments), "%s --psptest-output=%s --psptest-return=%s", child_path, result_path, launcher_path);
    if (length < 0 || (size_t)length >= sizeof(arguments)) {
        write_state("idle", -1);
        return -3;
    }

    memset(&parameters, 0, sizeof(parameters));
    parameters.size = sizeof(parameters);
    parameters.args = (SceSize)(length + 1);
    parameters.argp = arguments;
    parameters.key = NULL;

    print_header("Running");
    pspDebugScreenPrintf("%s\n\n", tests[index].module);
    print_note("The test runs in a separate process and returns here when complete.");

    result = sceKernelLoadExec(child_path, &parameters);
    write_state("idle", -1);
    return result;
}

static int next_test_after(int index, const char *mode) {
    int candidate;

    for (candidate = index + 1; candidate < test_count; candidate++) {
        if (strcmp(mode, "failed") == 0 && tests[candidate].status != TEST_FAIL) {
            continue;
        }
        return candidate;
    }

    return -1;
}

static void count_statuses(int *passed, int *failed, int *warnings, int *pending) {
    int index;

    *passed = 0;
    *failed = 0;
    *warnings = 0;
    *pending = 0;

    for (index = 0; index < test_count; index++) {
        switch (tests[index].status) {
            case TEST_PASS: (*passed)++; break;
            case TEST_FAIL: (*failed)++; break;
            case TEST_WARNING: (*warnings)++; break;
            default: (*pending)++; break;
        }
    }
}

static void show_summary(void) {
    int passed;
    int failed;
    int warnings;
    int pending;

    count_statuses(&passed, &failed, &warnings, &pending);
    print_header("Results");

    set_color(COLOR_WHITE);
    pspDebugScreenPrintf("Tests:    %d\n\n", test_count);

    set_color(COLOR_GREEN);
    pspDebugScreenPrintf("PASS      %d\n", passed);
    set_color(COLOR_RED);
    pspDebugScreenPrintf("FAIL      %d\n", failed);
    set_color(COLOR_AMBER);
    pspDebugScreenPrintf("WARN      %d\n", warnings);
    set_color(COLOR_WHITE);
    pspDebugScreenPrintf("PENDING   %d\n\n", pending);

    if (interrupted_module[0] != '\0') {
        set_color(COLOR_AMBER);
        pspDebugScreenPrintf("Interrupted: %s\n\n", interrupted_module);
    }

    print_note("WARN includes skipped, incomplete, or interrupted tests.");
    print_note("Press O to return.");
    while ((read_press() & PSP_CTRL_CIRCLE) == 0) {
    }
}

static void browse_tests(void) {
    int selected = 0;

    if (test_count == 0) {
        return;
    }

    for (;;) {
        int first = (selected / PAGE_ROWS) * PAGE_ROWS;
        int last = first + PAGE_ROWS;
        int index;
        unsigned int buttons;

        if (last > test_count) {
            last = test_count;
        }

        print_header("Browse");
        for (index = first; index < last; index++) {
            set_color(index == selected ? COLOR_AMBER : COLOR_WHITE);
            pspDebugScreenPrintf("%c %-30s ", index == selected ? '>' : ' ', tests[index].module);
            set_color(status_color(tests[index].status));
            pspDebugScreenPrintf("%s\n", status_name(tests[index].status));
        }

        pspDebugScreenPrintf("\n");
        print_note("UP/DOWN select   X run   O back");

        buttons = read_press();
        if ((buttons & PSP_CTRL_UP) != 0) {
            selected = selected == 0 ? test_count - 1 : selected - 1;
        } else if ((buttons & PSP_CTRL_DOWN) != 0) {
            selected = selected + 1 == test_count ? 0 : selected + 1;
        } else if ((buttons & PSP_CTRL_CROSS) != 0) {
            int result = launch_test(selected, "single");
            if (result < 0) {
                tests[selected].status = TEST_WARNING;
                print_header("Warning");
                set_color(COLOR_AMBER);
                pspDebugScreenPrintf("Could not launch %s\n", tests[selected].module);
                print_note("Press O to return.");
                while ((read_press() & PSP_CTRL_CIRCLE) == 0) {
                }
            }
            return;
        } else if ((buttons & PSP_CTRL_CIRCLE) != 0) {
            return;
        }
    }
}

static void continue_run(const RunState *state) {
    int next;

    if (!state->running || state->index < 0 || state->index >= test_count) {
        return;
    }

    load_results();
    if (strcmp(state->mode, "single") == 0) {
        write_state("idle", -1);
        return;
    }

    next = next_test_after(state->index, state->mode);
    if (next >= 0) {
        launch_test(next, state->mode);
    } else {
        write_state("idle", -1);
    }
}

int main(int argc, char **argv) {
    const char *returned_result;
    RunState state;

    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);

    derive_root_path(argc, argv);

    if (load_manifest() < 0) {
        print_header("Error");
        set_color(COLOR_RED);
        pspDebugScreenPrintf("manifest.tsv could not be opened.\n\n");
        print_note(root_path);
        print_note("Press START to exit.");
        while ((read_press() & PSP_CTRL_START) == 0) {
        }
        sceKernelExitGame();
        return 1;
    }

    {
        char results_path[320];
        make_path(results_path, sizeof(results_path), "results");
        sceIoMkdir(results_path, 0777);
    }

    load_results();
    state = read_state();
    returned_result = argument_value(argc, argv, "--psptest-result");

    if (state.running) {
        char expected_result[320];
        result_path_for(state.index, expected_result, sizeof(expected_result));

        if (returned_result != NULL || load_result(&tests[state.index]) > 0) {
            continue_run(&state);
        } else {
            snprintf(interrupted_module, sizeof(interrupted_module), "%s", state.module);
            tests[state.index].status = TEST_WARNING;
            write_state("idle", -1);
        }
    }

    for (;;) {
        int passed;
        int failed;
        int warnings;
        int pending;
        unsigned int buttons;

        load_results();
        count_statuses(&passed, &failed, &warnings, &pending);

        print_header("Hardware Test Launcher");

        set_color(COLOR_WHITE);
        pspDebugScreenPrintf("Tests: %d   ", test_count);
        set_color(COLOR_GREEN);
        pspDebugScreenPrintf("PASS %d   ", passed);
        set_color(COLOR_RED);
        pspDebugScreenPrintf("FAIL %d   ", failed);
        set_color(COLOR_AMBER);
        pspDebugScreenPrintf("WARN %d   ", warnings);
        set_color(COLOR_WHITE);
        pspDebugScreenPrintf("PENDING %d\n\n", pending);

        if (interrupted_module[0] != '\0') {
            set_color(COLOR_AMBER);
            pspDebugScreenPrintf("Previous test interrupted: %s\n\n", interrupted_module);
        }

        set_color(COLOR_WHITE);
        pspDebugScreenPrintf("X       Run all automated tests\n");
        pspDebugScreenPrintf("O       Browse tests\n");
        pspDebugScreenPrintf("[]      Rerun failures\n");
        pspDebugScreenPrintf("TRIANGLE Results\n");
        pspDebugScreenPrintf("START   Exit\n\n");
        print_note("Each module runs from its own EBOOT.PBP and returns to this launcher.");

        buttons = read_press();
        if ((buttons & PSP_CTRL_CROSS) != 0) {
            if (test_count > 0) {
                launch_test(0, "all");
            }
        } else if ((buttons & PSP_CTRL_CIRCLE) != 0) {
            browse_tests();
        } else if ((buttons & PSP_CTRL_SQUARE) != 0) {
            int first = next_test_after(-1, "failed");
            if (first >= 0) {
                launch_test(first, "failed");
            }
        } else if ((buttons & PSP_CTRL_TRIANGLE) != 0) {
            show_summary();
        } else if ((buttons & PSP_CTRL_START) != 0) {
            sceKernelExitGame();
            return 0;
        }
    }
}
