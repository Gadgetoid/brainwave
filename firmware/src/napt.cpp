// Rewrites TCP, UDP and ICMP packets in place with incremental checksum updates; host-tested in tests/napt_test.cpp.
#include "napt.h"

namespace napt {

namespace {

constexpr uint8_t protocol_icmp = 1;
constexpr uint8_t protocol_tcp = 6;
constexpr uint8_t protocol_udp = 17;
constexpr uint8_t icmp_echo_reply = 0;
constexpr uint8_t icmp_unreachable = 3;
constexpr uint8_t icmp_echo_request = 8;
constexpr uint8_t icmp_time_exceeded = 11;
constexpr uint8_t icmp_parameter_problem = 12;
constexpr uint8_t tcp_flag_fin = 0x01;
constexpr uint8_t tcp_flag_rst = 0x04;
constexpr uint16_t dynamic_port_first = 20000;
constexpr uint16_t dynamic_port_count = 10000;
constexpr uint16_t preserved_port_first = 1024;
constexpr uint16_t preserved_port_end = 49152;
constexpr uint32_t tcp_idle_ms = 30 * 60 * 1000;
constexpr uint32_t tcp_closing_ms = 20 * 1000;
constexpr uint32_t udp_idle_ms = 120 * 1000;
constexpr uint32_t icmp_idle_ms = 30 * 1000;

struct Header {
    size_t header_bytes;
    size_t total;
    uint8_t protocol;
    bool later_fragment;
};

bool parse(const uint8_t *packet, size_t length, Header &header) {
    if (length < 20 || (packet[0] >> 4) != 4) {
        return false;
    }
    header.header_bytes = static_cast<size_t>(packet[0] & 0x0f) * 4;
    header.total = read16(packet + 2);
    if (header.header_bytes < 20 || header.total < header.header_bytes || header.total > length) {
        return false;
    }
    header.protocol = packet[9];
    header.later_fragment = (read16(packet + 6) & 0x1fff) != 0;
    return true;
}

size_t l4_minimum(uint8_t protocol) {
    switch (protocol) {
    case protocol_tcp:
        return 20;
    case protocol_udp:
        return 8;
    case protocol_icmp:
        return 8;
    default:
        return 0;
    }
}

uint8_t *l4_checksum_field(uint8_t *l4, uint8_t protocol) {
    if (protocol == protocol_tcp) {
        return l4 + 16;
    }
    if (protocol == protocol_udp) {
        return read16(l4 + 6) ? l4 + 6 : nullptr;
    }
    return nullptr;
}

void fix_udp_zero(uint8_t *field, uint8_t protocol) {
    if (field && protocol == protocol_udp && read16(field) == 0) {
        write16(field, 0xffff);
    }
}

bool decrement_ttl(uint8_t *packet) {
    if (packet[8] <= 1) {
        return false;
    }
    uint16_t old_word = read16(packet + 8);
    packet[8]--;
    adjust_checksum16(packet + 10, old_word, read16(packet + 8));
    return true;
}

uint32_t idle_limit(uint8_t protocol, bool closing) {
    if (protocol == protocol_tcp) {
        return closing ? tcp_closing_ms : tcp_idle_ms;
    }
    if (protocol == protocol_udp) {
        return udp_idle_ms;
    }
    return icmp_idle_ms;
}

}

uint16_t read16(const uint8_t *bytes) {
    return static_cast<uint16_t>(bytes[0] << 8 | bytes[1]);
}

uint32_t read32(const uint8_t *bytes) {
    return static_cast<uint32_t>(bytes[0]) << 24 | static_cast<uint32_t>(bytes[1]) << 16 | static_cast<uint32_t>(bytes[2]) << 8 | bytes[3];
}

void write16(uint8_t *bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value >> 8);
    bytes[1] = static_cast<uint8_t>(value);
}

void write32(uint8_t *bytes, uint32_t value) {
    write16(bytes, static_cast<uint16_t>(value >> 16));
    write16(bytes + 2, static_cast<uint16_t>(value));
}

void adjust_checksum16(uint8_t *field, uint16_t old_word, uint16_t new_word) {
    uint32_t sum = static_cast<uint16_t>(~read16(field));
    sum += static_cast<uint16_t>(~old_word);
    sum += new_word;
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    write16(field, static_cast<uint16_t>(~sum));
}

void adjust_checksum32(uint8_t *field, uint32_t old_value, uint32_t new_value) {
    adjust_checksum16(field, static_cast<uint16_t>(old_value >> 16), static_cast<uint16_t>(new_value >> 16));
    adjust_checksum16(field, static_cast<uint16_t>(old_value), static_cast<uint16_t>(new_value));
}

uint16_t compute_checksum(const uint8_t *bytes, size_t length, uint32_t initial) {
    uint32_t sum = initial;
    for (size_t index = 0; index + 1 < length; index += 2) {
        sum += read16(bytes + index);
    }
    if (length & 1) {
        sum += static_cast<uint32_t>(bytes[length - 1]) << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

void Translator::configure_inside(uint32_t network, uint32_t mask) {
    inside_network = network & mask;
    inside_mask = mask;
}

void Translator::set_outside_address(uint32_t address) {
    if (address != outside) {
        clear();
    }
    outside = address;
}

void Translator::clear() {
    for (Mapping &mapping : mappings) {
        if (!mapping.fixed) {
            mapping.used = false;
        }
    }
}

bool Translator::add_forward(uint8_t protocol, uint32_t inside_address, uint16_t port) {
    remove_forward(protocol, port);
    Mapping *existing = find_outside(protocol, port);
    if (existing) {
        existing->used = false;
    }
    for (Mapping &mapping : mappings) {
        if (!mapping.used) {
            mapping = Mapping{true, false, true, protocol, port, port, inside_address, 0};
            return true;
        }
    }
    return false;
}

void Translator::remove_forward(uint8_t protocol, uint16_t port) {
    for (Mapping &mapping : mappings) {
        if (mapping.used && mapping.fixed && mapping.protocol == protocol && mapping.outside_port == port) {
            mapping.used = false;
            mapping.fixed = false;
        }
    }
}

size_t Translator::active_mappings() const {
    size_t count = 0;
    for (const Mapping &mapping : mappings) {
        count += mapping.used ? 1 : 0;
    }
    return count;
}

void Translator::expire(uint32_t now_ms) {
    for (Mapping &mapping : mappings) {
        if (mapping.used && !mapping.fixed && now_ms - mapping.last_ms > idle_limit(mapping.protocol, mapping.closing)) {
            mapping.used = false;
        }
    }
}

bool Translator::is_inside(uint32_t address) const {
    return (address & inside_mask) == inside_network;
}

Translator::Mapping *Translator::find_inside(uint8_t protocol, uint32_t address, uint16_t port) {
    for (Mapping &mapping : mappings) {
        if (mapping.used && mapping.protocol == protocol && mapping.inside_address == address && mapping.inside_port == port) {
            return &mapping;
        }
    }
    return nullptr;
}

Translator::Mapping *Translator::find_outside(uint8_t protocol, uint16_t port) {
    for (Mapping &mapping : mappings) {
        if (mapping.used && mapping.protocol == protocol && mapping.outside_port == port) {
            return &mapping;
        }
    }
    return nullptr;
}

bool Translator::outside_port_used(uint8_t protocol, uint16_t port) {
    return find_outside(protocol, port) != nullptr;
}

Translator::Mapping *Translator::create(uint8_t protocol, uint32_t address, uint16_t port, uint32_t now_ms) {
    Mapping *slot = nullptr;
    for (Mapping &mapping : mappings) {
        if (mapping.fixed) {
            continue;
        }
        if (!mapping.used) {
            slot = &mapping;
            break;
        }
        if (!slot || now_ms - mapping.last_ms > now_ms - slot->last_ms) {
            slot = &mapping;
        }
    }
    if (!slot) {
        return nullptr;
    }
    slot->used = false;

    uint16_t outside_port = 0;
    if (port >= preserved_port_first && port < preserved_port_end && !outside_port_used(protocol, port)) {
        outside_port = port;
    }
    for (uint16_t attempt = 0; !outside_port && attempt < dynamic_port_count; attempt++) {
        uint16_t candidate = static_cast<uint16_t>(dynamic_port_first + next_port);
        next_port = static_cast<uint16_t>((next_port + 1) % dynamic_port_count);
        if (!outside_port_used(protocol, candidate)) {
            outside_port = candidate;
        }
    }
    if (!outside_port) {
        return nullptr;
    }
    *slot = Mapping{true, false, false, protocol, port, outside_port, address, now_ms};
    return slot;
}

Verdict Translator::outbound(uint8_t *packet, size_t length, uint32_t now_ms) {
    Header header;
    if (!parse(packet, length, header)) {
        return Verdict::drop;
    }
    uint32_t source = read32(packet + 12);
    uint32_t destination = read32(packet + 16);
    if (is_inside(destination) || destination == 0xffffffff || (destination >> 28) == 0xe) {
        return Verdict::local;
    }
    if (!outside || destination == outside) {
        return outside ? Verdict::local : Verdict::drop;
    }
    if (!is_inside(source)) {
        return Verdict::drop;
    }
    if (header.later_fragment) {
        if (!decrement_ttl(packet)) {
            return Verdict::drop;
        }
        write32(packet + 12, outside);
        adjust_checksum32(packet + 10, source, outside);
        return Verdict::forward;
    }

    size_t minimum = l4_minimum(header.protocol);
    if (!minimum || header.header_bytes + minimum > header.total) {
        return Verdict::drop;
    }
    uint8_t *l4 = packet + header.header_bytes;
    uint8_t *port_field = l4;
    uint8_t *checksum_field = l4_checksum_field(l4, header.protocol);
    if (header.protocol == protocol_icmp) {
        if (l4[0] != icmp_echo_request) {
            return Verdict::drop;
        }
        port_field = l4 + 4;
        checksum_field = l4 + 2;
    }

    if (!decrement_ttl(packet)) {
        return Verdict::drop;
    }
    uint16_t inside_port = read16(port_field);
    Mapping *mapping = find_inside(header.protocol, source, inside_port);
    if (!mapping) {
        mapping = create(header.protocol, source, inside_port, now_ms);
        if (!mapping) {
            return Verdict::drop;
        }
    }
    mapping->last_ms = now_ms;
    if (header.protocol == protocol_tcp && (l4[13] & (tcp_flag_fin | tcp_flag_rst))) {
        mapping->closing = true;
    }

    write16(port_field, mapping->outside_port);
    if (checksum_field) {
        adjust_checksum16(checksum_field, inside_port, mapping->outside_port);
        if (header.protocol != protocol_icmp) {
            adjust_checksum32(checksum_field, source, outside);
        }
        fix_udp_zero(checksum_field, header.protocol);
    }
    write32(packet + 12, outside);
    adjust_checksum32(packet + 10, source, outside);
    return Verdict::forward;
}

Verdict Translator::inbound(uint8_t *packet, size_t length, uint32_t now_ms) {
    Header header;
    if (!outside || !parse(packet, length, header)) {
        return Verdict::local;
    }
    uint32_t destination = read32(packet + 16);
    if (destination != outside || header.later_fragment) {
        return Verdict::local;
    }
    size_t minimum = l4_minimum(header.protocol);
    if (!minimum || header.header_bytes + minimum > header.total) {
        return Verdict::local;
    }
    uint8_t *l4 = packet + header.header_bytes;
    uint8_t *port_field = l4 + 2;
    uint8_t *checksum_field = l4_checksum_field(l4, header.protocol);
    if (header.protocol == protocol_icmp) {
        uint8_t type = l4[0];
        if (type == icmp_unreachable || type == icmp_time_exceeded || type == icmp_parameter_problem) {
            return inbound_icmp_error(packet, header.header_bytes, header.total, now_ms);
        }
        if (type != icmp_echo_reply) {
            return Verdict::local;
        }
        port_field = l4 + 4;
        checksum_field = l4 + 2;
    }

    uint16_t outside_port = read16(port_field);
    Mapping *mapping = find_outside(header.protocol, outside_port);
    if (!mapping) {
        return Verdict::local;
    }
    if (!decrement_ttl(packet)) {
        return Verdict::drop;
    }
    mapping->last_ms = now_ms;
    if (header.protocol == protocol_tcp && (l4[13] & (tcp_flag_fin | tcp_flag_rst))) {
        mapping->closing = true;
    }

    write16(port_field, mapping->inside_port);
    if (checksum_field) {
        adjust_checksum16(checksum_field, outside_port, mapping->inside_port);
        if (header.protocol != protocol_icmp) {
            adjust_checksum32(checksum_field, outside, mapping->inside_address);
        }
        fix_udp_zero(checksum_field, header.protocol);
    }
    write32(packet + 16, mapping->inside_address);
    adjust_checksum32(packet + 10, outside, mapping->inside_address);
    return Verdict::forward;
}

Verdict Translator::inbound_icmp_error(uint8_t *packet, size_t header_bytes, size_t total, uint32_t now_ms) {
    uint8_t *icmp = packet + header_bytes;
    uint8_t *inner = icmp + 8;
    size_t available = total - header_bytes - 8;
    if (available < 20 || (inner[0] >> 4) != 4) {
        return Verdict::local;
    }
    size_t inner_header_bytes = static_cast<size_t>(inner[0] & 0x0f) * 4;
    if (inner_header_bytes < 20 || inner_header_bytes + 8 > available || read32(inner + 12) != outside) {
        return Verdict::local;
    }
    uint8_t inner_protocol = inner[9];
    uint8_t *inner_l4 = inner + inner_header_bytes;
    size_t inner_l4_available = available - inner_header_bytes;
    uint8_t *inner_port_field = inner_l4;
    if (inner_protocol == protocol_icmp) {
        if (inner_l4[0] != icmp_echo_request) {
            return Verdict::local;
        }
        inner_port_field = inner_l4 + 4;
    } else if (inner_protocol != protocol_tcp && inner_protocol != protocol_udp) {
        return Verdict::local;
    }

    uint16_t outside_port = read16(inner_port_field);
    Mapping *mapping = find_outside(inner_protocol, outside_port);
    if (!mapping) {
        return Verdict::local;
    }
    if (!decrement_ttl(packet)) {
        return Verdict::drop;
    }
    mapping->last_ms = now_ms;
    uint8_t *icmp_checksum = icmp + 2;

    uint8_t *inner_checksum = nullptr;
    if (inner_protocol == protocol_tcp && inner_l4_available >= 18) {
        inner_checksum = inner_l4 + 16;
    } else if (inner_protocol == protocol_udp && read16(inner_l4 + 6)) {
        inner_checksum = inner_l4 + 6;
    } else if (inner_protocol == protocol_icmp) {
        inner_checksum = inner_l4 + 2;
    }
    if (inner_checksum) {
        uint16_t before = read16(inner_checksum);
        adjust_checksum16(inner_checksum, outside_port, mapping->inside_port);
        if (inner_protocol != protocol_icmp) {
            adjust_checksum32(inner_checksum, outside, mapping->inside_address);
        }
        fix_udp_zero(inner_checksum, inner_protocol);
        adjust_checksum16(icmp_checksum, before, read16(inner_checksum));
    }

    write16(inner_port_field, mapping->inside_port);
    adjust_checksum16(icmp_checksum, outside_port, mapping->inside_port);

    uint16_t inner_ip_checksum_before = read16(inner + 10);
    write32(inner + 12, mapping->inside_address);
    adjust_checksum32(inner + 10, outside, mapping->inside_address);
    adjust_checksum32(icmp_checksum, outside, mapping->inside_address);
    adjust_checksum16(icmp_checksum, inner_ip_checksum_before, read16(inner + 10));

    write32(packet + 16, mapping->inside_address);
    adjust_checksum32(packet + 10, outside, mapping->inside_address);
    return Verdict::forward;
}

}
