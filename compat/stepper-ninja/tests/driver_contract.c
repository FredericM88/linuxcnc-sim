/* SPDX-License-Identifier: MIT
 * Execute the complete driver with real HAL headers and test-only HAL/network
 * storage. No HAL module is loaded and no network packet leaves this process.
 */
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include "hal_compat.h"

static unsigned char captured[256];
static size_t captured_size;
static ssize_t capture_send(int fd, const void *buffer, size_t size, int flags,
                            const void *address, socklen_t length)
{
    (void)fd; (void)flags; (void)address; (void)length;
    assert(size <= sizeof(captured));
    memcpy(captured, buffer, size);
    captured_size = size;
    printf("wire %zu ", size);
    for (size_t i = 0; i < size; ++i) printf("%02x", ((const unsigned char *)buffer)[i]);
    puts("");
    return (ssize_t)size;
}
static unsigned char incoming[256];
static ssize_t incoming_size;
static ssize_t supply_recv(int fd, void *buffer, size_t size, int flags,
                           void *address, socklen_t *length)
{
    (void)fd; (void)flags; (void)address; (void)length;
    assert(incoming_size <= (ssize_t)size);
    if (incoming_size > 0) memcpy(buffer, incoming, (size_t)incoming_size);
    return incoming_size;
}
#define sendto capture_send
#define recvfrom supply_recv
#include DRIVER_SOURCE
#undef sendto
#undef recvfrom

#ifdef SIMULATOR_PROFILE
#ifdef KBMATRIX
#error "simulator profile must not reserve the keyboard-matrix GPIOs"
#endif
_Static_assert(breakout_board == 0, "simulator profile must use custom Board 0");
_Static_assert(stepgens == 4, "simulator profile must expose four step generators");
_Static_assert(encoders == 3, "simulator profile must expose three encoders");
_Static_assert(use_pwm == 1 && pwm_count == 1, "simulator profile must expose one PWM");
_Static_assert(ANALOG_CH == 0, "simulator profile must not expose analog channels");

_Static_assert(sizeof(transmission_pc_pico_t) == 37, "command packet size changed");
_Static_assert(_Alignof(transmission_pc_pico_t) == 1, "command packet lost packed layout");
_Static_assert(offsetof(transmission_pc_pico_t, stepgen_command) == 0, "stepgen offset changed");
_Static_assert(offsetof(transmission_pc_pico_t, outputs) == 16, "outputs offset changed");
_Static_assert(offsetof(transmission_pc_pico_t, pwm_duty) == 24, "PWM duty offset changed");
_Static_assert(offsetof(transmission_pc_pico_t, pwm_frequency) == 28, "PWM frequency offset changed");
_Static_assert(offsetof(transmission_pc_pico_t, pio_timing) == 32, "PIO timing offset changed");
_Static_assert(offsetof(transmission_pc_pico_t, enc_control) == 34, "encoder control offset changed");
_Static_assert(offsetof(transmission_pc_pico_t, packet_id) == 35, "command packet ID offset changed");
_Static_assert(offsetof(transmission_pc_pico_t, checksum) == 36, "command checksum offset changed");

_Static_assert(sizeof(transmission_pico_pc_t) == 61, "response packet size changed");
_Static_assert(_Alignof(transmission_pico_pc_t) == 1, "response packet lost packed layout");
_Static_assert(offsetof(transmission_pico_pc_t, encoder_counter) == 0, "encoder counter offset changed");
_Static_assert(offsetof(transmission_pico_pc_t, encoder_velocity) == 12, "encoder velocity offset changed");
_Static_assert(offsetof(transmission_pico_pc_t, encoder_timestamp) == 24, "encoder timestamp offset changed");
_Static_assert(offsetof(transmission_pico_pc_t, interrupt_data) == 36, "index event offset changed");
_Static_assert(offsetof(transmission_pico_pc_t, inputs) == 37, "inputs offset changed");
_Static_assert(offsetof(transmission_pico_pc_t, jitter) == 53, "jitter offset changed");
_Static_assert(offsetof(transmission_pico_pc_t, step_ring_fill) == 57, "ring fill offset changed");
_Static_assert(offsetof(transmission_pico_pc_t, step_ring_status) == 58, "ring status offset changed");
_Static_assert(offsetof(transmission_pico_pc_t, packet_id) == 59, "response packet ID offset changed");
_Static_assert(offsetof(transmission_pico_pc_t, checksum) == 60, "response checksum offset changed");

static const uint8_t profile_step_pins[] = stepgen_steps;
static const uint8_t profile_dir_pins[] = stepgen_dirs;
static const uint8_t profile_encoder_bases[] = enc_pins;
static const uint8_t profile_index_pins[] = enc_index_pins;
static const uint8_t profile_pwm_pins[] = pwm_pin;
#endif

/* Storage is test-owned. Only HAL's real inline accessors interpret API-1 refs. */
static struct {
    char name[128];
    int direction;
    char kind;
    union { bool bit; int32_t s32; uint32_t u32; double real; int64_t sint; uint64_t uint; } value;
} pins[128];
static int pin_count, export_count, exit_count;
static int fail_at = -1;
static void *component_storage;
static int allocate_pin(int dir, int id, char kind, const char *fmt, va_list args)
{
    assert(id == 42 && pin_count < 128);
    if (pin_count == fail_at) return -ENOMEM;
    pins[pin_count].direction = dir;
    pins[pin_count].kind = kind;
    vsnprintf(pins[pin_count].name, sizeof(pins[pin_count].name), fmt, args);
    return pin_count++;
}
#if defined(HAL_API_VERSION) && HAL_API_VERSION >= 1
#define NEW_PIN(api, ref_type, value_type, member, kind) \
    int hal_pin_new_##api(int id, hal_pdir_t dir, ref_type *ref, value_type def, const char *fmt, ...) { \
        va_list args; va_start(args, fmt); int n = allocate_pin(dir, id, kind, fmt, args); va_end(args); \
        if (n < 0) return n; \
        pins[n].value.member = def; *ref = (ref_type)&pins[n].value; return 0; }
NEW_PIN(bool, hal_bool_t, rtapi_bool, uint, 'b')
NEW_PIN(si32, hal_sint_t, rtapi_s32, sint, 's')
NEW_PIN(ui32, hal_uint_t, rtapi_u32, uint, 'u')
NEW_PIN(real, hal_real_t, rtapi_real, real, 'f')
#else
#define NEW_PIN(api, value_type, member, kind) \
    int hal_pin_##api##_newf(hal_pin_dir_t dir, value_type **ref, int id, const char *fmt, ...) { \
        va_list args; va_start(args, fmt); int n = allocate_pin(dir, id, kind, fmt, args); va_end(args); \
        if (n < 0) return n; \
        *ref = (void *)&pins[n].value.member; return 0; }
NEW_PIN(bit, hal_bit_t, bit, 'b')
NEW_PIN(s32, hal_s32_t, s32, 's')
NEW_PIN(u32, hal_u32_t, u32, 'u')
NEW_PIN(float, hal_float_t, real, 'f')
#endif
#undef NEW_PIN
int hal_init(const char *name) { assert(strcmp(name, "stepgen-ninja") == 0); return 42; }
void *hal_malloc(long size) { component_storage = calloc(1, (size_t)size); return component_storage; }
int hal_exit(int id) { assert(id == 42); ++exit_count; return 0; }
int hal_ready(int id) { assert(id == 42 && export_count == 3); return 0; }
void rtapi_print_msg(msg_level_t level, const char *fmt, ...) { (void)level; (void)fmt; }
int rtapi_set_msg_level(int level) { return level; }
#if defined(HAL_API_VERSION) && HAL_API_VERSION >= 1
int hal_export_funct(const char *name, void (*funct)(void *, long), void *arg, int reentrant, int id)
#else
int hal_export_funct(const char *name, void (*funct)(void *, long), void *arg, int uses_fp, int reentrant, int id)
#endif
{
#if !defined(HAL_API_VERSION)
    assert(uses_fp == 1);
#endif
    assert(funct && arg == hal_data && reentrant == 1 && id == 42);
    ++export_count;
    printf("export %s %d\n", name, reentrant);
    return 0;
}
static void snapshot(const char *label)
{
    printf("snapshot %s %d\n", label, pin_count);
    for (int i = 0; i < pin_count; ++i) {
        printf("%s %s %c ", pins[i].name, pins[i].direction == HAL_IN ? "in" : "out", pins[i].kind);
        switch (pins[i].kind) {
        case 'b': printf("%d", pins[i].value.bit); break;
        case 's': printf("%" PRId32, pins[i].value.s32); break;
        case 'u': printf("%" PRIu32, pins[i].value.u32); break;
        case 'f': printf("%a", pins[i].value.real); break;
        }
        puts("");
    }
}

#ifdef SIMULATOR_PROFILE
static int require_pin(const char *name, int direction, char kind)
{
    for (int i = 0; i < pin_count; ++i) {
        if (strcmp(pins[i].name, name) == 0) {
            assert(pins[i].direction == direction && pins[i].kind == kind);
            return i;
        }
    }
    assert(!"required HAL pin was not exported");
    return -1;
}

static void verify_profile_gpio_layout(void)
{
    const uint8_t expected_steps[] = {29, 2, 4, 6};
    const uint8_t expected_dirs[] = {1, 3, 5, 7};
    const uint8_t expected_encoders[] = {10, 14, 23};
    const uint8_t expected_indexes[] = {9, GP_NULL, GP_NULL};
    const uint8_t expected_inputs[] = {22, 26, 27, 28};
    const uint8_t expected_outputs[] = {8, 12};
    const uint8_t expected_pwm[] = {13};
    assert(memcmp(profile_step_pins, expected_steps, sizeof(expected_steps)) == 0);
    assert(memcmp(profile_dir_pins, expected_dirs, sizeof(expected_dirs)) == 0);
    assert(memcmp(profile_encoder_bases, expected_encoders, sizeof(expected_encoders)) == 0);
    assert(memcmp(profile_index_pins, expected_indexes, sizeof(expected_indexes)) == 0);
    assert(memcmp(input_pins, expected_inputs, sizeof(expected_inputs)) == 0);
    assert(memcmp(output_pins, expected_outputs, sizeof(expected_outputs)) == 0);
    assert(memcmp(profile_pwm_pins, expected_pwm, sizeof(expected_pwm)) == 0);
    assert(in_pins_no == 4 && out_pins_no == 2);

    uint8_t used[32] = {0};
#define CLAIM(gpio) do { assert((gpio) < 32 && !used[(gpio)]); used[(gpio)] = 1; } while (0)
    for (size_t i = 0; i < sizeof(profile_step_pins); ++i) CLAIM(profile_step_pins[i]);
    for (size_t i = 0; i < sizeof(profile_dir_pins); ++i) CLAIM(profile_dir_pins[i]);
    for (size_t i = 0; i < sizeof(profile_encoder_bases); ++i) {
        CLAIM(profile_encoder_bases[i]);
        CLAIM(profile_encoder_bases[i] + 1);
    }
    for (size_t i = 0; i < sizeof(profile_index_pins); ++i)
        if (profile_index_pins[i] != GP_NULL) CLAIM(profile_index_pins[i]);
    for (size_t i = 0; i < sizeof(input_pins); ++i) CLAIM(input_pins[i]);
    for (size_t i = 0; i < sizeof(output_pins); ++i) CLAIM(output_pins[i]);
    for (size_t i = 0; i < sizeof(profile_pwm_pins); ++i) CLAIM(profile_pwm_pins[i]);
    const uint8_t reserved[] = {GPIO_RESET, GPIO_MISO, GPIO_CS, GPIO_SCK,
                                GPIO_MOSI, GPIO_INT, LED_GPIO};
    for (size_t i = 0; i < sizeof(reserved); ++i) assert(!used[reserved[i]]);
#undef CLAIM
}

static void verify_profile_hal_interface(void)
{
    for (int i = 0; i < 4; ++i) {
        char name[128];
        snprintf(name, sizeof(name), "stepgen-ninja.0.stepgen.%d.command", i);
        require_pin(name, HAL_IN, 'f');
    }
    for (int i = 0; i < 3; ++i) {
        char name[128];
        snprintf(name, sizeof(name), "stepgen-ninja.0.encoder.%d.raw-count", i);
        require_pin(name, HAL_OUT, 's');
    }
    require_pin("stepgen-ninja.0.output.gp8", HAL_IN, 'b');
    require_pin("stepgen-ninja.0.output.gp12", HAL_IN, 'b');
    require_pin("stepgen-ninja.0.pwm.0.enable", HAL_IN, 'b');
    require_pin("stepgen-ninja.0.pwm.0.duty", HAL_IN, 'u');
    require_pin("stepgen-ninja.0.pwm.0.frequency", HAL_IN, 'u');
    require_pin("stepgen-ninja.0.pwm.0.min-limit", HAL_IN, 'u');
    require_pin("stepgen-ninja.0.pwm.0.max-scale", HAL_IN, 'u');
    require_pin("stepgen-ninja.0.encoder.0.raw-count", HAL_OUT, 's');
    require_pin("stepgen-ninja.0.encoder.0.position", HAL_OUT, 'f');
    require_pin("stepgen-ninja.0.encoder.0.scale", HAL_IN, 'f');
    require_pin("stepgen-ninja.0.encoder.0.velocity-rps", HAL_OUT, 'f');
    require_pin("stepgen-ninja.0.encoder.0.index-enable", HAL_IN, 'b');
    require_pin("stepgen-ninja.0.encoder.0.velocity-rpm", HAL_OUT, 'f');
    require_pin("stepgen-ninja.0.encoder.0.debug-reset", HAL_IN, 'b');
    for (int i = 0; i < pin_count; ++i) assert(strstr(pins[i].name, ".analog.") == NULL);
}

static transmission_pc_pico_t captured_command(void)
{
    transmission_pc_pico_t packet;
    assert(captured_size == sizeof(packet));
    memcpy(&packet, captured, sizeof(packet));
    assert(packet.checksum == calculate_checksum(&packet, sizeof(packet) - 1));
    return packet;
}

static void verify_profile_command_generation(module_data_t *d)
{
    sn_set_bit(d->output[0], 1);
    sn_set_bit(d->output[1], 1);
    sn_set_bit(d->pwm_enable[0], 1);
    sn_set_u32(d->pwm_output[0], 2048);
    sn_set_u32(d->pwm_frequency[0], 10000);
    sn_set_u32(d->pwm_min_limit[0], 0);
    sn_set_u32(d->pwm_maxscale[0], 4096);
    sn_set_bit(d->enc_index[0], 1);
    udp_io_process_send(d, 1000000);
    transmission_pc_pico_t packet = captured_command();
    assert(packet.outputs[0] == 3 && packet.outputs[1] == 0);
    assert(packet.pwm_duty[0] == 10000 && packet.pwm_frequency[0] == 10000);
    assert(packet.enc_control == 1);

    /* Output GPIO12 is ordinal 1: it must set packet bit 1, not GPIO bit 12. */
    sn_set_bit(d->output[0], 0);
    udp_io_process_send(d, 1000000);
    packet = captured_command();
    assert(packet.outputs[0] == 2 && packet.outputs[1] == 0);
    assert((packet.outputs[0] & (1u << 12)) == 0);

    sn_set_bit(d->output[0], 1);
    sn_set_bit(d->output[1], 0);
    sn_set_bit(d->enc_index[0], 0);
}
#endif

static void receive_packet(transmission_pico_pc_t *packet)
{
    packet->checksum = calculate_checksum(packet, sizeof(*packet) - 1);
    memcpy(incoming, packet, sizeof(*packet));
    incoming_size = sizeof(*packet);
    udp_io_process_recv(hal_data, 1000000);
}
int main(int argc, char **argv)
{
    if (argc == 2) fail_at = atoi(argv[1]);
    ip_address = "127.0.0.1:0"; /* Ephemeral local socket; send/receive are replaced above. */
    int result = rtapi_app_main();
    if (fail_at >= 0) {
        assert(result == -ENOMEM && exit_count == 1);
        close(hal_data[0].sockfd);
        free(tx_buffer); free(rx_buffer); free(component_storage);
        return 0;
    }
    assert(result == 0);
#ifdef SIMULATOR_PROFILE
    assert(pin_count == 72);
    verify_profile_gpio_layout();
    verify_profile_hal_interface();
#else
    assert(pin_count == 66);
#endif
    snapshot("defaults");
    module_data_t *d = hal_data;
    sn_set_u32(d->period, 1000000);
    sn_set_bit(d->io_ready_in, 1);
    sn_set_bit(d->output[0], 1);
    for (int i = 0; i < stepgens; ++i) {
        sn_set_bit(d->enable[i], 1);
        sn_set_float(d->scale[i], 400);
    }
    watchdog_process(d, 1000000);
#ifdef SIMULATOR_PROFILE
    verify_profile_command_generation(d);
#endif
    udp_io_process_send(d, 1000000);
    for (int cycle = 1; cycle <= 4; ++cycle) {
        for (int i = 0; i < stepgens; ++i) sn_set_float(d->command[i], cycle * (i % 2 ? -0.025 : 0.025));
        udp_io_process_send(d, 1000000);
    }
    snapshot("position");
    for (int i = 0; i < stepgens; ++i) {
        sn_set_bit(d->mode[i], 1);
        sn_set_float(d->command[i], i % 2 ? -10 : 10);
    }
    udp_io_process_send(d, 1000000);
    snapshot("velocity");
    sn_set_bit(d->debug_steps_reset, 1);
    sn_set_bit(d->enable[2], 0);
    sn_set_u32(d->pulse_width, 5000);
    udp_io_process_send(d, 1000000);
    snapshot("reset-disabled-pulse");
    transmission_pico_pc_t packet = {0};
    packet.jitter = 1250;
    packet.step_ring_fill = 2;
    packet.step_ring_status = STEP_RING_STATUS_ACTIVE | STEP_RING_STATUS_UNDERFLOW | STEP_RING_STATUS_OVERFLOW;
    for (int i = 0; i < encoders; ++i) {
        packet.encoder_counter[i] = -200 + i;
        packet.encoder_timestamp[i] = 1000;
        sn_set_float(d->enc_scale[i], 100);
    }
    receive_packet(&packet);
    for (unsigned bit = 0; bit < 128; ++bit) {
        memset(packet.inputs, 0, sizeof(packet.inputs));
        packet.inputs[bit / 32] = 1u << (bit % 32);
        receive_packet(&packet);
        for (unsigned i = 0; i < in_pins_no; ++i) {
            assert(sn_get_bit(d->input[i]) == (bit == input_pins[i]));
            assert(sn_get_bit(d->input_not[i]) == (bit != input_pins[i]));
        }
    }
    for (int i = 0; i < encoders; ++i) {
        packet.encoder_counter[i] += 30;
        packet.encoder_timestamp[i] += 1000;
        packet.encoder_velocity[i] = 30;
    }
    sn_set_bit(d->enc_reset[0], 1);
    receive_packet(&packet);
    snapshot("encoder-feedback");
    sn_set_bit(d->enc_index[0], 1);
    packet.interrupt_data = 1;
    receive_packet(&packet);
    assert(sn_get_bit(d->enc_index[0]) == 0);
    packet.interrupt_data = 0;
    for (int i = 0; i < encoders; ++i) packet.encoder_timestamp[i] += 3000000;
    receive_packet(&packet);
    snapshot("encoder-index-timeout");
    sn_set_s32(d->raw_count[0], INT32_MIN);
    assert(sn_get_s32(d->raw_count[0]) == INT32_MIN);
    sn_set_s32(d->raw_count[0], INT32_MAX);
    sn_set_u32(d->period, UINT32_MAX);
    assert(sn_get_u32(d->period) == UINT32_MAX);
    snapshot("integer-boundaries");
#if defined(HAL_API_VERSION) && HAL_API_VERSION >= 1
    /* HAL-1 storage is 64-bit; the driver's integer values must remain 32-bit. */
    uint64_t wide = UINT64_C(0x100000005);
    assert(sn_get_u32((sn_u32_pin)&wide) == 5);
    sn_set_u32((sn_u32_pin)&wide, UINT32_MAX);
    assert(wide == UINT32_MAX);
    int64_t signed_wide = INT64_C(0x1ffffffff);
    assert(sn_get_s32((sn_s32_pin)&signed_wide) == -1);
    sn_set_s32((sn_s32_pin)&signed_wide, INT32_MIN);
    assert(signed_wide == INT32_MIN);
#endif
    d->watchdog_expired = 1;
    udp_io_process_recv(d, 1000000);
    udp_io_process_send(d, 1000000);
    snapshot("watchdog");
    /* Preserve, and expose, upstream's existing checksum-error handle reset. */
    d->watchdog_expired = 0;
    incoming[rx_size - 1] ^= 1;
    udp_io_process_recv(d, 1000000);
    assert(d->connected == NULL && d->checksum_error == 1);
    snapshot("checksum-error");
    rtapi_app_exit();
    free(tx_buffer); free(rx_buffer); free(component_storage);
    return 0;
}
