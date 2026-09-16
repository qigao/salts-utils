#ifndef CFLOW_FS_H
#define CFLOW_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <salts_fs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_fs_service { void *impl; } cflow_fs_service;
typedef enum cflow_fs_operation_kind { CFLOW_FS_STAT = 0, CFLOW_FS_LSTAT, CFLOW_FS_READ_DIRECTORY, CFLOW_FS_MKDIR, CFLOW_FS_RMDIR, CFLOW_FS_RENAME, CFLOW_FS_UNLINK } cflow_fs_operation_kind;
typedef enum cflow_fs_submit_status { CFLOW_FS_SUBMIT_ACCEPTED = 0, CFLOW_FS_SUBMIT_INVALID_ARGUMENT, CFLOW_FS_SUBMIT_FULL, CFLOW_FS_SUBMIT_CLOSED, CFLOW_FS_SUBMIT_ID_EXHAUSTED } cflow_fs_submit_status;
typedef struct cflow_fs_submit_result { cflow_fs_submit_status status; uint64_t request_id; } cflow_fs_submit_result;
typedef enum cflow_fs_cancel_status { CFLOW_FS_CANCEL_REQUESTED = 0, CFLOW_FS_CANCEL_ALREADY_RUNNING, CFLOW_FS_CANCEL_NOT_FOUND, CFLOW_FS_CANCEL_CLOSED, CFLOW_FS_CANCEL_INVALID_ARGUMENT } cflow_fs_cancel_status;
typedef enum cflow_fs_lifecycle { CFLOW_FS_OPEN = 0, CFLOW_FS_CLOSING, CFLOW_FS_CLOSED } cflow_fs_lifecycle;
typedef struct cflow_fs_dir_buffer { salts_fs_dirent_t *entries; size_t entry_capacity; char *names; size_t names_capacity; size_t entry_count; size_t names_used; } cflow_fs_dir_buffer;
typedef void (*cflow_fs_completion_fn)(void *user, uint64_t request_id, cflow_fs_operation_kind operation, int result);
typedef struct cflow_fs_config { size_t worker_count; size_t request_capacity; size_t path_capacity; cflow_fs_completion_fn completion; void *completion_user; } cflow_fs_config;
typedef struct cflow_fs_stats { size_t capacity; size_t accepted; size_t running; size_t completed; size_t cancelled; size_t in_use; size_t rejected_full; size_t rejected_closed; cflow_fs_lifecycle lifecycle; } cflow_fs_stats;

int cflow_fs_service_init(cflow_fs_service *service, const cflow_fs_config *config);
cflow_fs_submit_result cflow_fs_try_stat(cflow_fs_service *service, const char *path, salts_fs_stat_t *out);
cflow_fs_submit_result cflow_fs_try_lstat(cflow_fs_service *service, const char *path, salts_fs_stat_t *out);
cflow_fs_submit_result cflow_fs_try_read_directory(cflow_fs_service *service, const char *path, cflow_fs_dir_buffer *out);
cflow_fs_submit_result cflow_fs_try_mkdir(cflow_fs_service *service, const char *path, int mode);
cflow_fs_submit_result cflow_fs_try_rmdir(cflow_fs_service *service, const char *path);
cflow_fs_submit_result cflow_fs_try_rename(cflow_fs_service *service, const char *old_path, const char *new_path);
cflow_fs_submit_result cflow_fs_try_unlink(cflow_fs_service *service, const char *path);
cflow_fs_cancel_status cflow_fs_try_cancel(cflow_fs_service *service, uint64_t request_id);
int cflow_fs_run_ready(cflow_fs_service *service, size_t max_completions, size_t *completed);
int cflow_fs_close(cflow_fs_service *service);
bool cflow_fs_is_quiescent(const cflow_fs_service *service);
bool cflow_fs_get_stats(const cflow_fs_service *service, cflow_fs_stats *out);
int cflow_fs_destroy(cflow_fs_service *service);

#ifdef __cplusplus
}
#endif
#endif
