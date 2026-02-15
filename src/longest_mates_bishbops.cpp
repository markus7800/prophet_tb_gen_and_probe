#include <iostream>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
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
    std::string bishop_config;
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

// 2 bishops
std::vector<std::string> B2 = {
"B20v00",
"B11v00",

"B10v10",
"B10v01",

"B00v20",
"B00v11",
};

// 3 bishops
std::vector<std::string> B3 = {
"B30v00",
"B21v00",

"B20v10",
"B20v01",
"B11v10",

"B10v20",
"B10v02",
"B10v11",

"B00v30",
"B00v21",
};

// 4 bishops
std::vector<std::string> B4 = {
"B40v00",
"B31v00",
"B22v00",

"B30v10",
"B30v01",
"B21v10",
"B21v01",

"B20v20",
"B20v11",
"B20v02",
"B11v20",
"B11v11",

"B10v30",
"B10v21",
"B10v12",
"B10v03",

"B00v40",
"B00v31",
"B00v22",
};


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

    Bitboard WHITE_SQs = 0;
    Bitboard BLACK_SQs = 0;
    for (Square sq = SQ_A1; sq <= SQ_H8; ++sq) {
        if (sq_to_color(sq) == WHITE) {
            WHITE_SQs |= square_bb(sq);
        } else {
            BLACK_SQs |= square_bb(sq);
        }
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


    std::vector<CSVEntry> entries;

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

        int bishop_count = std::count(egtb_id.begin(), egtb_id.end(), 'B');
        if (bishop_count < 2) continue;
        std::vector<std::string> B;
        if (bishop_count == 2) {
            B = B2;
        } else if (bishop_count == 3) {
            B = B3;
        } else {
            B = B4;
        }
        std::unordered_map<std::string,int> b_to_ix;
        for (uint j = 0; j < B.size(); j++) {
            b_to_ix[B[j]] = j;
        }
        size_t firstK = egtb_id.find('K');
        size_t secondK = egtb_id.find('K', firstK + 1);
        std::string stuff1 = egtb_id.substr(firstK + 1, secondK - firstK - 1);
        std::string stuff2 = egtb_id.substr(secondK + 1);
        int stm_bishop_count = std::count(stuff1.begin(), stuff1.end(), 'B');
        int sntm_bishop_count = std::count(stuff2.begin(), stuff2.end(), 'B');

        if (!egtb.exists()) {
            #pragma omp critical
            {
                std::cout << egtb_id << " does not exist." << std::endl;
            }
            continue;
        }
        egtb.init_compressed_tb();


        int16_t longest_mate[19] = {int16_t()};
        uint64_t longest_mate_ix[19] = {0};
        for (uint j = 0; j < B.size(); j++) {
            std::string config = B[j];
            int b_stm_bishop_count = (int) (config[1] - '0') + (int) (config[2] - '0');
            int b_sntm_bishop_count = (int) (config[4] - '0') + (int) (config[5] - '0');
            if (b_stm_bishop_count == stm_bishop_count && b_sntm_bishop_count == sntm_bishop_count) {
                longest_mate[j] = WIN_IN(0) + 1;
            } else {
                longest_mate[j] = -1;
            }
            longest_mate_ix[j] = 0;
        }

        for (uint64_t ix = 0; ix < egtb.num_pos; ix++) {
            int16_t val = egtb.get_value(ix);

            bool lb = false;
            for (uint j = 0; j < B.size(); j++) lb = lb || (val < longest_mate[j]);

            if (0 < val && lb) {
                EGPosition pos;
                pos.reset();
                egtb.pos_at_ix(pos, ix, WHITE);
                
                int STM_1 = popcount(pos.pieces(WHITE, BISHOP) & WHITE_SQs);
                int STM_2 = popcount(pos.pieces(WHITE, BISHOP) & BLACK_SQs);

                int SNTM_1 = popcount(pos.pieces(BLACK, BISHOP) & WHITE_SQs);
                int SNTM_2 = popcount(pos.pieces(BLACK, BISHOP) & BLACK_SQs);

                if (STM_1 < STM_2 || (STM_1 == STM_2 && SNTM_1 < SNTM_2)) {
                    int tmp = STM_2;
                    STM_2 = STM_1;
                    STM_1 = tmp;

                    tmp = SNTM_2;
                    SNTM_2 = SNTM_1;
                    SNTM_1 = tmp;
                }
                std::stringstream ss;
                ss << "B";
                ss << STM_1;
                ss << STM_2;
                ss << "v";
                ss << SNTM_1;
                ss << SNTM_2;
                int Bix = b_to_ix[ss.str()];

                if (val < longest_mate[Bix]) {
                    longest_mate[Bix] = val;
                    longest_mate_ix[Bix] = ix;
                }
            }
        }

        #pragma omp critical
        {
        for (uint j = 0; j < B.size(); j++) {
            std::string config = B[j];

            if (longest_mate[j] == -1) continue;
        
            if (longest_mate[j] == WIN_IN(0) + 1) {
                entries.push_back({egtb_id, config, egtb.num_pos, egtb.CTB->compressed_filesize, "", -1, ""});
            } else {
                EGPosition pos;
                pos.reset();
                egtb.pos_at_ix(pos, longest_mate_ix[j], WHITE);


                int mate_in_dtm = WIN_IN(0) - egtb.get_value(longest_mate_ix[j]);
                
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

                entries.push_back({egtb_id, config, egtb.num_pos, egtb.CTB->compressed_filesize, pos.fen(), mate_in_dtm, mate_line});
            }
        }

            count++;
            std::cout << "Finished " << count << "/" << egtb_paths.size() << ": " << egtb_id << std::endl;
        }

    }
    
    TimePoint t1 = now();
    std::cout << "Finished in " << (double) (t1-t0) / 1000 << "s. Probe count = " << probe_count << std::endl;


    std::ofstream file("longest_mates.csv");
    file << "id,bishop_config,fen,dtm";
    file << (compute_lines ? ",line\n" : "\n");

    for (CSVEntry entry : entries) {
        if (entry.dtm == -1) {
            file << entry.egtb_id << "," << entry.bishop_config << ",,,\n";
        } else if (entry.dtm != -2) {
            file << entry.egtb_id << "," << entry.bishop_config << "," << entry.fen << "," << entry.dtm << "," << entry.mate_line << "\n";
        }
    }

    file.close();

    prophet_tb_deinit();
}