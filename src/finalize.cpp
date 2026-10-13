#include <iostream>
#include <vector>
#include <algorithm>
#include <set>
#include <prophet.h>
#include <prophet_common.h>
#include "misc.h"
#ifdef OMP
#include <omp.h>
#endif
#include <filesystem>
namespace fs = std::filesystem;

/*
Finalizes generated tables for distribution: chooses the digit order of the file index that compresses best
(see digit_order.h) and rewrites the files with it. The values do not change, only their order in the file.

build/finalize.out [folder] [threads] (options) (optional: [egtb_id] ...)
  --out DIR           write the finalized files to DIR (same folder structure) instead of replacing them
  --level L           zstd level of the finalized files and of the estimation (default 19)
  --sample-blocks N   number of blocks that are test-compressed per digit order (default 256)
  --min-gain G        keep the canonical order unless another order is estimated to be smaller by G (default 0.01)
  --dry-run           only estimate, do not write

For every table, all orders of the groups of identical pieces are estimated by compressing the same sample of
blocks (of the file index) with the global value dictionary of the table. The finalized file is verified against
the original values before it replaces the original file. Files that are already finalized are skipped, files for
which the canonical order is best stay unchanged (and are estimated again on the next run).
Finalized tables can still be probed and loaded, but the generator needs the original files of promotion tables
(it reads them sequentially in canonical order).
*/

struct Options {
    std::string out_dir;
    int level = 19;
    uint64_t sample_blocks = 256;
    double min_gain = 0.01;
    bool dry_run = false;
};

// estimated compressed size of the sampled blocks for every order
std::vector<uint64_t> estimate_orders(EGTB& egtb, const std::vector<std::vector<int>>& orders, uint64_t block_size, const Options& opt, int nthreads) {
    uint64_t n_blocks = (egtb.num_pos + block_size - 1) / block_size;
    std::vector<uint64_t> sample;
    uint64_t k = std::min(n_blocks, opt.sample_blocks);
    for (uint64_t i = 0; i < k; i++) sample.push_back(i * n_blocks / k);

    ValueDict dict;
    build_value_dict(egtb.TB, egtb.num_pos, nthreads, dict);
    std::vector<int16_t> code_of(65536, -1);
    for (uint64_t i = 0; i < dict.size; i++) code_of[(uint16_t) dict.values[i]] = i;

    std::vector<uint64_t> sizes(orders.size(), 0);
    uint64_t n_tasks = orders.size() * sample.size();
    #pragma omp parallel num_threads(nthreads)
    {
        std::vector<int16_t> values(block_size);
        std::vector<uint8_t> payload(3 * block_size);
        std::vector<uint8_t> compressed(ZSTD_compressBound(3 * block_size));
        ZSTD_CCtx* cctx = ZSTD_createCCtx();
        #pragma omp for schedule(dynamic, 1)
        for (uint64_t task = 0; task < n_tasks; task++) {
            uint64_t o = task / sample.size();
            uint64_t start = sample[task % sample.size()] * block_size;
            uint64_t n = std::min(block_size, egtb.num_pos - start);
            DigitCursor cursor(egtb.stm_pieces, egtb.sntm_pieces, egtb.kntm_poscounts, egtb.num_nonep_pos, orders[o]);
            for (uint64_t i = 0; i < n; i++) values[i] = egtb.TB[cursor.to_canonical(start + i)];
            uint64_t payload_size = encode_block(values.data(), n, code_of.data(), dict.size, payload.data());
            uint64_t size = ZSTD_compressCCtx(cctx, compressed.data(), compressed.size(), payload.data(), payload_size, opt.level);
            #pragma omp atomic
            sizes[o] += size;
        }
        ZSTD_freeCCtx(cctx);
    }
    return sizes;
}

// number of values of filename that differ from egtb.TB (canonical) when read with the given order
uint64_t verify_file(EGTB& egtb, const std::string& filename, const std::vector<int>& order, int nthreads) {
    CompressedTB ctb(0, filename);
    std::vector<int> slots;
    if (!unpack_digit_order(ctb.digit_order, egtb.stm_pieces, egtb.sntm_pieces, slots) || slots != order || ctb.num_pos != egtb.num_pos) return UINT64_MAX;
    if (!ctb.check_integrity(nthreads)) return UINT64_MAX;
    uint64_t n_wrong = 0;
    #pragma omp parallel num_threads(nthreads) reduction(+:n_wrong)
    {
        DecompressCtx dctx(ctb.block_size);
        DigitCursor cursor(egtb.stm_pieces, egtb.sntm_pieces, egtb.kntm_poscounts, egtb.num_nonep_pos, order);
        #pragma omp for schedule(static)
        for (uint64_t block_ix = 0; block_ix < ctb.n_blocks; block_ix++) {
            if (!ctb.decompress_block(block_ix, &dctx, dctx.uncompressed_buf)) { n_wrong++; continue; }
            uint64_t start = block_ix * ctb.block_size;
            for (uint64_t i = 0; i < ctb.get_block_npos(block_ix); i++) {
                n_wrong += dctx.uncompressed_buf[i] != egtb.TB[cursor.to_canonical(start + i)];
            }
        }
    }
    return n_wrong;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "usage: finalize.out [folder] [threads] (--out DIR) (--level L) (--sample-blocks N) (--min-gain G) (--dry-run) (egtb_id ...)" << std::endl;
        return 2;
    }
    std::string folder = argv[1];
    int nthreads = atoi(argv[2]);
    #ifndef OMP
    assert(nthreads == 1);
    #endif
    Options opt;
    std::set<std::string> ids;
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) opt.out_dir = argv[++i];
        else if (arg == "--level" && i + 1 < argc) opt.level = atoi(argv[++i]);
        else if (arg == "--sample-blocks" && i + 1 < argc) opt.sample_blocks = strtoull(argv[++i], NULL, 10);
        else if (arg == "--min-gain" && i + 1 < argc) opt.min_gain = atof(argv[++i]);
        else if (arg == "--dry-run") opt.dry_run = true;
        else ids.insert(arg);
    }

    Bitboards::init();
    init_kkx_table();
    init_kkp_table();
    init_tril();
    init_egtb_id_to_ix();

    std::vector<fs::path> files;
    for (const auto & entry : fs::recursive_directory_iterator(folder)) {
        if (entry.path().extension() != COMP_EXT) continue;
        std::string filename = entry.path().filename().u8string();
        std::string egtb_id = filename.substr(0, filename.find_first_of("."));
        if (ids.empty() || ids.count(egtb_id)) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    uint64_t total_before = 0, total_after = 0, n_changed = 0, n_skipped = 0, n_kept = 0;
    TimePoint t0 = now();
    for (const fs::path& path : files) {
        TimePoint t_start = now();
        std::string filename = path.filename().u8string();
        std::string egtb_id = filename.substr(0, filename.find_first_of("."));
        EGTB egtb(path.parent_path().u8string(), egtb_id);
        egtb.init_compressed_tb();
        if (egtb.CTB->digit_order != 0) {
            std::cout << egtb_id << ": already finalized." << std::endl;
            n_skipped++;
            continue;
        }
        uint64_t block_size = egtb.CTB->block_size;
        uint64_t size_before = fs::file_size(path);
        egtb.load_egtb_from_compressed_file(nthreads);

        std::vector<std::vector<int>> orders = all_digit_orders(egtb.stm_pieces, egtb.sntm_pieces);
        std::vector<uint64_t> estimates = estimate_orders(egtb, orders, block_size, opt, nthreads);
        uint64_t best = std::min_element(estimates.begin(), estimates.end()) - estimates.begin();
        double gain = 1.0 - (double) estimates[best] / estimates[0];
        if (gain < opt.min_gain) best = 0; // canonical order
        const std::vector<int>& order = orders[best];

        std::cout << egtb_id << ": " << orders.size() << " orders, best " << digit_order_name(order)
                  << " (canonical " << digit_order_name(orders[0]) << "), estimated " << -100.0 * (1.0 - (double) estimates[best] / estimates[0]) << "%";

        if (opt.dry_run || best == 0) {
            // the canonical order is kept: the file stays as it is (no rewrite, it is not marked as finalized)
            std::cout << (best == 0 && !opt.dry_run ? ", file unchanged" : "") << std::endl;
            if (best == 0 && !opt.dry_run) { total_before += size_before; total_after += size_before; n_kept++; }
            egtb.free_tb();
            continue;
        }

        fs::path target = opt.out_dir.empty() ? path : fs::path(opt.out_dir) / fs::relative(path, folder);
        fs::create_directories(target.parent_path());
        std::string tmp = target.u8string() + ".finalize";
        uint64_t chunk_size = block_size * std::max((uint64_t) 1, (uint64_t) (1 << 20) / block_size);
        auto fill = [&egtb, &order](uint64_t start_ix, uint64_t n, int16_t* out) {
            DigitCursor cursor(egtb.stm_pieces, egtb.sntm_pieces, egtb.kntm_poscounts, egtb.num_nonep_pos, order);
            for (uint64_t i = 0; i < n; i++) out[i] = egtb.TB[cursor.to_canonical(start_ix + i)];
        };
        block_compress_stream(egtb.num_pos, chunk_size, fill, nthreads, opt.level, block_size, tmp, true, false, pack_digit_order(order));

        uint64_t n_wrong = verify_file(egtb, tmp, order, nthreads);
        if (n_wrong != 0) {
            std::cerr << std::endl << "Verification of " << tmp << " failed (" << n_wrong << " wrong values), " << path << " is unchanged." << std::endl;
            fs::remove(tmp);
            return 1;
        }
        egtb.free_tb();
        egtb.free_compressed_tb(); // unmap the original before replacing it
        fs::rename(tmp, target);

        uint64_t size_after = fs::file_size(target);
        total_before += size_before;
        total_after += size_after;
        n_changed++;
        std::cout << ", " << size_before << " -> " << size_after << " bytes (" << -100.0 * (1.0 - (double) size_after / size_before)
                  << "%) in " << (double) (now() - t_start) / 1000.0 << "s" << std::endl;
    }

    std::cout << "Finalized " << files.size() - n_skipped << " files (" << n_changed << " with a different digit order, "
              << n_kept << " unchanged, " << n_skipped << " already finalized): " << total_before << " -> " << total_after << " bytes";
    if (total_before > 0) std::cout << " (" << -100.0 * (1.0 - (double) total_after / total_before) << "%)";
    std::cout << " in " << (double) (now() - t0) / 1000.0 << "s." << std::endl;
    return 0;
}
