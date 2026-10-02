#include "data_bind_binary_version.h"

const char *data_bind_binary_version(void) {
    return DATA_BIND_BINARY_VERSION_STRING;
}

void data_bind_binary_version_components(int *major, int *minor, int *patch) {
    if (major) *major = DATA_BIND_BINARY_VERSION_MAJOR;
    if (minor) *minor = DATA_BIND_BINARY_VERSION_MINOR;
    if (patch) *patch = DATA_BIND_BINARY_VERSION_PATCH;
}

int data_bind_binary_version_compatible(
    int required_major,
    int required_minor,
    int required_patch) {
    (void)required_patch;
    if (DATA_BIND_BINARY_VERSION_MAJOR != required_major) return 0;
    if (DATA_BIND_BINARY_VERSION_MINOR < required_minor) return 0;
    return 1;
}
