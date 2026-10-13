#include <iostream>
#include <vector>
#include <random>
#include <prophet.h>
#include <prophet_common.h>
#include "legacy_v1.h"
#include <filesystem>
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

void test_roundtrip(const std::string& scratch, uint64_t num_pos, uint64_t block_size) {
    std::vector<int16_t> TB = make_table(num_pos, 2 + num_pos);
    std::vector<int16_t> work = TB; // block_compress_TB overwrites its input
    std::string filename = scratch + "/roundtrip_" + std::to_string(num_pos) + "_" + std::to_string(block_size) + COMP_EXT;
    block_compress_TB(work.data(), num_pos, 4, 3, block_size, filename, true, false);

    CompressedHeader header;
    CHECK(read_compressed_header(filename, header));
    CHECK(header.version == COMP_VERSION);
    CHECK(header.header_size == COMP_V2_HEADER_SIZE);
    CHECK(header.num_pos == num_pos);
    CHECK(header.block_size == block_size);
    CHECK(header.checksum == compute_checksum(TB.data(), num_pos, 1));
    CHECK(is_complete_compressed_file(filename));

    {
        CompressedTB ctb(0, filename);
        CHECK(ctb.version == COMP_VERSION);
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
    std::cout << "test_roundtrip(" << num_pos << ", " << block_size << ") done" << std::endl;
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

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "usage: unit_tests.out [scratch_folder] (optional: [reference_root])" << std::endl;
        return 2;
    }
    std::string scratch = argv[1];
    fs::create_directories(scratch);
    prophet_tb_init();

    test_checksum();
    test_roundtrip(scratch, 1, 32768);
    test_roundtrip(scratch, 100000, 32768);
    test_roundtrip(scratch, 100000, 1024);
    test_roundtrip(scratch, 300000, 131072); // block size > 65535 uses UINT32 block sizes
    if (argc > 2) test_v1(scratch, argv[2]);

    std::cout << (n_failed == 0 ? "All unit tests passed." : "Some unit tests FAILED.") << std::endl;
    return n_failed == 0 ? 0 : 1;
}
