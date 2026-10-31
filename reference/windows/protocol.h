/* SPDX-License-Identifier: MIT
 * Original reference protocol. This is not a Call of Duty/RICOCHET interface.
 * All fields have fixed widths; no kernel pointers or virtual addresses cross it.
 */
#ifndef RICOCHET_REFERENCE_PROTOCOL_H
#define RICOCHET_REFERENCE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define RL_PROTOCOL_VERSION 1u
#define RL_RING_CAPACITY 256u
#define RL_MAX_BATCH_EVENTS 32u
#define RL_DEVICE_TYPE 0x8000u
#define RL_DEVICE_PATH L"\\\\.\\RicochetReference"

/* CTL_CODE: vendor device type, vendor functions, METHOD_BUFFERED,
 * FILE_READ_DATA | FILE_WRITE_DATA. Kept portable for ABI checks. */
#define RL_IOCTL(function) \
    ((RL_DEVICE_TYPE << 16) | (3u << 14) | ((function) << 2))
#define RL_IOCTL_START RL_IOCTL(0x800u)
#define RL_IOCTL_READ  RL_IOCTL(0x801u)
#define RL_IOCTL_STOP  RL_IOCTL(0x802u)

enum RL_EVENT_KIND {
    RL_EVENT_SESSION_START = 1,
    RL_EVENT_PROCESS_CREATE = 2,
    RL_EVENT_PROCESS_EXIT = 3,
    RL_EVENT_THREAD_CREATE = 4,
    RL_EVENT_THREAD_EXIT = 5,
    RL_EVENT_IMAGE_LOAD = 6
};

typedef struct RL_REQUEST {
    uint32_t version;
    uint32_t size;
} RL_REQUEST;

typedef struct RL_READ_REQUEST {
    uint32_t version;
    uint32_t size;
    uint32_t max_events;
    uint32_t reserved;
} RL_READ_REQUEST;

typedef struct RL_SESSION {
    uint32_t version;
    uint32_t size;
    uint64_t session_id;
    uint64_t target_process_id;
} RL_SESSION;

typedef struct RL_EVENT {
    uint64_t sequence;
    uint64_t time_100ns; /* UTC, 100 ns since 1601-01-01; not a monotonic clock. */
    uint64_t process_id;
    uint64_t thread_id;
    uint64_t related_process_id; /* Parent PID for a child-creation event. */
    uint64_t image_size; /* Byte count only; never an image base/address. */
    uint32_t kind;
    uint32_t flags; /* Reserved; zero in version 1. */
} RL_EVENT;

typedef struct RL_BATCH {
    uint32_t version;
    uint32_t size; /* Actual returned bytes, header plus event_count events. */
    uint32_t event_count;
    uint32_t reserved;
    uint64_t session_id;
    uint64_t dropped_events; /* Cumulative, drop-newest overflow policy. */
    uint64_t next_sequence; /* Next attempted event, including dropped events. */
    RL_EVENT events[RL_MAX_BATCH_EVENTS];
} RL_BATCH;

#define RL_BATCH_HEADER_SIZE ((uint32_t)offsetof(RL_BATCH, events))
#define RL_BATCH_BYTES(count) \
    (RL_BATCH_HEADER_SIZE + (uint32_t)(count) * (uint32_t)sizeof(RL_EVENT))

/* Compile-time assertions also work in the WDK's C compiler. */
#define RL_ASSERT_LAYOUT(name, condition) \
    typedef char rl_assert_##name[(condition) ? 1 : -1]
RL_ASSERT_LAYOUT(request_size, sizeof(RL_REQUEST) == 8);
RL_ASSERT_LAYOUT(read_request_size, sizeof(RL_READ_REQUEST) == 16);
RL_ASSERT_LAYOUT(session_size, sizeof(RL_SESSION) == 24);
RL_ASSERT_LAYOUT(event_size, sizeof(RL_EVENT) == 56);
RL_ASSERT_LAYOUT(event_kind_offset, offsetof(RL_EVENT, kind) == 48);
RL_ASSERT_LAYOUT(batch_header_size, offsetof(RL_BATCH, events) == 40);
RL_ASSERT_LAYOUT(batch_size, sizeof(RL_BATCH) == 1832);

#endif
