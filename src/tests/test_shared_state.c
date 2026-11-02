#include "../embed/gcl_shared_state.h"

#ifdef _WIN32
#define GCL_TEST_STATE_PATH "src\\tests\\gcl_shared_state_test.state"
#define SET_ENV(name, value) _putenv_s(name, value)
#else
#define GCL_TEST_STATE_PATH "src/tests/gcl_shared_state_test.state"
#define SET_ENV(name, value) setenv(name, value, 1)
#endif

int main(void) {
    char value[32] = { 0 };
    char boundary_key[GCL_SHM_KEY_LEN];
    char long_key[GCL_SHM_KEY_LEN + 1];
    int failed = 0;

    remove(GCL_TEST_STATE_PATH);
    if (SET_ENV("GCL_SHARED_FILE", GCL_TEST_STATE_PATH) != 0) {
        return 1;
    }

    if (!gcl_shared_set("short-key", "42") ||
        !gcl_shared_get("short-key", value, sizeof(value)) ||
        strcmp(value, "42") != 0) {
        fprintf(stderr, "short keys must round-trip\n");
        failed = 1;
    }

    memset(boundary_key, 'k', sizeof(boundary_key) - 1);
    boundary_key[sizeof(boundary_key) - 1] = '\0';
    memset(long_key, 'k', sizeof(long_key) - 1);
    long_key[sizeof(long_key) - 1] = '\0';
    if (!gcl_shared_set(boundary_key, "boundary") ||
        !gcl_shared_get(boundary_key, value, sizeof(value)) ||
        strcmp(value, "boundary") != 0) {
        fprintf(stderr, "maximum-length keys must round-trip\n");
        failed = 1;
    }

    if (gcl_shared_set(long_key, "too long") ||
        gcl_shared_get(long_key, value, sizeof(value)) ||
        !gcl_shared_get(boundary_key, value, sizeof(value)) ||
        strcmp(value, "boundary") != 0) {
        fprintf(stderr, "overlong keys must be rejected\n");
        failed = 1;
    }

    gcl_shared_cleanup();
    remove(GCL_TEST_STATE_PATH);
    return failed;
}
