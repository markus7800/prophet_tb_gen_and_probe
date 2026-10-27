#include <iostream>
#include <prophet.h>
#include <prophet_common.h>
#include <random>
#ifdef OMP
#include <omp.h>
#endif
#include <filesystem>
namespace fs = std::filesystem;

int dtm_to_wdl(int dtm) {
    if (dtm == -1001) return -1001;
    if (dtm == 1000) return WDL_DRAW;
    return dtm > 0 ? WDL_WIN : WDL_LOSS;
}

// compares prophet_tb_probe_wdl with sign of prophet_tb_probe_dtm for every used position of each table
// (or max_pos random positions for larger tables), requires .egtb.bz and .wdl.bz files in folder
bool check_wdl_against_dtm(std::string folder, uint64_t max_pos, [[maybe_unused]] int nthreads) {
    bool all_ok = true;
    for (const auto & entry : fs::recursive_directory_iterator(folder)) {
        if (entry.path().extension() != COMP_EXT || entry.path().stem().extension() != DTM_EXT) continue;
        std::string filename = entry.path().filename().u8string();
        std::string egtb_id = filename.substr(0, filename.find_first_of("."));
        EGTB egtb = EGTB(entry.path().parent_path().u8string(), egtb_id);

        bool exhaustive = egtb.num_pos <= max_pos;
        uint64_t n = exhaustive ? egtb.num_pos : max_pos;
        uint64_t n_checked = 0, n_mismatch = 0, n_wdl[3] = {0, 0, 0};

        #pragma omp parallel num_threads(nthreads) reduction(+:n_checked,n_mismatch)
        {
            DecompressCtx* dctx = prophet_tb_create_decompress_ctx();
            std::mt19937_64 rng(entry.path().u8string().size());
            uint64_t local_wdl[3] = {0, 0, 0};

            #pragma omp for schedule(dynamic, 4096)
            for (uint64_t i = 0; i < n; i++) {
                uint64_t ix = exhaustive ? i : rng() % egtb.num_pos;
                for (Color stm : {WHITE, BLACK}) {
                    EGPosition pos;
                    pos.reset();
                    egtb.pos_at_ix(pos, ix, stm);
                    if (!egtb.pos_ix_is_used(pos, ix)) continue;
                    int wdl = probe_position_wdl_dctx(pos, dctx);
                    int expected = dtm_to_wdl(probe_position_dtm_dctx(pos, dctx));
                    n_checked++;
                    if (wdl != expected) {
                        n_mismatch++;
                        #pragma omp critical
                        std::cout << egtb_id << " MISMATCH " << pos.fen() << ": wdl=" << wdl << ", dtm-wdl=" << expected << std::endl;
                    } else if (wdl != -1001) {
                        local_wdl[wdl + 1]++;
                    }
                }
            }
            #pragma omp critical
            for (int k = 0; k < 3; k++) n_wdl[k] += local_wdl[k];
            prophet_tb_free_decompress_ctx(dctx);
        }
        std::cout << egtb_id << ": checked " << n_checked << (exhaustive ? " (all)" : " (sampled)") << " positions, ";
        std::cout << n_wdl[2] << " wins, " << n_wdl[1] << " draws, " << n_wdl[0] << " losses, " << n_mismatch << " mismatches" << std::endl;
        all_ok = all_ok && n_mismatch == 0;
    }
    return all_ok;
}

int main(int argc, char *argv[]) {

    std::string folder = argc > 1 ? argv[1] : "egtbs";
    int nthreads = argc > 2 ? atoi(argv[2]) : 1;
    uint64_t max_pos = argc > 3 ? strtoull(argv[3], NULL, 10) : 1000000;
    #ifndef OMP
    assert(nthreads == 1);
    #endif

    prophet_tb_init();

    int filecount = prophet_tb_add_path(folder.c_str());

    std::cout << "filecount: " << filecount << std::endl;
    prophet_tb_load_all_files();
    size_t compressed_filesize = prophet_tb_get_size_on_disk_of_loaded_files();
    std::cout << "Init ProphetTB: Loaded " << filecount << " tables (" << (int) ceil((double) compressed_filesize / (1024*1024*1024)) << "GiB)" << std::endl;

    const int pieces[6] = {W_KING, W_QUEEN, B_KING, NO_PIECE, NO_PIECE, NO_PIECE};
    const int squares[6] = {SQ_E1, SQ_E2, SQ_E8, SQ_NONE, SQ_NONE, SQ_NONE};
    const int stm = BLACK;
    const int ep_square = SQ_NONE;
    int dtm = prophet_tb_probe_dtm(pieces, squares, stm, ep_square);
    std::cout << "dtm = " << dtm << std::endl;
    int wdl = prophet_tb_probe_wdl(pieces, squares, stm, ep_square);
    std::cout << "wdl = " << wdl << std::endl;

    bool ok = check_wdl_against_dtm(folder, max_pos, nthreads);
    std::cout << (ok ? "WDL consistent with DTM." : "WDL INCONSISTENT with DTM!") << std::endl;
    return ok ? 0 : 1;
}
