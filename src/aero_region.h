#ifndef AERO_REGION_H
#define AERO_REGION_H

// Selected once, after ROM validation and before starting any game threads.
// Host tests without Japanese generated code retain their USA address contract.
#ifdef AERO_JAPAN_SUPPORT
#ifdef __cplusplus
extern "C" {
#endif
extern int aero_japan;
#ifdef __cplusplus
}
#endif
#define AERO_ADDR(us, jp) (aero_japan ? (jp) : (us))
#else
#define AERO_ADDR(us, jp) (us)
#endif

#endif
