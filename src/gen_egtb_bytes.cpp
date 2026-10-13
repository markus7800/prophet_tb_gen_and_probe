// Generation with 1 byte per position (GenEGTB::gen_bytes), included by gen_egtb.cpp.
/*
Instead of int16 values, the retrograde analysis keeps one byte code per position. Exact levels (DTM in plies)
are only stored for a window of GEN_WINDOW levels [B, B + GEN_WINDOW) around the current level:

    UNUSED, UNKNOWN, DRAW (at least a draw, may still become a win), OLD_WIN, OLD_LOSS (resolved below the window),
    FAR_WIN, FAR_LOSS, FAR_MAYBE (values from captures/promotions above the window, exact values in the far list),
    WIN(i)   = win in B + 1 + 2i plies    (wins have odd, losses even levels, B is even)
    LOSS(i)  = loss in B + 2i plies
    MAYBE(i) = MAYBELOSS_IN(B + 2i)

The algorithm is the same as in gen (int16): at level L, step 2a needs the losses at L - 1 and the wins at L
(to compare with longer wins from captures), 2b the wins at L and 2c the maybe losses at L + 1 and whether
successors are wins at <= L. Everything below L - 1 is only needed as resolved win/loss. Before level L, if
L + 1 does not fit into the window, the window slides to B' = L - 1:
1. a snapshot of all positions resolved below B' (the only ones whose exact value is lost) is written to disk,
2. codes below B' become OLD_WIN / OLD_LOSS, the others are re-encoded relative to B',
3. far list entries that now fall into the window are converted to window codes.

Init (step 1, unchanged int16 code) runs per side: allocate int16 for one side, retrograde from the promotion
tables, init, then convert the side in place to byte codes and release the upper half of the memory. Values
beyond the window go to the far list (one file per thread, at most one record per position).

The final int16 values are assembled block by block from the final byte codes and the snapshots and written
with block_compress_stream, so there is never a full int16 table in memory.
Snapshots and far lists are written to $PROPHET_SCRATCH (default: [folder]/scratch) and deleted afterwards.
*/

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <zstd.h>

#ifndef GEN_WINDOW
#define GEN_WINDOW 160
#endif
static_assert(GEN_WINDOW % 2 == 0 && GEN_WINDOW >= 4 && 8 + 3 * (GEN_WINDOW / 2) <= 255, "GEN_WINDOW must be even and in [4, 164]");

namespace gb {

const uint8_t UNUSED_B = 0;
const uint8_t UNKNOWN_B = 1;
const uint8_t DRAW_B = 2;
const uint8_t OLD_WIN = 3;
const uint8_t OLD_LOSS = 4;
const uint8_t FAR_WIN = 5;
const uint8_t FAR_LOSS = 6;
const uint8_t FAR_MAYBE = 7;
const int H = GEN_WINDOW / 2;
const int WIN0 = 8;
const int LOSS0 = WIN0 + H;
const int MAYBE0 = LOSS0 + H;
const int CODE_END = MAYBE0 + H;
const uint8_t INVALID_B = 255; // never stored, marks invalid conversions in lookup tables

inline bool in_window(int level, int B) { return B <= level && level < B + GEN_WINDOW; }
inline uint8_t win_code(int level, int B) { return WIN0 + (level - B - 1) / 2; }
inline uint8_t loss_code(int level, int B) { return LOSS0 + (level - B) / 2; }
inline uint8_t maybe_code(int level, int B) { return MAYBE0 + (level - B) / 2; }
inline bool is_win_code(uint8_t c) { return WIN0 <= c && c < LOSS0; }
inline bool is_loss_code(uint8_t c) { return LOSS0 <= c && c < MAYBE0; }
inline bool is_maybe_code(uint8_t c) { return MAYBE0 <= c && c < CODE_END; }
inline int win_level(uint8_t c, int B) { return B + 1 + 2 * (c - WIN0); }
inline int loss_level(uint8_t c, int B) { return B + 2 * (c - LOSS0); }
inline int maybe_level(uint8_t c, int B) { return B + 2 * (c - MAYBE0); }

// win whose level is <= level (in int16: WIN_IN(win level) >= WIN_IN(level))
inline bool is_win_leq(uint8_t c, int level, int B) {
    return c == OLD_WIN || (is_win_code(c) && win_level(c, B) <= level);
}

enum Family { F_WIN, F_LOSS, F_MAYBE, F_OTHER, F_INVALID };

// classifies an int16 value of the generation (after init, no lower bounds)
inline Family family_of(int16_t v, int& level) {
    if (v == UNUSED || v == UNKNOWN || v == 0) { level = 0; return F_OTHER; }
    if (v > 0 && v <= WIN_IN(0)) { level = WIN_IN(0) - v; return level % 2 == 1 ? F_WIN : F_INVALID; }
    if (v < 0 && v >= LOSS_IN(0)) { level = v - LOSS_IN(0); return level % 2 == 0 ? F_LOSS : F_INVALID; }
    if (v >= MAYBELOSS_IN(0) && v < MAYBELOSS_IN(0) + 2 * WIN_IN(0)) { level = v - MAYBELOSS_IN(0); return level % 2 == 0 ? F_MAYBE : F_INVALID; }
    return F_INVALID;
}

// code of an int16 value with window base B (level >= B), far is set if the level is above the window
inline bool code_from_value(int16_t v, int B, uint8_t& code, bool& far) {
    far = false;
    if (v == UNUSED) { code = UNUSED_B; return true; }
    if (v == UNKNOWN) { code = UNKNOWN_B; return true; }
    if (v == 0) { code = DRAW_B; return true; }
    int level;
    Family f = family_of(v, level);
    if (f == F_INVALID || level < B) return false;
    far = !in_window(level, B);
    if (f == F_WIN) code = far ? FAR_WIN : win_code(level, B);
    else if (f == F_LOSS) code = far ? FAR_LOSS : loss_code(level, B);
    else code = far ? FAR_MAYBE : maybe_code(level, B);
    return true;
}

inline uint8_t far_code(Family f) { return f == F_WIN ? FAR_WIN : (f == F_LOSS ? FAR_LOSS : FAR_MAYBE); }

// ---- far list: records (ix << 16 | uint16 value), one append-only file per thread

struct FarWriter {
    FILE* f = nullptr;
    std::vector<uint64_t> buf;
    uint64_t n = 0;
    uint64_t written = 0;
    void open(const std::string& filename) {
        f = fopen(filename.c_str(), "wb");
        if (f == NULL) { std::cerr << "Could not open " << filename << std::endl; exit(1); }
        buf.resize(1 << 16);
    }
    void add(uint64_t ix, int16_t v) {
        buf[n++] = (ix << 16) | (uint16_t) v;
        if (n == buf.size()) flush();
    }
    void flush() {
        if (n > 0 && fwrite(buf.data(), sizeof(uint64_t), n, f) != n) { std::cerr << "Error writing far list" << std::endl; exit(1); }
        written += n;
        n = 0;
    }
    void close() { flush(); fclose(f); f = nullptr; }
};

// ---- snapshots: chunks of byte codes, zstd compressed, written when the window slides

struct Snapshot {
    int base;      // window base when the snapshot was taken
    int next_base; // window base after the slide, the snapshot holds the values resolved below
    std::string filename;
    std::vector<uint64_t> offsets; // n_chunks + 1
    int fd = -1;
};

// snapshot byte: 0 = not resolved in this window, 1..H = WIN(i + 1), H+1..2H = LOSS(i + 1 - H)
inline int16_t snapshot_value(uint8_t s, int base) {
    if (s <= H) return WIN_IN(base + 1 + 2 * (s - 1));
    return LOSS_IN(base + 2 * (s - 1 - H));
}

void remove_file(const std::string& filename) {
    if (std::remove(filename.c_str()) != 0) std::cerr << "Could not remove " << filename << std::endl;
}

void* map_zeroed(uint64_t bytes) {
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(EXIT_FAILURE); }
    return p;
}
uint64_t page_round(uint64_t bytes) {
    uint64_t page = sysconf(_SC_PAGESIZE);
    return (bytes + page - 1) / page * page;
}

} // namespace gb


void GenEGTB::gen_bytes(int nthreads) {
    using namespace gb;
    std::cout << "Generate " << WTM_EGTB->num_pos << " " << WTM_EGTB->id << " and " << BTM_EGTB->num_pos << " " << BTM_EGTB->id
              << " with 1 byte per position (window " << GEN_WINDOW << ")" << std::endl;
    TimePoint t0 = now();

    bool all_tb_dependencies_loaded = (WTM_EGTB->npawns + BTM_EGTB->npawns == 0) || !disable_allocate_promotion_tb;
    load_tb_dependencies(nthreads, all_tb_dependencies_loaded, true);
    std::cout << "MiB allocated: " << bytes_allocated / (1024*1024) << std::endl;
    uint64_t dependency_bytes = bytes_allocated;

    const char* scratch_env = getenv("PROPHET_SCRATCH");
    std::string scratch = scratch_env ? std::string(scratch_env) : folder + "/scratch";
    system(("mkdir -p " + scratch).c_str());
    std::string prefix = scratch + "/" + WTM_EGTB->id + "_";

    uint64_t chunk_size = BLOCKSIZE * std::max((uint64_t) 1, (uint64_t) (1 << 20) / BLOCKSIZE);

    TimePoint t1 = now();
    std::cout << "Finished allocate and load in " << (double) (t1 - t0) / 1000.0 << "s." << std::endl;

    // ---- Step 1. init per side, then convert to byte codes (index = wtm)

    EGTB* SIDE_EGTB[2] = {BTM_EGTB, WTM_EGTB};
    uint8_t* T[2];
    uint64_t T_bytes[2];
    std::vector<std::string> far_files[2];
    uint64_t N_LEVEL_POS = 0;
    int16_t MIN_LEVEL = 0;
    double promo_seconds = 0;
    uint64_t peak_bytes = bytes_allocated;

    for (int side = 0; side <= 1; side++) {
        EGTB* E = SIDE_EGTB[side];
        uint64_t N = E->num_pos;
        assert (N < (1ULL << 48));
        uint64_t bytes16 = page_round(N * sizeof(int16_t));
        int16_t* T16 = (int16_t*) map_zeroed(bytes16);
        E->TB = T16;
        E->loaded = true;
        E->mmaped = false;
        bytes_allocated += bytes16;
        peak_bytes = std::max(peak_bytes, bytes_allocated);

        if (!all_tb_dependencies_loaded) {
            TimePoint tp0 = now();
            retrograde_promotion_tbs_side(nthreads, 1 - side); // wtm = 1 writes into BTM_EGTB
            promo_seconds += (double) (now() - tp0) / 1000.0;
        }
        init_side(nthreads, side, N_LEVEL_POS, MIN_LEVEL);

        // in-place conversion: byte ix overwrites int16 entry ix / 2, which has already been read if all entries
        // below ix are converted; chunks [a, 2a) can be converted in parallel because they only overwrite entries < a
        uint8_t* T8 = (uint8_t*) T16;
        std::vector<FarWriter> writers(nthreads);
        for (int t = 0; t < nthreads; t++) {
            far_files[side].push_back(prefix + std::to_string(side) + ".far" + std::to_string(t));
            writers[t].open(far_files[side].back());
        }
        uint64_t n_invalid = 0;
        uint64_t a = std::min(N, (uint64_t) 4096);
        for (uint64_t ix = 0; ix < a; ix++) {
            int16_t v = T16[ix];
            uint8_t code; bool far;
            if (!code_from_value(v, 0, code, far)) n_invalid++;
            if (far) writers[0].add(ix, v);
            T8[ix] = code;
        }
        while (a < N) {
            uint64_t b = std::min(2 * a, N);
            #pragma omp parallel num_threads(nthreads) reduction(+:n_invalid)
            {
                #ifdef OMP
                FarWriter& writer = writers[omp_get_thread_num()];
                #else
                FarWriter& writer = writers[0];
                #endif
                #pragma omp for schedule(static, CHUNKSIZE)
                for (uint64_t ix = a; ix < b; ix++) {
                    int16_t v = T16[ix];
                    uint8_t code; bool far;
                    if (!code_from_value(v, 0, code, far)) n_invalid++;
                    if (far) writer.add(ix, v);
                    T8[ix] = code;
                }
            }
            a = b;
        }
        uint64_t n_far = 0;
        for (FarWriter& w : writers) { w.close(); n_far += w.written; }
        if (n_invalid > 0) {
            std::cerr << "Invalid values after init in " << E->id << ": " << n_invalid << std::endl;
            exit(1);
        }

        // release the upper half
        T_bytes[side] = page_round(N);
        if (bytes16 > T_bytes[side]) munmap(T8 + T_bytes[side], bytes16 - T_bytes[side]);
        bytes_allocated -= bytes16 - T_bytes[side];
        T[side] = T8;
        E->TB = nullptr;
        E->loaded = false;
        std::cout << "Converted " << E->id << " to 1 byte per position, " << n_far << " values beyond the window." << std::endl;
    }
    if (!all_tb_dependencies_loaded) {
        std::cout << "Finished retrograde from promotion tables in " << promo_seconds << "s." << std::endl;
    }

    TimePoint t2 = now();
    std::cout << "Finished init in " << (double) (t2 - t1) / 1000.0 << "s." << std::endl;
    std::cout << "MIN_LEVEL = " << int(MIN_LEVEL) << std::endl;

    // ---- window slides

    int B = 0; // window base
    std::vector<Snapshot> snapshots[2];

    auto slide = [&](int new_base) {
        assert (new_base % 2 == 0 && new_base > B);
        uint8_t snap_lut[256], rebase_lut[256];
        for (int c = 0; c < 256; c++) {
            snap_lut[c] = 0;
            if (c < WIN0) {
                rebase_lut[c] = c;
            } else if (is_win_code(c)) {
                int level = win_level(c, B);
                if (level < new_base) { rebase_lut[c] = OLD_WIN; snap_lut[c] = 1 + (c - WIN0); }
                else rebase_lut[c] = win_code(level, new_base);
            } else if (is_loss_code(c)) {
                int level = loss_level(c, B);
                if (level < new_base) { rebase_lut[c] = OLD_LOSS; snap_lut[c] = 1 + H + (c - LOSS0); }
                else rebase_lut[c] = loss_code(level, new_base);
            } else if (is_maybe_code(c)) {
                int level = maybe_level(c, B);
                rebase_lut[c] = level < new_base ? INVALID_B : maybe_code(level, new_base);
            } else {
                rebase_lut[c] = INVALID_B;
            }
        }

        uint64_t snapshot_bytes = 0;
        for (int side = 0; side <= 1; side++) {
            uint64_t N = SIDE_EGTB[side]->num_pos;
            uint64_t n_chunks = (N + chunk_size - 1) / chunk_size;
            Snapshot snap;
            snap.base = B;
            snap.next_base = new_base;
            snap.filename = prefix + std::to_string(side) + ".snap" + std::to_string(snapshots[side].size());
            snap.offsets.assign(n_chunks + 1, 0);
            FILE* f = fopen(snap.filename.c_str(), "wb");
            if (f == NULL) { std::cerr << "Could not open " << snap.filename << std::endl; exit(1); }
            uint8_t* TS = T[side];
            uint64_t n_invalid = 0;
            #pragma omp parallel num_threads(nthreads) reduction(+:n_invalid)
            {
                std::vector<uint8_t> in(chunk_size), out(ZSTD_compressBound(chunk_size));
                ZSTD_CCtx* cctx = ZSTD_createCCtx();
                #pragma omp for ordered schedule(static, 1)
                for (uint64_t chunk_ix = 0; chunk_ix < n_chunks; chunk_ix++) {
                    uint64_t start = chunk_ix * chunk_size;
                    uint64_t n = std::min(chunk_size, N - start);
                    for (uint64_t i = 0; i < n; i++) {
                        uint8_t c = TS[start + i];
                        in[i] = snap_lut[c];
                        uint8_t r = rebase_lut[c];
                        n_invalid += r == INVALID_B;
                        TS[start + i] = r;
                    }
                    size_t size = ZSTD_compressCCtx(cctx, out.data(), out.size(), in.data(), n, 1);
                    if (ZSTD_isError(size)) { std::cerr << "Snapshot compression failed" << std::endl; exit(1); }
                    #pragma omp ordered
                    {
                        if (fwrite(out.data(), 1, size, f) != size) { std::cerr << "Error writing " << snap.filename << std::endl; exit(1); }
                        snap.offsets[chunk_ix + 1] = size;
                    }
                }
                ZSTD_freeCCtx(cctx);
            }
            fclose(f);
            if (n_invalid > 0) {
                std::cerr << "Invalid codes when sliding the window of " << SIDE_EGTB[side]->id << ": " << n_invalid << std::endl;
                exit(1);
            }
            for (uint64_t i = 0; i < n_chunks; i++) snap.offsets[i + 1] += snap.offsets[i];
            snapshot_bytes += snap.offsets[n_chunks];
            snapshots[side].push_back(snap);
        }

        // far list entries that fall into the new window
        uint64_t n_entered = 0;
        for (int side = 0; side <= 1; side++) {
            uint8_t* TS = T[side];
            std::vector<std::string>& files = far_files[side];
            #pragma omp parallel for num_threads(nthreads) schedule(dynamic, 1) reduction(+:n_entered)
            for (uint64_t fi = 0; fi < files.size(); fi++) {
                FILE* f = fopen(files[fi].c_str(), "rb");
                if (f == NULL) { std::cerr << "Could not open " << files[fi] << std::endl; exit(1); }
                std::vector<uint64_t> buf(1 << 16);
                size_t n;
                while ((n = fread(buf.data(), sizeof(uint64_t), buf.size(), f)) > 0) {
                    for (size_t i = 0; i < n; i++) {
                        uint64_t ix = buf[i] >> 16;
                        int16_t v = (int16_t) (uint16_t) (buf[i] & 0xffff);
                        int level;
                        Family fam = family_of(v, level);
                        if (level < new_base || !in_window(level, new_base)) continue; // entered before or still far
                        // the position may have been resolved differently in the meantime (e.g. a shorter win)
                        if (TS[ix] != far_code(fam)) continue;
                        uint8_t code; bool far;
                        code_from_value(v, new_base, code, far);
                        TS[ix] = code;
                        n_entered++;
                    }
                }
                fclose(f);
            }
        }
        std::cout << "Moved window from " << B << " to " << new_base << ": snapshot " << snapshot_bytes / 1024 << " KiB, "
                  << n_entered << " values entered the window." << std::endl;
        B = new_base;
    };

    // ---- Step 2. Retrograde analysis (same as gen, on byte codes)

    int16_t LEVEL = 0;
    while (true) {
        LEVEL++;
        if (LEVEL + 1 >= B + GEN_WINDOW) slide(LEVEL - 1);
        N_LEVEL_POS = 0;

        for (int wtm = 0; wtm <= 1; ++wtm) {
            EGTB* LOSS_EGTB = SIDE_EGTB[wtm];
            EGTB* WIN_EGTB = SIDE_EGTB[1 - wtm];
            Color LOSS_COLOR = wtm ? WHITE : BLACK;
            uint8_t* LT = T[wtm];
            uint8_t* WT = T[1 - wtm];

            // Step 2a. From each lost position take a move backwards and set as win.
            uint8_t loss_target = loss_code(LEVEL - 1, B);
            uint8_t new_win = win_code(LEVEL, B);
            const IndexLayout* loss_layout = LOSS_EGTB->get_layout();
            #pragma omp parallel num_threads(nthreads)
            {
                PosDecoder decoder(loss_layout, LOSS_COLOR);
                #pragma omp for schedule(static,CHUNKSIZE)
                for (uint64_t ix = 0; ix < LOSS_EGTB->num_pos; ix++) {
                    if (LT[ix] != loss_target) continue;
                    EGPosition pos;
                    decoder.pos_at_ix(pos, ix);
                    assert (LOSS_EGTB->pos_ix_is_used(pos,ix));
                    uint64_t win_ixs[MAX_MOVES];
                    int n_win_ixs = 0;
                    for (UndoInfo rev_move : EGUndoInfoList(pos)) {
                        pos.do_rev_move(rev_move);
                        uint64_t win_ix = WIN_EGTB->ix_from_pos(pos);
                        __builtin_prefetch(&WT[win_ix], 1);
                        win_ixs[n_win_ixs++] = win_ix;
                        pos.undo_rev_move(rev_move);
                    }
                    for (int i = 0; i < n_win_ixs; i++) {
                        // in int16: !IS_SET(val) || val < WIN_IN(LEVEL), i.e. everything except wins in <= LEVEL plies
                        if (!is_win_leq(WT[win_ixs[i]], LEVEL, B)) WT[win_ixs[i]] = new_win;
                    }
                }
            }

            // Step 2b. For each new win position, take move backwards and flag as potential (maybe) loss
            uint8_t new_maybe = maybe_code(LEVEL + 1, B);
            const IndexLayout* win_layout = WIN_EGTB->get_layout();
            #pragma omp parallel num_threads(nthreads)
            {
                PosDecoder decoder(win_layout, ~LOSS_COLOR);
                #pragma omp for schedule(static,CHUNKSIZE) reduction(+:N_LEVEL_POS)
                for (uint64_t win_ix = 0; win_ix < WIN_EGTB->num_pos; win_ix++) {
                    if (WT[win_ix] != new_win) continue;
                    EGPosition pos;
                    decoder.pos_at_ix(pos, win_ix);
                    assert (WIN_EGTB->pos_ix_is_used(pos,win_ix));
                    N_LEVEL_POS++;
                    uint64_t maybe_loss_ixs[MAX_MOVES];
                    int n_maybe_loss_ixs = 0;
                    for (UndoInfo rev_move : EGUndoInfoList(pos)) {
                        pos.do_rev_move(rev_move);
                        uint64_t maybe_loss_ix = LOSS_EGTB->ix_from_pos(pos);
                        __builtin_prefetch(&LT[maybe_loss_ix], 1);
                        maybe_loss_ixs[n_maybe_loss_ixs++] = maybe_loss_ix;
                        pos.undo_rev_move(rev_move);
                    }
                    for (int i = 0; i < n_maybe_loss_ixs; i++) {
                        uint8_t c = LT[maybe_loss_ixs[i]];
                        if (c == UNKNOWN_B) {
                            LT[maybe_loss_ixs[i]] = new_maybe;
                        } else {
                            // set values are kept, maybe losses have to be at LEVEL + 1 or later (see gen)
                            assert (c == DRAW_B || c == OLD_WIN || c == OLD_LOSS || c == FAR_WIN || c == FAR_LOSS || c == FAR_MAYBE ||
                                    is_win_code(c) || is_loss_code(c) || (is_maybe_code(c) && maybe_level(c, B) >= LEVEL + 1));
                        }
                    }
                }
            }
        }

        std::cout << N_LEVEL_POS << " positions at level " << LEVEL << std::endl;

        LEVEL++;

        // Step 2c. for each potential (maybe) loss, check all forward moves to see if we can confirm loss
        N_LEVEL_POS = 0;
        for (int wtm = 0; wtm <= 1; ++wtm) {
            EGTB* LOSS_EGTB = SIDE_EGTB[wtm];
            EGTB* WIN_EGTB = SIDE_EGTB[1 - wtm];
            Color LOSS_COLOR = wtm ? WHITE : BLACK;
            uint8_t* LT = T[wtm];
            uint8_t* WT = T[1 - wtm];
            uint8_t maybe_target = maybe_code(LEVEL, B);
            uint8_t new_loss = loss_code(LEVEL, B);

            const IndexLayout* loss_layout = LOSS_EGTB->get_layout();
            #pragma omp parallel num_threads(nthreads)
            {
                PosDecoder decoder(loss_layout, LOSS_COLOR);
                #pragma omp for schedule(static,CHUNKSIZE) reduction(+:N_LEVEL_POS)
                for (uint64_t ix = 0; ix < LOSS_EGTB->num_pos; ix++) {
                    if (LT[ix] != maybe_target) continue;
                    EGPosition pos;
                    decoder.pos_at_ix(pos, ix);
                    assert (LOSS_EGTB->pos_ix_is_used(pos,ix));

                    // all non-capture, non-promotion moves have to lead to wins in <= LEVEL - 1 plies
                    // (in int16: max over -val stays LOSS_IN(LEVEL-1); UNUSED does not refute, as in gen)
                    EGMoveList moveList = EGMoveList(pos);
                    assert (moveList.size() > 0); // cannot happen at maybe loss position
                    bool refuted = false;
                    for (Move move : moveList) {
                        UndoInfo u = pos.do_move(move);
                        PieceType promotion = move.type_of() == PROMOTION ? move.promotion_type() : NO_PIECE_TYPE;
                        if (!promotion && !u.captured) {
                            uint8_t c = WT[WIN_EGTB->ix_from_pos(pos)];
                            refuted = !(c == UNUSED_B || is_win_leq(c, LEVEL - 1, B));
                        }
                        pos.undo_move(u);
                        if (refuted) break;
                    }

                    if (!refuted) {
                        LT[ix] = new_loss;
                        N_LEVEL_POS++;
                    } else {
                        // maybeloss can be refuted by finding non-capture non-promo move
                        LT[ix] = UNKNOWN_B;
                    }
                }
            }
        }

        std::cout << N_LEVEL_POS << " positions at level " << LEVEL << std::endl;

        if (LEVEL > MIN_LEVEL && N_LEVEL_POS == 0) { break; }
    }

    TimePoint t3 = now();
    uint64_t scratch_bytes = 0;
    for (int side = 0; side <= 1; side++) {
        for (Snapshot& snap : snapshots[side]) scratch_bytes += snap.offsets.back();
    }
    std::cout << "Window slides: " << snapshots[0].size() << ", snapshots: " << scratch_bytes / (1024*1024) << " MiB" << std::endl;
    std::cout << "Finished retrograde in " << (double) (t3 - t2) / 1000.0 << "s." << std::endl;

    // ---- Step 3+5. Assemble the int16 values from the final codes and the snapshots and write to disk

    // final codes: 0 = value, 1 = resolved below the window (value in a snapshot), 2 = invalid
    int16_t final_value[256];
    uint8_t final_kind[256];
    for (int c = 0; c < 256; c++) {
        final_value[c] = 0;
        final_kind[c] = 2;
        if (c == UNUSED_B || c == UNKNOWN_B || c == DRAW_B) final_kind[c] = 0; // draws (and unused) are 0
        else if (c == OLD_WIN || c == OLD_LOSS) final_kind[c] = 1;
        else if (is_win_code(c)) { final_kind[c] = 0; final_value[c] = WIN_IN(win_level(c, B)); }
        else if (is_loss_code(c)) { final_kind[c] = 0; final_value[c] = LOSS_IN(loss_level(c, B)); }
        // FAR_* and MAYBE must not remain after the retrograde analysis
    }

    struct ThreadBuffers {
        std::vector<uint8_t> compressed;
        std::vector<uint8_t> snapshot;
        ZSTD_DCtx* dctx;
    };
    std::vector<ThreadBuffers> buffers(nthreads);
    for (ThreadBuffers& tb : buffers) {
        tb.compressed.resize(ZSTD_compressBound(chunk_size));
        tb.snapshot.resize(chunk_size);
        tb.dctx = ZSTD_createDCtx();
    }
    const int16_t PENDING = INT16_MIN;

    auto make_fill = [&](int side) {
        return [&, side](uint64_t start_ix, uint64_t n, int16_t* out) {
            #ifdef OMP
            ThreadBuffers& tb = buffers[omp_get_thread_num()];
            #else
            ThreadBuffers& tb = buffers[0];
            #endif
            const uint8_t* TS = T[side];
            uint64_t n_pending = 0, n_invalid = 0;
            for (uint64_t i = 0; i < n; i++) {
                uint8_t c = TS[start_ix + i];
                if (final_kind[c] == 0) out[i] = final_value[c];
                else if (final_kind[c] == 1) { out[i] = PENDING; n_pending++; }
                else n_invalid++;
            }
            uint64_t chunk_ix = start_ix / chunk_size;
            for (Snapshot& snap : snapshots[side]) {
                if (n_pending == 0) break;
                uint64_t size = snap.offsets[chunk_ix + 1] - snap.offsets[chunk_ix];
                if (pread(snap.fd, tb.compressed.data(), size, snap.offsets[chunk_ix]) != (ssize_t) size) {
                    std::cerr << "Error reading " << snap.filename << std::endl; exit(1);
                }
                size_t res = ZSTD_decompressDCtx(tb.dctx, tb.snapshot.data(), n, tb.compressed.data(), size);
                if (ZSTD_isError(res) || res != n) { std::cerr << "Corrupt snapshot " << snap.filename << std::endl; exit(1); }
                for (uint64_t i = 0; i < n; i++) {
                    if (tb.snapshot[i] != 0 && out[i] == PENDING) {
                        out[i] = snapshot_value(tb.snapshot[i], snap.base);
                        n_pending--;
                    }
                }
            }
            if (n_pending > 0 || n_invalid > 0) {
                std::cerr << "Could not assemble values of " << SIDE_EGTB[side]->id << " at " << start_ix << ": "
                          << n_pending << " missing, " << n_invalid << " invalid" << std::endl;
                exit(1);
            }
        };
    };

    for (int side = 0; side <= 1; side++) {
        for (Snapshot& snap : snapshots[side]) {
            snap.fd = open(snap.filename.c_str(), O_RDONLY);
            if (snap.fd == -1) { std::cerr << "Could not open " << snap.filename << std::endl; exit(1); }
        }
    }

    system(("mkdir -p " + WTM_EGTB->folder).c_str());
    bool symmetric = WTM_EGTB->id == BTM_EGTB->id;
    for (int side = 1; side >= (symmetric ? 1 : 0); side--) {
        EGTB* E = SIDE_EGTB[side];
        if (compress) {
            block_compress_stream(E->num_pos, chunk_size, make_fill(side), nthreads, COMPRESSION_LEVEL, BLOCKSIZE,
                                  E->get_filename() + COMP_EXT, true, true);
        } else {
            FILE* f = fopen(E->get_filename().c_str(), "wb");
            std::vector<int16_t> values(chunk_size);
            auto fill = make_fill(side);
            for (uint64_t start_ix = 0; start_ix < E->num_pos; start_ix += chunk_size) {
                uint64_t n = std::min(chunk_size, E->num_pos - start_ix);
                fill(start_ix, n, values.data());
                fwrite(values.data(), sizeof(int16_t), n, f);
            }
            fclose(f);
        }
    }
    if (symmetric) std::cout << "Stored " << WTM_EGTB->id << std::endl;
    else std::cout << "Stored " << WTM_EGTB->id << " and " << BTM_EGTB->id << std::endl;

    TimePoint t4 = now();
    std::cout << "Wrote to disk in " << (double) (t4 - t3) / 1000.0 << "s." << std::endl;

    // ---- clean up generation memory and scratch files
    for (ThreadBuffers& tb : buffers) ZSTD_freeDCtx(tb.dctx);
    for (int side = 0; side <= 1; side++) {
        munmap(T[side], T_bytes[side]);
        bytes_allocated -= T_bytes[side];
        for (Snapshot& snap : snapshots[side]) { close(snap.fd); remove_file(snap.filename); }
        for (std::string& filename : far_files[side]) remove_file(filename);
    }
    std::cout << "MiB for the tables: peak " << (peak_bytes - dependency_bytes) / (1024*1024)
              << " during init, " << (T_bytes[0] + T_bytes[1]) / (1024*1024) << " during retrograde (int16: "
              << (2 * (WTM_EGTB->num_pos + BTM_EGTB->num_pos)) / (1024*1024) << "), dependencies: "
              << dependency_bytes / (1024*1024) << std::endl;

    // ---- Step 6. Consistency checks from compressed files
    if (do_consistency_checks) {
        TimePoint tt0 = now();
        check_consistency_max_bytes_allocated(nthreads, CONSISTENCY_CHECK_MAX_BYTES);
        TimePoint tt1 = now();
        std::cout << "Finished in-memory consistency checks in " << (double) (tt1 - tt0)/ 1000.0 << "s." << std::endl;
    }

    // ---- Step 7. Clean up
    free_tb_dependencies();
    assert (bytes_allocated == 0);
    assert (bytes_mmaped == 0);

    TimePoint t6 = now();
    std::cout << "Finished TBs " << WTM_EGTB->id << " and " << BTM_EGTB->id << " in " << (double) (t6 - t0)/ 1000.0 << "s." << std::endl;
}
