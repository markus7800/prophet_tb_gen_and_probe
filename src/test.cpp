#include <iostream>
#include <prophet.h>
#include <prophet_common.h>
#include <filesystem>
namespace fs = std::filesystem;

using namespace Prophet;

int main() {

    std::string folder = "egtbs";

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
 
    const int pieces[6] = {W_KING, W_QUEEN, B_KING, NO_PIECE, NO_PIECE, NO_PIECE};
    const int squares[6] = {SQ_E1, SQ_E2, SQ_E8, SQ_NONE, SQ_NONE, SQ_NONE};
    const int stm = BLACK;
    const int ep_square = SQ_NONE;
    int dtm = prophet_tb_probe_dtm(pieces, squares, stm, ep_square);
    std::cout << "dtm = " << dtm << std::endl;
}