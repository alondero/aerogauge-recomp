#ifndef AERO_REGION_H
#define AERO_REGION_H

// Selected once, after ROM validation and before starting any game threads.
// Thread creation publishes this value to the game threads (happens-before).
// Never modify it after startup; concurrent readers need no atomic access.
// Host tests without Japanese generated code retain their USA address contract.
#ifdef AERO_JAPAN_SUPPORT
#ifdef __cplusplus
extern "C" {
#endif
extern int aero_japan;
#ifdef __cplusplus
}
#endif
#define AERO_IS_JP (aero_japan != 0)
#define AERO_BRANCH(us, jp) (AERO_IS_JP ? (jp) : (us))
#else
#define AERO_IS_JP (0)
#define AERO_BRANCH(us, jp) (us)
#endif
// Address/offset selection; use AERO_BRANCH for other regional values.
#define AERO_ADDR(us, jp) AERO_BRANCH(us, jp)

#endif
