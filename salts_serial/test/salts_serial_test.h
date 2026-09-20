/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 Salts Project
 */

#ifndef SALTS_SERIAL_TEST_H
#define SALTS_SERIAL_TEST_H

#include "salts_serial_internal.h"

void salts_serial_set_backend_ops_for_testing(const salts_serial_backend_ops_t *ops);
void salts_serial_test_set_handle(salts_serial_t *serial, salts_port_handle_t *handle);

#endif
