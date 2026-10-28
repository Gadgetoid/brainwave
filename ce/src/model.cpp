// The application model: the latest module status, scan results and the signal history drawn on the Details tab.
#include "model.h"

#include <string.h>

app_model model = {};

namespace {

int parse_signed(const char *text) {
    bool negative = *text == '-';
    if (negative) {
        text++;
    }
    int value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text++ - '0');
    }
    return negative ? -value : value;
}

}

void model_add_history(int rssi) {
    int slot = (model.history_start + model.history_count) % history_limit;
    model.history[slot] = rssi;
    if (model.history_count < history_limit) {
        model.history_count++;
    } else {
        model.history_start = (model.history_start + 1) % history_limit;
    }
}

int model_history_at(int index) {
    return model.history[(model.history_start + index) % history_limit];
}

bool model_connected() {
    return model.status.reachable && !strcmp(model.status.wifi, "connected");
}

int model_rssi() {
    if (!model_connected() || !model.status.rssi[0] || !strcmp(model.status.rssi, "-")) {
        return history_gap;
    }
    return parse_signed(model.status.rssi);
}
