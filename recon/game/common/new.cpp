/* game/common/new.cpp -- RECONSTRUCTED (GCC 2.x operator new/delete runtime wrappers; C++ TU,
 *   4 fns -> eaclib EACPSXZ reservememadr / purgememadr). */
#include "../../nfs4_types.h"
#include "../../lib/libfns.h"
#ifdef AP_WIN /* PORTABILITY-REVIEWED: host allocator ABI boundary */
extern "C" void *NFSHS_HostAlloc(unsigned int);
extern "C" void NFSHS_HostFree(void *);
#endif



/* ---- __builtin_new  [NEW.CPP:16-27] SLD-VERIFIED ---- */
void *__builtin_new(unsigned int size)

{
  void *pvVar1;

#ifdef AP_WIN /* PORTABILITY-REVIEWED: PSX heap maps to reclaiming host arena */
  pvVar1 = NFSHS_HostAlloc(size);
#else
  pvVar1 = reservememadr((char *)0x0,(int)size,0);
#endif
  return pvVar1;
}

/* ---- __builtin_vec_new  [NEW.CPP:31-42] SLD-VERIFIED ---- */
void *__builtin_vec_new(unsigned int size)

{
  void *pvVar1;

#ifdef AP_WIN /* PORTABILITY-REVIEWED: PSX heap maps to reclaiming host arena */
  pvVar1 = NFSHS_HostAlloc(size);
#else
  pvVar1 = reservememadr((char *)0x0,(int)size,0);
#endif
  return pvVar1;
}

/* ---- __builtin_delete  [NEW.CPP:46-47] SLD-VERIFIED ---- */
void __builtin_delete(void *deleteMe)

{
#ifdef AP_WIN /* PORTABILITY-REVIEWED: PSX free maps to host arena free */
  NFSHS_HostFree(deleteMe);
#else
  purgememadr(deleteMe);
#endif
  return;
}

/* ---- __builtin_vec_delete  [NEW.CPP:51-52] SLD-VERIFIED ---- */
void __builtin_vec_delete(void *deleteMe)

{
#ifdef AP_WIN /* PORTABILITY-REVIEWED: PSX free maps to host arena free */
  NFSHS_HostFree(deleteMe);
#else
  purgememadr(deleteMe);
#endif
  return;
}

#ifdef AP_WIN /* PORTABILITY-REVIEWED: host compiler ABI aliases only */
/* Keep explicit new/delete on the reconstructed PsyQ allocator hooks */
void *operator new[](unsigned int size) { return __builtin_vec_new(size); }
void operator delete(void *p) { __builtin_delete(p); }
void operator delete[](void *p) { __builtin_vec_delete(p); }
#endif

/* end of new.cpp */
