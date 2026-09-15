/* game/common/group.cpp -- RECONSTRUCTED (NFS4 PSX SerializedGroup container layer; C++ TU)
 *   6 SerializedGroup methods (emitted free-fn w/ explicit int `this` per object.obj convention):
 *   LocateNextGroupType/LocateGroupType/LocateGroupNum/LocateCreateGroupType/CreateLiteGroup/
 *   CreateLiteGroupDataSize. Full SYM-locals applied. GTE-free.
 */
#include "../../nfs4_types.h"
#include "group_externs.h"
#include "group.h"   /* canonical plain-typed prototypes = the intra-TU + cross-TU declarations
                        (replaces the old int-typed forward decls; #148 link-reconcile) */


/* ---- LocateNextGroupType__15SerializedGroupi  [GROUP.CPP:44-58] SLD-VERIFIED ---- */
extern "C" SerializedGroup * LocateNextGroupType(SerializedGroup *this_,int type)

{
  SerializedGroup *group;

  group = (SerializedGroup *)((u_char *)this_ + this_->m_length);
  if (group->m_type == type) {
    return group;
  }
  return (SerializedGroup *)0x0;
}

/* ---- LocateGroupType__15SerializedGroupii  [GROUP.CPP:63-99] SLD-VERIFIED ---- */
SerializedGroup * LocateGroupType(SerializedGroup *this_,int type,int index)

{
  int count;
  int numElems;
  SerializedGroup *group;
  bool bVar1;
  u_int uVar2;
  int iVar4;
  int iVar5;
  
  iVar5 = 0;
  iVar4 = this_->m_num_elements;
  group = this_ + 1;
  while( true ) {
    iVar4 = iVar4 + -1;
    if (iVar4 == -1) {
      return (SerializedGroup *)0x0;
    }
    if ((group->m_type == type) && (bVar1 = iVar5 == index, iVar5 = iVar5 + 1, bVar1)) break;
    uVar2 = group->m_length & 3;
    if (uVar2 != 0) {
      group->m_length = (group->m_length + 4U) - uVar2;
    }
    group = (SerializedGroup *)((u_char *)group + group->m_length);
  }
  return group;
}

/* ---- LocateGroupNum__15SerializedGroupi  [GROUP.CPP:103-116] SLD-VERIFIED ---- */
SerializedGroup * LocateGroupNum(SerializedGroup *this_,int index)

{
  int numElems;
  SerializedGroup *group;
  int i;
  int iVar1;
  int iVar2;

  group = this_ + 1;
  if (this_->m_num_elements <= index) {
    return (SerializedGroup *)0;
  }
  iVar1 = 0;
  if (0 < index) {
    do {
      iVar1 = iVar1 + 1;
      group = (SerializedGroup *)((u_char *)group + group->m_length);
    } while (iVar1 < index);
  }
  return group;
}

/* ---- LocateCreateGroupType__15SerializedGroupiP9SimpleMemi  [GROUP.CPP:120-134] SLD-VERIFIED ---- */
void *
LocateCreateGroupType(SerializedGroup *this_,int type,SimpleMem *mem_,int index)

{
  SerializedGroup *source;

  source = LocateGroupType(this_,type,index);
  if (source == (SerializedGroup *)0x0) {
    return (void *)0x0;
  }
  /* PsyQ calls LocateGroupType a second time before CreateLiteGroup. */
  source = LocateGroupType(this_,type,index);
  return CreateLiteGroup(this_,source,mem_);
}

/* ---- CreateLiteGroup__15SerializedGroupP15SerializedGroupP9SimpleMem  [GROUP.CPP:168-181] SLD-VERIFIED ---- */
Group * CreateLiteGroup(SerializedGroup *this_,SerializedGroup *source_,SimpleMem *mem_)

{
  int newLen;
  Group * ret;
  u_int *puVar1;
  int n;

  n = source_->m_length + -0xc;
  puVar1 = (u_int *)Alloc(mem_,n,0);
  *puVar1 = source_->m_num_elements;
  /* Original 0x8009C2BC-0x8009C2C4 passes n unchanged to blockmove. */
  blockmove((void *)(source_ + 1),puVar1 + 1,n);
  return (Group *)puVar1;
}

/* ---- CreateLiteGroupDataSize__15SerializedGroupP15SerializedGroupP9SimpleMemi  [GROUP.CPP:186-199] SLD-VERIFIED ---- */
Group *
CreateLiteGroupDataSize(SerializedGroup *this_,SerializedGroup *source_,SimpleMem *mem_,int dataSize)

{
  int newLen;
  Group * ret;
  u_int *puVar1;

  puVar1 = (u_int *)Alloc(mem_,dataSize + 4,0);
  *puVar1 = 0;
  /* Original keeps dataSize+4 in s1 and passes that same value as a2. */
  blockmove((void *)(source_ + 1),puVar1 + 1,dataSize + 4);
  return (Group *)puVar1;
}

/* end of group.cpp */
