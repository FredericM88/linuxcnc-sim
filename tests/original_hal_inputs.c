/* Execute the ORIGINAL Board-0 exporter and receiver using small HAL test stubs.
 * No copied implementation of the mapping under test, no edits to the original. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <transmission.h>
#define module_name "stepgen-ninja"
#define HAL_OUT 1
#define HAL_IN 2
#define RTAPI_MSG_ERR 1
const uint8_t input_pins[] = in_pins;
const uint8_t output_pins[] = out_pins;
const uint8_t in_pins_no = sizeof(input_pins);
const uint8_t out_pins_no = sizeof(output_pins);
typedef struct { unsigned char *input[4], *input_not[4], *output[1]; } module_data_t;
static unsigned char values[9];
static char names[9][80];
static int directions[9], pin_count;
static transmission_pico_pc_t response;
static transmission_pico_pc_t *rx_buffer = &response;
static transmission_pc_pico_t request;
static transmission_pc_pico_t *tx_buffer = &request;
static int hal_pin_bit_newf(int direction, unsigned char **pin, int component, const char *name, ...) {
    (void)component;
    if(pin_count>=9)return -1;
    *pin=&values[pin_count]; directions[pin_count]=direction;
    snprintf(names[pin_count],80,"%s",name); ++pin_count; return 0;
}
static void rtapi_print_msg(int level,const char *format,...) { (void)level;(void)format; }
#include "hal-driver/modules/breakoutboard_hal_0.c"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void) {
    module_data_t data={0}; char name[80];
    CHECK(bb_hal_setup_pins(&data,0,0,name,sizeof(name))==0);
    CHECK(pin_count==9 && in_pins_no==4 && out_pins_no==1);
    const unsigned bits[]={22,26,27,28};
    for(unsigned i=0;i<4;++i) {
        snprintf(name,sizeof(name),"stepgen-ninja.0.input.gp%u",bits[i]); CHECK(strcmp(names[2*i],name)==0);
        snprintf(name,sizeof(name),"stepgen-ninja.0.input.gp%u-not",bits[i]); CHECK(strcmp(names[2*i+1],name)==0);
        CHECK(directions[2*i]==HAL_OUT && directions[2*i+1]==HAL_OUT);
    }
    for(unsigned bit=0;bit<128;++bit) {
        memset(&response,0,sizeof(response)); response.inputs[bit/32]=1u<<(bit%32);
        bb_hal_process_recv(&data);
        for(unsigned i=0;i<4;++i) {CHECK(*data.input[i]==(bit==bits[i]));CHECK(*data.input_not[i]==(bit!=bits[i]));}
    }
    for(unsigned i=0;i<4;++i)response.inputs[i]=UINT32_MAX;
    bb_hal_process_recv(&data);
    for(unsigned i=0;i<4;++i){CHECK(*data.input[i]==1);CHECK(*data.input_not[i]==0);}
    memset(&response,0,sizeof(response));bb_hal_process_recv(&data);
    for(unsigned i=0;i<4;++i){CHECK(*data.input[i]==0);CHECK(*data.input_not[i]==1);}
    bb_hal_process_send(&data); /* Also compile the unmodified send function. */
    puts("PASS: original HAL exports gp22/gp26/gp27/gp28 and inverses; all 128 wire bits checked");
    return 0;
}
