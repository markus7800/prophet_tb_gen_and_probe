#include <iostream>
#include <vector>
#include <algorithm>
#include <set>
#include <unordered_map>
#include <prophet.h>
#include <prophet_common.h>
#include "capture_value.h"
#include "misc.h"
#ifdef OMP
#include <omp.h>
#endif
#include <filesystem>
namespace fs = std::filesystem;

/*
Finalizes generated tables for distribution. Per table file:
1. Digit order (see digit_order.h): all orders of the groups of identical pieces are estimated by compressing the
   same sample of blocks (of the file index) with the global value dictionary of the table, the best one is used.
2. Don't-cares: values of unused indices (broken positions) are replaced by the previous value in the file, so that
   they compress well. If enabled for the table, also the values of positions whose value is obtained by a capture
   (capture don't-cares, see capture_value.h) are replaced by the previous value if that is not better than the true
   value; probing then returns max(stored value, best capture). Capture don't-cares are enabled if they make the
   sample at least --dc-min-gain smaller (FLAG_CAPTURE_DC).
The file is rewritten if the result is estimated to be at least --min-gain smaller than the original, or if it has
an older format version (v1, which is converted), and it is verified against the original values before it replaces
the original file.

build/finalize.out [folder] [threads] (options) (optional: [egtb_id] ...)
  --out DIR           write the finalized files to DIR (same folder structure relative to [folder]) instead of
                      replacing them; files that are not rewritten are copied, so that DIR is a complete set of the
                      processed files (probing needs the capture tables)
  --root DIR          root of the table set ([root]/<n>men/<k>pawns) in which the capture tables are looked up
                      (default [folder]), so that [folder] can be a subfolder, e.g. egtbs/5men with --root egtbs
  --level L           zstd level of the finalized files and of the estimation (default 19)
  --sample-blocks N   number of blocks that are test-compressed per variant (default 256)
  --min-gain G        rewrite a file only if it is estimated to be smaller by G (default 0.01)
  --dc-min-gain G     use capture don't-cares only if they are estimated to save G (default 0.10)
  --no-dc             never use capture don't-cares
  --dry-run           only estimate, do not write; the summary adds up the estimated sizes (file size scaled by the
                      estimated gain of the files that would be rewritten)

Tables are processed in reverse generation order: the capture tables of a table (fewer pieces) are processed after
it, so they still have their original values when replacing files in place. Files that are already finalized are
skipped, files that would not get smaller stay unchanged (and are estimated again on the next run).
Every table needs the original values of its capture tables (one piece fewer) from [root], i.e. they must not have
capture don't-cares yet. The finalized set does not have to be closed: only the capture tables have to exist in
[root]. When replacing files in place over several runs, finalize the tables with more pieces first (e.g. 6-men
before 5-men); with --out and the originals in [root], any order works.
Finalized tables can be probed and (without capture don't-cares) loaded, but the generator needs the original files
of all dependencies with capture don't-cares, and of promotion tables with a changed digit order.
*/

struct Options {
    std::string out_dir;
    std::string root; // empty: [folder]
    int level = 19;
    uint64_t sample_blocks = 256;
    double min_gain = 0.01;
    double dc_min_gain = 0.10;
    bool use_dc = true;
    bool dry_run = false;
};

// bit per canonical index
struct Bits {
    std::vector<uint64_t> words;
    void resize(uint64_t n) { words.assign((n + 63) / 64, 0); }
    bool get(uint64_t ix) const { return (words[ix / 64] >> (ix % 64)) & 1; }
};

// value written to the file for a position with true value v (see top), prev is the previous written value
inline int16_t fill_value(int16_t v, bool used, bool capture_dc, bool use_dc, int16_t& prev) {
    int16_t s;
    if (!used) s = prev;
    else if (use_dc && capture_dc && prev <= v) s = prev;
    else s = v;
    prev = s;
    return s;
}

struct TableInfo {
    EGTB* egtb;
    Bits used;       // index is a position of the table
    Bits capture_dc; // value of the position is obtained by a capture
    uint64_t n_used = 0;
    uint64_t n_capture_dc = 0;
};

// key of the material of a position (stm counts, sntm counts)
inline uint32_t material_key(const EGPosition& pos) {
    uint32_t key = 0;
    Color stm = pos.side_to_move();
    for (PieceType pt = PAWN; pt <= QUEEN; ++pt) key = key * 8 + pos.count(stm, pt);
    for (PieceType pt = PAWN; pt <= QUEEN; ++pt) key = key * 8 + pos.count(~stm, pt);
    return key;
}
inline uint32_t material_key(const int stm_pieces[6], const int sntm_pieces[6]) {
    uint32_t key = 0;
    for (PieceType pt = PAWN; pt <= QUEEN; ++pt) key = key * 8 + stm_pieces[pt];
    for (PieceType pt = PAWN; pt <= QUEEN; ++pt) key = key * 8 + sntm_pieces[pt];
    return key;
}

// loads the tables reached by captures (and capture-promotions) from the table (original values needed)
bool load_capture_tables(EGTB& egtb, const std::string& root, int nthreads, std::unordered_map<uint32_t, EGTB*>& tables) {
    int s[6], n[6];
    for (int i = 0; i < 6; i++) { s[i] = egtb.stm_pieces[i]; n[i] = egtb.sntm_pieces[i]; }
    std::vector<std::pair<std::vector<int>, std::vector<int>>> children; // (stm, sntm) after the capture
    for (PieceType cap = PAWN; cap <= QUEEN; ++cap) {
        if (n[cap] == 0) continue;
        n[cap]--;
        children.push_back({std::vector<int>(n, n + 6), std::vector<int>(s, s + 6)});
        if (s[PAWN] > 0 && cap != PAWN) {
            for (PieceType promo = KNIGHT; promo <= QUEEN; ++promo) {
                s[PAWN]--; s[promo]++;
                children.push_back({std::vector<int>(n, n + 6), std::vector<int>(s, s + 6)});
                s[PAWN]++; s[promo]--;
            }
        }
        n[cap]++;
    }
    for (auto& child : children) {
        uint32_t key = material_key(child.first.data(), child.second.data());
        if (tables.count(key)) continue;
        EGTB* table = new EGTB(child.first.data(), child.second.data(), root);
        if (!table->exists()) {
            std::cerr << "Missing capture table " << table->id << " of " << egtb.id << " in " << table->folder << " (see --root)." << std::endl;
            delete table;
            return false;
        }
        table->init_compressed_tb();
        if (table->capture_dc) {
            std::cerr << "Capture table " << table->id << " of " << egtb.id << " already has capture don't-cares "
                      << "(finalize larger tables first, or use --out)." << std::endl;
            delete table;
            return false;
        }
        table->load_egtb_from_compressed_file(nthreads);
        tables[key] = table;
    }
    return true;
}

// marks used positions and positions whose value is obtained by a capture
void compute_bits(TableInfo& info, const std::unordered_map<uint32_t, EGTB*>& tables, int nthreads) {
    EGTB& egtb = *info.egtb;
    info.used.resize(egtb.num_pos);
    info.capture_dc.resize(egtb.num_pos);
    uint64_t n_used = 0, n_capture_dc = 0;
    const IndexLayout* layout = egtb.get_layout();
    auto child = [&tables](EGPosition& pos) -> int16_t {
        auto it = tables.find(material_key(pos));
        if (it == tables.end()) return 2 * WIN_IN(0); // not available
        EGTB* table = it->second;
        return table->TB[table->ix_from_pos(pos)];
    };
    uint64_t n_words = info.used.words.size();
    #pragma omp parallel num_threads(nthreads) reduction(+:n_used,n_capture_dc)
    {
        PosDecoder decoder(layout, WHITE);
        #pragma omp for schedule(dynamic, 64)
        for (uint64_t w = 0; w < n_words; w++) {
            uint64_t used_word = 0, dc_word = 0;
            for (uint64_t ix = w * 64; ix < std::min(egtb.num_pos, (w + 1) * 64); ix++) {
                EGPosition pos;
                decoder.pos_at_ix(pos, ix);
                if (!egtb.pos_ix_is_used(pos, ix)) continue;
                used_word |= 1ULL << (ix % 64);
                n_used++;
                int16_t capture_value;
                CaptureValueStatus status = best_capture_value(pos, child, capture_value);
                assert (status != CAPTURE_TABLE_MISSING);
                if (status == CAPTURE_VALUE && capture_value == egtb.TB[ix]) {
                    dc_word |= 1ULL << (ix % 64);
                    n_capture_dc++;
                }
            }
            info.used.words[w] = used_word;
            info.capture_dc.words[w] = dc_word;
        }
    }
    info.n_used = n_used;
    info.n_capture_dc = n_capture_dc;
}

// a way to write a table: digit order, and whether values are replaced (unused, and capture don't-cares if use_dc)
struct Variant {
    std::vector<int> order;
    bool fill;
    bool use_dc;
};

// estimated compressed size of the same sample of blocks for every variant
std::vector<uint64_t> estimate(const TableInfo& info, const std::vector<Variant>& variants, uint64_t block_size, const Options& opt, int nthreads) {
    const EGTB& egtb = *info.egtb;
    uint64_t n_blocks = (egtb.num_pos + block_size - 1) / block_size;
    std::vector<uint64_t> sample;
    uint64_t k = std::min(n_blocks, opt.sample_blocks);
    for (uint64_t i = 0; i < k; i++) sample.push_back(i * n_blocks / k);

    // dictionary of the original values (filled values are copies of other values, except for 0 at a block start)
    ValueDict dict;
    build_value_dict(egtb.TB, egtb.num_pos, nthreads, dict);
    std::vector<int16_t> code_of(65536, -1);
    for (uint64_t i = 0; i < dict.size; i++) code_of[(uint16_t) dict.values[i]] = i;

    std::vector<uint64_t> sizes(variants.size(), 0);
    uint64_t n_tasks = variants.size() * sample.size();
    #pragma omp parallel num_threads(nthreads)
    {
        std::vector<int16_t> values(block_size);
        std::vector<uint8_t> payload(3 * block_size);
        std::vector<uint8_t> compressed(ZSTD_compressBound(3 * block_size));
        ZSTD_CCtx* cctx = ZSTD_createCCtx();
        #pragma omp for schedule(dynamic, 1)
        for (uint64_t task = 0; task < n_tasks; task++) {
            const Variant& variant = variants[task / sample.size()];
            uint64_t start = sample[task % sample.size()] * block_size;
            uint64_t n = std::min(block_size, egtb.num_pos - start);
            DigitCursor cursor(egtb.stm_pieces, egtb.sntm_pieces, egtb.kntm_poscounts, egtb.num_nonep_pos, variant.order);
            int16_t prev = 0;
            for (uint64_t i = 0; i < n; i++) {
                uint64_t ix = cursor.to_canonical(start + i);
                values[i] = variant.fill ? fill_value(egtb.TB[ix], info.used.get(ix), info.capture_dc.get(ix), variant.use_dc, prev) : egtb.TB[ix];
            }
            uint64_t payload_size = encode_block(values.data(), n, code_of.data(), dict.size, payload.data());
            uint64_t size = ZSTD_compressCCtx(cctx, compressed.data(), compressed.size(), payload.data(), payload_size, opt.level);
            #pragma omp atomic
            sizes[task / sample.size()] += size;
        }
        ZSTD_freeCCtx(cctx);
    }
    return sizes;
}

// number of stored values of filename that are not valid for the true values in info (read with the variant's order)
uint64_t verify_file(const TableInfo& info, const std::string& filename, const Variant& variant, int nthreads) {
    const EGTB& egtb = *info.egtb;
    CompressedTB ctb(0, filename);
    std::vector<int> slots;
    if (!unpack_digit_order(ctb.digit_order, egtb.stm_pieces, egtb.sntm_pieces, slots) || slots != variant.order || ctb.num_pos != egtb.num_pos) return UINT64_MAX;
    if (((ctb.flags & FLAG_CAPTURE_DC) != 0) != variant.use_dc) return UINT64_MAX;
    if (!ctb.check_integrity(nthreads)) return UINT64_MAX;
    uint64_t n_wrong = 0;
    #pragma omp parallel num_threads(nthreads) reduction(+:n_wrong)
    {
        DecompressCtx dctx(ctb.block_size);
        DigitCursor cursor(egtb.stm_pieces, egtb.sntm_pieces, egtb.kntm_poscounts, egtb.num_nonep_pos, variant.order);
        #pragma omp for schedule(static)
        for (uint64_t block_ix = 0; block_ix < ctb.n_blocks; block_ix++) {
            if (!ctb.decompress_block(block_ix, &dctx, dctx.uncompressed_buf)) { n_wrong++; continue; }
            uint64_t start = block_ix * ctb.block_size;
            for (uint64_t i = 0; i < ctb.get_block_npos(block_ix); i++) {
                uint64_t ix = cursor.to_canonical(start + i);
                int16_t stored = dctx.uncompressed_buf[i];
                int16_t v = egtb.TB[ix];
                if (!info.used.get(ix)) continue; // never probed
                if (variant.use_dc && info.capture_dc.get(ix)) n_wrong += stored > v; // max(stored, best capture) == v
                else n_wrong += stored != v;
            }
        }
    }
    return n_wrong;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "usage: finalize.out [folder] [threads] (--out DIR) (--root DIR) (--level L) (--sample-blocks N) (--min-gain G) (--dc-min-gain G) (--no-dc) (--dry-run) (egtb_id ...)" << std::endl;
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
        else if (arg == "--root" && i + 1 < argc) opt.root = argv[++i];
        else if (arg == "--level" && i + 1 < argc) opt.level = atoi(argv[++i]);
        else if (arg == "--sample-blocks" && i + 1 < argc) opt.sample_blocks = strtoull(argv[++i], NULL, 10);
        else if (arg == "--min-gain" && i + 1 < argc) opt.min_gain = atof(argv[++i]);
        else if (arg == "--dc-min-gain" && i + 1 < argc) opt.dc_min_gain = atof(argv[++i]);
        else if (arg == "--no-dc") opt.use_dc = false;
        else if (arg == "--dry-run") opt.dry_run = true;
        else ids.insert(arg);
    }
    while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
    if (opt.root.empty()) opt.root = folder;
    while (opt.root.size() > 1 && opt.root.back() == '/') opt.root.pop_back(); // get_default_folder needs no trailing /
    if (!fs::is_directory(folder) || !fs::is_directory(opt.root)) {
        std::cerr << "Folder " << folder << " or root " << opt.root << " is not a directory." << std::endl;
        return 2;
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
    // reverse generation order: the capture tables of a table are processed after it
    std::unordered_map<std::string, int> generation_order;
    std::vector<std::string> all_ids = get_egtb_identifiers(0, 4);
    for (uint64_t i = 0; i < all_ids.size(); i++) generation_order[all_ids[i]] = i;
    auto order_of = [&](const fs::path& path) {
        std::string filename = path.filename().u8string();
        auto it = generation_order.find(filename.substr(0, filename.find_first_of(".")));
        return it == generation_order.end() ? -1 : it->second;
    };
    std::sort(files.begin(), files.end(), [&](const fs::path& a, const fs::path& b) { return order_of(a) > order_of(b); });

    // with --out, files that are not rewritten are copied
    auto copy_unchanged = [&](const fs::path& path) {
        if (opt.out_dir.empty() || opt.dry_run) return;
        fs::path target = fs::path(opt.out_dir) / fs::relative(path, folder);
        fs::create_directories(target.parent_path());
        fs::copy_file(path, target, fs::copy_options::overwrite_existing);
    };

    uint64_t total_before = 0, total_after = 0, n_changed = 0, n_skipped = 0, n_kept = 0, n_dc = 0;
    // --dry-run: sizes of the files that would be rewritten or kept, and how many
    uint64_t est_before = 0, est_after = 0, est_rewritten = 0, est_dc = 0, est_kept = 0;
    TimePoint t0 = now();
    for (const fs::path& path : files) {
        TimePoint t_start = now();
        std::string filename = path.filename().u8string();
        std::string egtb_id = filename.substr(0, filename.find_first_of("."));
        EGTB egtb(path.parent_path().u8string(), egtb_id);
        egtb.init_compressed_tb();
        if (egtb.CTB->digit_order != 0 || egtb.capture_dc) {
            std::cout << egtb_id << ": already finalized." << std::endl;
            n_skipped++;
            copy_unchanged(path);
            continue;
        }
        uint64_t block_size = egtb.CTB->block_size;
        uint64_t size_before = fs::file_size(path);
        egtb.load_egtb_from_compressed_file(nthreads);

        // 1. digit order on the original values
        TableInfo info;
        info.egtb = &egtb;
        std::vector<std::vector<int>> orders = all_digit_orders(egtb.stm_pieces, egtb.sntm_pieces);
        std::vector<Variant> order_variants;
        for (auto& order : orders) order_variants.push_back({order, false, false});
        std::vector<uint64_t> order_estimates = estimate(info, order_variants, block_size, opt, nthreads);
        uint64_t best = std::min_element(order_estimates.begin(), order_estimates.end()) - order_estimates.begin();
        if (1.0 - (double) order_estimates[best] / order_estimates[0] < opt.min_gain) best = 0;
        const std::vector<int>& order = orders[best];

        // 2. don't-cares in that order (capture tables are read from the root folder)
        std::unordered_map<uint32_t, EGTB*> capture_tables;
        if (!load_capture_tables(egtb, opt.root, nthreads, capture_tables)) {
            for (auto& kv : capture_tables) { kv.second->free_tb(); delete kv.second; }
            egtb.free_tb();
            return 1;
        }
        compute_bits(info, capture_tables, nthreads);
        for (auto& kv : capture_tables) { kv.second->free_tb(); delete kv.second; }
        std::vector<Variant> fill_variants = {{order, true, false}, {order, true, true}};
        std::vector<uint64_t> fill_estimates = estimate(info, fill_variants, block_size, opt, nthreads);
        bool use_dc = opt.use_dc && info.n_capture_dc > 0 && fill_estimates[1] <= (1.0 - opt.dc_min_gain) * fill_estimates[0];
        Variant variant = {order, true, use_dc};
        double gain = 1.0 - (double) fill_estimates[use_dc ? 1 : 0] / order_estimates[0];

        std::cout << egtb_id << ": order " << digit_order_name(order) << " (canonical " << digit_order_name(orders[0]) << ", "
                  << -100.0 * (1.0 - (double) order_estimates[best] / order_estimates[0]) << "%), "
                  << 100.0 * info.n_capture_dc / std::max(info.n_used, (uint64_t) 1) << "% capture don't-cares ("
                  << -100.0 * (1.0 - (double) fill_estimates[1] / fill_estimates[0]) << "%, " << (use_dc ? "used" : "not used")
                  << "), estimated " << -100.0 * gain << "%";

        // files of an older format version (v1) are always rewritten, so that the set is in the current version
        bool old_version = egtb.CTB->version != COMP_VERSION;
        if (old_version) std::cout << ", converted from v" << egtb.CTB->version;
        bool rewrite = gain >= opt.min_gain || old_version;
        if (opt.dry_run) {
            uint64_t size_estimate = rewrite ? (uint64_t) std::llround(size_before * std::max(0.0, 1.0 - gain)) : size_before;
            est_before += size_before;
            est_after += size_estimate;
            if (rewrite) { est_rewritten++; est_dc += use_dc; } else { est_kept++; }
            std::cout << (rewrite ? ", would be rewritten" : ", would stay unchanged") << std::endl;
            egtb.free_tb();
            continue;
        }
        if (!rewrite) {
            // the file stays as it is (not marked as finalized)
            std::cout << ", file unchanged" << std::endl;
            total_before += size_before; total_after += size_before; n_kept++;
            egtb.free_tb();
            copy_unchanged(path);
            continue;
        }

        fs::path target = opt.out_dir.empty() ? path : fs::path(opt.out_dir) / fs::relative(path, folder);
        fs::create_directories(target.parent_path());
        std::string tmp = target.u8string() + ".finalize";
        uint64_t chunk_size = block_size * std::max((uint64_t) 1, (uint64_t) (1 << 20) / block_size);
        auto fill = [&info, &variant, block_size](uint64_t start_ix, uint64_t n, int16_t* out) {
            const EGTB& e = *info.egtb;
            DigitCursor cursor(e.stm_pieces, e.sntm_pieces, e.kntm_poscounts, e.num_nonep_pos, variant.order);
            int16_t prev = 0;
            for (uint64_t i = 0; i < n; i++) {
                if ((start_ix + i) % block_size == 0) prev = 0; // the fill restarts in every block (as in the estimate)
                uint64_t ix = cursor.to_canonical(start_ix + i);
                out[i] = fill_value(e.TB[ix], info.used.get(ix), info.capture_dc.get(ix), variant.use_dc, prev);
            }
        };
        block_compress_stream(egtb.num_pos, chunk_size, fill, nthreads, opt.level, block_size, tmp, true, false,
                              pack_digit_order(order), use_dc ? FLAG_CAPTURE_DC : 0);

        uint64_t n_wrong = verify_file(info, tmp, variant, nthreads);
        if (n_wrong != 0) {
            std::cerr << std::endl << "Verification of " << tmp << " failed (" << n_wrong << " wrong values), " << path << " is unchanged." << std::endl;
            fs::remove(tmp);
            egtb.free_tb();
            return 1;
        }
        egtb.free_tb();
        egtb.free_compressed_tb(); // unmap the original before replacing it
        fs::rename(tmp, target);

        uint64_t size_after = fs::file_size(target);
        total_before += size_before;
        total_after += size_after;
        n_changed++;
        n_dc += use_dc;
        std::cout << ", " << size_before << " -> " << size_after << " bytes (" << -100.0 * (1.0 - (double) size_after / size_before)
                  << "%) in " << (double) (now() - t_start) / 1000.0 << "s" << std::endl;
    }

    if (opt.dry_run) {
        std::cout << "Dry run: " << files.size() - n_skipped << " files (" << est_rewritten << " would be rewritten, " << est_dc
                  << " of them with capture don't-cares, " << est_kept << " unchanged, " << n_skipped << " already finalized): "
                  << est_before << " -> estimated " << est_after << " bytes";
        if (est_before > 0) std::cout << " (" << -100.0 * (1.0 - (double) est_after / est_before) << "%)";
        std::cout << " in " << (double) (now() - t0) / 1000.0 << "s." << std::endl;
        return 0;
    }
    std::cout << "Finalized " << files.size() - n_skipped << " files (" << n_changed << " rewritten, " << n_dc << " of them with capture don't-cares, "
              << n_kept << " unchanged, " << n_skipped << " already finalized): " << total_before << " -> " << total_after << " bytes";
    if (total_before > 0) std::cout << " (" << -100.0 * (1.0 - (double) total_after / total_before) << "%)";
    std::cout << " in " << (double) (now() - t0) / 1000.0 << "s." << std::endl;
    return 0;
}
