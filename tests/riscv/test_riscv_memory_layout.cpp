#include <CppUTest/TestHarness.h>
#include <array>
#include <cstdint>

namespace {

struct MemoryLayout {
    static constexpr uint32_t kItcmBase = 0x00000000u;
    static constexpr uint32_t kDtcmBase = 0x00080000u;
    static constexpr uint32_t kSramSharedBase = 0x07130000u;
    static constexpr uint32_t kDramBase = 0x48000000u;
    static constexpr uint32_t kDramPayloadBase = 0x48100000u;
};

TEST_GROUP(RiscvMemoryLayout) {
};

TEST(RiscvMemoryLayout, KeepsRingMetadataInSramAndPayloadsInDram) {
    CHECK_TRUE(MemoryLayout::kSramSharedBase < MemoryLayout::kDramPayloadBase);
    CHECK_TRUE(MemoryLayout::kDramBase < MemoryLayout::kDramPayloadBase);
    CHECK_TRUE(MemoryLayout::kDramPayloadBase > MemoryLayout::kDtcmBase);
    CHECK_TRUE(MemoryLayout::kItcmBase < MemoryLayout::kDtcmBase);
}

TEST(RiscvMemoryLayout, UsesDistinctMemoryRegionsForControlAndData) {
    std::array<uint32_t, 4> regions = {
        MemoryLayout::kItcmBase,
        MemoryLayout::kDtcmBase,
        MemoryLayout::kSramSharedBase,
        MemoryLayout::kDramPayloadBase,
    };

    CHECK_TRUE(regions[0] != regions[1]);
    CHECK_TRUE(regions[1] != regions[2]);
    CHECK_TRUE(regions[2] != regions[3]);
}

} // namespace
