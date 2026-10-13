#include <iostream>
#include <vector>
#include <set>
#include <random>
#include <prophet.h>
#include <prophet_common.h>
#include "legacy_v1.h"
#include "capture_value.h"
#include <map>
#include <mutex>
#include "misc.h"
#ifdef OMP
#include <omp.h>
#endif
#include <filesystem>
namespace fs = std::filesystem;

/*
Exhaustively compares freshly generated tables in [new_root] against reference tables in [ref_root].
The reference tables are read with the frozen v1 index and file format (legacy_v1.h), the new tables with the
current code. For every used position of a new table, its position is decoded, mapped to the reference index
and the values have to be equal. Additionally, the number of used positions has to agree and [n_samples]
random positions per table are probed through the prophet API (exercises the probing code path).
For tables with capture don't-cares (finalize), a stored value that differs from the reference has to be worse
than the reference value, and the best capture (computed with the reference capture tables) has to have the
reference value.

build/compare_tables.out [ref_root] [new_root] [threads] [n_samples] (optional: [egtb_id] ...)
Returns 0 if all tables agree.
*/

int raw_to_dtm(int16_t raw) {
    if (raw == 0) return 1000;
    if (raw > 0) return WIN_IN(0) - raw;
    return LOSS_IN(0) - raw;
}

bool v1_pos_ix_is_used(EGPosition const &pos, uint64_t ix, int npieces, uint64_t num_nonep_pos, const uint64_t* kc) {
    if (popcount(pos.pieces()) != npieces) return false;
    if (pos.sntm_in_check()) return false;
    if (ix >= num_nonep_pos && !pos.check_ep(pos.ep_square())) return false;
    return v1::ix_from_pos_(pos, kc) == ix;
}

// reference tables (v1) of capture targets, loaded on demand
struct ReferenceTables {
    std::string ref_root;
    std::map<std::string, std::pair<std::vector<uint64_t>, std::vector<int16_t>>> tables; // id -> (poscounts, values)
    std::mutex mutex;

    // raw value of pos in the reference table (> WIN_IN(0) if not available)
    int16_t value(const EGPosition& pos) {
        int s[6] = {0}, n[6] = {0};
        for (PieceType pt = PAWN; pt <= QUEEN; ++pt) { s[pt] = pos.count(pos.side_to_move(), pt); n[pt] = pos.count(~pos.side_to_move(), pt); }
        std::string id = get_egtb_identifier(s, n);
        std::pair<std::vector<uint64_t>, std::vector<int16_t>>* table;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = tables.find(id);
            if (it == tables.end()) {
                std::vector<uint64_t> kc(N_KKP + 1);
                uint64_t nonep, ep, num;
                v1::compute_poscounts(s, n, kc.data(), nonep, ep, num);
                std::vector<int16_t> values(num);
                std::string filename = get_default_folder(ref_root, s, n) + "/" + id + ".egtb" + COMP_EXT;
                if (!v1::load_compressed_table(filename, num, values.data(), 1)) return 2 * WIN_IN(0);
                it = tables.emplace(id, std::make_pair(kc, values)).first;
            }
            table = &it->second;
        }
        return table->second[v1::ix_from_pos_(pos, table->first.data())];
    }
};

int main(int argc, char *argv[]) {
    if (argc < 5) {
        std::cerr << "usage: compare_tables.out [ref_root] [new_root] [threads] [n_samples] (optional: [egtb_id] ...)" << std::endl;
        return 2;
    }
    std::string ref_root = argv[1];
    std::string new_root = argv[2];
    int nthreads = atoi(argv[3]);
    uint64_t n_samples = atoll(argv[4]);
    #ifndef OMP
    assert(nthreads == 1);
    #endif

    prophet_tb_init();
    int filecount = prophet_tb_add_path(new_root.c_str());
    std::cout << "Found " << filecount << " tables in " << new_root << std::endl;

    std::set<std::string> egtb_ids;
    if (argc > 5) {
        for (int i = 5; i < argc; i++) egtb_ids.insert(argv[i]);
    } else {
        for (const auto & entry : fs::recursive_directory_iterator(new_root)) {
            if (entry.path().extension() == COMP_EXT) {
                std::string filename = entry.path().filename().u8string();
                egtb_ids.insert(filename.substr(0, filename.find_first_of(".")));
            }
        }
    }

    uint64_t n_ok = 0, n_failed = 0;
    TimePoint t0 = now();
    for (std::string egtb_id : egtb_ids) {
        TimePoint t_start = now();
        EGTB egtb = EGTB(get_default_folder_for_id(new_root, egtb_id), egtb_id);

        uint64_t kc[N_KKP + 1], v1_num_nonep_pos, v1_num_ep_pos, v1_num_pos;
        v1::compute_poscounts(egtb.stm_pieces, egtb.sntm_pieces, kc, v1_num_nonep_pos, v1_num_ep_pos, v1_num_pos);
        std::string ref_filename = get_default_folder_for_id(ref_root, egtb_id) + "/" + egtb_id + ".egtb" + COMP_EXT;

        std::vector<int16_t> REF(v1_num_pos);
        if (!v1::load_compressed_table(ref_filename, v1_num_pos, REF.data(), nthreads)) {
            std::cout << egtb_id << ": FAILED (could not read reference " << ref_filename << ")" << std::endl;
            n_failed++;
            continue;
        }
        if (!egtb.exists()) {
            std::cout << egtb_id << ": FAILED (missing in " << new_root << ")" << std::endl;
            n_failed++;
            continue;
        }
        egtb.load_egtb_from_compressed_file(nthreads);
        bool capture_dc = egtb.capture_dc;
        ReferenceTables capture_tables;
        capture_tables.ref_root = ref_root;

        // new -> reference: every used position has the same value
        uint64_t n_used = 0, n_mismatch = 0, first_mismatch = UINT64_MAX;
        #pragma omp parallel for num_threads(nthreads) schedule(static, 4096) reduction(+:n_used,n_mismatch) reduction(min:first_mismatch)
        for (uint64_t ix = 0; ix < egtb.num_pos; ix++) {
            EGPosition pos;
            pos.reset();
            egtb.pos_at_ix(pos, ix, WHITE);
            if (!egtb.pos_ix_is_used(pos, ix)) continue;
            n_used++;
            uint64_t ref_ix = v1::ix_from_pos_(pos, kc);
            if (egtb.TB[ix] != REF[ref_ix]) {
                bool ok = false;
                if (capture_dc && egtb.TB[ix] < REF[ref_ix]) {
                    // a capture don't-care: the best capture has the value
                    int16_t capture_value;
                    auto child = [&capture_tables](EGPosition& child_pos) { return capture_tables.value(child_pos); };
                    ok = best_capture_value(pos, child, capture_value) == CAPTURE_VALUE && capture_value == REF[ref_ix];
                }
                if (!ok) {
                    n_mismatch++;
                    first_mismatch = std::min(first_mismatch, ix);
                }
            }
        }

        // reference: count used positions, so that no position is missing in the new table
        uint64_t n_used_ref = 0;
        #pragma omp parallel for num_threads(nthreads) schedule(static, 4096) reduction(+:n_used_ref)
        for (uint64_t ix = 0; ix < v1_num_pos; ix++) {
            EGPosition pos;
            pos.reset();
            v1::pos_at_ix_(pos, ix, WHITE, egtb.stm_pieces, egtb.sntm_pieces, kc);
            if (v1_pos_ix_is_used(pos, ix, egtb.npieces, v1_num_nonep_pos, kc)) n_used_ref++;
        }

        // probing through the prophet API
        uint64_t n_probe_mismatch = 0, n_probed = 0;
        #pragma omp parallel num_threads(nthreads) reduction(+:n_probe_mismatch,n_probed)
        {
            #ifdef OMP
            std::mt19937_64 rng(12345 + omp_get_thread_num());
            #else
            std::mt19937_64 rng(12345);
            #endif
            std::uniform_int_distribution<uint64_t> dist(0, egtb.num_pos - 1);
            prophet_tb_decompress_ctx* dctx = prophet_tb_create_decompress_ctx();
            #pragma omp for schedule(static)
            for (uint64_t s = 0; s < n_samples; s++) {
                EGPosition pos;
                uint64_t ix = 0;
                bool found = false;
                for (int tries = 0; tries < 1000 && !found; tries++) {
                    ix = dist(rng);
                    pos.reset();
                    egtb.pos_at_ix(pos, ix, WHITE);
                    found = egtb.pos_ix_is_used(pos, ix);
                }
                if (!found) continue;
                int pieces[6], squares[6], n = 0;
                Bitboard occ = pos.pieces();
                while (occ) {
                    Square sq = pop_lsb(occ);
                    pieces[n] = pos.piece_on(sq);
                    squares[n] = sq;
                    n++;
                }
                for (; n < 6; n++) { pieces[n] = NO_PIECE; squares[n] = SQ_NONE; }
                int ep_square = pos.ep_square() == SQ_NONE ? SQ_NONE : pos.ep_square();
                int dtm = prophet_tb_probe_dtm_dctx(pieces, squares, pos.side_to_move(), ep_square, dctx);
                n_probed++;
                if (dtm != raw_to_dtm(REF[v1::ix_from_pos_(pos, kc)])) n_probe_mismatch++;
            }
            prophet_tb_free_decompress_ctx(dctx);
        }
        egtb.free_tb();

        bool ok = n_mismatch == 0 && n_used == n_used_ref && n_probe_mismatch == 0;
        std::cout << egtb_id << ": " << (ok ? "OK" : "FAILED") << (capture_dc ? " [capture don't-cares]" : "") << " (used: " << n_used << "/" << n_used_ref
                  << ", mismatches: " << n_mismatch << ", probes: " << n_probe_mismatch << "/" << n_probed << " wrong";
        if (n_mismatch > 0) std::cout << ", first mismatch at ix " << first_mismatch;
        std::cout << ", " << (double) (now() - t_start) / 1000.0 << "s)" << std::endl;
        if (ok) n_ok++; else n_failed++;
    }

    std::cout << "Compared " << n_ok + n_failed << " tables: " << n_ok << " OK, " << n_failed << " FAILED in "
              << (double) (now() - t0) / 1000.0 << "s." << std::endl;
    prophet_tb_deinit();
    return n_failed == 0 ? 0 : 1;
}
