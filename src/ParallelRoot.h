#include<thread>

class ParallelRoot{
public:
/*** pthread ***/
    static void rootParallization(MonteCarloTree *tree, EnvParameter env) {

        if (env.time > 0) {
            auto start = chrono::steady_clock::now();
            auto end = chrono::steady_clock::now();
            auto diff_time = chrono::duration<double, milli>(end - start).count();
            while (diff_time < env.time)
            {
                tree->tree_policy();
                end = chrono::steady_clock::now();
                diff_time = chrono::duration<double, milli>(end - start).count();
            }
        } else if (env.simulation_counts > 0) {
            int count_sim = 0;
            while(count_sim < env.simulation_counts) {
                tree->tree_policy();
                count_sim++;
            }
        }
        // cout << "Count sim: " << count_sim << '\n';
    }

    static void Pthread(MonteCarloTree *trees, EnvParameter env) {

        // -c is the total budget across all workers.
        auto workerEnv = [&](int index) {
            EnvParameter local = env;
            if (env.time <= 0 && env.simulation_counts > 0) {
                local.simulation_counts = env.simulation_counts / env.thread_num
                    + (index < env.simulation_counts % env.thread_num ? 1 : 0);
            }
            return local;
        };
        std::thread workers[ env.thread_num ];
        for(int i=1; i < env.thread_num; i++) {
            workers[i] = std::thread(ParallelRoot::rootParallization, &trees[i], workerEnv(i));
        }
        
        ParallelRoot::rootParallization(&trees[0], workerEnv(0));
        for(int i=1; i < env.thread_num; i++) {
            workers[i].join();
        }

    }
    /*** pthread ***/

    /*** OpenMP ***/
    static void OMP(MonteCarloTree *trees, const EnvParameter &env) {

        omp_set_num_threads(env.thread_num);

        if (env.time > 0) {
            #pragma omp parallel for
            for(int i = 0; i < env.thread_num; i++){
                auto start = chrono::steady_clock::now();
                auto end = chrono::steady_clock::now();
                auto diff_time = chrono::duration<double, milli>(end - start).count();

                while (diff_time < env.time)
                {
                    trees[i].tree_policy();
                    end = chrono::steady_clock::now();
                    diff_time = chrono::duration<double, milli>(end - start).count();
                }
                // cout << "Count: " << count_sim << '\n';
            }
        }
        else if (env.simulation_counts > 0) {
            #pragma omp parallel for
            for(int i = 0; i < env.thread_num; i++){
                const int budget = env.simulation_counts / env.thread_num
                    + (i < env.simulation_counts % env.thread_num ? 1 : 0);
                int count_sim = 0;

                while (count_sim < budget) {
                    trees[i].tree_policy();
                    count_sim++;
                }
                // cout << "Count: " << count_sim << '\n';
            }
        }

    }
};