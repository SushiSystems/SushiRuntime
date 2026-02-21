#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/scheduler/task.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>

// #include <oneapi/mkl.hpp> // Uncomment when linking against MKL

using namespace SushiRuntime;

/**
 * @brief Example of a Host-side Task that wraps a library call (e.g., oneMKL).
 * 
 * This demonstrates how SushiBLAS can submit tasks to SushiRuntime's scheduler
 * while retaining control over the queue submission process required by libraries.
 */
class BlasGemmTask : public Scheduler::Task
{
public:
    BlasGemmTask(const char* name) : Scheduler::Task(name) {}

    /**
     * @brief Identify this task as a HOST task.
     * Use this mode for library calls like oneMKL that take a queue& directly.
     */
    ExecutionMode get_execution_mode() const override { return ExecutionMode::HOST; }
    
    // Override base implementation (unused in HOST mode)
    void execute(sycl::handler& /*h*/) override {} 

    /**
     * @brief The core execution logic for the library call.
     * 
     * @param q The SYCL queue provided by SushiRuntime.
     * @param deps Dependencies from previous tasks in the graph.
     * @return sycl::event The event returned by the library call.
     */
    sycl::event execute_host(sycl::queue& q, const std::vector<sycl::event>& deps) override
    {
        std::cout << "[SushiBLAS Prototype] Submitting GEMM task to queue..." << std::endl;
        
        // --- Real Implementation would look like this: ---
        // oneapi::mkl::blas::gemm(q, oneapi::mkl::transpose::nontrans, oneapi::mkl::transpose::nontrans,
        //                         m, n, k, alpha, A, lda, B, ldb, beta, C, ldc, deps);
        
        // --- Simulated Implementation ---
        // We submit a dummy command to simulate work and respect dependencies.
        auto e = q.submit([&](sycl::handler& h) 
        {
            h.depends_on(deps);
            h.host_task([=](){
                std::cout << "[SushiBLAS Prototype] Executing oneMKL GEMM..." << std::endl;
                // std::this_thread::sleep_for(std::chrono::milliseconds(10));
                std::cout << "[SushiBLAS Prototype] GEMM Complete." << std::endl;
            });
        });
        
        return e;
    }
    
    // Splitting is typically not handled by the wrapper for BLAS operations
    std::optional<sushi_ptr<Scheduler::Task>> split() override { return std::nullopt; }
    bool is_splittable() const override { return false; }
};

int main()
{
    try 
    {
        std::cout << "Initializing SushiRuntime Context..." << std::endl;
        Execution::RuntimeContext ctx;
        Scheduler::TaskScheduler scheduler(ctx);
        
        std::cout << "Creating BLAS Task..." << std::endl;
        auto task = make_sushi<BlasGemmTask>("OneMKL_GEMM_RowMajor");
        
        std::cout << "Submitting Task to Scheduler..." << std::endl;
        scheduler.submit(task.get());
        
        // In a real scenario, we would use TaskGraph via 'wait()' or similar synchronization.
        // Here we just wait a bit for the async task to complete.
        std::cout << "Waiting for completion..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        std::cout << "Done." << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
