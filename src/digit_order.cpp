#include "digit_order.h"
#include <algorithm>

static const char SLOT_CHAR[N_DIGIT_SLOTS] = {'P', 'p', 'Q', 'R', 'B', 'N', 'q', 'r', 'b', 'n'};
static const PieceType SLOT_PT[N_DIGIT_SLOTS] = {PAWN, PAWN, QUEEN, ROOK, BISHOP, KNIGHT, QUEEN, ROOK, BISHOP, KNIGHT};
static const bool SLOT_STM[N_DIGIT_SLOTS] = {true, false, true, true, true, true, false, false, false, false};

std::vector<int> nonempty_slots(const int stm_pieces[6], const int sntm_pieces[6]) {
    std::vector<int> slots;
    for (int s = 0; s < N_DIGIT_SLOTS; s++) {
        if ((SLOT_STM[s] ? stm_pieces : sntm_pieces)[SLOT_PT[s]] > 0) slots.push_back(s);
    }
    return slots;
}

uint64_t pack_digit_order(const std::vector<int>& slots) {
    uint64_t packed = slots.size();
    for (uint64_t i = 0; i < slots.size(); i++) packed |= (uint64_t) slots[i] << (4 + 4 * i);
    return packed;
}

bool unpack_digit_order(uint64_t packed, const int stm_pieces[6], const int sntm_pieces[6], std::vector<int>& slots) {
    std::vector<int> expected = nonempty_slots(stm_pieces, sntm_pieces);
    uint64_t n = packed & 15;
    if (n != expected.size()) return false;
    slots.clear();
    for (uint64_t i = 0; i < n; i++) slots.push_back((packed >> (4 + 4 * i)) & 15);
    std::vector<int> sorted = slots;
    std::sort(sorted.begin(), sorted.end());
    return sorted == expected && (packed >> (4 + 4 * n)) == 0;
}

std::string digit_order_name(const std::vector<int>& slots) {
    std::string name;
    for (int s : slots) name += SLOT_CHAR[s];
    return name;
}

std::vector<std::vector<int>> all_digit_orders(const int stm_pieces[6], const int sntm_pieces[6]) {
    std::vector<int> slots = nonempty_slots(stm_pieces, sntm_pieces); // sorted, i.e. canonical order first
    std::vector<std::vector<int>> orders;
    do { orders.push_back(slots); } while (std::next_permutation(slots.begin(), slots.end()));
    return orders;
}

void slice_radices(const int stm_pieces[6], const int sntm_pieces[6], int kix, uint64_t radix[N_DIGIT_SLOTS]) {
    bool no_pawns = stm_pieces[PAWN] + sntm_pieces[PAWN] == 0;
    Square kntm_sq = no_pawns ? Square(KKX_KNTM_SQ[kix]) : Square(KKP_KNTM_SQ[kix]);
    Square ktm_sq  = no_pawns ? Square(KKX_KTM_SQ[kix]) : Square(KKP_KTM_SQ[kix]);
    uint64_t n_squares_available_to_sntm = 62;

    uint64_t n_domain = 48 - num_unblockablechecks(kntm_sq, PAWN) - bool(square_bb(kntm_sq) & PawnSquaresBB) - bool(square_bb(ktm_sq) & PawnSquaresBB);
    radix[0] = number_of_ordered_tuples(n_domain, stm_pieces[PAWN]);
    n_squares_available_to_sntm -= stm_pieces[PAWN];

    n_domain = 48 - stm_pieces[PAWN] - bool(square_bb(kntm_sq) & PawnSquaresBB) - bool(square_bb(ktm_sq) & PawnSquaresBB);
    radix[1] = number_of_ordered_tuples(n_domain, sntm_pieces[PAWN]);
    n_squares_available_to_sntm -= sntm_pieces[PAWN];

    for (int s = 2; s < 6; s++) {
        PieceType pt = SLOT_PT[s];
        n_domain = 64 - num_unblockablechecks(kntm_sq, pt) - 2 + bool(square_bb(ktm_sq) & unblockablechecks_bb(kntm_sq, pt));
        radix[s] = number_of_ordered_tuples(n_domain, stm_pieces[pt]);
        n_squares_available_to_sntm -= stm_pieces[pt];
    }
    for (int s = 6; s < 10; s++) {
        PieceType pt = SLOT_PT[s];
        radix[s] = number_of_ordered_tuples(n_squares_available_to_sntm, sntm_pieces[pt]);
        n_squares_available_to_sntm -= sntm_pieces[pt];
    }
}

DigitCursor::DigitCursor(const int stm_pieces_[6], const int sntm_pieces_[6], const uint64_t* kntm_poscounts_, uint64_t num_nonep_pos_, const std::vector<int>& slots) {
    for (int i = 0; i < 6; i++) {
        stm_pieces[i] = stm_pieces_[i];
        sntm_pieces[i] = sntm_pieces_[i];
    }
    kntm_poscounts = kntm_poscounts_;
    n_kk = stm_pieces[PAWN] + sntm_pieces[PAWN] == 0 ? N_KKX : N_KKP;
    num_nonep_pos = num_nonep_pos_;
    n_order = slots.size();
    identity = true;
    for (int i = 0; i < n_order; i++) {
        order[i] = slots[i];
        identity = identity && (i == 0 || slots[i - 1] < slots[i]);
    }
    kix = -1;
}

void DigitCursor::load_slice(uint64_t ix) {
    if (kix >= 0 && kntm_poscounts[kix] <= ix && ix < kntm_poscounts[kix + 1]) return;
    kix = std::upper_bound(kntm_poscounts, kntm_poscounts + n_kk + 1, ix) - kntm_poscounts - 1;
    slice_radices(stm_pieces, sntm_pieces, kix, radix);
}

uint64_t DigitCursor::to_file(uint64_t ix) {
    if (identity || ix >= num_nonep_pos) return ix;
    load_slice(ix);
    uint64_t local = ix - kntm_poscounts[kix];
    // canonical digits (empty slots have radix 1 and digit 0)
    uint64_t digit[N_DIGIT_SLOTS];
    for (int s = 0; s < N_DIGIT_SLOTS; s++) {
        digit[s] = local % radix[s];
        local /= radix[s];
    }
    uint64_t file_local = 0, multiplier = 1;
    for (int i = 0; i < n_order; i++) {
        file_local += digit[order[i]] * multiplier;
        multiplier *= radix[order[i]];
    }
    return kntm_poscounts[kix] + file_local;
}

uint64_t DigitCursor::to_canonical(uint64_t file_ix) {
    if (identity || file_ix >= num_nonep_pos) return file_ix;
    load_slice(file_ix); // the permutation stays within the slice
    uint64_t local = file_ix - kntm_poscounts[kix];
    uint64_t digit[N_DIGIT_SLOTS] = {0};
    for (int i = 0; i < n_order; i++) {
        digit[order[i]] = local % radix[order[i]];
        local /= radix[order[i]];
    }
    uint64_t canonical_local = 0, multiplier = 1;
    for (int s = 0; s < N_DIGIT_SLOTS; s++) {
        canonical_local += digit[s] * multiplier;
        multiplier *= radix[s];
    }
    return kntm_poscounts[kix] + canonical_local;
}
