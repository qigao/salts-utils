/*
 * Copyright (c) 2026 Salts Project
 */

#include "tinytest.h"
#define TINYMOCK_GENERATE_FUNCTION_OVERRIDES 1
#define TINYMOCK_SELECTIVE_FUNCTION_OVERRIDES 1
#define TINYMOCK_SELECTED_FUNCTION_fake_nonblocking_read TINYMOCk_PP_PROBE_()
#define TINYMOCK_SELECTED_FUNCTION_fake_nonblocking_write TINYMOCk_PP_PROBE_()
#include "tinymock_function.h"
#include "tlog.h"
#include "salts_serial_internal.h"
#include "salts_serial_test.h"
#include "salts/thread.h"

#include <string.h>

static int fake_handle_marker = 0;
#define FAKE_HANDLE ((salts_port_handle_t *)&fake_handle_marker)

/* Test-owned function declarations script the backend adapters; the selected
 * override set leaves production thread and serial declarations untouched. */
FunctionDeclResult(value, int, CMETA_RESULT_VALUE, fake_nonblocking_read,
             (void *, handle, CMETA_PARAM_IN, &cmeta_type_void_ptr, CMETA_ABI_OBJECT_POINTER),
             (void *, buffer, CMETA_PARAM_IN, &cmeta_type_void_ptr, CMETA_ABI_OBJECT_POINTER),
             (size_t, count, CMETA_PARAM_IN, &cmeta_type_size, CMETA_ABI_SCALAR));
FunctionDeclResult(value, int, CMETA_RESULT_VALUE, fake_nonblocking_write,
             (void *, handle, CMETA_PARAM_IN, &cmeta_type_void_ptr, CMETA_ABI_OBJECT_POINTER),
             (const void *, buffer, CMETA_PARAM_IN, &cmeta_type_void_ptr, CMETA_ABI_OBJECT_POINTER),
             (size_t, count, CMETA_PARAM_IN, &cmeta_type_size, CMETA_ABI_SCALAR));
TINYMOCk_FUNCTION_DECLARE(fake_nonblocking_read);
TINYMOCk_FUNCTION_DECLARE(fake_nonblocking_write);

static unsigned char fake_rx_data[16];
static size_t fake_rx_len;
static unsigned char fake_tx_data[16];
static size_t fake_tx_len;
static size_t storage_clone_calls;

static bool fail_fourth_storage_clone(tstr *out, tstr source) {
  storage_clone_calls++;
  if (storage_clone_calls == 4) return false;
  return salts_serial_port_info_storage_clone_string(out, source);
}

static void fill_port_storage(salts_serial_port_info_storage_t *storage) {
  memset(storage, 0, sizeof(*storage));
  storage->name = tstr_dup("COM7");
  storage->description = tstr_dup("USB serial port");
  storage->usb_manufacturer = tstr_dup("Turbo");
  storage->usb_product = tstr_dup("Bridge");
  storage->usb_serial = tstr_dup("ABC123");
  storage->bluetooth_address = tstr_dup("00:11:22:33:44:55");
  salts_serial_port_info_storage_refresh_view(storage);
}

static size_t mock_min_size(size_t a, size_t b) {
  return a < b ? a : b;
}

static salts_serial_result_t fake_nonblocking_read_impl(salts_port_handle_t *handle, void *buf,
                                                        size_t count, size_t *bytes_read) {
  salts_serial_result_t result = fake_nonblocking_read(handle, buf, count);
  if (result == SALTS_SERIAL_OK && buf && bytes_read) {
    size_t copy_len = mock_min_size(count, fake_rx_len);
    if (copy_len > 0) {
      memcpy(buf, fake_rx_data, copy_len);
      if (fake_rx_len > copy_len) {
        memmove(fake_rx_data, fake_rx_data + copy_len, fake_rx_len - copy_len);
      }
      fake_rx_len -= copy_len;
    }
    *bytes_read = copy_len;
  }
  return result;
}

static salts_serial_result_t fake_nonblocking_write_impl(salts_port_handle_t *handle,
                                                          const void *buf, size_t count,
                                                          size_t *bytes_written) {
  salts_serial_result_t result = fake_nonblocking_write(handle, buf, count);
  if (result == SALTS_SERIAL_OK && buf && bytes_written) {
    size_t copy_len = mock_min_size(count, sizeof(fake_tx_data) - fake_tx_len);
    if (copy_len > 0) {
      memcpy(fake_tx_data + fake_tx_len, buf, copy_len);
      fake_tx_len += copy_len;
    }
    *bytes_written = copy_len;
  }
  return result;
}

static void fake_close(salts_port_handle_t *handle) {
  (void)handle;
}

static const salts_serial_backend_ops_t fake_backend_ops = {
    .close = fake_close,
    .nonblocking_read = fake_nonblocking_read_impl,
    .nonblocking_write = fake_nonblocking_write_impl,
};

suite("salts_serial mocked backend") {
#if defined(_WIN32) || defined(__CYGWIN__)
  it("owns converted Windows metadata through tstr storage traits") {
    salts_serial_port_info_storage_t source = {0};
    salts_serial_port_info_storage_t copy = {0};
    source.name = salts_serial_test_wchar_to_utf8(L"COM7");
    source.description = salts_serial_test_wchar_to_utf8(L"\u4e32\u53e3");
    source.usb_manufacturer = salts_serial_test_wchar_to_utf8(L"");
    check_not_null(source.name);
    check_not_null(source.description);
    check_not_null(source.usb_manufacturer);
    check_equal(tstr_len(source.name), 4u);
    check_equal(tstr_len(source.description), 6u);
    check_equal(tstr_len(source.usb_manufacturer), 0u);
    check_true(salts_serial_port_info_storage_copy(&copy, &source));
    salts_serial_port_info_storage_destroy(&source);
    check_equal(copy.view.name, "COM7");
    check_equal(copy.view.description, "\xe4\xb8\xb2\xe5\x8f\xa3");
    check_equal(copy.view.usb_manufacturer, "");
    salts_serial_port_info_storage_destroy(&copy);
  }
#endif
  static salts_serial_t *serial;

  before_each() {
    int scripted_result = SALTS_SERIAL_OK;
    serial = NULL;
    memset(fake_rx_data, 0, sizeof(fake_rx_data));
    memset(fake_tx_data, 0, sizeof(fake_tx_data));
    fake_rx_len = 0;
    fake_tx_len = 0;

    TINYMOCk_FUNCTION_RESET(fake_nonblocking_read);
    TINYMOCk_FUNCTION_RESET(fake_nonblocking_write);
    check_true(TINYMOCk_FUNCTION_SET_RETURN(fake_nonblocking_read, scripted_result));
    check_true(TINYMOCk_FUNCTION_SET_RETURN(fake_nonblocking_write, scripted_result));

    salts_serial_set_backend_ops_for_testing(&fake_backend_ops);
  }

  after_each() {
    /* Destroy joins the sole pump worker before its borrowed mock history and
     * return state are released, including fatal assertion paths. */
    salts_serial_destroy(serial);
    serial = NULL;
    salts_serial_set_backend_ops_for_testing(NULL);
    TINYMOCk_FUNCTION_DESTROY(fake_nonblocking_read);
    TINYMOCk_FUNCTION_DESTROY(fake_nonblocking_write);
  }

  it("pumps mocked rx bytes into the rx SPSC ring") {
    salts_serial_config_t config;
    char out[4] = {0};
    size_t bytes_read = 0;

    salts_serial_config_default(&config);
    config.rx_buffer_size = 8;
    config.tx_buffer_size = 8;
    config.poll_interval_ms = 5;

    fake_rx_data[0] = 'A';
    fake_rx_data[1] = 'B';
    fake_rx_data[2] = 'C';
    fake_rx_len = 3;

    check_equal(salts_serial_create(&serial, &config), SALTS_SERIAL_OK);
    check_not_null(serial);
    salts_serial_test_set_handle(serial, FAKE_HANDLE);

    check_equal(salts_serial_start_async(serial), SALTS_SERIAL_OK);
    cmeta_sleep_ms(30);
    check_equal(salts_serial_stop_async(serial), SALTS_SERIAL_OK);

    check_equal(salts_serial_rx_available(serial), 3);
    check_equal(salts_serial_read_buffered(serial, out, sizeof(out), &bytes_read),
                SALTS_SERIAL_OK);
    check_equal(bytes_read, 3);
    check_equal(out, "ABC", 3);

    TINYMOCk_FUNCTION_VERIFY_AT_LEAST(fake_nonblocking_read, 1u);
    check_true(TINYMOCk_FUNCTION_ARG_POINTER_EQUAL(
        fake_nonblocking_read, 0u, "handle", FAKE_HANDLE));

    TLOG_DEBUGF("mocked rx pump delivered {} bytes", bytes_read);
  }

  it("drains tx bytes through the mocked backend") {
    salts_serial_config_t config;
    size_t bytes_buffered = 0;

    salts_serial_config_default(&config);
    config.rx_buffer_size = 8;
    config.tx_buffer_size = 8;
    config.poll_interval_ms = 5;

    check_equal(salts_serial_create(&serial, &config), SALTS_SERIAL_OK);
    check_not_null(serial);
    salts_serial_test_set_handle(serial, FAKE_HANDLE);

    check_equal(salts_serial_start_async(serial), SALTS_SERIAL_OK);
    check_equal(salts_serial_write_buffered(serial, "XY", 2, &bytes_buffered),
                SALTS_SERIAL_OK);
    check_equal(bytes_buffered, 2);

    cmeta_sleep_ms(30);
    check_equal(salts_serial_stop_async(serial), SALTS_SERIAL_OK);

    check_equal(fake_tx_len, 2);
    check_equal(fake_tx_data, "XY", 2);

    TINYMOCk_FUNCTION_VERIFY_AT_LEAST(fake_nonblocking_write, 1u);
    check_true(TINYMOCk_FUNCTION_ARG_POINTER_EQUAL(
        fake_nonblocking_write, 0u, "handle", FAKE_HANDLE));

    TLOG_DEBUGF("mocked tx pump drained {} bytes", fake_tx_len);
  }

  it("owns all port-info strings through copy move and repeated destroy") {
    salts_serial_port_info_storage_t source;
    salts_serial_port_info_storage_t moved = {0};
    salts_serial_port_info_vec_t items = {0};
    salts_serial_port_info_storage_t *stored;

    fill_port_storage(&source);
    check_equal(salts_serial_port_info_vec_t_init(&items), STL_OK);
    check_equal(salts_serial_port_info_vec_t_push(&items, source), STL_OK);
    stored = salts_serial_port_info_vec_t_at(&items, 0);
    check_not_null(stored);
    check_equal(tstr_cmp(stored->name, source.name), 0);
    check_equal(tstr_cmp(stored->description, source.description), 0);
    check_equal(tstr_cmp(stored->usb_manufacturer, source.usb_manufacturer), 0);
    check_equal(tstr_cmp(stored->usb_product, source.usb_product), 0);
    check_equal(tstr_cmp(stored->usb_serial, source.usb_serial), 0);
    check_equal(tstr_cmp(stored->bluetooth_address, source.bluetooth_address), 0);
    check(stored->name != source.name);

    salts_serial_port_info_storage_destroy(&source);
    check_equal(stored->view.name, "COM7");
    salts_serial_port_info_storage_move(&moved, stored);
    check_null(stored->name);
    check_equal(moved.view.usb_serial, "ABC123");
    salts_serial_port_info_storage_move(&moved, &moved);
    check_equal(moved.view.usb_serial, "ABC123");
    salts_serial_port_info_storage_destroy(&moved);
    salts_serial_port_info_storage_destroy(&moved);
    salts_serial_port_info_vec_t_destroy(&items);
  }

  it("rolls back every temporary string when an entry copy fails") {
    salts_serial_port_info_storage_t source;
    salts_serial_port_info_storage_t destination = {0};

    fill_port_storage(&source);
    storage_clone_calls = 0;
    check(!salts_serial_port_info_storage_copy_with(
        &destination, &source, fail_fourth_storage_clone));
    check_equal(storage_clone_calls, 4);
    check_null(destination.name);
    check_null(destination.description);
    check_null(destination.usb_manufacturer);
    check_null(destination.usb_product);
    check_null(destination.usb_serial);
    check_null(destination.bluetooth_address);
    salts_serial_port_info_storage_destroy(&source);
    salts_serial_port_info_storage_destroy(&destination);
  }
}
