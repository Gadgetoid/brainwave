// A one-client DHCP server for the Brain.
#pragma once

#include <cstdint>

void dhcp_server_init();
uint32_t dhcp_server_leased_address();
