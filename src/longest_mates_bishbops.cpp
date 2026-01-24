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

struct CSVEntry {
    std::string egtb_id;
    uint64_t num_pos;
    uint64_t bytes;
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
        if (egtb.stm_pieces[BISHOP] != 2 && egtb.sntm_pieces[BISHOP] != 2) {
            continue;
        }

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

        for (uint64_t ix = 0; ix < egtb.num_pos; ix++) {
            int16_t val = egtb.get_value(ix);

            if (0 < val && val < longest_mate) {
                EGPosition pos;
                pos.reset();
                egtb.pos_at_ix(pos, ix, WHITE);
                
                bool skip = false;
                for (Color c : {WHITE, BLACK}) {
                    Bitboard b = pos.pieces(c, BISHOP);
                    if (popcount(b) == 2) {
                        Square b1 = pop_lsb(b);
                        Square b2 = pop_lsb(b);
                        skip = ((b1 % 2) == (b2 % 2)); // same colored bishop
                    }
                }

                if (!skip) {
                    longest_mate = val;
                    longest_mate_ix = ix;
                }
            }
        }

        if (longest_mate == WIN_IN(0) + 1) {
            entries[i] = {egtb_id, egtb.num_pos, egtb.CTB->compressed_filesize, "", -1, ""};
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

            entries[i] = {egtb_id, egtb.num_pos, egtb.CTB->compressed_filesize, pos.fen(), mate_in_dtm, mate_line};
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
    file << "id,fen,dtm,line\n";

    for (uint i = 0; i < egtb_paths.size(); i++) {
        CSVEntry entry = entries[i];
        if (entry.dtm == -1) {
            file << entry.egtb_id << ",,,\n";
        } else {
            file << entry.egtb_id << "," << entry.fen << "," << entry.dtm << "," << entry.mate_line << "\n";
        }
    }

    file.close();

    prophet_tb_deinit();
}