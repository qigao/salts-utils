#ifndef CFLOW_FS_WATCH_H
#define CFLOW_FS_WATCH_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_fs_watch { void *impl; } cflow_fs_watch;
typedef enum cflow_fs_watch_event_kind { CFLOW_FS_WATCH_CREATED = 0, CFLOW_FS_WATCH_REMOVED, CFLOW_FS_WATCH_MODIFIED, CFLOW_FS_WATCH_ATTRIBUTES, CFLOW_FS_WATCH_RENAMED, CFLOW_FS_WATCH_ROOT_CHANGED, CFLOW_FS_WATCH_RESCAN_REQUIRED } cflow_fs_watch_event_kind;
typedef enum cflow_fs_watch_entry_type { CFLOW_FS_WATCH_ENTRY_UNKNOWN = 0, CFLOW_FS_WATCH_ENTRY_FILE, CFLOW_FS_WATCH_ENTRY_DIRECTORY } cflow_fs_watch_entry_type;
typedef struct cflow_fs_watch_event { cflow_fs_watch_event_kind kind; const char *path; const char *old_path; cflow_fs_watch_entry_type entry_type; } cflow_fs_watch_event;
typedef void (*cflow_fs_watch_event_fn)(void *user, const cflow_fs_watch_event *event);
typedef struct cflow_fs_watch_config { bool recursive; size_t event_capacity; size_t watch_capacity; size_t path_capacity; size_t native_buffer_capacity; cflow_fs_watch_event_fn event; void *event_user; } cflow_fs_watch_config;
typedef enum cflow_fs_watch_lifecycle { CFLOW_FS_WATCH_OPEN = 0, CFLOW_FS_WATCH_CLOSING, CFLOW_FS_WATCH_CLOSED } cflow_fs_watch_lifecycle;
typedef struct cflow_fs_watch_stats { size_t capacity; size_t queued; size_t delivered; size_t suppressed; size_t rescan_required; bool awaiting_rescan; cflow_fs_watch_lifecycle lifecycle; } cflow_fs_watch_stats;

int cflow_fs_watch_open(cflow_fs_watch *watch, const char *path, const cflow_fs_watch_config *config);
int cflow_fs_watch_run_ready(cflow_fs_watch *watch, size_t max_events, size_t *delivered);
int cflow_fs_watch_acknowledge_rescan(cflow_fs_watch *watch);
int cflow_fs_watch_close(cflow_fs_watch *watch);
bool cflow_fs_watch_is_quiescent(const cflow_fs_watch *watch);
bool cflow_fs_watch_get_stats(const cflow_fs_watch *watch, cflow_fs_watch_stats *out);
int cflow_fs_watch_destroy(cflow_fs_watch *watch);

#ifdef __cplusplus
}
#endif
#endif
