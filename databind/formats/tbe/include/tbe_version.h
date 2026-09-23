#ifndef TBE_VERSION_H
#define TBE_VERSION_H

#define TBE_VERSION_MAJOR 1
#define TBE_VERSION_MINOR 0
#define TBE_VERSION_PATCH 0

#define TBE_VERSION_STRING "1.0.0"

/**
 * @brief Get the TBE format/backend version string.
 * @return A static string in the format "MAJOR.MINOR.PATCH".
 */
const char *tbe_version(void);

/**
 * @brief Get the library version as separate components.
 * @param major Pointer to store major version (can be NULL).
 * @param minor Pointer to store minor version (can be NULL).
 * @param patch Pointer to store patch version (can be NULL).
 */
void tbe_version_components(int *major, int *minor, int *patch);

/**
 * @brief Check if the library version is compatible with required version.
 * @param required_major Required major version.
 * @param required_minor Required minor version.
 * @param required_patch Required patch version.
 * @return 1 if compatible, 0 if not compatible.
 */
int tbe_version_compatible(int required_major, int required_minor, int required_patch);


#endif /* TBE_VERSION_H */
