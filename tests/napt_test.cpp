// Host tests for the address translator: UDP, TCP, ICMP echo and errors, port remapping, expiry and forwards.
#include <cstdio>
#include <cstring>
#include <vector>

#include "napt.h"

using namespace napt;

namespace {

int failures = 0;

void check(bool condition, const char *what) {
    if (!condition) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

constexpr uint32_t brain = 0x0a580002;
constexpr uint32_t router = 0x0a580001;
constexpr uint32_t outside = 0xc0a8012a;
constexpr uint32_t server = 0x08080808;

uint32_t pseudo_sum(const uint8_t *packet, uint8_t protocol, size_t l4_length) {
    uint32_t sum = 0;
    sum += read16(packet + 12) + read16(packet + 14) + read16(packet + 16) + read16(packet + 18);
    sum += protocol;
    sum += static_cast<uint32_t>(l4_length);
    return sum;
}

std::vector<uint8_t> ip_packet(uint8_t protocol, uint32_t source, uint32_t destination, const std::vector<uint8_t> &l4) {
    std::vector<uint8_t> packet(20 + l4.size());
    packet[0] = 0x45;
    write16(&packet[2], static_cast<uint16_t>(packet.size()));
    write16(&packet[4], 0x1234);
    packet[8] = 64;
    packet[9] = protocol;
    write32(&packet[12], source);
    write32(&packet[16], destination);
    write16(&packet[10], compute_checksum(packet.data(), 20));
    memcpy(&packet[20], l4.data(), l4.size());
    return packet;
}

void finish_l4_checksum(std::vector<uint8_t> &packet, size_t checksum_offset) {
    uint8_t protocol = packet[9];
    size_t l4_length = packet.size() - 20;
    write16(&packet[20 + checksum_offset], 0);
    uint32_t initial = protocol == 1 ? 0 : pseudo_sum(packet.data(), protocol, l4_length);
    uint16_t checksum = compute_checksum(&packet[20], l4_length, initial);
    if (protocol == 17 && checksum == 0) {
        checksum = 0xffff;
    }
    write16(&packet[20 + checksum_offset], checksum);
}

bool ip_checksum_valid(const uint8_t *packet) {
    return compute_checksum(packet, static_cast<size_t>(packet[0] & 0x0f) * 4) == 0;
}

bool l4_checksum_valid(const std::vector<uint8_t> &packet) {
    uint8_t protocol = packet[9];
    size_t l4_length = packet.size() - 20;
    uint32_t initial = protocol == 1 ? 0 : pseudo_sum(packet.data(), protocol, l4_length);
    return compute_checksum(&packet[20], l4_length, initial) == 0;
}

std::vector<uint8_t> udp(uint32_t source, uint16_t source_port, uint32_t destination, uint16_t destination_port, const char *body) {
    std::vector<uint8_t> l4(8 + strlen(body));
    write16(&l4[0], source_port);
    write16(&l4[2], destination_port);
    write16(&l4[4], static_cast<uint16_t>(l4.size()));
    memcpy(&l4[8], body, strlen(body));
    std::vector<uint8_t> packet = ip_packet(17, source, destination, l4);
    finish_l4_checksum(packet, 6);
    return packet;
}

std::vector<uint8_t> tcp(uint32_t source, uint16_t source_port, uint32_t destination, uint16_t destination_port, uint8_t flags) {
    std::vector<uint8_t> l4(20 + 5);
    write16(&l4[0], source_port);
    write16(&l4[2], destination_port);
    write32(&l4[4], 1000);
    l4[12] = 0x50;
    l4[13] = flags;
    write16(&l4[14], 8192);
    memcpy(&l4[20], "hello", 5);
    std::vector<uint8_t> packet = ip_packet(6, source, destination, l4);
    finish_l4_checksum(packet, 16);
    return packet;
}

Translator make_translator() {
    Translator translator;
    translator.configure_inside(0x0a580000, 0xffffff00);
    translator.set_outside_address(outside);
    return translator;
}

void test_udp_round_trip() {
    Translator translator = make_translator();
    std::vector<uint8_t> query = udp(brain, 3000, server, 53, "query");
    check(translator.outbound(query.data(), query.size(), 0) == Verdict::forward, "udp outbound forwarded");
    check(read32(&query[12]) == outside, "udp source rewritten");
    check(read16(&query[20]) == 3000, "udp port preserved");
    check(query[8] == 63, "udp ttl decremented");
    check(ip_checksum_valid(query.data()), "udp outbound ip checksum");
    check(l4_checksum_valid(query), "udp outbound checksum");

    std::vector<uint8_t> answer = udp(server, 53, outside, 3000, "answer!");
    check(translator.inbound(answer.data(), answer.size(), 10) == Verdict::forward, "udp inbound forwarded");
    check(read32(&answer[16]) == brain, "udp destination restored");
    check(ip_checksum_valid(answer.data()), "udp inbound ip checksum");
    check(l4_checksum_valid(answer), "udp inbound checksum");

    std::vector<uint8_t> stranger = udp(server, 53, outside, 3001, "x");
    check(translator.inbound(stranger.data(), stranger.size(), 10) == Verdict::local, "unmapped port left to the stack");
}

void test_udp_zero_checksum() {
    Translator translator = make_translator();
    std::vector<uint8_t> query = udp(brain, 3000, server, 123, "ntp");
    write16(&query[26], 0);
    check(translator.outbound(query.data(), query.size(), 0) == Verdict::forward, "udp without checksum forwarded");
    check(read16(&query[26]) == 0, "udp zero checksum kept");
}

void test_tcp_and_remap() {
    Translator translator = make_translator();
    std::vector<uint8_t> low = tcp(brain, 68, server, 80, 0x02);
    check(translator.outbound(low.data(), low.size(), 0) == Verdict::forward, "tcp outbound forwarded");
    uint16_t mapped = read16(&low[20]);
    check(mapped >= 20000 && mapped < 30000, "privileged source port remapped");
    check(l4_checksum_valid(low), "tcp outbound checksum after remap");

    std::vector<uint8_t> reply = tcp(server, 80, outside, mapped, 0x12);
    check(translator.inbound(reply.data(), reply.size(), 5) == Verdict::forward, "tcp reply forwarded");
    check(read16(&reply[22]) == 68, "tcp destination port restored");
    check(l4_checksum_valid(reply), "tcp inbound checksum");

    std::vector<uint8_t> fin = tcp(brain, 68, server, 80, 0x11);
    translator.outbound(fin.data(), fin.size(), 10);
    translator.expire(10 + 21000);
    check(translator.active_mappings() == 0, "closing tcp mapping expires");
}

void test_icmp_echo_and_error() {
    Translator translator = make_translator();
    std::vector<uint8_t> echo_l4(12);
    echo_l4[0] = 8;
    write16(&echo_l4[4], 0x0042);
    write16(&echo_l4[6], 1);
    std::vector<uint8_t> echo = ip_packet(1, brain, server, echo_l4);
    finish_l4_checksum(echo, 2);
    check(translator.outbound(echo.data(), echo.size(), 0) == Verdict::forward, "echo forwarded");
    check(l4_checksum_valid(echo), "echo checksum");
    uint16_t identifier = read16(&echo[24]);

    std::vector<uint8_t> reply_l4(12);
    write16(&reply_l4[4], identifier);
    write16(&reply_l4[6], 1);
    std::vector<uint8_t> reply = ip_packet(1, server, outside, reply_l4);
    finish_l4_checksum(reply, 2);
    check(translator.inbound(reply.data(), reply.size(), 1) == Verdict::forward, "echo reply forwarded");
    check(read16(&reply[24]) == 0x0042, "echo identifier restored");
    check(l4_checksum_valid(reply), "echo reply checksum");

    std::vector<uint8_t> sent = udp(brain, 4000, server, 9999, "probe");
    translator.outbound(sent.data(), sent.size(), 2);
    std::vector<uint8_t> error_l4(8 + 28);
    error_l4[0] = 3;
    error_l4[1] = 3;
    memcpy(&error_l4[8], sent.data(), 28);
    std::vector<uint8_t> error = ip_packet(1, server, outside, error_l4);
    finish_l4_checksum(error, 2);
    check(translator.inbound(error.data(), error.size(), 3) == Verdict::forward, "icmp error forwarded");
    check(read32(&error[16]) == brain, "icmp error outer destination");
    check(read32(&error[28 + 12]) == brain, "icmp error inner source");
    check(read16(&error[28 + 20]) == 4000, "icmp error inner port");
    check(ip_checksum_valid(&error[28]), "icmp error inner ip checksum");
    check(l4_checksum_valid(error), "icmp error checksum");
}

void test_local_and_drop() {
    Translator translator = make_translator();
    std::vector<uint8_t> to_router = udp(brain, 68, router, 67, "dhcp");
    check(translator.outbound(to_router.data(), to_router.size(), 0) == Verdict::local, "router traffic is local");
    std::vector<uint8_t> broadcast = udp(brain, 68, 0xffffffff, 67, "dhcp");
    check(translator.outbound(broadcast.data(), broadcast.size(), 0) == Verdict::local, "broadcast is local");
    Translator offline;
    offline.configure_inside(0x0a580000, 0xffffff00);
    std::vector<uint8_t> query = udp(brain, 3000, server, 53, "query");
    check(offline.outbound(query.data(), query.size(), 0) == Verdict::drop, "no uplink drops");
    std::vector<uint8_t> short_packet = udp(brain, 3000, server, 53, "query");
    check(translator.outbound(short_packet.data(), 19, 0) == Verdict::drop, "short packet drops");
}

}

void test_forward() {
    Translator translator = make_translator();
    check(translator.add_forward(6, brain, 2100), "forward added");
    std::vector<uint8_t> inbound = tcp(0xc0a80105, 50000, outside, 2100, 0x02);
    check(translator.inbound(inbound.data(), inbound.size(), 0) == Verdict::forward, "forwarded inbound connection");
    check(read32(&inbound[16]) == brain && read16(&inbound[22]) == 2100, "forward destination");
    check(l4_checksum_valid(inbound), "forward inbound checksum");
    std::vector<uint8_t> reply = tcp(brain, 2100, 0xc0a80105, 50000, 0x12);
    check(translator.outbound(reply.data(), reply.size(), 1) == Verdict::forward, "forward reply");
    check(read32(&reply[12]) == outside && read16(&reply[20]) == 2100, "forward reply source");
    translator.expire(10u * 60 * 60 * 1000);
    std::vector<uint8_t> later = tcp(0xc0a80105, 50001, outside, 2100, 0x02);
    check(translator.inbound(later.data(), later.size(), 0) == Verdict::forward, "forward survives expiry");
    translator.set_outside_address(outside + 1);
    translator.set_outside_address(outside);
    std::vector<uint8_t> after_change = tcp(0xc0a80105, 50002, outside, 2100, 0x02);
    check(translator.inbound(after_change.data(), after_change.size(), 0) == Verdict::forward, "forward survives address change");
    translator.remove_forward(6, 2100);
    std::vector<uint8_t> removed = tcp(0xc0a80105, 50003, outside, 2100, 0x02);
    check(translator.inbound(removed.data(), removed.size(), 0) == Verdict::local, "removed forward is local");
}

int main() {
    test_forward();
    test_udp_round_trip();
    test_udp_zero_checksum();
    test_tcp_and_remap();
    test_icmp_echo_and_error();
    test_local_and_drop();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("napt: all tests passed\n");
    return 0;
}
