#include <iostream>
#include <vector>
#include <random>
#include <prophet.h>
#include <prophet_common.h>
#include "legacy_v1.h"
#include <filesystem>
#include <chrono>
#include <functional>
#include <map>
#include <cstring>
namespace fs = std::filesystem;

/*
Unit tests for the table format and index code.
build/unit_tests.out [scratch_folder] (optional: [reference_root] with v1 tables, e.g. ~/prophet_egtbs/egtbs_32768)
*/

static int n_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::cout << "FAILED: " << #cond << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; n_failed++; } } while (0)

std::vector<int16_t> make_table(uint64_t num_pos, uint64_t seed) {
    // values like in a real table: mostly draws and short wins/losses, some long mates
    std::mt19937_64 rng(seed);
    std::vector<int16_t> TB(num_pos);
    for (uint64_t ix = 0; ix < num_pos; ix++) {
        uint64_t r = rng() % 100;
        int level = (int) (rng() % 64);
        if (r < 40) TB[ix] = 0;
        else if (r < 70) TB[ix] = WIN_IN(2 * level + 1);
        else if (r < 99) TB[ix] = LOSS_IN(2 * level);
        else TB[ix] = (rng() % 2) ? WIN_IN(301 + 2 * (rng() % 100)) : LOSS_IN(300 + 2 * (rng() % 100));
    }
    return TB;
}

void test_checksum() {
    std::vector<int16_t> TB = make_table(100000, 1);
    uint64_t c = compute_checksum(TB.data(), TB.size(), 1);
    CHECK(compute_checksum(TB.data(), TB.size(), 4) == c); // independent of number of threads

    // sign flip (win <-> loss with same distance)
    std::vector<int16_t> T2 = TB;
    T2[123] = 0; T2[124] = 0;
    uint64_t c0 = compute_checksum(T2.data(), T2.size(), 1);
    T2[123] = 500; T2[124] = -500;
    uint64_t c1 = compute_checksum(T2.data(), T2.size(), 1);
    T2[123] = -500; T2[124] = 500;
    uint64_t c2 = compute_checksum(T2.data(), T2.size(), 1);
    CHECK(c1 != c2);
    CHECK(c0 != c1);

    // single sign flip
    std::vector<int16_t> T3 = TB;
    T3[777] = 990; uint64_t d1 = compute_checksum(T3.data(), T3.size(), 1);
    T3[777] = -990; uint64_t d2 = compute_checksum(T3.data(), T3.size(), 1);
    CHECK(d1 != d2);
    // the v1 checksum cannot see this
    std::vector<int16_t> T4 = T3; T4[777] = 990;
    CHECK(compute_checksum_v1(T3.data(), T3.size(), 1) == compute_checksum_v1(T4.data(), T4.size(), 1));

    // swap of two different values
    std::vector<int16_t> T5 = TB;
    T5[10] = 995; T5[20] = -980;
    uint64_t e1 = compute_checksum(T5.data(), T5.size(), 1);
    std::swap(T5[10], T5[20]);
    uint64_t e2 = compute_checksum(T5.data(), T5.size(), 1);
    CHECK(e1 != e2);

    // large values (> 1023) do not collide with the position
    std::vector<int16_t> T6 = TB;
    T6[5] = 2047; uint64_t f1 = compute_checksum(T6.data(), T6.size(), 1);
    T6[5] = 1023; uint64_t f2 = compute_checksum(T6.data(), T6.size(), 1);
    CHECK(f1 != f2);

    std::cout << "test_checksum done" << std::endl;
}

std::vector<int16_t> make_small_table(uint64_t num_pos, uint64_t seed) {
    // at most 21 distinct values, no escape needed
    std::mt19937_64 rng(seed);
    std::vector<int16_t> TB(num_pos);
    for (uint64_t ix = 0; ix < num_pos; ix++) {
        uint64_t r = rng() % 3;
        int level = (int) (rng() % 10);
        TB[ix] = r == 0 ? 0 : (r == 1 ? WIN_IN(2 * level + 1) : LOSS_IN(2 * level));
    }
    return TB;
}

void test_block_codec() {
    ValueDict dict;
    std::vector<int16_t> TB = make_table(50000, 3);
    build_value_dict(TB.data(), TB.size(), 2, dict);
    CHECK(dict.escape == (EGTB_MAX_DICT < 330)); // make_table has about 330 distinct values
    for (uint64_t i = 1; i < dict.size; i++) CHECK(dict.values[i - 1] != dict.values[i]);
    CHECK(dict.values[0] == 0); // draws are the most frequent value

    std::vector<int16_t> code_of(65536, -1);
    for (uint64_t i = 0; i < dict.size; i++) code_of[(uint16_t) dict.values[i]] = i;
    std::vector<uint8_t> payload(3 * TB.size());
    uint64_t payload_size = encode_block(TB.data(), TB.size(), code_of.data(), dict.size, payload.data());
    std::vector<int16_t> D(TB.size());
    CHECK(decode_block(payload.data(), payload_size, TB.size(), dict, D.data()));
    CHECK(D == TB);

    // inconsistent payloads are rejected
    CHECK(!decode_block(payload.data(), payload_size - 1, TB.size(), dict, D.data()));
    CHECK(!decode_block(payload.data(), payload_size + 2, TB.size(), dict, D.data()));
    std::vector<uint8_t> bad = payload;
    bad[0] = 255; // invalid code (or escape without escaped value)
    if (dict.size < 255) CHECK(!decode_block(bad.data(), payload_size, TB.size(), dict, D.data()));

    std::vector<int16_t> S = make_small_table(50000, 4);
    build_value_dict(S.data(), S.size(), 2, dict);
    CHECK(!dict.escape || EGTB_MAX_DICT < 21);
    std::cout << "test_block_codec done" << std::endl;
}

void test_roundtrip(const std::string& scratch, uint64_t num_pos, uint64_t block_size, bool small_dict) {
    std::vector<int16_t> TB = small_dict ? make_small_table(num_pos, 2 + num_pos) : make_table(num_pos, 2 + num_pos);
    std::vector<int16_t> work = TB; // block_compress_TB overwrites its input
    std::string filename = scratch + "/roundtrip_" + std::to_string(num_pos) + "_" + std::to_string(block_size) + COMP_EXT;
    block_compress_TB(work.data(), num_pos, 4, 3, block_size, filename, true, false);

    CompressedHeader header;
    CHECK(read_compressed_header(filename, header));
    CHECK(header.version == COMP_VERSION);
    CHECK(header.flags & FLAG_RANK_ENCODED);
    CHECK(header.header_size > COMP_V2_HEADER_SIZE);
    CHECK(header.num_pos == num_pos);
    CHECK(header.block_size == block_size);
    CHECK(header.checksum == compute_checksum(TB.data(), num_pos, 1));
    CHECK(is_complete_compressed_file(filename));

    {
        CompressedTB ctb(0, filename);
        CHECK(ctb.version == COMP_VERSION);
        CHECK(ctb.dict.size > 0);
        if (!small_dict && num_pos > 10000) CHECK(ctb.dict.escape == (EGTB_MAX_DICT < 330));
        if (small_dict) CHECK(!ctb.dict.escape || EGTB_MAX_DICT < 21);
        CHECK(ctb.check_integrity(4));
        std::vector<int16_t> D(num_pos);
        ctb.decompress_to_array(4, D.data());
        CHECK(D == TB);
        DecompressCtx dctx(block_size);
        std::mt19937_64 rng(7);
        bool all_equal = true;
        for (int i = 0; i < 10000; i++) {
            uint64_t ix = rng() % num_pos;
            all_equal = all_equal && ctb.get_value_dctx(ix, &dctx) == TB[ix];
        }
        CHECK(all_equal);
    }

    // truncated file is detected
    fs::resize_file(filename, fs::file_size(filename) - 1);
    CHECK(!is_complete_compressed_file(filename));
    // truncated v2 header is not mistaken for a v1 header
    fs::resize_file(filename, COMP_V2_HEADER_SIZE - 8);
    CHECK(!read_compressed_header(filename, header));
    CHECK(!is_complete_compressed_file(filename));
    fs::remove(filename);
    std::cout << "test_roundtrip(" << num_pos << ", " << block_size << (small_dict ? ", small dictionary" : "") << ") done" << std::endl;
}

void test_v1(const std::string& scratch, const std::string& ref_root) {
    for (std::string egtb_id : {"KRK", "KQKR", "KPKP"}) {
        std::string filename = get_default_folder_for_id(ref_root, egtb_id) + "/" + egtb_id + ".egtb" + COMP_EXT;
        if (!fs::exists(filename)) { std::cout << "skip test_v1 for " << filename << std::endl; continue; }
        EGTB egtb(get_default_folder_for_id(ref_root, egtb_id), egtb_id);

        CompressedHeader header;
        CHECK(read_compressed_header(filename, header));
        CHECK(header.version == 1);
        CHECK(is_complete_compressed_file(filename));

        std::vector<int16_t> REF(egtb.num_pos);
        CHECK(v1::load_compressed_table(filename, egtb.num_pos, REF.data(), 4));
        std::vector<int16_t> D(egtb.num_pos);
        {
            CompressedTB ctb(0, filename);
            CHECK(ctb.version == 1);
            CHECK(ctb.check_integrity(4));
            ctb.decompress_to_array(4, D.data());
        }
        CHECK(D == REF);

        // conversion to the current version keeps all values
        std::string target = scratch + "/" + egtb_id + ".egtb" + COMP_EXT;
        recompress_TB(filename, target, 4, 3, 32768, false);
        CHECK(read_compressed_header(target, header));
        CHECK(header.version == COMP_VERSION);
        {
            CompressedTB ctb(0, target);
            CHECK(ctb.check_integrity(4));
            std::vector<int16_t> D2(egtb.num_pos);
            ctb.decompress_to_array(4, D2.data());
            CHECK(D2 == REF);
        }
        fs::remove(target);
        std::cout << "test_v1(" << egtb_id << ") done" << std::endl;
    }
}

void test_fast_tril() {
    int a[8], b[8];
    for (uint64_t n = 1; n <= 4; n++) {
        bool ok = true;
        for (uint64_t t = 0; t < number_of_ordered_tuples(64, n); t++) {
            tril_from_linear(n, t, a);
            fast_tril_from_linear(n, t, b);
            for (uint64_t i = 0; i < n; i++) ok = ok && a[i] == b[i];
        }
        CHECK(ok);
    }
    std::cout << "test_fast_tril done" << std::endl;
}

bool same_position(const EGPosition& a, const EGPosition& b) {
    return memcmp(&a, &b, sizeof(EGPosition)) == 0;
}

// compares PosDecoder with pos_at_ix_ for all indices (n_random == 0) or for random indices and sequential runs
uint64_t check_decoder(const std::string& egtb_id, uint64_t n_random) {
    EGTB egtb("unused", egtb_id);
    uint64_t n_mismatch = 0;
    for (Color stm : {WHITE, BLACK}) {
        if (n_random == 0) {
            #pragma omp parallel reduction(+:n_mismatch)
            {
                PosDecoder seq(egtb.get_layout(), stm), rnd(egtb.get_layout(), stm);
                EGPosition p1, p2;
                #pragma omp for schedule(static, 65536)
                for (uint64_t ix = 0; ix < egtb.num_pos; ix++) {
                    p1.reset(); egtb.pos_at_ix(p1, ix, stm);
                    seq.pos_at_ix(p2, ix);
                    n_mismatch += !same_position(p1, p2);
                    // every 7th index, so that the slice hint has to jump
                    if (ix % 7 == 0) {
                        rnd.pos_at_ix(p2, ix);
                        n_mismatch += !same_position(p1, p2);
                    }
                }
            }
        } else {
            PosDecoder seq(egtb.get_layout(), stm), rnd(egtb.get_layout(), stm);
            EGPosition p1, p2;
            std::mt19937_64 rng(egtb.num_pos + stm);
            for (uint64_t i = 0; i < n_random; i++) {
                uint64_t ix = rng() % egtb.num_pos;
                p1.reset(); egtb.pos_at_ix(p1, ix, stm);
                rnd.pos_at_ix(p2, ix);
                n_mismatch += !same_position(p1, p2);
                // short sequential run from ix (crosses digit carries and slice boundaries)
                if (i % 100 == 0) {
                    for (uint64_t jx = ix; jx < std::min(egtb.num_pos, ix + 5000); jx++) {
                        p1.reset(); egtb.pos_at_ix(p1, jx, stm);
                        seq.pos_at_ix(p2, jx);
                        n_mismatch += !same_position(p1, p2);
                    }
                }
            }
            // slice boundaries and the first en-passant indices
            for (uint64_t ix : {(uint64_t) 0, egtb.num_nonep_pos - 1, std::min(egtb.num_pos - 1, egtb.num_nonep_pos), egtb.num_pos - 1}) {
                p1.reset(); egtb.pos_at_ix(p1, ix, stm);
                rnd.pos_at_ix(p2, ix);
                n_mismatch += !same_position(p1, p2);
            }
        }
    }
    return n_mismatch;
}

void test_decoder() {
    // exhaustive for all tables up to 4 pieces
    uint64_t n_mismatch = 0;
    for (std::string egtb_id : get_egtb_identifiers(0, 2)) n_mismatch += check_decoder(egtb_id, 0);
    CHECK(n_mismatch == 0);
    // exhaustive for some 5-men tables, sampled for some 6-men tables
    // (pawns on both sides incl. en-passant, identical pieces up to 4, pawnless)
    for (std::string egtb_id : {"KNPKB", "KPPKP", "KQRKR", "KPPPK", "KBBKN"}) {
        uint64_t n = check_decoder(egtb_id, 0);
        if (n) std::cout << egtb_id << ": " << n << " mismatches" << std::endl;
        CHECK(n == 0);
    }
    for (std::string egtb_id : {"KRPKRP", "KPPPPK", "KPPKPP", "KQRBKN", "KBBBKN", "KNNKNN", "KQPKRB"}) {
        uint64_t n = check_decoder(egtb_id, 200000);
        if (n) std::cout << egtb_id << ": " << n << " mismatches" << std::endl;
        CHECK(n == 0);
    }
    std::cout << "test_decoder done" << std::endl;
}

void bench_decoder(const std::string& egtb_id) {
    EGTB egtb("unused", egtb_id);
    const uint64_t N = 2000000;
    std::mt19937_64 rng(1);
    std::vector<uint64_t> ixs(N);
    for (auto& x : ixs) x = rng() % egtb.num_nonep_pos;
    uint64_t start = rng() % (egtb.num_nonep_pos - N);
    EGPosition pos;
    PosDecoder dec(egtb.get_layout(), WHITE);
    uint64_t sink = 0;
    auto time = [&](auto f) {
        auto t0 = std::chrono::steady_clock::now();
        for (uint64_t i = 0; i < N; i++) { f(i); sink += pos.pieces(); }
        return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / N;
    };
    double old_rnd = time([&](uint64_t i) { pos.reset(); egtb.pos_at_ix(pos, ixs[i], WHITE); });
    double new_rnd = time([&](uint64_t i) { dec.pos_at_ix(pos, ixs[i]); });
    double old_seq = time([&](uint64_t i) { pos.reset(); egtb.pos_at_ix(pos, start + i, WHITE); });
    double new_seq = time([&](uint64_t i) { dec.pos_at_ix(pos, start + i); });
    printf("bench %-7s pos_at_ix_: random %5.1f ns, sequential %5.1f ns | PosDecoder: random %5.1f ns, sequential %5.1f ns (%lu)\n",
        egtb_id.c_str(), old_rnd, old_seq, new_rnd, new_seq, (unsigned long) (sink & 1));
}

// all table ids with n non-king pieces (both orientations)
std::vector<std::string> all_ids(int n) {
    std::vector<std::string> ids;
    std::vector<int> pc(10, 0);
    std::function<void(int, int)> rec = [&](int i, int left) {
        if (i == 9) {
            pc[9] = left;
            int w[6] = {0}, b[6] = {0};
            for (int k = 0; k < 5; k++) { w[k + 1] = pc[k]; b[k + 1] = pc[k + 5]; }
            ids.push_back(get_egtb_identifier(w, b));
            return;
        }
        for (int k = 0; k <= left; k++) { pc[i] = k; rec(i + 1, left - k); }
    };
    rec(0, n);
    return ids;
}

void test_split_index_sizes() {
    // the parts of a split table cover exactly the positions of the original index (Vandermonde's identity)
    uint64_t n_split = 0, n_bad = 0;
    for (int n = 1; n <= 5; n++) {
        for (std::string egtb_id : all_ids(n)) {
            EGTB egtb("unused", egtb_id);
            if (!egtb.split()) continue;
            n_split++;
            uint64_t kc[N_KKP + 1], nonep, ep, num;
            v1::compute_poscounts(egtb.stm_pieces, egtb.sntm_pieces, kc, nonep, ep, num);
            bool ok = egtb.num_pos == num && egtb.num_nonep_pos == nonep && egtb.num_ep_pos == ep;
            uint64_t end = 0;
            for (int p = 0; p < egtb.n_parts(); p++) {
                ok = ok && egtb.part_start(p) == end && egtb.part_size(p) > 0;
                end = egtb.part_end(p);
            }
            ok = ok && end == egtb.num_pos;
            int expected_parts = egtb.npawns == 1 ? 4 : ((egtb.stm_pieces[PAWN] == 1) ? 16 : 10);
            ok = ok && egtb.n_parts() == expected_parts;
            if (!ok) { std::cout << egtb_id << ": part sizes do not match the original index" << std::endl; n_bad++; }
        }
    }
    CHECK(n_bad == 0);
    std::cout << "test_split_index_sizes done (" << n_split << " split tables up to 7 pieces)" << std::endl;
}

// every position that is used in the original (v1) index maps to a used index of the new index that decodes to
// the same position (up to symmetry), and both indices have the same number of used positions
uint64_t check_split_bijection(const std::string& egtb_id, uint64_t n_random) {
    EGTB egtb("unused", egtb_id);
    uint64_t kc[N_KKP + 1], v1_nonep, v1_ep, v1_num;
    v1::compute_poscounts(egtb.stm_pieces, egtb.sntm_pieces, kc, v1_nonep, v1_ep, v1_num);
    uint64_t n_bad = 0, n_used_v1 = 0, n_used_new = 0;
    for (Color stm : {WHITE, BLACK}) {
        uint64_t n_v1 = n_random ? n_random : v1_num;
        #pragma omp parallel for schedule(static, 4096) reduction(+:n_bad,n_used_v1)
        for (uint64_t i = 0; i < n_v1; i++) {
            uint64_t ix = n_random ? (i * 0x9E3779B97F4A7C15ULL) % v1_num : i;
            EGPosition pos;
            pos.reset();
            v1::pos_at_ix_(pos, ix, stm, egtb.stm_pieces, egtb.sntm_pieces, kc);
            if (popcount(pos.pieces()) != egtb.npieces || pos.sntm_in_check()) continue;
            if (ix >= v1_nonep && !pos.check_ep(pos.ep_square())) continue;
            if (v1::ix_from_pos_(pos, kc) != ix) continue;
            n_used_v1++;
            uint64_t new_ix = egtb.ix_from_pos(pos);
            EGPosition pos2;
            pos2.reset();
            egtb.pos_at_ix(pos2, new_ix, stm);
            if (!egtb.pos_ix_is_used(pos2, new_ix) || v1::ix_from_pos_(pos2, kc) != ix) n_bad++;
        }
        if (n_random == 0) {
            #pragma omp parallel for schedule(static, 4096) reduction(+:n_used_new)
            for (uint64_t ix = 0; ix < egtb.num_pos; ix++) {
                EGPosition pos;
                pos.reset();
                egtb.pos_at_ix(pos, ix, stm);
                n_used_new += egtb.pos_ix_is_used(pos, ix);
            }
        }
    }
    if (n_random == 0 && n_used_new != n_used_v1) n_bad++;
    return n_bad;
}

void test_split_bijection() {
    uint64_t n_bad = 0;
    for (int n = 1; n <= 2; n++) {
        for (std::string egtb_id : all_ids(n)) {
            if (EGTB("unused", egtb_id).split()) n_bad += check_split_bijection(egtb_id, 0);
        }
    }
    CHECK(n_bad == 0);
    // 5 pieces: one pawn, two pawns of one side, one pawn each (en passant)
    for (std::string egtb_id : {"KNPKB", "KPKRP"}) {
        uint64_t n = check_split_bijection(egtb_id, 0);
        if (n) std::cout << egtb_id << ": " << n << " bad" << std::endl;
        CHECK(n == 0);
    }
    for (std::string egtb_id : {"KBKNP", "KPPKN", "KNKPP", "KRPKP"}) {
        uint64_t n = check_split_bijection(egtb_id, 5000000);
        if (n) std::cout << egtb_id << ": " << n << " bad" << std::endl;
        CHECK(n == 0);
    }
    for (std::string egtb_id : {"KRPKRP", "KQPKRB", "KRBKPP", "KPPKRB", "KRPPKR"}) {
        uint64_t n = check_split_bijection(egtb_id, 2000000);
        if (n) std::cout << egtb_id << ": " << n << " bad" << std::endl;
        CHECK(n == 0);
    }
    std::cout << "test_split_bijection done" << std::endl;
}

// promotions only lead to parts that promotion_compatible allows
void test_promotion_compatible() {
    uint64_t n_promotions = 0, n_bad = 0;
    std::vector<std::string> ids = {"KPKP", "KPPK", "KKPP", "KPK", "KNPKB", "KPPKN", "KPKNP"};
    for (std::string egtb_id : ids) {
        EGTB egtb("unused", egtb_id);
        // tables after a promotion (white promotes, black to move)
        std::map<std::string, EGTB*> children;
        for (PieceType pt = KNIGHT; pt <= QUEEN; ++pt) {
            int s[6], n[6];
            for (int i = 0; i < 6; i++) { s[i] = egtb.sntm_pieces[i]; n[i] = egtb.stm_pieces[i]; }
            if (n[PAWN] == 0) continue;
            n[PAWN]--; n[pt]++;
            std::string child_id = get_egtb_identifier(s, n);
            children[child_id] = new EGTB("unused", child_id);
        }
        #pragma omp parallel for schedule(dynamic, 65536) reduction(+:n_promotions,n_bad)
        for (uint64_t ix = 0; ix < egtb.num_pos; ix++) {
            EGPosition pos;
            pos.reset();
            egtb.pos_at_ix(pos, ix, WHITE);
            if (!egtb.pos_ix_is_used(pos, ix)) continue;
            for (Move move : EGMoveList(pos)) {
                if (move.type_of() != PROMOTION || pos.piece_on(move.to_sq()) != NO_PIECE) continue; // capture-promotion tables are always loaded completely
                UndoInfo u = pos.do_move(move);
                int s[6] = {0}, n[6] = {0};
                for (int pt = PAWN; pt <= QUEEN; pt++) { s[pt] = pos.count(BLACK, PieceType(pt)); n[pt] = pos.count(WHITE, PieceType(pt)); }
                EGTB* child = children.at(get_egtb_identifier(s, n));
                uint64_t child_ix = child->ix_from_pos(pos);
                n_promotions++;
                if (!promotion_compatible(egtb.index, egtb.part_of(ix), child->index, child->part_of(child_ix))) n_bad++;
                pos.undo_move(u);
            }
        }
        for (auto& kv : children) delete kv.second;
    }
    CHECK(n_bad == 0);
    CHECK(n_promotions > 0);
    std::cout << "test_promotion_compatible done (" << n_promotions << " promotions)" << std::endl;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "usage: unit_tests.out [scratch_folder] (optional: [reference_root])" << std::endl;
        return 2;
    }
    std::string scratch = argv[1];
    fs::create_directories(scratch);
    prophet_tb_init();

    test_checksum();
    test_block_codec();
    test_roundtrip(scratch, 1, 32768, false);
    test_roundtrip(scratch, 100000, 32768, false);
    test_roundtrip(scratch, 100000, 32768, true);
    test_roundtrip(scratch, 100000, 1024, false);
    test_roundtrip(scratch, 300000, 131072, false); // block size > 65535 uses UINT32 block sizes
    if (argc > 2) test_v1(scratch, argv[2]);
    test_fast_tril();
    test_split_index_sizes();
    test_split_bijection();
    test_promotion_compatible();
    test_decoder();
    for (std::string egtb_id : {"KNPKB", "KPPKP", "KQRKR", "KRPKRP", "KQRBKN"}) bench_decoder(egtb_id);

    std::cout << (n_failed == 0 ? "All unit tests passed." : "Some unit tests FAILED.") << std::endl;
    return n_failed == 0 ? 0 : 1;
}
