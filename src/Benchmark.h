#pragma once

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <omp.h>

// Include ไฟล์นี้หลังฟังก์ชัน setPolicy() ใน main.cpp

// ============================================================
// บันทึกเกมอ้างอิงด้วย Serial 1 เธรด
// ============================================================
inline int recordReference(const std::string &path, int count) {
    if (count <= 0) {
        throw std::runtime_error("Count must be positive");
    }

    std::ifstream existing(path);
    if (existing.good()) {
        throw std::runtime_error(
            "Reference already exists; choose a new filename"
        );
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Cannot write reference");
    }

    out << "MCTS_REFERENCE_V1\n";

    EnvParameter env = {
        count, -1, 1,
        "Serial", "Serial",
        "openmp", "openmp"
    };

    board currentBoard;
    int moves = 0;

    while (true) {
        const PIECE player = currentBoard.take_turn();

        auto legalMoves = currentBoard.get_available_move(player);

        if (legalMoves.empty()) {
            out << "END terminal\n";
            break;
        }

        Log log;

        Pair move = Policy::MCTS_Serial(
            currentBoard,
            player,
            env,
            log
        );

        if (std::find(
                legalMoves.begin(),
                legalMoves.end(),
                move
            ) == legalMoves.end()) {
            throw std::runtime_error(
                "Reference policy returned an invalid move"
            );
        }

        out << static_cast<int>(move.prev) << ' '
            << static_cast<int>(move.next) << '\n';

        currentBoard.move(move.prev, move.next, player);
        moves++;

        std::cout << "Recorded move " << moves
                  << " Player="
                  << (player == BLACK ? "Black" : "White")
                  << '\n';

        // ใช้ขีดจำกัดฝั่ง Black เช่นเดียวกับโปรแกรมเดิม
        if ((moves + 1) / 2 > LIMIT_STEP) {
            out << "END limit\n";

            std::cout
                << "Reference stopped at LIMIT_STEP "
                << "(not a terminal game).\n";

            break;
        }
    }

    out.close();

    if (!out) {
        throw std::runtime_error("Failed to save reference");
    }

    std::cout << "Saved " << moves
              << " positions to " << path << '\n';

    return 0;
}

// ============================================================
// โหลดลำดับเดิน แล้วสร้างกระดานก่อนแต่ละตา
// ============================================================
inline std::vector<board> loadReference(const std::string &path) {
    std::ifstream in(path);

    if (!in) {
        throw std::runtime_error(
            "Cannot open reference file: " + path
        );
    }

    std::string line;

    if (!std::getline(in, line) ||
        line != "MCTS_REFERENCE_V1") {
        throw std::runtime_error("Invalid reference header");
    }

    std::vector<board> positions;
    board currentBoard;
    bool ended = false;

    while (std::getline(in, line)) {
        if (line == "END terminal" || line == "END limit") {
            if (line == "END terminal" &&
                !currentBoard.get_available_move(
                    currentBoard.take_turn()
                ).empty()) {
                throw std::runtime_error(
                    "Reference is not terminal"
                );
            }

            std::cout << "Reference stop: "
                      << line.substr(4) << '\n';

            ended = true;
            break;
        }

        std::istringstream row(line);

        int from;
        int to;
        std::string extra;

        if (!(row >> from >> to) ||
            (row >> extra) ||
            from < 0 || from >= board::SIZE ||
            to < 0 || to >= board::SIZE) {
            throw std::runtime_error("Invalid reference move");
        }

        Pair move(
            static_cast<char>(from),
            static_cast<char>(to)
        );

        const PIECE player = currentBoard.take_turn();

        auto legalMoves =
            currentBoard.get_available_move(player);

        if (std::find(
                legalMoves.begin(),
                legalMoves.end(),
                move
            ) == legalMoves.end()) {
            throw std::runtime_error("Illegal reference move");
        }

        // เก็บกระดานก่อนเดิน รวมสถานะผู้เล่นที่ต้องเดิน
        positions.push_back(currentBoard);

        currentBoard.move(move.prev, move.next, player);
    }

    if (!ended || positions.empty()) {
        throw std::runtime_error(
            "Incomplete or empty reference"
        );
    }

    while (std::getline(in, line)) {
        if (!line.empty()) {
            throw std::runtime_error(
                "Trailing reference data"
            );
        }
    }

    return positions;
}

// ============================================================
// Benchmark
//
// Tree / Root / Leaf: 1, 2, 3, 4 เธรด
// Serial: 1 เธรด
// Workload: baseCount, baseCount*2, baseCount*4
// ============================================================
inline int benchmarkReference(
    const std::string &path,
    const std::string &policyName,
    int baseCount,
    int rounds,
    const std::string &method,
    const std::string &prefix
) {
    if (baseCount <= 0 ||
        baseCount > std::numeric_limits<int>::max() / 4 ||
        rounds <= 0) {
        throw std::runtime_error("Invalid count or rounds");
    }

    if (method != "openmp" && method != "pthread") {
        throw std::runtime_error(
            "Method must be openmp or pthread"
        );
    }

    if (policyName != "Root" &&
        policyName != "Tree" &&
        policyName != "Leaf" &&
        policyName != "Serial") {
        throw std::runtime_error(
            "Policy must be Root, Tree, Leaf or Serial"
        );
    }

    auto policy = setPolicy(policyName);

    const std::vector<board> positions =
        loadReference(path);

    std::size_t blackPositions = 0;
    std::size_t whitePositions = 0;

    for (const auto &position : positions) {
        if (position.take_turn() == BLACK) {
            blackPositions++;
        } else {
            whitePositions++;
        }
    }

    const int threadCounts[] = {1, 2, 3, 4};

    const int settingCount =
        (policyName == "Serial") ? 1 : 4;

    std::ofstream details(prefix + "_moves.csv");
    std::ofstream runs(prefix + "_rounds.csv");
    std::ofstream summary(prefix + "_summary.csv");

    if (!details || !runs || !summary) {
        throw std::runtime_error(
            "Cannot open result CSV files"
        );
    }

    details << std::setprecision(12);
    runs << std::setprecision(12);
    summary << std::setprecision(12);

    details
        << "count,threads,round,position,player,"
        << "searches,seconds\n";

    runs
        << "count,threads,round,positions,searches,"
        << "black_seconds,white_seconds,total_seconds\n";

    summary
        << "policy,method,count,threads,rounds,positions,"
        << "searches_per_round,"
        << "mean_black_seconds,mean_white_seconds,"
        << "mean_total_seconds,mean_seconds_per_position,"
        << "speedup,"
        << "black_positions,white_positions,"
        << "mean_seconds_per_black_position,"
        << "mean_seconds_per_white_position\n";

    omp_set_dynamic(0);

    std::cout << "\n===== Benchmark Settings =====\n";
    std::cout << "Policy: " << policyName << '\n';
    std::cout << "Method: " << method << '\n';
    std::cout << "Reference: " << path << '\n';
    std::cout << "Positions: " << positions.size() << '\n';
    std::cout << "Black positions: " << blackPositions << '\n';
    std::cout << "White positions: " << whitePositions << '\n';
    std::cout << "Rounds per condition: " << rounds << '\n';
    std::cout << "Workloads: "
              << baseCount << ", "
              << baseCount * 2 << ", "
              << baseCount * 4 << '\n';

    std::cout << "Threads:";
    for (int i = 0; i < settingCount; i++) {
        std::cout << ' ' << threadCounts[i];
    }
    std::cout << "\n\n";

    for (int multiplier : {1, 2, 4}) {
        const int count = baseCount * multiplier;

        double totalSums[4] = {};
        double blackSums[4] = {};
        double whiteSums[4] = {};

        for (int round = 0; round < rounds; round++) {
            // สลับลำดับการทดลองเธรดในแต่ละรอบ
            for (int offset = 0;
                 offset < settingCount;
                 offset++) {

                const int index =
                    (round + offset) % settingCount;

                const int threads = threadCounts[index];

                omp_set_num_threads(threads);

                EnvParameter env = {
                    count, -1, threads,
                    policyName, policyName,
                    method, method
                };

                double blackSeconds = 0.0;
                double whiteSeconds = 0.0;
                long long searches = 0;

                for (std::size_t i = 0;
                     i < positions.size();
                     i++) {

                    // เตรียมข้อมูลก่อนเริ่มจับเวลา
                    board testBoard = positions[i];
                    const PIECE player = testBoard.take_turn();
                    Log log;

                    // จับเวลาเฉพาะการเรียกค้นหา
                    const double start = omp_get_wtime();

                    Pair move = policy(
                        testBoard,
                        player,
                        env,
                        log
                    );

                    const double seconds =
                        omp_get_wtime() - start;

                    // ตรวจผลหลังหยุดจับเวลา
                    auto legalMoves =
                        testBoard.get_available_move(player);

                    if (std::find(
                            legalMoves.begin(),
                            legalMoves.end(),
                            move
                        ) == legalMoves.end()) {
                        throw std::runtime_error(
                            "Benchmark returned invalid move "
                            "at position " + std::to_string(i + 1)
                        );
                    }

                    if (log.search_count != count) {
                        throw std::runtime_error(
                            "Search budget mismatch: expected " +
                            std::to_string(count) +
                            ", got " +
                            std::to_string(log.search_count) +
                            ". Check the workload-sharing code."
                        );
                    }

                    searches += log.search_count;

                    if (player == BLACK) {
                        blackSeconds += seconds;
                    } else {
                        whiteSeconds += seconds;
                    }

                    // เขียน CSV นอกช่วงจับเวลา
                    details
                        << count << ','
                        << threads << ','
                        << round + 1 << ','
                        << i + 1 << ','
                        << (player == BLACK ? "Black" : "White")
                        << ','
                        << log.search_count << ','
                        << seconds << '\n';

                    // ไม่เดินตามผลค้นหาใหม่
                    // กระดานถัดไปมาจากชุดอ้างอิงเสมอ
                }

                const double totalSeconds =
                    blackSeconds + whiteSeconds;

                blackSums[index] += blackSeconds;
                whiteSums[index] += whiteSeconds;
                totalSums[index] += totalSeconds;

                runs
                    << count << ','
                    << threads << ','
                    << round + 1 << ','
                    << positions.size() << ','
                    << searches << ','
                    << blackSeconds << ','
                    << whiteSeconds << ','
                    << totalSeconds << '\n';

                std::cout
                    << "Count=" << count
                    << " Threads=" << threads
                    << " Round=" << round + 1
                    << "/" << rounds
                    << " Black=" << blackSeconds
                    << " White=" << whiteSeconds
                    << " Total=" << totalSeconds
                    << " seconds\n";

                details.flush();
                runs.flush();

                if (!details || !runs) {
                    throw std::runtime_error(
                        "Failed to write round results"
                    );
                }
            }
        }

        const double baselineMean =
            totalSums[0] / rounds;

        std::cout
            << "\n===== Summary: Count="
            << count << " =====\n";

        for (int i = 0; i < settingCount; i++) {
            const double meanBlack =
                blackSums[i] / rounds;

            const double meanWhite =
                whiteSums[i] / rounds;

            const double meanTotal =
                totalSums[i] / rounds;

            const double meanPerPosition =
                meanTotal / positions.size();

            const double meanPerBlackPosition =
                blackPositions > 0
                    ? meanBlack / blackPositions
                    : 0.0;

            const double meanPerWhitePosition =
                whitePositions > 0
                    ? meanWhite / whitePositions
                    : 0.0;

            if (meanTotal <= 0.0) {
                throw std::runtime_error(
                    "Measured time is too small for speedup"
                );
            }

            // เปรียบเทียบกับ 1 เธรดของ policy เดียวกัน
            const double speedup =
                baselineMean / meanTotal;

            const long long searchesPerRound =
                static_cast<long long>(positions.size()) * count;

            summary
                << policyName << ','
                << method << ','
                << count << ','
                << threadCounts[i] << ','
                << rounds << ','
                << positions.size() << ','
                << searchesPerRound << ','
                << meanBlack << ','
                << meanWhite << ','
                << meanTotal << ','
                << meanPerPosition << ','
                << speedup << ','
                << blackPositions << ','
                << whitePositions << ','
                << meanPerBlackPosition << ','
                << meanPerWhitePosition << '\n';

            std::cout
                << "Threads=" << threadCounts[i]
                << " Mean total=" << meanTotal
                << " seconds"
                << " Mean per position=" << meanPerPosition
                << " seconds"
                << " Speedup=" << speedup
                << '\n';
        }

        summary.flush();

        if (!summary) {
            throw std::runtime_error(
                "Failed to write summary"
            );
        }

        std::cout << '\n';
    }

    std::cout << "Saved results:\n";
    std::cout << prefix << "_moves.csv\n";
    std::cout << prefix << "_rounds.csv\n";
    std::cout << prefix << "_summary.csv\n";

    return 0;
}