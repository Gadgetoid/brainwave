// Address and port translation between the Brain's network and WiFi, independent of lwIP.
#pragma once

#include <cstddef>
#include <cstdint>

namespace napt {

enum class Verdict { local, forward, drop };

class Translator {
public:
    void configure_inside(uint32_t network, uint32_t mask);
    void set_outside_address(uint32_t address);
    uint32_t outside_address() const { return outside; }
    Verdict outbound(uint8_t *packet, size_t length, uint32_t now_ms);
    Verdict inbound(uint8_t *packet, size_t length, uint32_t now_ms);
    void expire(uint32_t now_ms);
    void clear();
    size_t active_mappings() const;
    bool add_forward(uint8_t protocol, uint32_t inside_address, uint16_t port);
    void remove_forward(uint8_t protocol, uint16_t port);

private:
    struct Mapping {
        bool used;
        bool closing;
        bool fixed;
        uint8_t protocol;
        uint16_t inside_port;
        uint16_t outside_port;
        uint32_t inside_address;
        uint32_t last_ms;
    };

    static constexpr size_t mapping_limit = 128;

    Mapping *find_inside(uint8_t protocol, uint32_t address, uint16_t port);
    Mapping *find_outside(uint8_t protocol, uint16_t port);
    Mapping *create(uint8_t protocol, uint32_t address, uint16_t port, uint32_t now_ms);
    bool outside_port_used(uint8_t protocol, uint16_t port);
    bool is_inside(uint32_t address) const;
    Verdict inbound_icmp_error(uint8_t *packet, size_t header_bytes, size_t total, uint32_t now_ms);

    Mapping mappings[mapping_limit] = {};
    uint32_t inside_network = 0;
    uint32_t inside_mask = 0;
    uint32_t outside = 0;
    uint16_t next_port = 0;
};

uint16_t read16(const uint8_t *bytes);
uint32_t read32(const uint8_t *bytes);
void write16(uint8_t *bytes, uint16_t value);
void write32(uint8_t *bytes, uint32_t value);
void adjust_checksum16(uint8_t *field, uint16_t old_word, uint16_t new_word);
void adjust_checksum32(uint8_t *field, uint32_t old_value, uint32_t new_value);
uint16_t compute_checksum(const uint8_t *bytes, size_t length, uint32_t initial = 0);

}
