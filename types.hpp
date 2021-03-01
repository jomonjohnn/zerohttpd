
#pragma once

enum class EventType {
    Accept,
    Read,
    Write,
    Quit,
};

struct request {
    EventType event_type;
    int iovec_count;
    int client_socket;
    struct iovec iov[];
};
