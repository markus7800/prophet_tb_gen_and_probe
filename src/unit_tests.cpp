#include <iostream>
#include <vector>
#include <random>
#include <prophet.h>
#include <prophet_common.h>
#include "legacy_v1.h"
#include <filesystem>
#include <chrono>
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

// the file index of every digit order is a permutation of each king pair slice
uint64_t check_digit_orders(const std::string& egtb_id) {
    EGTB egtb("unused", egtb_id);
    uint64_t n_bad = 0;
    int n_kk = egtb.npawns == 0 ? N_KKX : N_KKP;
    // radices of the slots multiply to the slice sizes
    for (int kix = 0; kix < n_kk; kix++) {
        uint64_t radix[N_DIGIT_SLOTS], product = 1;
        slice_radices(egtb.stm_pieces, egtb.sntm_pieces, kix, radix);
        for (int s = 0; s < N_DIGIT_SLOTS; s++) product *= radix[s];
        n_bad += product != egtb.kntm_poscounts[kix + 1] - egtb.kntm_poscounts[kix];
    }
    for (const std::vector<int>& order : all_digit_orders(egtb.stm_pieces, egtb.sntm_pieces)) {
        std::vector<int> unpacked;
        n_bad += !unpack_digit_order(pack_digit_order(order), egtb.stm_pieces, egtb.sntm_pieces, unpacked) || unpacked != order;
        std::vector<char> seen(egtb.num_pos, 0);
        #pragma omp parallel reduction(+:n_bad)
        {
            DigitCursor cursor(egtb.stm_pieces, egtb.sntm_pieces, egtb.kntm_poscounts, egtb.num_nonep_pos, order);
            #pragma omp for schedule(static, 65536)
            for (uint64_t ix = 0; ix < egtb.num_pos; ix++) {
                uint64_t file_ix = cursor.to_file(ix);
                n_bad += file_ix >= egtb.num_pos || cursor.to_canonical(file_ix) != ix;
                if (file_ix < egtb.num_pos) seen[file_ix] = 1; // races only write 1
                if (ix >= egtb.num_nonep_pos) n_bad += file_ix != ix; // en-passant section is not permuted
            }
        }
        for (char c : seen) n_bad += c == 0;
    }
    // invalid orders are rejected
    std::vector<int> slots;
    std::vector<int> canonical = nonempty_slots(egtb.stm_pieces, egtb.sntm_pieces);
    std::vector<int> wrong = canonical;
    wrong.push_back(canonical.back() == 9 ? 0 : 9);
    n_bad += unpack_digit_order(pack_digit_order(wrong), egtb.stm_pieces, egtb.sntm_pieces, slots);
    return n_bad;
}

void test_digit_order(const std::string& scratch) {
    uint64_t n_bad = 0;
    for (std::string egtb_id : get_egtb_identifiers(1, 2)) n_bad += check_digit_orders(egtb_id);
    for (std::string egtb_id : {"KNPKB", "KPPKP", "KQRKR", "KBBKN"}) {
        uint64_t n = check_digit_orders(egtb_id);
        if (n) std::cout << egtb_id << ": " << n << " bad" << std::endl;
        n_bad += n;
    }
    CHECK(n_bad == 0);

    // the digit order is stored in the header and kept by recompress
    std::vector<int16_t> TB = make_table(100000, 5);
    std::vector<int> order = {5, 3, 6}; // some order of three slots
    uint64_t packed = pack_digit_order(order);
    std::string filename = scratch + "/digit_order" + COMP_EXT;
    std::string filename2 = scratch + "/digit_order2" + COMP_EXT;
    block_compress_TB(TB.data(), TB.size(), 4, 3, 32768, filename, true, false, packed);
    {
        CompressedTB ctb(0, filename);
        CHECK(ctb.digit_order == packed);
        CHECK(ctb.flags & FLAG_DIGIT_ORDER);
        CHECK(ctb.check_integrity(4));
    }
    recompress_TB(filename, filename2, 4, 3, 1024, false);
    {
        CompressedTB ctb(0, filename2);
        CHECK(ctb.digit_order == packed);
        std::vector<int16_t> D(TB.size());
        ctb.decompress_to_array(4, D.data());
        CHECK(D == TB);
    }
    fs::remove(filename);
    fs::remove(filename2);
    std::cout << "test_digit_order done" << std::endl;
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
    test_decoder();
    test_digit_order(scratch);
    for (std::string egtb_id : {"KNPKB", "KPPKP", "KQRKR", "KRPKRP", "KQRBKN"}) bench_decoder(egtb_id);

    std::cout << (n_failed == 0 ? "All unit tests passed." : "Some unit tests FAILED.") << std::endl;
    return n_failed == 0 ? 0 : 1;
}
