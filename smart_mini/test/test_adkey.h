#ifndef _TEST_ADKEY_H_
#define _TEST_ADKEY_H_

#define KEY_NONE    0x00u
#define KEY_PLAY    0x01u
#define KEY_PREV    0x02u
#define KEY_NEXT    0x03u
#define KEY_UNKNOWN 0xffu

void test_adkey_raw_run(void);
void test_adkey_map_run(void);

#endif // _TEST_ADKEY_H_
