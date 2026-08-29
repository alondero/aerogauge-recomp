// Native replacement for the game's two per-frame course-geometry registrars --
// the CPU-side visibility window behind AeroGauge's large-scale pop-in (the far-plane
// extension in aero_draw_distance.cpp made the world visible; this makes the game
// actually SUBMIT it). Routed here by `[patches] ignored` in aerogauge.us.toml
// (gen_syms_toml.py NATIVE_NAMES), same mechanism as guPerspectiveF.
//
// Decoded course model (byte-verified against a live RDRAM capture, 2026-07-16):
//   track byte  0x8013FF9B -> course row 0x8008B290 + 0x14*track:
//     row[0]     byte map: craft section index (u16 @ craft+4) -> zone id
//     row[1]     zone visibility rows: 3 bytes per zone (a hand-authored PVS)
//     row[2]     zone -> object-list table   (also reachable as *(0x8013FF44))
//     row[+0x10] zone -> section-DL group table (immediately precedes row[2])
//   Section groups: 8-byte {u32 dlptr; u16 hw4; u16 hw6} pairs, dlptr==0 terminated.
//   Object lists: 0x28-stride entries {u32 dlptr; u16 hw4; ...; u32 callback @+0x20;
//     u16 hw26 @+0x26}, terminated when the NEXT entry's dlptr is 0. The callback is
//     a one-shot node initialiser (stamps per-entry translation/rotation).
//
// Original behaviour (both registrars, ROM 0x80007150 / 0x80007310): look up the
// craft's zone, then register ONLY the 3 zones in its visibility row into the
// per-craft draw lists. Each draw list is a fixed 48-slot arena (0xB8-byte nodes,
// slot 0 = sentinel, so 47 usable) whose ACTIVE chain is linked through node+0xA4
// by the registration helpers.
//
// Full-track mode (config `full_track`, default on):
//   * Sections have no callbacks and identity transforms, so ALL zones' section DLs
//     are merged into a handful of synthetic display lists (one per distinct
//     (hw4, hw6) bucket, built once per course in RDRAM the game never touches)
//     and registered as ONE node each -- constant arena usage.
//   * Objects need their per-entry init callbacks, so they are registered
//     individually through the game's own helper; if a list outgrows its 47-slot
//     arena the extra nodes are placed in a side arena (init'd with the same
//     per-list handler) and spliced into the node+0xA4 chain.
// The off path is a faithful transcription of the original 3-zone window,
// A/B-verifiable with the AERO_DL_GEOMSET probe (stub_renderer.cpp).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "recomp.h"

#include "aero_config.h"
#include "aero_full_track_policy.h"

// Recompiled game helpers (RecompiledFuncs/, ROM-derived) this module calls back into.
extern "C" {
void func_800077B4(uint8_t* rdram, recomp_context* ctx); // section-node registration
void func_800078A8(uint8_t* rdram, recomp_context* ctx); // section list left-empty reset
void func_8000791C(uint8_t* rdram, recomp_context* ctx); // object-node registration (+ callback)
void func_80020504(uint8_t* rdram, recomp_context* ctx); // node reset (zone-list empty tail)
void func_800204E0(uint8_t* rdram, recomp_context* ctx); // node init: handler ptr + reset
}

namespace {

constexpr uint32_t TRACK_BYTE  = 0x8013FF9B; // current track (s8)
constexpr uint32_t COURSE_ROWS = 0x8008B290; // course row table, stride 0x14
constexpr uint32_t COURSE_PTR  = 0x8013FF44; // -> zone-object table; 0 = no course loaded

// Node arena geometry (verified: race-init loop 0x80006590 stamps s0 = 0..0x2B20
// step 0xB8 over each list; helper places node = list + counter*0xB8, counter
// starting at 1 past the sentinel).
constexpr uint32_t NODE_SIZE  = 0xB8;
constexpr uint32_t REAL_SLOTS = 47;   // usable per-list nodes (slot 0 = sentinel)

// Per-list handler tables the race-init loop passes to func_800204E0 for the three
// zone-object lists (craft+0x5704 / +0x8224 / +0xAD44 in that order).
constexpr uint32_t OBJ_HANDLERS[3] = { 0x80095118, 0x80095120, 0x80095128 };
constexpr uint32_t OBJ_LISTS[3]    = { 0x5704, 0x8224, 0xAD44 };
constexpr uint32_t OBJ_EMPTY[3]    = { 0x57BC, 0x82DC, 0xADFC };

// Reserved RDRAM the game never touches (highest observed use 0x803C87xx of the
// port's fixed 8 MB; this is a 4 MB-era title). Layout:
constexpr uint32_t RES_BASE     = 0x80700000;
constexpr uint32_t RES_COUNTERS = RES_BASE;            // 4 registration counters
constexpr uint32_t RES_SIDE_PAD = RES_BASE + 0x20;     // absorbs helper's prev-link write
constexpr uint32_t RES_SIDE     = RES_BASE + 0x40;     // side arenas (per craft x list)
constexpr uint32_t SIDE_SLOTS   = 64;
constexpr uint32_t MAX_CRAFTS   = 4;
constexpr uint32_t SIDE_ARENA_BYTES = SIDE_SLOTS * NODE_SIZE;
constexpr uint32_t RES_FAKES    = RES_SIDE + MAX_CRAFTS * 3 * SIDE_ARENA_BYTES;
constexpr uint32_t MAX_BUCKETS  = 16;                  // fake entries, 0x28 bytes each
constexpr uint32_t RES_DLS      = RES_FAKES + MAX_BUCKETS * 0x28;
constexpr uint32_t RES_END      = 0x80800000;
// The fake-entry + synthetic-DL region is double-banked: a course rebuild (track
// change, save-state restore) writes the OTHER bank, so a frame already in flight
// on the graphics thread never sees its DLs overwritten mid-walk.
constexpr uint32_t BANK_SPAN    = (RES_END - RES_FAKES) / 2;

constexpr int MAX_ZONES = 64;

enum class SectionFilter : uint8_t {
    All,
    PvsGatedOnly,
};

// Debug bisection knobs (scaffolding — see https://github.com/... issue TBD when
// filed; remove once the per-zone regression check ships). Unset = feature default:
//   AERO_FT_SECTIONS=0 / AERO_FT_OBJECTS=0  -> windowed path for that registrar only
//   AERO_FT_ZONE_MASK=<hex u64>             -> full-track includes only zones whose
//                                              bit is set (sections AND objects)
//   AERO_FT_TRACE=1                         -> one-shot stderr progress during
//                                              course rebuild (event-driven, not
//                                              periodic — gfx-thread safe)
bool is_env_knob_enabled(const char* name) {
    const char* v = std::getenv(name);
    return v == nullptr || v[0] == '\0' || !(v[0] == '0' && v[1] == '\0');
}
uint64_t zone_mask() {
    static uint64_t mask = [] {
        const char* v = std::getenv("AERO_FT_ZONE_MASK");
        return v && v[0] ? std::strtoull(v, nullptr, 16) : ~0ull;
    }();
    return mask;
}
bool zone_enabled(uint8_t z) { return z >= 64 || (zone_mask() >> z) & 1; }

gpr A(uint32_t v) { return (gpr)(int32_t)v; }

uint32_t rw(uint8_t* rdram, uint32_t a)  { return (uint32_t)MEM_W(0, A(a)); }
void     ww(uint8_t* rdram, uint32_t a, uint32_t v) { MEM_W(0, A(a)) = (int32_t)v; }
uint16_t rhu(uint8_t* rdram, uint32_t a) { return MEM_HU(0, A(a)); }
void     whu(uint8_t* rdram, uint32_t a, uint16_t v) { MEM_HU(0, A(a)) = v; }
int8_t   rb(uint8_t* rdram, uint32_t a)  { return MEM_B(0, A(a)); }
uint8_t  rbu(uint8_t* rdram, uint32_t a) { return MEM_BU(0, A(a)); }

// Plausible game pointer: KSEG0, word-aligned, below the reserved region.
bool vptr(uint32_t v) {
    return (v & 0xFF000003u) == 0x80000000u && (v & 0x00FFFFFFu) < (RES_BASE & 0x00FFFFFFu);
}

void call3(uint8_t* rdram, recomp_context* ctx,
           void (*fn)(uint8_t*, recomp_context*), uint32_t a0, uint32_t a1, uint32_t a2) {
    ctx->r4 = A(a0); ctx->r5 = A(a1); ctx->r6 = A(a2);
    fn(rdram, ctx);
}
void call1(uint8_t* rdram, recomp_context* ctx,
           void (*fn)(uint8_t*, recomp_context*), uint32_t a0) {
    ctx->r4 = A(a0);
    fn(rdram, ctx);
}

struct CourseKey {
    int32_t track = -1;
    uint32_t map = 0, vis = 0, zoneobj = 0, dlgroups = 0;
    bool operator==(const CourseKey& o) const {
        return track == o.track && map == o.map && vis == o.vis &&
               zoneobj == o.zoneobj && dlgroups == o.dlgroups;
    }
};

struct Bucket {
    uint16_t hw4 = 0, hw6 = 0;
    int list = 0;             // 0 -> craft+0xC4, 1 -> craft+0x2BE4
    uint32_t fake_entry = 0;  // 0x28-byte entry in reserved RDRAM
    std::vector<uint32_t> dls;
};

struct BuiltCourse {
    CourseKey key;
    bool valid = false;
    int zones = 0;
    std::vector<Bucket> buckets;
    // Craft registry for side arenas (2P+ have separate list chains).
    uint32_t crafts[MAX_CRAFTS] = {};
    int n_crafts = 0;
    bool side_inited[MAX_CRAFTS][3] = {};
    bool warned_side_overflow = false;
};

BuiltCourse g_course;

// --- zone enumeration ---------------------------------------------------------------

// Primary: the section-group table (row[+0x10]) is laid out immediately before the
// zone-object table (row[2]) in the loaded course blob, so the gap is the zone count.
// Every entry of both tables must look sane or we reject the derivation.
int zone_count_adjacent(uint8_t* rdram, const CourseKey& k) {
    if (k.zoneobj <= k.dlgroups) return -1;
    uint32_t gap = k.zoneobj - k.dlgroups;
    if (gap % 4 != 0) return -1;
    uint32_t n = gap / 4;
    if (n < 1 || n > MAX_ZONES) return -1;
    for (uint32_t z = 0; z < n; z++) {
        uint32_t g = rw(rdram, k.dlgroups + z * 4);
        uint32_t o = rw(rdram, k.zoneobj + z * 4);
        if (!vptr(g)) return -1;
        if (o != 0 && !vptr(o)) return -1;
    }
    return (int)n;
}

// Fallback: closure over the game's own visibility graph, seeded from the craft's
// current zone. Only zone ids the game itself could ever dereference.
int zone_closure(uint8_t* rdram, const CourseKey& k, uint8_t seed, uint8_t* out) {
    bool seen[256] = {};
    uint8_t stack[256];
    int sp = 0, n = 0;
    stack[sp++] = seed;
    seen[seed] = true;
    while (sp > 0) {
        uint8_t z = stack[--sp];
        if (n < MAX_ZONES) out[n++] = z;
        for (int i = 0; i < 3; i++) {
            uint8_t nz = rbu(rdram, k.vis + (uint32_t)z * 3 + (uint32_t)i);
            if (!seen[nz]) { seen[nz] = true; if (sp < 256) stack[sp++] = nz; }
        }
    }
    return n;
}

// --- synthetic section DLs ------------------------------------------------------------

int bucket_list_for(uint16_t hw4) {
    uint16_t t = hw4 & 0xF;
    if (t == 0 || t == 8) return 0;   // -> craft+0xC4
    if (t == 1) return 1;             // -> craft+0x2BE4
    return -1;                        // skipped by the original too
}

// Build (or rebuild) the synthetic DLs + fake entries for the current course.
bool build_course(uint8_t* rdram, const CourseKey& k) {
    const bool trace = std::getenv("AERO_FT_TRACE") != nullptr;
    if (trace) { std::fprintf(stderr, "[ft] build_course enter track=%d\n", k.track); std::fflush(stderr); }
    g_course = BuiltCourse{};
    g_course.key = k;

    int zones = zone_count_adjacent(rdram, k);
    uint8_t zone_ids[MAX_ZONES];
    if (zones > 0) {
        for (int z = 0; z < zones; z++) zone_ids[z] = (uint8_t)z;
    } else {
        // Adjacency layout not confirmed on this course: fall back to the visibility
        // closure from zone 0 (tracks loop, so the graph is connected in practice).
        zones = zone_closure(rdram, k, rbu(rdram, k.map), zone_ids);
        std::fprintf(stderr, "[fulltrack] table-adjacency derivation failed; visibility"
                             " closure found %d zones\n", zones);
        if (zones <= 0) return false;
    }
    g_course.zones = zones;
    if (trace) { std::fprintf(stderr, "[ft] zones=%d\n", zones); std::fflush(stderr); }

    // Collect section pairs into (hw4, hw6) buckets.
    int total = 0;
    for (int zi = 0; zi < zones; zi++) {
        if (!zone_enabled(zone_ids[zi])) continue;
        uint32_t g = rw(rdram, k.dlgroups + (uint32_t)zone_ids[zi] * 4);
        if (!vptr(g) || rw(rdram, g) == 0) continue;
        for (uint32_t idx = 0; idx < 512; idx++) {
            uint32_t e = g + idx * 8;
            uint32_t dl = rw(rdram, e);
            if (dl == 0) break;
            uint16_t hw4 = rhu(rdram, e + 4), hw6 = rhu(rdram, e + 6);
            int list = bucket_list_for(hw4);
            if (list < 0 || !vptr(dl)) continue;
            // Enclosed-shell geometry the artists rely on the PVS to hide stays
            // on the faithful per-frame window instead of being merged into an
            // always-drawn bucket. This is normally hw4 bit 0x10; the policy also
            // records one byte-verified Bikini authoring exception (including its
            // display-list address so a table reorder cannot retarget the rule).
            if (aero::full_track::pvs_gated_section(
                    static_cast<uint8_t>(k.track), zone_ids[zi], dl, hw4)) continue;
            Bucket* b = nullptr;
            for (auto& bb : g_course.buckets)
                if (bb.hw4 == hw4 && bb.hw6 == hw6) { b = &bb; break; }
            if (!b) {
                if (g_course.buckets.size() >= MAX_BUCKETS) {
                    std::fprintf(stderr, "[fulltrack] bucket overflow -- windowed fallback\n");
                    return false;
                }
                g_course.buckets.push_back(Bucket{hw4, hw6, list, 0, {}});
                b = &g_course.buckets.back();
            }
            b->dls.push_back(dl);
            ++total;
        }
    }

    if (trace) { std::fprintf(stderr, "[ft] collected %d DLs, %zu buckets\n", total, g_course.buckets.size()); std::fflush(stderr); }
    // Emit one synthetic DL + one fake 8-byte-compatible entry per bucket, into
    // the bank the in-flight frame is NOT using. First build_course (boot, with
    // bank 0 sitting uninitialised in RDRAM) picks bank 1 so the boot frame's
    // reads stay on an empty region; subsequent rebuilds toggle 1 <-> 0.
    static bool s_first = true;
    static int s_bank = 1;
    int bank = s_first ? s_bank : (s_bank ^= 1, s_bank);
    s_first = false;
    uint32_t bank_base = RES_FAKES + (uint32_t)bank * BANK_SPAN;
    uint32_t bank_end  = bank_base + BANK_SPAN;
    uint32_t dlcur = bank_base + MAX_BUCKETS * 0x28;
    for (size_t i = 0; i < g_course.buckets.size(); i++) {
        Bucket& b = g_course.buckets[i];
        uint32_t fake = bank_base + (uint32_t)i * 0x28;
        for (uint32_t off = 0; off < 0x28; off += 4) ww(rdram, fake + off, 0);
        b.fake_entry = fake;
        uint32_t need = ((uint32_t)b.dls.size() + 1) * 8;
        if (dlcur + need > bank_end) {
            std::fprintf(stderr, "[fulltrack] DL arena overflow -- windowed fallback\n");
            return false;
        }
        uint32_t dl = dlcur;
        for (uint32_t d : b.dls) {
            ww(rdram, dlcur, 0x06000000u);      // gsSPDisplayList (push)
            ww(rdram, dlcur + 4, d);
            dlcur += 8;
        }
        ww(rdram, dlcur, 0xB8000000u);          // gsSPEndDisplayList
        ww(rdram, dlcur + 4, 0);
        dlcur += 8;
        ww(rdram, fake, dl);                     // entry word0 = synthetic DL
        whu(rdram, fake + 4, b.hw4);
        whu(rdram, fake + 6, b.hw6);
        // callback (+0x20) and hw26 (+0x26) stay zero: no init, default pass byte.
    }

    g_course.valid = true;
    std::fprintf(stderr, "[fulltrack] course track=%d: %d zones, %d section DLs -> %zu"
                         " synthetic buckets\n",
                 k.track, zones, total, g_course.buckets.size());
    return true;
}

CourseKey read_key(uint8_t* rdram) {
    CourseKey k;
    k.track = rb(rdram, TRACK_BYTE);
    uint32_t row = COURSE_ROWS + (uint32_t)(k.track) * 0x14;
    k.map      = rw(rdram, row);
    k.vis      = rw(rdram, row + 4);
    k.zoneobj  = rw(rdram, row + 8);
    k.dlgroups = rw(rdram, row + 0x10);
    return k;
}

bool key_sane(const CourseKey& k) {
    return k.track >= 0 && k.track < 16 &&
           vptr(k.map) && vptr(k.vis) && vptr(k.zoneobj) && vptr(k.dlgroups);
}

bool ensure_course(uint8_t* rdram) {
    CourseKey k = read_key(rdram);
    if (!key_sane(k)) return false;
    if (g_course.valid && g_course.key == k) return true;
    return build_course(rdram, k);
}

// --- side arena for zone-object overflow ----------------------------------------------

int craft_slot(uint32_t craft) {
    for (int i = 0; i < g_course.n_crafts; i++)
        if (g_course.crafts[i] == craft) return i;
    if (g_course.n_crafts >= (int)MAX_CRAFTS) {
        // Registry full of crafts from an earlier race on the same course -- only
        // live crafts call the registrars, so restart it rather than dropping spills.
        g_course.n_crafts = 0;
        for (auto& row : g_course.side_inited)
            for (bool& b : row) b = false;
    }
    g_course.crafts[g_course.n_crafts] = craft;
    return g_course.n_crafts++;
}

uint32_t side_base(int craft_slot, int list) {
    return RES_SIDE + ((uint32_t)craft_slot * 3 + (uint32_t)list) * SIDE_ARENA_BYTES;
}

void ensure_side_init(uint8_t* rdram, recomp_context* ctx, int cslot, int list) {
    if (g_course.side_inited[cslot][list]) return;
    uint32_t base = side_base(cslot, list);
    for (uint32_t s = 0; s < SIDE_SLOTS; s++) {
        // Same one-time init the race-init loop applies to the real arena slots:
        // node+0 = per-list handler table, node+0xA8 = 0, then the reset helper.
        ctx->r4 = A(base + s * NODE_SIZE);
        ctx->r5 = A(OBJ_HANDLERS[list]);
        func_800204E0(rdram, ctx);
    }
    g_course.side_inited[cslot][list] = true;
}

// Register the section-DL entries from the 3-zone PVS window into the craft's
// section lists. With SectionFilter::All the loop emits every entry exactly as
// the original ROM did; with SectionFilter::PvsGatedOnly it emits only the
// enclosed-shell pieces the full-track enhancement keeps on the faithful window.
void register_pvs_sections(uint8_t* rdram, recomp_context* ctx,
                           uint32_t craft, const CourseKey& k,
                           uint8_t zone, uint32_t c0, uint32_t c1,
                           SectionFilter filter) {
    for (int i = 0; i < 3; i++) {
        uint8_t z = rbu(rdram, k.vis + (uint32_t)zone * 3 + (uint32_t)i);
        uint32_t g = rw(rdram, k.dlgroups + (uint32_t)z * 4);
        if (!vptr(g) || rw(rdram, g) == 0) continue;
        for (uint32_t idx = 0; ; idx++) {
            uint32_t e = g + idx * 8;
            if (idx > 0 && rw(rdram, e) == 0) break;
            uint32_t dl = rw(rdram, e);
            uint16_t hw4 = rhu(rdram, e + 4);
            if (filter == SectionFilter::PvsGatedOnly &&
                !aero::full_track::pvs_gated_section(
                    static_cast<uint8_t>(k.track), z, dl, hw4)) continue;
            uint16_t t = hw4 & 0xF;
            if (t == 0 || t == 8)
                call3(rdram, ctx, func_800077B4, c0, craft + 0xC4, e);
            else if (t == 1)
                call3(rdram, ctx, func_800077B4, c1, craft + 0x2BE4, e);
        }
    }
}

// Register one zone-object entry into list `li` of `craft`, spilling past the real
// 47-slot arena into the side arena (chain-linked; the walker follows node+0xA4).
void register_object(uint8_t* rdram, recomp_context* ctx, uint32_t craft, int cslot,
                     int li, uint32_t counter_addr, uint32_t entry) {
    uint32_t list = craft + OBJ_LISTS[li];
    uint32_t cnt = rw(rdram, counter_addr);   // helper places node = a1 + cnt*NODE_SIZE
    if (cnt <= REAL_SLOTS) {
        call3(rdram, ctx, func_8000791C, counter_addr, list, entry);
        return;
    }
    if (cslot < 0) return;                    // >4 crafts: never expected; drop quietly
    uint32_t s = cnt - (REAL_SLOTS + 1);      // side slot index
    if (s >= SIDE_SLOTS) {
        if (!g_course.warned_side_overflow) {
            g_course.warned_side_overflow = true;
            std::fprintf(stderr, "[fulltrack] side arena exhausted (list +0x%X)\n",
                         OBJ_LISTS[li]);
        }
        return;
    }
    ensure_side_init(rdram, ctx, cslot, li);
    uint32_t node = side_base(cslot, li) + s * NODE_SIZE;
    // a1 chosen so the helper's `a1 + cnt*NODE_SIZE` lands on our side node. Its
    // prev-link write (*(node-0x14) = node) hits the previous side node's +0xA4 --
    // or, for the first side node, the RES_SIDE_PAD scratch -- so splice manually.
    call3(rdram, ctx, func_8000791C, counter_addr, node - cnt * NODE_SIZE, entry);
    if (s == 0) {
        uint32_t last_real = list + REAL_SLOTS * NODE_SIZE;
        ww(rdram, last_real + 0xA4, node);
    }
}

} // namespace

// --- the two registrars (ABI-compatible with the ROM functions they replace) ----------

// ROM 0x80007150: track-ribbon sections -> lists craft+0xC4 (types 0/8) and
// craft+0x2BE4 (type 1). Counters start at 0; a list left at 0 gets the
// func_800078A8 "left empty" tail. Returns the number of registered nodes.
extern "C" void aeroRegisterTrackSections(uint8_t* rdram, recomp_context* ctx) {
    uint32_t self = (uint32_t)ctx->r4;
    uint32_t craft = rw(rdram, self + 8);
    uint32_t c0 = RES_COUNTERS, c1 = RES_COUNTERS + 4;
    ww(rdram, c0, 0);
    ww(rdram, c1, 0);

    static const bool dbg_sections = is_env_knob_enabled("AERO_FT_SECTIONS");
    bool full = aero::config::full_track() && dbg_sections && ensure_course(rdram);
    if (full) {
        for (auto& b : g_course.buckets)
            call3(rdram, ctx, func_800077B4,
                  b.list == 0 ? c0 : c1,
                  craft + (b.list == 0 ? 0xC4 : 0x2BE4),
                  b.fake_entry);
        // PVS-hidden shell entries (skipped by build_course) are registered exactly
        // as the original does: per entry, only from the craft's 3-zone visibility
        // row, so sealed corridor caps and equivalent authoring exceptions cannot
        // occlude the track.
        CourseKey k = read_key(rdram);
        uint32_t sect = rhu(rdram, craft + 4);
        uint8_t zone = rbu(rdram, k.map + sect);
        register_pvs_sections(rdram, ctx, craft, k, zone, c0, c1,
                              SectionFilter::PvsGatedOnly);
    } else {
        // Faithful transcription of the original 3-zone visibility window.
        CourseKey k = read_key(rdram);
        uint32_t sect = rhu(rdram, craft + 4);
        uint8_t zone = rbu(rdram, k.map + sect);
        register_pvs_sections(rdram, ctx, craft, k, zone, c0, c1,
                              SectionFilter::All);
    }

    uint32_t n0 = rw(rdram, c0), n1 = rw(rdram, c1);
    if (n0 == 0) call1(rdram, ctx, func_800078A8, craft + 0xC4);
    if (n1 == 0) call1(rdram, ctx, func_800078A8, craft + 0x2BE4);
    ctx->r2 = A(n0 + n1);
}

// ROM 0x80007310: landmark zone objects -> lists craft+0x5704 (types 0/8),
// craft+0x8224 (types 2/4), craft+0xAD44 (type 1); types 3/5/6/7 skipped.
// Counters start at 1 (slot 0 is the sentinel); a list left at 1 gets the
// func_80020504 tail on its first node. Returns the counter sum.
extern "C" void aeroRegisterZoneObjects(uint8_t* rdram, recomp_context* ctx) {
    uint32_t self = (uint32_t)ctx->r4;
    if (rw(rdram, COURSE_PTR) == 0) {   // original guard: no course loaded
        ctx->r2 = 0;
        return;
    }
    uint32_t craft = rw(rdram, self + 8);
    uint32_t table = rw(rdram, COURSE_PTR);
    uint32_t cA = RES_COUNTERS + 8, cB = RES_COUNTERS + 12, cC = RES_COUNTERS + 16;
    ww(rdram, cA, 1);   // -> +0x5704
    ww(rdram, cB, 1);   // -> +0x8224
    ww(rdram, cC, 1);   // -> +0xAD44

    static const bool dbg_objects = is_env_knob_enabled("AERO_FT_OBJECTS");
    bool full = aero::config::full_track() && dbg_objects && ensure_course(rdram);
    CourseKey k = read_key(rdram);
    uint32_t sect = rhu(rdram, craft + 4);
    uint8_t zone = rbu(rdram, k.map + sect);
    int cslot = full ? craft_slot(craft) : -1;

    uint8_t zone_ids[MAX_ZONES];
    int nz;
    if (full) {
        nz = g_course.zones;
        for (int z = 0; z < nz; z++) zone_ids[z] = (uint8_t)z;
    } else {
        nz = 3;
        for (int i = 0; i < 3; i++)
            zone_ids[i] = rbu(rdram, k.vis + (uint32_t)zone * 3 + (uint32_t)i);
    }

    for (int zi = 0; zi < nz; zi++) {
        if (full && !zone_enabled(zone_ids[zi])) continue;
        uint32_t o = rw(rdram, table + (uint32_t)zone_ids[zi] * 4);
        if (o == 0 || rw(rdram, o) == 0) continue;
        for (uint32_t e = o; ; e += 0x28) {
            uint16_t t = rhu(rdram, e + 4) & 0xF;
            if (t < 9) {
                if (t == 0 || t == 8) {
                    if (full) register_object(rdram, ctx, craft, cslot, 0, cA, e);
                    else call3(rdram, ctx, func_8000791C, cA, craft + 0x5704, e);
                } else if (t == 2 || t == 4) {
                    if (full) register_object(rdram, ctx, craft, cslot, 1, cB, e);
                    else call3(rdram, ctx, func_8000791C, cB, craft + 0x8224, e);
                } else if (t == 1) {
                    if (full) register_object(rdram, ctx, craft, cslot, 2, cC, e);
                    else call3(rdram, ctx, func_8000791C, cC, craft + 0xAD44, e);
                }
            }
            if (rw(rdram, e + 0x28) == 0) break;
        }
    }

    uint32_t nA = rw(rdram, cA), nB = rw(rdram, cB), nC = rw(rdram, cC);
    if (nA == 1) call1(rdram, ctx, func_80020504, craft + OBJ_EMPTY[0]);
    if (nB == 1) call1(rdram, ctx, func_80020504, craft + OBJ_EMPTY[1]);
    if (nC == 1) call1(rdram, ctx, func_80020504, craft + OBJ_EMPTY[2]);
    ctx->r2 = A(nA + nB + nC);
}
