#ifndef CFLOW_FS_WATCH_PUBLISHER_H
#define CFLOW_FS_WATCH_PUBLISHER_H

#include <salts/fs_watch.h>
#include <cflow/reactive.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_fs_watch_publisher_owner { void *impl; } cflow_fs_watch_publisher_owner;
typedef bool (*cflow_fs_watch_encode_fn)(void *user, const cflow_fs_watch_event *event, void *out_value);
typedef struct cflow_fs_watch_publisher_config {
  bool recursive;
  size_t event_capacity;
  size_t watch_capacity;
  size_t path_capacity;
  size_t native_buffer_capacity;
  const char *name;
  const cmeta_type_desc *output_type;
  cflow_fs_watch_encode_fn encode;
  void *encode_user;
} cflow_fs_watch_publisher_config;

int cflow_fs_watch_publisher_open(cflow_publisher *out, cflow_fs_watch_publisher_owner *owner,
                                  const char *path, const cflow_fs_watch_publisher_config *config);
int cflow_fs_watch_publisher_owner_acknowledge_rescan(cflow_fs_watch_publisher_owner *owner);
bool cflow_fs_watch_publisher_owner_get_stats(const cflow_fs_watch_publisher_owner *owner,
                                               cflow_fs_watch_stats *out);
int cflow_fs_watch_publisher_owner_close(cflow_fs_watch_publisher_owner *owner);

#ifdef __cplusplus
}
#endif
#endif
