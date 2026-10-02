#ifndef DATA_BIND_BINARY_VERSION_H
#define DATA_BIND_BINARY_VERSION_H

#define DATA_BIND_BINARY_VERSION_MAJOR 1
#define DATA_BIND_BINARY_VERSION_MINOR 0
#define DATA_BIND_BINARY_VERSION_PATCH 0
#define DATA_BIND_BINARY_VERSION_STRING "1.0.0"

const char *data_bind_binary_version(void);
void data_bind_binary_version_components(int *major, int *minor, int *patch);
int data_bind_binary_version_compatible(
    int required_major,
    int required_minor,
    int required_patch);

#endif /* DATA_BIND_BINARY_VERSION_H */
