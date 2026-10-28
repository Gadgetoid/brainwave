// A TinyUSB application class driver: RNDIS initialise, address query and packet filter, then Ethernet frames over bulk endpoints.
#include "rndis_host.h"

#include <cstring>

#include "lwip/pbuf.h"
#include "pico/time.h"
#include "tusb.h"
#include "host/usbh_pvt.h"

#include "brain_link.h"
#include "log.h"

namespace rndis {

namespace {

constexpr uint32_t message_packet = 0x00000001;
constexpr uint32_t message_initialise = 0x00000002;
constexpr uint32_t message_query = 0x00000004;
constexpr uint32_t message_set = 0x00000005;
constexpr uint32_t message_keepalive = 0x00000008;
constexpr uint32_t completion_bit = 0x80000000;
constexpr uint32_t oid_permanent_address = 0x01010101;
constexpr uint32_t oid_packet_filter = 0x0001010e;
constexpr uint32_t filter_directed_multicast_broadcast = 0x0000000b;
constexpr uint32_t packet_header_bytes = 44;
constexpr uint32_t receive_bytes = 16384;
constexpr uint32_t response_bytes = 1024;
constexpr uint32_t frame_bytes_max = 1600;
constexpr size_t transmit_slots = 8;
constexpr int response_attempts = 50;
constexpr uint32_t response_poll_ms = 20;
constexpr uint32_t retry_ms = 2000;
constexpr uint32_t receive_retry_ms = 20;
constexpr int receive_failure_limit = 10;

enum class Step { none, initialise, query_address, set_filter };
enum class Phase { absent, idle, sending, awaiting, polling, up, failed };

struct TransmitSlot {
    uint16_t length;
    uint8_t bytes[packet_header_bytes + frame_bytes_max + 1];
};

uint8_t device = 0;
uint8_t control_interface = 0;
uint8_t data_interface = 0;
uint8_t endpoint_in = 0;
uint8_t endpoint_out = 0;
uint16_t out_packet_size = 64;
bool configured = false;
Phase phase = Phase::absent;
Step step = Step::none;
uint32_t request_id = 0;
int attempts = 0;
uint32_t next_action_ms = 0;
uint8_t mac_address[6];

uint8_t command[64] __attribute__((aligned(4)));
uint16_t command_length = 0;
uint8_t response[response_bytes] __attribute__((aligned(4)));
tusb_control_request_t request;
uint8_t receive_buffer[receive_bytes] __attribute__((aligned(4)));

TransmitSlot transmit_queue[transmit_slots];
size_t transmit_head = 0;
size_t transmit_count = 0;
bool transmitting = false;
bool receiving = false;
bool receive_retry_pending = false;
uint32_t receive_retry_at = 0;
int receive_failures = 0;

uint32_t count_in = 0;
uint32_t count_out = 0;
uint32_t count_dropped = 0;

uint32_t now_ms() {
    return to_ms_since_boot(get_absolute_time());
}

void put32(uint8_t *bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
    bytes[2] = static_cast<uint8_t>(value >> 16);
    bytes[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t get32(const uint8_t *bytes) {
    return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | static_cast<uint32_t>(bytes[3]) << 24;
}

void build_command(Step for_step) {
    memset(command, 0, sizeof(command));
    request_id++;
    switch (for_step) {
    case Step::initialise:
        put32(command, message_initialise);
        put32(command + 4, 24);
        put32(command + 8, request_id);
        put32(command + 12, 1);
        put32(command + 16, 0);
        put32(command + 20, receive_bytes);
        command_length = 24;
        break;
    case Step::query_address:
        put32(command, message_query);
        put32(command + 4, 28);
        put32(command + 8, request_id);
        put32(command + 12, oid_permanent_address);
        put32(command + 20, 20);
        command_length = 28;
        break;
    case Step::set_filter:
        put32(command, message_set);
        put32(command + 4, 32);
        put32(command + 8, request_id);
        put32(command + 12, oid_packet_filter);
        put32(command + 16, 4);
        put32(command + 20, 20);
        put32(command + 28, filter_directed_multicast_broadcast);
        command_length = 32;
        break;
    case Step::none:
        break;
    }
}

void begin_step(Step next) {
    step = next;
    attempts = 0;
    build_command(next);
    phase = Phase::idle;
    next_action_ms = now_ms();
}

void start_receive();

void link_up() {
    receiving = false;
    receive_retry_pending = false;
    receive_failures = 0;
    phase = Phase::up;
    step = Step::none;
    log_line("rndis: up, Brain MAC %02x:%02x:%02x:%02x:%02x:%02x", mac_address[0], mac_address[1], mac_address[2], mac_address[3],
             mac_address[4], mac_address[5]);
    brain_link_set_up(true);
    start_receive();
}

void step_finished(bool answered, uint32_t status, uint32_t length) {
    switch (step) {
    case Step::initialise:
        if (!answered) {
            log_line("rndis: no answer to initialise; assuming the Brain is already initialised");
        } else if (status) {
            log_line("rndis: initialise status %08lx", static_cast<unsigned long>(status));
        }
        begin_step(Step::query_address);
        break;
    case Step::query_address:
        if (answered && !status && length >= 24 + 6 && get32(response + 16) >= 6 && 8 + get32(response + 20) + 6 <= length) {
            memcpy(mac_address, response + 8 + get32(response + 20), 6);
        }
        begin_step(Step::set_filter);
        break;
    case Step::set_filter:
        if (!answered || status) {
            log_line("rndis: packet filter failed; retrying");
            phase = Phase::failed;
            next_action_ms = now_ms() + retry_ms;
            return;
        }
        link_up();
        break;
    case Step::none:
        break;
    }
}

void response_complete(tuh_xfer_t *transfer) {
    if (transfer->daddr != device || phase != Phase::polling) {
        return;
    }
    uint32_t length = transfer->actual_len;
    uint32_t expected = get32(command) | completion_bit;
    if (transfer->result == XFER_RESULT_SUCCESS && length >= 16) {
        uint32_t type = get32(response);
        if (type == message_keepalive) {
            log_line("rndis: keepalive from the Brain");
        } else if (type == expected && get32(response + 8) == request_id) {
            step_finished(true, get32(response + 12), length);
            return;
        }
    }
    if (++attempts >= response_attempts) {
        step_finished(false, 0, 0);
        return;
    }
    phase = Phase::awaiting;
    next_action_ms = now_ms() + response_poll_ms;
}

void command_complete(tuh_xfer_t *transfer) {
    if (transfer->daddr != device || phase != Phase::sending) {
        return;
    }
    if (transfer->result != XFER_RESULT_SUCCESS) {
        log_line("rndis: command %08lx not sent (%d)", static_cast<unsigned long>(get32(command)), transfer->result);
        step_finished(false, 0, 0);
        return;
    }
    phase = Phase::awaiting;
    next_action_ms = now_ms();
}

bool control(uint8_t direction, uint8_t code, uint8_t *buffer, uint16_t length, tuh_xfer_cb_t complete) {
    request = {};
    request.bmRequestType_bit.recipient = TUSB_REQ_RCPT_INTERFACE;
    request.bmRequestType_bit.type = TUSB_REQ_TYPE_CLASS;
    request.bmRequestType_bit.direction = direction;
    request.bRequest = code;
    request.wValue = 0;
    request.wIndex = control_interface;
    request.wLength = length;
    tuh_xfer_t transfer = {};
    transfer.daddr = device;
    transfer.ep_addr = 0;
    transfer.setup = &request;
    transfer.buffer = buffer;
    transfer.complete_cb = complete;
    return tuh_control_xfer(&transfer);
}

void start_receive() {
    if (phase != Phase::up || receiving) {
        return;
    }
    if (!usbh_edpt_claim(device, endpoint_in)) {
        return;
    }
    if (!usbh_edpt_xfer(device, endpoint_in, receive_buffer, receive_bytes)) {
        usbh_edpt_release(device, endpoint_in);
        return;
    }
    receiving = true;
}

void start_transmit() {
    if (transmitting || !transmit_count || phase != Phase::up) {
        return;
    }
    TransmitSlot &slot = transmit_queue[transmit_head];
    if (!usbh_edpt_claim(device, endpoint_out)) {
        return;
    }
    if (!usbh_edpt_xfer(device, endpoint_out, slot.bytes, slot.length)) {
        usbh_edpt_release(device, endpoint_out);
        return;
    }
    transmitting = true;
}

void handle_received(const uint8_t *data, uint32_t total) {
    uint32_t offset = 0;
    while (total - offset >= 8) {
        uint32_t type = get32(data + offset);
        uint32_t message_length = get32(data + offset + 4);
        if (message_length < 8 || message_length > total - offset) {
            break;
        }
        if (type == message_packet && message_length >= packet_header_bytes) {
            uint32_t data_offset = get32(data + offset + 8);
            uint32_t data_length = get32(data + offset + 12);
            if (data_offset <= message_length - 8 && data_length <= message_length - 8 - data_offset) {
                count_in++;
                brain_link_input(data + offset + 8 + data_offset, data_length);
            }
        }
        offset += message_length;
    }
}

void reset_state() {
    receiving = false;
    receive_retry_pending = false;
    receive_failures = 0;
    configured = false;
    phase = Phase::absent;
    step = Step::none;
    transmit_count = 0;
    transmitting = false;
    device = 0;
}

bool driver_init() {
    reset_state();
    return true;
}

bool driver_deinit() {
    reset_state();
    return true;
}

bool is_rndis_control(const tusb_desc_interface_t *interface) {
    uint8_t interface_class = interface->bInterfaceClass;
    uint8_t subclass = interface->bInterfaceSubClass;
    uint8_t protocol = interface->bInterfaceProtocol;
    return (interface_class == TUSB_CLASS_CDC && subclass == 0x02 && protocol == 0xff) ||
           (interface_class == TUSB_CLASS_WIRELESS_CONTROLLER && subclass == 0x01 && protocol == 0x03) ||
           (interface_class == TUSB_CLASS_MISC && subclass == 0x04 && protocol == 0x01);
}

uint16_t driver_open(uint8_t rhport, uint8_t dev_addr, const tusb_desc_interface_t *interface, uint16_t max_len) {
    if (device || !is_rndis_control(interface)) {
        return 0;
    }
    const uint8_t *start = reinterpret_cast<const uint8_t *>(interface);
    const uint8_t *end = start + max_len;
    const uint8_t *cursor = tu_desc_next(start);
    const tusb_desc_interface_t *data = nullptr;
    while (tu_desc_in_bounds(cursor, end) && tu_desc_len(cursor)) {
        if (tu_desc_type(cursor) == TUSB_DESC_INTERFACE) {
            data = reinterpret_cast<const tusb_desc_interface_t *>(cursor);
            break;
        }
        cursor = tu_desc_next(cursor);
    }
    if (!data || data->bInterfaceClass != TUSB_CLASS_CDC_DATA) {
        return 0;
    }
    uint8_t found_in = 0;
    uint8_t found_out = 0;
    uint16_t found_out_size = 64;
    cursor = tu_desc_next(cursor);
    while (tu_desc_in_bounds(cursor, end) && tu_desc_len(cursor) && tu_desc_type(cursor) != TUSB_DESC_INTERFACE) {
        if (tu_desc_type(cursor) == TUSB_DESC_ENDPOINT) {
            const tusb_desc_endpoint_t *endpoint = reinterpret_cast<const tusb_desc_endpoint_t *>(cursor);
            if (endpoint->bmAttributes.xfer == TUSB_XFER_BULK) {
                if (!tuh_edpt_open(dev_addr, endpoint)) {
                    return 0;
                }
                if (tu_edpt_dir(endpoint->bEndpointAddress) == TUSB_DIR_IN) {
                    found_in = endpoint->bEndpointAddress;
                } else {
                    found_out = endpoint->bEndpointAddress;
                    found_out_size = tu_edpt_packet_size(endpoint);
                }
            }
        }
        cursor = tu_desc_next(cursor);
    }
    if (!found_in || !found_out) {
        return 0;
    }
    device = dev_addr;
    control_interface = interface->bInterfaceNumber;
    data_interface = data->bInterfaceNumber;
    endpoint_in = found_in;
    endpoint_out = found_out;
    out_packet_size = found_out_size ? found_out_size : 64;
    memset(mac_address, 0, sizeof(mac_address));
    log_line("rndis: interfaces %u and %u, bulk in %02x out %02x (%u bytes)", control_interface, data_interface, endpoint_in,
             endpoint_out, out_packet_size);
    return static_cast<uint16_t>(cursor - start);
}

bool driver_set_config(uint8_t dev_addr, uint8_t interface_number) {
    if (dev_addr != device || interface_number != control_interface) {
        return false;
    }
    configured = true;
    usbh_driver_set_config_complete(dev_addr, data_interface);
    begin_step(Step::initialise);
    return true;
}

bool driver_transfer(uint8_t dev_addr, uint8_t endpoint, xfer_result_t result, uint32_t bytes) {
    if (dev_addr != device) {
        return false;
    }
    if (endpoint == endpoint_in) {
        receiving = false;
        if (result != XFER_RESULT_SUCCESS) {
            receive_failures++;
            log_line("rndis: receive result %d (%d in a row)", result, receive_failures);
            if (receive_failures >= receive_failure_limit) {
                log_line("rndis: link failing; reinitialising");
                receive_failures = 0;
                brain_link_set_up(false);
                phase = Phase::failed;
                next_action_ms = now_ms() + retry_ms;
                return true;
            }
            receive_retry_pending = true;
            receive_retry_at = now_ms() + receive_retry_ms;
            return true;
        }
        receive_failures = 0;
        if (bytes) {
            handle_received(receive_buffer, bytes);
        }
        start_receive();
    } else if (endpoint == endpoint_out) {
        transmitting = false;
        if (result == XFER_RESULT_SUCCESS) {
            count_out++;
        } else {
            log_line("rndis: send result %d", result);
        }
        transmit_head = (transmit_head + 1) % transmit_slots;
        transmit_count--;
        start_transmit();
    }
    return true;
}

void driver_close(uint8_t dev_addr) {
    if (dev_addr != device) {
        return;
    }
    log_line("rndis: Brain disconnected");
    reset_state();
    brain_link_set_up(false);
}

const usbh_class_driver_t driver = {
    .name = "RNDIS",
    .init = driver_init,
    .deinit = driver_deinit,
    .open = driver_open,
    .set_config = driver_set_config,
    .xfer_cb = driver_transfer,
    .close = driver_close,
};

}

void task(uint32_t now) {
    if (receive_retry_pending && phase == Phase::up && (int32_t)(now - receive_retry_at) >= 0) {
        receive_retry_pending = false;
        start_receive();
    }
    if (!configured || (int32_t)(now - next_action_ms) < 0) {
        return;
    }
    switch (phase) {
    case Phase::failed:
        begin_step(Step::initialise);
        break;
    case Phase::idle:
        phase = Phase::sending;
        if (!control(TUSB_DIR_OUT, 0x00, command, command_length, command_complete)) {
            phase = Phase::idle;
            next_action_ms = now + response_poll_ms;
        }
        break;
    case Phase::awaiting:
        phase = Phase::polling;
        if (!control(TUSB_DIR_IN, 0x01, response, response_bytes, response_complete)) {
            phase = Phase::awaiting;
            next_action_ms = now + response_poll_ms;
        }
        break;
    default:
        break;
    }
}

bool is_up() {
    return phase == Phase::up;
}

bool send_frame(const pbuf *frame) {
    if (phase != Phase::up || frame->tot_len > frame_bytes_max) {
        return false;
    }
    if (transmit_count == transmit_slots) {
        count_dropped++;
        return false;
    }
    TransmitSlot &slot = transmit_queue[(transmit_head + transmit_count) % transmit_slots];
    uint32_t length = packet_header_bytes + frame->tot_len;
    uint32_t message_length = length % out_packet_size ? length : length + 1;
    memset(slot.bytes, 0, packet_header_bytes);
    put32(slot.bytes, message_packet);
    put32(slot.bytes + 4, message_length);
    put32(slot.bytes + 8, packet_header_bytes - 8);
    put32(slot.bytes + 12, frame->tot_len);
    pbuf_copy_partial(frame, slot.bytes + packet_header_bytes, frame->tot_len, 0);
    if (message_length > length) {
        slot.bytes[length] = 0;
    }
    slot.length = static_cast<uint16_t>(message_length);
    transmit_count++;
    start_transmit();
    return true;
}

uint32_t frames_in() {
    return count_in;
}

uint32_t frames_out() {
    return count_out;
}

uint32_t frames_dropped() {
    return count_dropped;
}

}

extern "C" const usbh_class_driver_t *usbh_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &rndis::driver;
}
