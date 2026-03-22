#include "hazard_ptr/hazard_ptr.h"

#include <gtest/gtest.h>

// Demonstrate some basic assertions.
TEST(HazardPtrTest, Basic) {
  NanoCU::HazPtr::HazPtrManager<int, 5, 10> hazmanager{};

  hazmanager.check_hazptr(nullptr);
  hazmanager.local_retired_cnt();
  hazmanager.global_max_hazptr_cnt();
  hazmanager.collect_all_hazptrs();
  hazmanager.retire(nullptr);
  hazmanager.set_hazptr(0, nullptr);
  hazmanager.reclaim_local();
}
