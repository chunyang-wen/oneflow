#ifndef ONEFLOW_CORE_VM_VM_MEM_ZONE_DESC_MSG_H_
#define ONEFLOW_CORE_VM_VM_MEM_ZONE_DESC_MSG_H_

#include "oneflow/core/common/object_msg.h"

namespace oneflow {
namespace vm {

using VmMemZoneTypeId = int32_t;

static const VmMemZoneTypeId kHostMemTypeId = 0;
static const VmMemZoneTypeId kGpuMemTypeId = 1;

// clang-format off
FLAT_MSG_BEGIN(VmMemZoneId);
  FLAT_MSG_DEFINE_OPTIONAL(VmMemZoneTypeId, vm_mem_zone_type_id);
  FLAT_MSG_DEFINE_OPTIONAL(int64_t, parallel_id);

  // methods
  FLAT_MSG_DEFINE_COMPARE_OPERATORS_BY_MEMCMP();
FLAT_MSG_END(VmMemZoneId);
// clang-format on

// clang-format off
OBJECT_MSG_BEGIN(VmMemZoneDesc);
  // fields
  OBJECT_MSG_DEFINE_OPTIONAL(int32_t, num_machine);
  OBJECT_MSG_DEFINE_OPTIONAL(int32_t, num_device);

  // links
  OBJECT_MSG_DEFINE_SKIPLIST_KEY(4, VmMemZoneTypeId, vm_mem_zone_type_id);
OBJECT_MSG_END(VmMemZoneDesc);
// clang-format on

}  // namespace vm
}  // namespace oneflow

#endif  // ONEFLOW_CORE_VM_VM_MEM_ZONE_DESC_MSG_H_
