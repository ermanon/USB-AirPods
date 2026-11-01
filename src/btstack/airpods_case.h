// SPDX-License-Identifier: GPL-3.0-only
#ifndef AIRPODS_CASE_H
#define AIRPODS_CASE_H
#include "btstack.h"
void airpods_case_init(void);
void airpods_case_select_peer(const bd_addr_t peer);
void airpods_case_session(const bd_addr_t peer, bool connected);
void airpods_case_control(const uint8_t *packet, uint16_t size);
bool airpods_case_parked(void);
void airpods_case_reset(void);
#endif
