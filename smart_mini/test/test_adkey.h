#ifndef _TEST_ADKEY_H_
#define _TEST_ADKEY_H_

#define KEY_NONE    0x00u
#define KEY_PLAY    0x01u
#define KEY_PREV    0x02u
#define KEY_NEXT    0x03u
#define KEY_UNKNOWN 0xffu

#define KEY_SHORT    0x0000u
#define KEY_SHORT_UP 0x0800u
#define KEY_LONG     0x0a00u
#define KEY_LONG_UP  0x0c00u
#define KEY_HOLD     0x0e00u

void test_adkey_raw_run(void);
void test_adkey_map_run(void);
void test_adkey_debounce_run(void);
void test_adkey_long_run(void);
void test_adkey_hold_run(void);

#endif // _TEST_ADKEY_H_
