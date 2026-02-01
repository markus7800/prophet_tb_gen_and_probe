#include <iostream>
#include <vector>
#include <fstream>
#include <prophet.h>
#include <prophet_common.h>
#include "misc.h"
#ifdef OMP
#include <omp.h>
#endif
#include <filesystem>
namespace fs = std::filesystem;

#define COMPUTE_WDL_STATS 1

struct CSVEntry {
    std::string egtb_id;
    uint64_t num_entries;
    uint64_t bytes;
#if COMPUTE_WDL_STATS
    uint64_t num_win_pos;
    uint64_t num_draw_pos;
    uint64_t num_loss_pos;
    uint64_t num_win_ix;
    uint64_t num_draw_ix;
    uint64_t num_loss_ix;
    uint64_t num_broken_ix;
#endif
    std::string fen;
    int dtm;
    std::string mate_line;
};

std::vector<Move> get_mate_line(EGPosition pos, DecompressCtx* dctx) {
    int16_t val = probe_position_raw_dctx(pos, dctx);
    std::vector<Move> pv;
    while (val != LOSS_IN(0)) {
        val = -val;
        if (val > 0) val++;
        if (val < 0) val--;
        bool found = false;
        for (Move move : EGMoveList(pos)) {
            UndoInfo u = pos.do_move(move);
            int16_t move_val = probe_position_raw_dctx(pos, dctx);
            pos.undo_move(u);
            if (move_val == val) {
                pv.push_back(move);
                pos.do_move(move);
                found = true;
                break;
            }
        }
        assert (found);
    }
    return pv;
}

int main(int argc, char *argv[]) {
    assert (argc == 5);
    std::string folder = argv[1];
    int max_npieces = atoi(argv[2]);
    int nthreads = atoi(argv[3]);
    bool compute_lines = atoi(argv[4]);
    #ifndef OMP
    assert(nthreads == 1);
    #endif
    assert (0 <= max_npieces && max_npieces <= 4);
    std::cout << "Probing " << folder << " with " << nthreads << " threads." << std::endl;
    if (compute_lines) {
        std::cout << "Compute all mate lines..." << std::endl;
    }

    prophet_tb_init();

    int filecount = 0;
    filecount += prophet_tb_add_path((fs::path(folder).append("2men")).c_str());
    filecount += prophet_tb_add_path((fs::path(folder).append("3men")).c_str());
    filecount += prophet_tb_add_path((fs::path(folder).append("4men")).c_str());
    filecount += prophet_tb_add_path((fs::path(folder).append("5men")).c_str());
    filecount += prophet_tb_add_path((fs::path(folder).append("6men_minimal")).c_str());
    filecount += prophet_tb_add_path((fs::path(folder).append("6men_remaining")).c_str());


    std::cout << "filecount: " << filecount << std::endl;
    prophet_tb_load_all_files();
    size_t compressed_filesize = prophet_tb_get_size_on_disk_of_loaded_files();
    std::cout << "Init ProphetTB: Loaded " << filecount << " tables (" << (int) ceil((double) compressed_filesize / (1024*1024*1024)) << "GiB)" << std::endl;
        

    std::vector<fs::path> all_egtb_paths;
    for (const auto & entry : fs::recursive_directory_iterator(folder)) {
        if (entry.path().extension() == COMP_EXT) {
            std::string filename = entry.path().filename().u8string();
            std::string egtb_id = filename.substr(0, filename.find_first_of("."));
            all_egtb_paths.push_back(entry.path());
        }
    }

    // sort
    std::vector<fs::path> egtb_paths;
    for (std::string egtb_id : get_egtb_identifiers(0, max_npieces)) {
        for (fs::path egtb_path : all_egtb_paths) {
            std::string filename = egtb_path.filename().u8string();
            if (egtb_id == filename.substr(0, filename.find_first_of("."))) {
                egtb_paths.push_back(egtb_path);
            }
        }
    }


    std::vector<CSVEntry> entries(egtb_paths.size());

    TimePoint t0 = now();

    uint64_t probe_count = 0;
    int count = 0;
    #pragma omp parallel for num_threads(nthreads) schedule(dynamic)
    for (uint i = 0; i < egtb_paths.size(); i++) {
        fs::path egtb_path = egtb_paths[i];
        std::string egtb_folder = egtb_path.parent_path().u8string();
        std::string egtb_filename = egtb_path.filename().u8string();;
        std::string egtb_id = egtb_filename.substr(0, egtb_filename.find_first_of("."));
        EGTB egtb = EGTB(egtb_folder, egtb_id);

        if (!egtb.exists()) {
            #pragma omp critical
            {
                std::cout << egtb_id << " does not exist." << std::endl;
            }
            continue;
        }
        egtb.init_compressed_tb();

        int16_t longest_mate = WIN_IN(0) + 1;
        uint64_t longest_mate_ix = 0;


        #if COMPUTE_WDL_STATS
        uint64_t num_win_pos = 0;
        uint64_t num_draw_pos = 0;
        uint64_t num_loss_pos = 0;
        uint64_t num_win_ix = 0;
        uint64_t num_draw_ix = 0;
        uint64_t num_loss_ix = 0;
        uint64_t num_broken_ix = 0;
        #endif

        for (uint64_t ix = 0; ix < egtb.num_pos; ix++) {
            int16_t val = egtb.get_value(ix);
            if (0 < val && val < longest_mate) {
                longest_mate = val;
                longest_mate_ix = ix;
            }
            #if COMPUTE_WDL_STATS
            EGPosition pos;
            // positions with pawns restrain king to the four leftmost columns
            uint64_t multiplier = 2;
            if (egtb.npawns == 0) {
                pos.reset();
                egtb.pos_at_ix(pos, ix, WHITE);
                // positions without pawns restrict the king to A1-D1-D4 triangle
                // but we need to account for diagonally symmetric positions
                multiplier = is_diag_symmetric(pos) ? 4 : 8;
            }
            num_win_ix += (val > 0);
            num_loss_ix += (val < 0);
            num_win_pos += multiplier * (val > 0);
            num_loss_pos += multiplier * (val < 0);
            if (val == 0) {
                // unused indexes (e.g. illegal positions) are mapped to 0 for compression
                if (egtb.npawns > 0) {
                    pos.reset();
                    egtb.pos_at_ix(pos, ix, WHITE);
                }
                bool used = egtb.pos_ix_is_used(pos, ix);
                num_draw_ix += used;
                num_draw_pos += multiplier * used;
                num_broken_ix += !used;
            }
            #endif
        }

        if (longest_mate == WIN_IN(0) + 1) {
            #if COMPUTE_WDL_STATS
                entries[i] = {egtb_id, egtb.num_pos, egtb.CTB->compressed_filesize,
                    num_win_pos, num_draw_pos, num_loss_pos,
                    num_win_ix, num_draw_ix, num_loss_ix, num_broken_ix,
                    "", -1, ""};
            #else
                entries[i] = {egtb_id, egtb.num_pos, egtb.CTB->compressed_filesize, "", -1, ""};
            #endif
        } else {
            EGPosition pos;
            pos.reset();
            egtb.pos_at_ix(pos, longest_mate_ix, WHITE);
            int mate_in_dtm = WIN_IN(0) - egtb.get_value(longest_mate_ix);
            
            std::string mate_line = "";
            if (compute_lines) {
                std::ostringstream mate_line_os;
                DecompressCtx* dctx = new DecompressCtx();
                for (Move move : get_mate_line(pos, dctx)) {
                    mate_line_os << move_to_uci(move) << " " ;
                }
                mate_line = mate_line_os.str();

                #pragma omp atomic
                probe_count += dctx->probe_count;
            }

            #if COMPUTE_WDL_STATS
                entries[i] = {egtb_id, egtb.num_pos, egtb.CTB->compressed_filesize,
                    num_win_pos, num_draw_pos, num_loss_pos,
                    num_win_ix, num_draw_ix, num_loss_ix, num_broken_ix,
                    pos.fen(), mate_in_dtm, mate_line};
            #else
                entries[i] = {egtb_id, egtb.num_pos, egtb.CTB->compressed_filesize, pos.fen(), mate_in_dtm, mate_line};
            #endif
        }

        #pragma omp critical
        {
            count++;
            std::cout << "Finished " << count << "/" << egtb_paths.size() << ": " << entries[i].egtb_id << " " << entries[i].fen << " " << entries[i].dtm << std::endl;
        }

    }
    
    TimePoint t1 = now();
    std::cout << "Finished in " << (double) (t1-t0) / 1000 << "s. Probe count = " << probe_count << std::endl;


    std::ofstream file("longest_mates.csv");
    #if COMPUTE_WDL_STATS
    file << "id,num_entries,bytes,num_win_pos,num_draw_pos,num_loss_pos,num_win_ix,num_draw_ix,num_loss_ix,num_broken_ix,fen,dtm,line\n";
    #else
    file << "id,num_entries,bytes,fen,dtm,line\n";
    #endif

    for (uint i = 0; i < egtb_paths.size(); i++) {
        CSVEntry entry = entries[i];
            file << entry.egtb_id << "," << entry.num_entries << "," << entry.bytes;
            #if COMPUTE_WDL_STATS
            file << "," << entry.num_win_pos << "," << entry.num_draw_pos << "," << entry.num_loss_pos;
            file << "," << entry.num_win_ix << "," << entry.num_draw_ix << "," << entry.num_loss_ix << "," << entry.num_broken_ix;
            #endif
        if (entry.dtm == -1) {
            file << ",,,\n";
        } else {
            file << "," << entry.fen << "," << entry.dtm << "," << entry.mate_line << "\n";
        }
    }

    file.close();

    prophet_tb_deinit();
}