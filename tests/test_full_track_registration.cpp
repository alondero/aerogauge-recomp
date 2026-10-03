// Exercise the native registrars at the ROM-helper boundary with synthetic RDRAM.
// The helpers record submitted entries; no ROM bytes or renderer are needed.
#undef NDEBUG
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

#include "recomp.h"
#include "aero_region.h"

int aero_japan = 0;
static bool full_track_enabled = true;
namespace aero::config {
bool full_track() { return full_track_enabled; }
}

extern "C" void aeroRegisterTrackSections(uint8_t*, recomp_context*);
extern "C" void aeroRegisterZoneObjects(uint8_t*, recomp_context*);

namespace {
alignas(8) std::array<uint8_t, 0x800000> ram;
constexpr uint32_t self = 0x80500000;
constexpr uint32_t craft = 0x80501000;
constexpr uint32_t map = 0x80400000;
constexpr uint32_t vis = 0x80401000;
constexpr uint32_t groups = 0x80402000;
std::vector<uint32_t> section_dls;
std::array<std::vector<uint32_t>, 3> object_entries;

gpr address(uint32_t a) { return static_cast<int32_t>(a); }
uint32_t word(uint8_t* rdram, uint32_t a) { return MEM_W(0, address(a)); }
void put_word(uint32_t a, uint32_t v) {
    uint8_t* rdram = ram.data();
    MEM_W(0, address(a)) = v;
}
void put_half(uint32_t a, uint16_t v) {
    uint8_t* rdram = ram.data();
    MEM_HU(0, address(a)) = v;
}
void put_byte(uint32_t a, uint8_t v) {
    uint8_t* rdram = ram.data();
    MEM_BU(0, address(a)) = v;
}
uint32_t object_group(uint8_t zone) { return 0x80410000 + zone * 0x2000; }
uint32_t section_dl(uint8_t zone) { return 0x80600000 + zone * 8; }

void setup(uint8_t track, const std::vector<uint8_t>& zones, bool adjacent) {
    ram.fill(0);
    section_dls.clear();
    for (auto& entries : object_entries) entries.clear();
    put_word(self + 8, craft);
    put_byte(AERO_ADDR(0x8013FF9Bu, 0x8013D01Bu), track);
    uint32_t objects = adjacent ? groups + zones.size() * 4 : 0x80404000;
    uint32_t row = AERO_ADDR(0x8008B290u, 0x8008AE40u) + track * 0x14;
    put_word(row, map);
    put_word(row + 4, vis);
    put_word(row + 8, objects);
    put_word(row + 0x10, groups);
    put_word(AERO_ADDR(0x8013FF44u, 0x8013CFC4u), objects);
    put_byte(map, zones.front());
    for (size_t i = 0; i < zones.size(); ++i) {
        uint8_t zone = zones[i];
        put_byte(vis + zone * 3, zone);
        put_byte(vis + zone * 3 + 1, zones[(i + 1) % zones.size()]);
        put_byte(vis + zone * 3 + 2, zones[(i + zones.size() - 1) % zones.size()]);
        uint32_t group = 0x80405000 + zone * 0x20;
        put_word(groups + zone * 4, group);
        put_word(group, section_dl(zone));
        put_word(objects + zone * 4, object_group(zone));
        put_word(object_group(zone), section_dl(zone));
    }
}

uint32_t call(recomp_func_t* fn) {
    recomp_context ctx{};
    ctx.r4 = address(self);
    ctx.r29 = address(0x806FF000);
    fn(ram.data(), &ctx);
    return static_cast<uint32_t>(ctx.r2);
}

void record_section(uint8_t* rdram, recomp_context* ctx) {
    uint32_t dl = word(rdram, ctx->r6);
    if (dl >= 0x80700000) {
        // Resolve synthetic G_DL commands to the same source DLs as the PVS path.
        while (word(rdram, dl) != 0xB8000000u) {
            assert(word(rdram, dl) == 0x06000000u);
            section_dls.push_back(word(rdram, dl + 4));
            dl += 8;
        }
    } else {
        section_dls.push_back(dl);
    }
    MEM_W(0, ctx->r4) += 1;
}

void record_object(uint8_t* rdram, recomp_context* ctx) {
    uint16_t type = MEM_HU(4, ctx->r6) & 0xF;
    int list = type == 1 ? 2 : (type == 2 || type == 4 ? 1 : 0);
    object_entries[list].push_back(static_cast<uint32_t>(ctx->r6));
    MEM_W(0, ctx->r4) += 1;
}
void ignore_node(uint8_t*, recomp_context*) {}

void test_sparse_zones() {
    // Chinatown's fallback has holes and reaches zones 28..31. The count is
    // not the largest zone ID; both registrars must keep the enumerated IDs.
    const std::vector<uint8_t> zones{0, 10, 29, 31};
    setup(2, zones, false);
    full_track_enabled = true;
    call(aeroRegisterTrackSections); // Builds the shared course cache first.
    assert(call(aeroRegisterZoneObjects) == zones.size() + 3);
    for (uint8_t zone : zones) {
        assert(std::count(section_dls.begin(), section_dls.end(), section_dl(zone)) == 1);
        assert(std::count(object_entries[0].begin(), object_entries[0].end(),
                          object_group(zone)) == 1);
    }

    object_entries[0].clear();
    full_track_enabled = false;
    assert(call(aeroRegisterZoneObjects) == 6);
    assert((object_entries[0] == std::vector<uint32_t>{
        object_group(0), object_group(10), object_group(31)}));
}

void test_dense_landmarks() {
    // Chinatown Jam has 150 type-0/8 objects, including the late decorations.
    setup(4, {0}, true);
    for (uint32_t i = 0; i < 150; ++i) {
        put_word(object_group(0) + i * 0x28, section_dl(0));
        put_half(object_group(0) + i * 0x28 + 4, (i & 1) ? 8 : 0);
    }
    full_track_enabled = true;
    assert(call(aeroRegisterZoneObjects) == 153);
    assert(object_entries[0].size() == 150);
    for (uint32_t i = 0; i < 150; ++i)
        assert(object_entries[0][i] == object_group(0) + i * 0x28);

    // A second frame must reuse the arena without losing the tail entries.
    object_entries[0].clear();
    assert(call(aeroRegisterZoneObjects) == 153);
    assert(object_entries[0].back() == object_group(0) + 149 * 0x28);
}
} // namespace

recomp_func_t* get_function(int32_t vram) {
    const uint32_t a = static_cast<uint32_t>(vram);
    if (a == AERO_ADDR(0x800077B4u, 0x80007C38u)) return record_section;
    if (a == AERO_ADDR(0x8000791Cu, 0x80007DA8u)) return record_object;
    if (a == AERO_ADDR(0x800078A8u, 0x80007D38u) ||
        a == AERO_ADDR(0x80020504u, 0x80020E54u) ||
        a == AERO_ADDR(0x800204E0u, 0x80020E30u)) return ignore_node;
    assert(false && "Unexpected ROM helper");
    return nullptr;
}

int main(int argc, char** argv) {
    aero_japan = argc > 1 && std::strcmp(argv[1], "jp") == 0;
    test_sparse_zones();
    test_dense_landmarks();
}
