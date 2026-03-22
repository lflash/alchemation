// Phase 25a smoke test: dispatches smoke_test.comp on the GPU and reads back results.
// Expected output: "GPU smoke test PASSED" with all 1024 values == 42.

#include "vk_context.hpp"
#include "gpu_buffer.hpp"
#include "compute_pipeline.hpp"
#include "gpu_runner.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
    constexpr uint32_t N = 1024;

    try {
        VulkanContext ctx(/*enableValidation=*/true);
        std::cout << "Vulkan device: " << ctx.deviceName() << '\n';

        // Output buffer: N uint32s, host-visible for easy readback.
        GpuBuffer<uint32_t> outBuf(ctx, N);

        // Zero-initialise on host before dispatch.
        std::vector<uint32_t> zeros(N, 0);
        outBuf.upload(zeros);

        // Load the compiled smoke-test shader.
        ComputePipeline pipe(ctx, "shaders/spv/smoke_test.comp.spv",
                             /*bindingCount=*/1,
                             /*pushConstantSize=*/sizeof(uint32_t));

        GpuRunner runner(ctx);

        // Dispatch: localSizeX = 64, groups = ceil(N / 64).
        uint32_t groups = (N + 63) / 64;
        uint32_t pushConst = N;

        runner.submit([&](VkCommandBuffer cmd) {
            pipe.bindBuffers(cmd, {outBuf.buffer()}, &pushConst);
            pipe.dispatch(cmd, groups);
        });

        // Read back and verify.
        std::vector<uint32_t> result(N);
        outBuf.download(result);

        bool ok = true;
        for (uint32_t i = 0; i < N; ++i) {
            if (result[i] != 42u) {
                std::cerr << "FAILED at index " << i
                          << ": expected 42, got " << result[i] << '\n';
                ok = false;
            }
        }

        if (ok)
            std::cout << "GPU smoke test PASSED (" << N << " values == 42)\n";

        return ok ? EXIT_SUCCESS : EXIT_FAILURE;

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
