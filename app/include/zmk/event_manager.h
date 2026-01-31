/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>

struct zmk_event_type {
    const char *name;
};

typedef struct {
    const struct zmk_event_type *event;
    uint8_t last_listener_index;
} zmk_event_t;

#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_EV_EVENT_HANDLED 1
#define ZMK_EV_EVENT_CAPTURED 2

typedef int (*zmk_listener_callback_t)(const zmk_event_t *eh);
struct zmk_listener {
    zmk_listener_callback_t callback;
};

struct zmk_event_subscription {
    const struct zmk_event_type *event_type;
    const struct zmk_listener *listener;
};

#define ZMK_EVENT_DECLARE(event_type)                                                              \
    struct event_type##_event {                                                                    \
        zmk_event_t header;                                                                        \
        struct event_type data;                                                                    \
    };                                                                                             \
    struct event_type##_event copy_raised_##event_type(const struct event_type *ev);               \
    int raise_##event_type(struct event_type);                                                     \
    struct event_type *as_##event_type(const zmk_event_t *eh);                                     \
    extern const struct zmk_event_type zmk_event_##event_type;

#define ZMK_EVENT_IMPL(event_type)                                                                 \
    const struct zmk_event_type zmk_event_##event_type = {.name = STRINGIFY(event_type)};          \
    const struct zmk_event_type *zmk_event_ref_##event_type __used                                 \
        __attribute__((__section__(".event_type"))) = &zmk_event_##event_type;                     \
    struct event_type##_event copy_raised_##event_type(const struct event_type *ev) {              \
        struct event_type##_event *outer = CONTAINER_OF(ev, struct event_type##_event, data);      \
        return *outer;                                                                             \
    };                                                                                             \
    int raise_##event_type(struct event_type data) {                                               \
        struct event_type##_event ev = {.data = data,                                              \
                                        .header = {.event = &zmk_event_##event_type}};             \
        return ZMK_EVENT_RAISE(ev);                                                                \
    };                                                                                             \
    struct event_type *as_##event_type(const zmk_event_t *eh) {                                    \
        return (eh->event == &zmk_event_##event_type) ? &((struct event_type##_event *)eh)->data   \
                                                      : NULL;                                      \
    };

#define ZMK_LISTENER(mod, cb) const struct zmk_listener zmk_listener_##mod = {.callback = cb};

#define ZMK_SUBSCRIPTION(mod, ev_type)                                                             \
    extern const struct zmk_listener zmk_listener_##mod;                                           \
    const Z_DECL_ALIGN(struct zmk_event_subscription)                                              \
        _CONCAT(_CONCAT(zmk_event_sub_, mod), ev_type) __used                                      \
        __attribute__((__section__(".event_subscription"))) = {                                    \
            .event_type = &zmk_event_##ev_type,                                                    \
            .listener = &zmk_listener_##mod,                                                       \
    };

#define ZMK_EVENT_RAISE(ev) zmk_event_manager_raise(&(ev).header)

#define ZMK_EVENT_RAISE_AFTER(ev, mod)                                                             \
    zmk_event_manager_raise_after(&(ev).header, &zmk_listener_##mod)

#define ZMK_EVENT_RAISE_AT(ev, mod) zmk_event_manager_raise_at(&(ev).header, &zmk_listener_##mod)

#define ZMK_EVENT_RELEASE(ev) zmk_event_manager_release(&(ev).header)

int zmk_event_manager_raise(zmk_event_t *event);
int zmk_event_manager_raise_after(zmk_event_t *event, const struct zmk_listener *listener);
int zmk_event_manager_raise_at(zmk_event_t *event, const struct zmk_listener *listener);
int zmk_event_manager_release(zmk_event_t *event);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)

#define __ZMK_RELAY_ASSERT_SIZE(event_type, identifier)                                            \
    BUILD_ASSERT(sizeof(struct event_type) <= CONFIG_ZMK_SPLIT_RELAY_EVENT_DATA_LEN,               \
                 "Payload of " STRINGIFY(event_type) " too large for relay event");                \
    BUILD_ASSERT(sizeof(#identifier) <= CONFIG_ZMK_SPLIT_RELAY_EVENT_TYPE_NAME_LEN,                \
                 "Name " STRINGIFY(identifier) " too large for relay event");

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

struct zmk_relay_event_received {
    uint8_t source;
    const char *event_name;
    const uint8_t *event_data;
    size_t event_data_size;
};

ZMK_EVENT_DECLARE(zmk_relay_event_received);

// Central: Consume relay event and raise corresponding event locally
/**
 * @brief Relay event listener for central role.
 * @param event_type name of event struct type
 * @param identifier short unique identifier for this event type to distinguish type in relay event
 */
#define ZMK_RELAY_EVENT_PERIPHERAL_TO_CENTRAL(event_type, identifier, source_field_name)           \
    __ZMK_RELAY_ASSERT_SIZE(event_type, identifier)                                                \
                                                                                                   \
    static char *_event_type##_relay_id = STRINGIFY(identifier);                                   \
    static int zmk_split_central_relay_event_listener_cb(const zmk_event_t *eh) {                  \
        struct zmk_relay_event_received *ev = as_zmk_relay_event_received(eh);                     \
        if (ev && strcmp(_event_type##_relay_id, ev->event_name) == 0) {                           \
            struct event_type original_ev;                                                         \
            if (sizeof(struct event_type) == ev->event_data_size) {                                \
                memcpy(&original_ev, ev->event_data, sizeof(struct event_type));                   \
            } else {                                                                               \
                LOG_WRN("Relay event data size mismatch for event type %s: expected %d, got %d",   \
                        _event_type##_relay_id, sizeof(struct event_type), ev->event_data_size);   \
                return ZMK_EV_EVENT_BUBBLE;                                                        \
            }                                                                                      \
            original_ev.source_field_name = ev->source + 1; /* 0 is central */                     \
            return raise_##event_type(original_ev);                                                \
        }                                                                                          \
        return ZMK_EV_EVENT_BUBBLE;                                                                \
    }                                                                                              \
    ZMK_LISTENER(event_type##_relay, zmk_split_central_relay_event_listener_cb);                   \
    ZMK_SUBSCRIPTION(event_type##_relay, zmk_relay_event_received);

#else // peripheral

#include <zmk/split/peripheral.h>

// Peripheral: Consume given event and send to central
/**
 * @brief Relay event listener for peripheral role.
 * @param event_type name of event struct type
 * @param identifier short unique identifier for this event type to distinguish type in relay event
 */
#define ZMK_RELAY_EVENT_PERIPHERAL_TO_CENTRAL(_event_type, identifier, source_field_name)          \
    __ZMK_RELAY_ASSERT_SIZE(_event_type, identifier)                                               \
                                                                                                   \
    static char *_event_type##_relay_id = STRINGIFY(identifier);                                   \
                                                                                                   \
    static int zmk_split_peripheral_relay_event_listener_cb(const zmk_event_t *eh) {               \
        struct _event_type *ev = as_##_event_type(eh);                                             \
        if (ev) {                                                                                  \
            struct zmk_split_transport_peripheral_event pev = {                                    \
                .type = ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_RELAY_EVENT,                     \
                .data = {.relay_event = {                                                          \
                             .header = {.event_data_size = sizeof(struct _event_type)},            \
                         }}};                                                                      \
            strcpy(pev.data.relay_event.event_type, _event_type##_relay_id);                       \
            memcpy(pev.data.relay_event.event_data, ev, sizeof(struct _event_type));               \
            return zmk_split_peripheral_report_event(&pev);                                        \
        }                                                                                          \
        return ZMK_EV_EVENT_BUBBLE;                                                                \
    }                                                                                              \
    ZMK_LISTENER(_event_type##_relay, zmk_split_peripheral_relay_event_listener_cb);               \
    ZMK_SUBSCRIPTION(_event_type##_relay, _event_type);

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)