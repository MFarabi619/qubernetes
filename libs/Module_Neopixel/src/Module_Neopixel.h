#ifndef MODULE_NEOPIXEL_H
#define MODULE_NEOPIXEL_H

#ifdef __cplusplus
extern "C" {
#endif

void neopixel_setup(void);
void neopixel_loop(void);

void neopixel_success(void);
void neopixel_error(void);
void neopixel_warning(void);
void neopixel_stop(void);

#ifdef __cplusplus
}
#endif

#endif
