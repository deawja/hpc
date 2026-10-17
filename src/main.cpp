#include <iostream>
#include <iomanip>
#include <memory>
#include <string>
#include <cstdlib>
#include <chrono>
#include <omp.h>

#include "Agent.h"
#include "MCTS.h"
#include "Policy.h"
#include "Log.h"

using namespace std;

auto setPolicy(string policy) {
    if (policy.empty()) {
        cerr << "Policy must not be empty!\n";
        exit(EXIT_FAILURE);
    }

    switch (policy[0]) {
        case 'S':
        case 's':
            return Policy::MCTS_Serial;

        case 'L':
        case 'l':
            return Policy::MCTS_Parallel_Leaf;

        case 'R':
        case 'r':
            return Policy::MCTS_Parallel_Root;

        case 'T':
        case 't':
            return Policy::MCTS_Parallel_Tree;

        default:
            cerr << "Unknown policy: " << policy << '\n';
            exit(EXIT_FAILURE);
    }
}

// ต้องอยู่หลัง setPolicy() เพราะ Benchmark.h เรียกใช้ฟังก์ชันนี้
#include "Benchmark.h"

int main(int argc, char *argv[]) {
    cout << fixed << setprecision(6);

    // ตรวจคำสั่ง Benchmark ก่อนส่งให้ตัวอ่านพารามิเตอร์เดิม
    try {
        if (argc > 1 && string(argv[1]) == "--record") {
            if (argc != 4) {
                throw runtime_error(
                    "Usage: mcts --record reference.moves count"
                );
            }

            return recordReference(
                argv[2],
                stoi(argv[3])
            );
        }

        if (argc > 1 && string(argv[1]) == "--benchmark") {
            if (argc != 8) {
                throw runtime_error(
                    "Usage: mcts --benchmark reference.moves "
                    "Tree baseCount rounds openmp outputPrefix"
                );
            }

            return benchmarkReference(
                argv[2],        // ไฟล์เกมอ้างอิง
                argv[3],        // Root, Tree หรือ Leaf
                stoi(argv[4]),  // workload เริ่มต้น
                stoi(argv[5]),  // จำนวนรอบ
                argv[6],        // openmp หรือ pthread
                argv[7]         // ชื่อขึ้นต้นไฟล์ผลลัพธ์
            );
        }
    } catch (const exception &error) {
        cerr << "Error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    /***** โหมดเล่นเกมปกติ *****/

    envParam = {
        1000, -1, 4,
        "Tree", "Serial",
        "openmp", "openmp"
    };

    setEnvParameter(argc, argv);

    if (envParam.thread_num <= 0) {
        cerr << "Thread count must be positive.\n";
        return EXIT_FAILURE;
    }

    if (envParam.simulation_counts <= 0 && envParam.time <= 0) {
        cerr << "Simulation count or time must be positive.\n";
        return EXIT_FAILURE;
    }

    omp_set_dynamic(0);
    omp_set_num_threads(envParam.thread_num);

    cout << "Simulation count: "
         << envParam.simulation_counts << '\n';
    cout << "Time setting: "
         << envParam.time << " ms\n";
    cout << "Thread setting: "
         << envParam.thread_num << '\n';
    cout << "Black policy: "
         << envParam.black_policy << '\n';
    cout << "White policy: "
         << envParam.white_policy << '\n';
    cout << "Black method: "
         << envParam.black_method << '\n';
    cout << "White method: "
         << envParam.white_method << "\n\n";

    auto blackPolicy = setPolicy(envParam.black_policy);
    auto whitePolicy = setPolicy(envParam.white_policy);

    Log playerLog, envirLog;

    board currentBoard;
    Agent player{BLACK}, envir{WHITE};

    WIN_STATE outcome = DRAW;
    const int limitStep = LIMIT_STEP;

    int blackMoves = 0;
    int whiteMoves = 0;
    int searchCalls = 0;

    double blackMoveTime = 0.0;
    double whiteMoveTime = 0.0;
    double totalActionTime = 0.0;

    bool stoppedByLimit = false;

    // จับเวลาทั้งเกม รวมการพิมพ์กระดานระหว่างเกม
    const double gameStart = omp_get_wtime();

    while (true) {
        const PIECE currentPlayer = currentBoard.take_turn();
        Pair move;

        // จับเวลาค้นหาและเดินหมากในตานี้
        const double moveStart = omp_get_wtime();

        if (currentPlayer == BLACK) {
            move = player.take_action(
                currentBoard,
                blackPolicy,
                playerLog,
                envParam
            );
        } else {
            move = envir.take_action(
                currentBoard,
                whitePolicy,
                envirLog,
                envParam
            );
        }

        const double moveElapsed =
            omp_get_wtime() - moveStart;

        searchCalls++;
        totalActionTime += moveElapsed;

        // ไม่นับครั้งที่เดินไม่สำเร็จในค่าเฉลี่ยต่อตา
        if (move == Pair{}) {
            cout << "[END] Player="
                 << (currentPlayer == BLACK ? "Black" : "White")
                 << ", No successful move"
                 << ", Time=" << moveElapsed
                 << " seconds\n";

            outcome = currentBoard.compare_piece();
            break;
        }

        if (currentPlayer == BLACK) {
            blackMoves++;
            blackMoveTime += moveElapsed;
        } else {
            whiteMoves++;
            whiteMoveTime += moveElapsed;
        }

        cout << "[MOVE " << blackMoves + whiteMoves << "] Player="
             << (currentPlayer == BLACK ? "Black" : "White")
             << ", Time=" << moveElapsed
             << " seconds\n";

        cout << currentBoard << '\n';

        // คงเงื่อนไขจำกัดจำนวนตาจากโปรแกรมเดิม
        if (player.getStep() > limitStep) {
            stoppedByLimit = true;
            outcome = currentBoard.compare_piece();
            break;
        }
    }

    const double gameElapsed =
        omp_get_wtime() - gameStart;

    const int totalMoves = blackMoves + whiteMoves;
    const double successfulMoveTime =
        blackMoveTime + whiteMoveTime;

    cout << "\n===== Game Result =====\n";

    cout << "Winner: ";
    if (outcome == BLACK_WIN) {
        cout << "Black\n";
    } else if (outcome == WHITE_WIN) {
        cout << "White\n";
    } else {
        cout << "Draw\n";
    }

    cout << "Stop reason: "
         << (stoppedByLimit
             ? "LIMIT_STEP reached"
             : "No successful move")
         << '\n';

    auto printSummary = [](
        const char* label,
        int moves,
        double seconds
    ) {
        const double average =
            moves > 0 ? seconds / moves : 0.0;

        cout << "\n===== " << label << " =====\n";
        cout << "Moves: " << moves << '\n';
        cout << "Successful move time: "
             << seconds << " seconds\n";
        cout << "Average time per move: "
             << average << " seconds\n";
    };

    printSummary("Black", blackMoves, blackMoveTime);
    printSummary("White", whiteMoves, whiteMoveTime);
    printSummary("Both Players", totalMoves, successfulMoveTime);

    cout << "\n===== Overall Timing =====\n";
    cout << "Total moves: " << totalMoves << '\n';
    cout << "Search calls: " << searchCalls << '\n';
    cout << "Total action time (including failed moves): "
         << totalActionTime << " seconds\n";
    cout << "Total game time (including board output): "
         << gameElapsed << " seconds\n";

    cout << "\n===== Player Log (Black) =====\n";
    playerLog.printLog();

    cout << "\n===== Envir Log (White) =====\n";
    envirLog.printLog();

    return EXIT_SUCCESS;
}