// USB host driver for the Brain's RNDIS network function.
#pragma once

#include <cstddef>
#include <cstdint>

struct pbuf;

namespace rndis {

void task(uint32_t now_ms);
bool is_up();
bool send_frame(const pbuf *frame);
uint32_t frames_in();
uint32_t frames_out();
uint32_t frames_dropped();

}
