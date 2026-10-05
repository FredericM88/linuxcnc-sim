#ifndef CONFIG_H
#define CONFIG_H
#include "internals.h"

/*
 * linuxcnc-sim's custom Board-0 profile for pinned Stepper-Ninja 1.1.
 *
 * This is a normal Stepper-Ninja hardware configuration.  It deliberately
 * contains no simulator protocol or spindle behavior; the GPIO numbers only
 * control which HAL resources the upstream driver exports.
 */

#define DEFAULT_MAC {0x00, 0x08, 0xDC, 0x12, 0x34, 0x56}
#define DEFAULT_IP {192, 168, 0, 177}
#define DEFAULT_PORT 8888
#define DEFAULT_GATEWAY {192, 168, 0, 1}
#define DEFAULT_SUBNET {255, 255, 255, 0}
#define DEFAULT_TIMEOUT 1000000

#define breakout_board 0

#if breakout_board < 1

#define io_expanders 0

#define stepgens 4
/* GP0 is reserved for the WIZnet reset signal in internals.h. */
#define stepgen_steps {GP29, PIN_4, PIN_6, PIN_9}
#define stepgen_dirs {PIN_2, PIN_5, PIN_7, PIN_10}
#define step_invert {0, 0, 0, 0, 0}

#define encoders 3
/* Each entry is the first GPIO of a distinct consecutive A/B pair. */
#define enc_pins {PIN_14, PIN_19, GP23}
#define enc_index_pins {PIN_12, PIN_NULL, PIN_NULL}
#define enc_index_active_level {high, high, high}

/* Preserve the pinned simulator-facing virtual input set. */
#define in_pins {PIN_29, PIN_31, PIN_32, PIN_34}
#define in_pullup {1, 1, 1, 1}

/* Ordinals 0 and 1 become command-packet output bits 0 and 1. */
#define out_pins {PIN_11, PIN_16}

#define use_pwm 1
#define pwm_count 1
#define pwm_pin {PIN_17}
#define pwm_invert {0}
#define default_pwm_frequency 10000
#define default_pwm_maxscale 4096
#define default_pwm_min_limit 0

/* Make the pinned source's implicit Board-0 default explicit. */
#define ANALOG_CH 0

#endif

#define raspberry_pi_spi 0

#define raspi_int_out 25
#define raspi_inputs {2, 3, 4, 14, 15, 16, 17, 18, 20, 21, 22, 23, 24, 27}
#define raspi_input_pullups {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define raspi_outputs {0, 1, 5, 6, 12, 13, 19, 26}

#define default_pulse_width 2500
#define default_step_scale 1000

#define use_timer_interrupt 0

#ifndef encoder_pio_version
#define encoder_pio_version ENCODER_PIO_SUBSTEP
#endif

#include "footer.h"
#include "kbmatrix.h"
#endif
