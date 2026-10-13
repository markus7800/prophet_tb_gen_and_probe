#include <iostream>
#include <set>
#include <map>
#include <functional>
#include <algorithm>
#include <cstdio>
#include <prophet.h>
#include <prophet_common.h>

/*
Computes the RAM that the generator needs per job (see ram_requirements.md), from the exact index sizes.

build/ram_requirements.out (optional: [n_pieces] ... (5, 6, 7 by default)) (optional: [egtb_id] ... to list its jobs)
e.g. build/ram_requirements.out 7 KRBPKPP

A job is one table pair (white to move, black to move); with the split of 1-2 pawn tables, one part of the
white-to-move table with the paired part of the black-to-move table. Memory per job, following GenEGTB:
- generation: both sides of the job's table (part) as int16, plus the dependencies that are loaded completely as int16:
  captures and capture-promotions, and for tables with <= 5 pieces also promotions (RETRO_PROMO_MIN_PIECES = 6)
- consistency check (>= 6 pieces, from compressed files): the same dependencies plus the largest unit that is loaded
  in one round (the smallest budget that works): without split a complete table side (other side of the table or a
  promotion table), with split one part (paired part of the other side or a compatible promotion part)
- 1-byte generation (branch optimisations-gen-bytes, with split): int16 for one side during init, then 1 byte per position
Process overhead (about 150 MiB) and the OS are not included.
*/

static const double MiB = 1024.0 * 1024;
static const double GiB = 1024.0 * 1024 * 1024;
static const int RETRO_PROMO_MIN_PIECES = 6;

struct Job {
    std::string id;
    int part;          // -1 if the table is not split
    double tables;     // both sides of the job (part)
    double deps;       // dependencies in memory
    double gen;        // tables + deps
    double check;      // smallest budget of the consistency check (0: in memory during generation)
    double bytes_gen;  // generation with 1 byte per position
    double peak() const { return std::max(gen, check); }
    std::string name() const { return part < 0 ? id : id + ".p" + std::to_string(part); }
};

struct Result {
    std::vector<Job> nosplit, split;
    uint64_t n_tables = 0;
    double largest_table_pair = 0;
    std::string largest_id;
};

Result analyse(int n_nonking) {
    Result res;
    std::set<std::string> seen;
    std::vector<int> pc(10, 0);
    std::map<std::string, EGTB*> cache;
    auto get = [&](const std::string& id) {
        auto it = cache.find(id);
        if (it != cache.end()) return it->second;
        EGTB* e = new EGTB("unused", id);
        cache[id] = e;
        return e;
    };

    std::function<void(int, int)> rec = [&](int i, int left) {
        if (i < 9) {
            for (int k = 0; k <= left; k++) { pc[i] = k; rec(i + 1, left - k); }
            return;
        }
        pc[9] = left;
        int w[6] = {0}, b[6] = {0};
        for (int k = 0; k < 5; k++) { w[k + 1] = pc[k]; b[k + 1] = pc[k + 5]; }
        std::string id = get_egtb_identifier(w, b), mid = get_egtb_identifier(b, w);
        if (!seen.insert(std::min(id, mid)).second) return;
        res.n_tables++;

        EGTB* W = get(id);
        EGTB* B = get(mid);
        bool all_loaded = W->npawns == 0 || W->npieces < RETRO_PROMO_MIN_PIECES;
        double pair_bytes = 2.0 * (W->num_pos + B->num_pos);
        if (pair_bytes > res.largest_table_pair) { res.largest_table_pair = pair_bytes; res.largest_id = id; }

        // dependencies, same enumeration as GenEGTB
        double deps = 0;
        auto add = [&](int* s, int* n) { deps += 2.0 * get(get_egtb_identifier(s, n))->num_pos; };
        for (int pt = PAWN; pt <= QUEEN; pt++) {
            if (w[pt]) { w[pt]--; add(w, b); w[pt]++; }
            if (b[pt]) { b[pt]--; add(b, w); b[pt]++; }
        }
        for (int pr = KNIGHT; pr <= QUEEN; pr++) {
            for (int cp = KNIGHT; cp <= QUEEN; cp++) {
                if (b[PAWN] && w[cp]) { w[cp]--; b[PAWN]--; b[pr]++; add(w, b); w[cp]++; b[PAWN]++; b[pr]--; }
                if (w[PAWN] && b[cp]) { b[cp]--; w[PAWN]--; w[pr]++; add(b, w); b[cp]++; w[PAWN]++; w[pr]--; }
            }
        }
        // promotion tables (the side that did not promote is to move)
        std::vector<EGTB*> promos_w, promos_b;
        for (int pr = KNIGHT; pr <= QUEEN; pr++) {
            if (w[PAWN]) { w[PAWN]--; w[pr]++; promos_w.push_back(get(get_egtb_identifier(b, w))); w[PAWN]++; w[pr]--; }
            if (b[PAWN]) { b[PAWN]--; b[pr]++; promos_b.push_back(get(get_egtb_identifier(w, b))); b[PAWN]++; b[pr]--; }
        }
        if (all_loaded) {
            for (EGTB* P : promos_w) deps += 2.0 * P->num_pos;
            for (EGTB* P : promos_b) deps += 2.0 * P->num_pos;
        }

        // smallest budget of the consistency check from compressed files for the job (W part p, B part q)
        auto check_budget = [&](int p, int q, bool split) -> double {
            if (all_loaded) return 0; // checked in memory during generation
            double unit_max = 0;
            for (int side = 0; side < 2; side++) {
                EGTB* L = side ? W : B;
                int lp = side ? p : q;
                EGTB* O = side ? B : W;
                int op = side ? q : p;
                std::vector<EGTB*>& promos = side ? promos_w : promos_b;
                unit_max = std::max(unit_max, 2.0 * (split ? O->part_size(op) : O->num_pos));
                for (EGTB* P : promos) {
                    if (!split) { unit_max = std::max(unit_max, 2.0 * P->num_pos); continue; }
                    for (int pp = 0; pp < P->n_parts(); pp++) {
                        if (promotion_compatible(L->index, lp, P->index, pp)) unit_max = std::max(unit_max, 2.0 * P->part_size(pp));
                    }
                }
            }
            return deps + unit_max;
        };

        // without split: one job for the whole table pair
        res.nosplit.push_back({id, -1, pair_bytes, deps, pair_bytes + deps, check_budget(0, 0, false), 0});

        // with split: one job per part pair (as GenEGTB::gen)
        bool symmetric = id == mid;
        for (int p = 0; p < W->n_parts(); p++) {
            int q = 0;
            if (W->split()) q = B->index.part_of_class_counts(W->index.parts[p].sntm_class_count, W->index.parts[p].stm_class_count);
            if (symmetric && q < p) continue;
            double nw = W->part_size(p), nb = B->part_size(q);
            double tables = 2.0 * (nw + nb);
            // the check loads promotion tables part by part, also if this table is not split
            Job j = {id, W->split() ? p : -1, tables, deps, tables + deps, check_budget(p, q, true), 0};
            j.bytes_gen = std::max(2.0 * nb, nb + 2.0 * nw) + deps;
            res.split.push_back(j);
        }
    };
    rec(0, n_nonking);
    for (auto& kv : cache) delete kv.second;
    return res;
}

int main(int argc, char* argv[]) {
    Bitboards::init();
    init_kkx_table();
    init_kkp_table();
    init_tril();

    std::vector<int> sizes;
    std::set<std::string> show;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find('K') == std::string::npos) sizes.push_back(atoi(argv[i]));
        else show.insert(arg);
    }
    if (sizes.empty()) sizes = {5, 6, 7};

    for (int n : sizes) {
        if (n < 3 || n > 7) { std::cerr << "piece count has to be in [3, 7]" << std::endl; return 1; }
        Result r = analyse(n - 2);
        double unit = n <= 5 ? MiB : GiB;
        const char* u = n <= 5 ? "MiB" : "GiB";
        printf("=== %d-men: %llu tables, largest table pair %s %.1f %s\n", n, (unsigned long long) r.n_tables, r.largest_id.c_str(), r.largest_table_pair / unit, u);

        for (int s = 0; s < 2; s++) {
            std::vector<Job>& jobs = s ? r.split : r.nosplit;
            auto worst = [&](auto key) {
                return *std::max_element(jobs.begin(), jobs.end(), [&](const Job& a, const Job& b) { return key(a) < key(b); });
            };
            Job g = worst([](const Job& j) { return j.gen; });
            Job c = worst([](const Job& j) { return j.check; });
            Job t = worst([](const Job& j) { return j.tables; });
            Job d = worst([](const Job& j) { return j.deps; });
            printf("%s: %zu jobs\n", s ? "with split" : "without split", jobs.size());
            printf("  worst generation: %s %.1f %s (tables %.1f, dependencies %.1f)\n", g.name().c_str(), g.gen / unit, u, g.tables / unit, g.deps / unit);
            printf("  worst check (smallest budget): %s %.1f %s\n", c.name().c_str(), c.check / unit, u);
            printf("  largest tables: %s %.1f %s, largest dependencies: %s %.1f %s\n", t.name().c_str(), t.tables / unit, u, d.name().c_str(), d.deps / unit, u);

            std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.peak() > b.peak(); });
            printf("  top 5 by peak:");
            for (int i = 0; i < 5 && i < (int) jobs.size(); i++) printf(" %s %.1f", jobs[i].name().c_str(), jobs[i].peak() / unit);
            printf(" %s\n", u);

            std::vector<double> thresholds = n <= 5 ? std::vector<double>{2, 4, 8, 16}
                                          : (n == 6 ? std::vector<double>{16, 32, 64, 90, 128}
                                                    : std::vector<double>{256, 512, 1024, 1300, 1397, 2048});
            printf("  jobs with peak above:");
            for (double x : thresholds) {
                int k = 0;
                for (const Job& j : jobs) k += j.peak() > x * GiB;
                printf(" %g GiB: %d,", x, k);
            }
            printf("\n");

            if (s) {
                Job bg = worst([](const Job& j) { return std::max(j.bytes_gen, j.check); });
                printf("  with 1-byte generation: worst %s %.1f %s (generation %.1f, check %.1f)\n", bg.name().c_str(),
                       std::max(bg.bytes_gen, bg.check) / unit, u, bg.bytes_gen / unit, bg.check / unit);
            }
            for (const Job& j : jobs) {
                if (show.count(j.id)) {
                    printf("  %s: generation %.1f %s (tables %.1f, dependencies %.1f), check %.1f %s\n", j.name().c_str(),
                           j.gen / unit, u, j.tables / unit, j.deps / unit, j.check / unit, u);
                }
            }
        }
    }
    return 0;
}
