// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <condition_variable>
#include <shared_mutex>
#include <thread>
#include <boost/container/small_vector.hpp>
#include "common/div_ceil.h"
#include <queue>
#include "common/slot_vector.h"
#include "common/types.h"
#include "common/unique_function.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/buffer_cache/memory_tracker_base.h"
#include "video_core/buffer_cache/range_set.h"
#include "video_core/multi_level_page_table.h"

namespace AmdGpu {
struct Liverpool;
}

namespace Core {
class MemoryManager;
}

namespace Shader {
namespace Gcn {
struct FetchShaderData;
}
struct Info;
} // namespace Shader

namespace Vulkan {
class GraphicsPipeline;
}

namespace VideoCore {

using BufferId = Common::SlotId;

static constexpr BufferId NULL_BUFFER_ID{0};

class TextureCache;

class BufferCache {
public:
    static constexpr u32 CACHING_PAGEBITS = 14;
    static constexpr u64 CACHING_PAGESIZE = u64{1} << CACHING_PAGEBITS;
    static constexpr u64 DEVICE_PAGESIZE = 16_KB;
    static constexpr u64 CACHING_NUMPAGES = u64{1} << (40 - CACHING_PAGEBITS);

    static constexpr u64 BDA_PAGETABLE_SIZE = CACHING_NUMPAGES * sizeof(vk::DeviceAddress);
    static constexpr u64 FAULT_BUFFER_SIZE = CACHING_NUMPAGES / 8; // Bit per page

    struct PageData {
        BufferId buffer_id{};
        u64 target_tick{};
    };

    struct Traits {
        using Entry = PageData;
        static constexpr size_t AddressSpaceBits = 40;
        static constexpr size_t FirstLevelBits = 16;
        static constexpr size_t PageBits = CACHING_PAGEBITS;
    };
    using PageTable = MultiLevelPageTable<Traits>;

    struct OverlapResult {
        boost::container::small_vector<BufferId, 16> ids;
        VAddr begin;
        VAddr end;
        bool has_stream_leap = false;
    };

public:
    explicit BufferCache(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler,
                         AmdGpu::Liverpool* liverpool, TextureCache& texture_cache,
                         PageManager& tracker);
    ~BufferCache();

    /// Returns a pointer to GDS device local buffer.
    [[nodiscard]] const Buffer* GetGdsBuffer() const noexcept {
        return &gds_buffer;
    }

    /// Retrieves the device local DBA page table buffer.
    [[nodiscard]] Buffer* GetBdaPageTableBuffer() noexcept {
        return &bda_pagetable_buffer;
    }

    /// Retrieves the fault buffer.
    [[nodiscard]] Buffer* GetFaultBuffer() noexcept {
        return &fault_buffer;
    }

    /// Retrieves the buffer with the specified id.
    [[nodiscard]] Buffer& GetBuffer(BufferId id) {
        return slot_buffers[id];
    }

    /// Retrieves a utility buffer optimized for specified memory usage.
    StreamBuffer& GetUtilityBuffer(MemoryUsage usage) noexcept {
        switch (usage) {
        case MemoryUsage::Stream:
            return stream_buffer;
        case MemoryUsage::Download:
            return download_buffer;
        case MemoryUsage::Upload:
            return staging_buffer;
        case MemoryUsage::DeviceLocal:
            return device_buffer;
        }
    }

    /// Invalidates any buffer in the logical page range.
    void InvalidateMemory(VAddr device_addr, u64 size);

    /// Waits on pending downloads in the logical page range.
    void ReadMemory(VAddr device_addr, u64 size);

    /// Binds host vertex buffers for the current draw.
    void BindVertexBuffers(const Vulkan::GraphicsPipeline& pipeline);

    /// Bind host index buffer for the current draw.
    void BindIndexBuffer(u32 index_offset);

    /// Writes a value to GPU buffer. (uses command buffer to temporarily store the data)
    void InlineData(VAddr address, const void* value, u32 num_bytes, bool is_gds);

    /// Performs buffer to buffer data copy on the GPU.
    void CopyBuffer(VAddr dst, VAddr src, u32 num_bytes, bool dst_gds, bool src_gds);

    /// Schedules pending GPU modified ranges since last commit to be copied back the host memory.
    bool CommitPendingDownloads(bool wait_done);

    /// Obtains a buffer for the specified region.
    [[nodiscard]] std::pair<Buffer*, u32> ObtainBuffer(VAddr gpu_addr, u32 size, bool is_written,
                                                       bool is_texel_buffer = false,
                                                       BufferId buffer_id = {});

    /// Attempts to obtain a buffer without modifying the cache contents.
    [[nodiscard]] std::pair<Buffer*, u32> ObtainBufferForImage(VAddr gpu_addr, u32 size);

    /// Return true when a region is registered on the cache
    [[nodiscard]] bool IsRegionRegistered(VAddr addr, size_t size);

    /// Return true when a CPU region is modified from the CPU
    [[nodiscard]] bool IsRegionCpuModified(VAddr addr, size_t size);

    /// Return true when a CPU region is modified from the GPU
    [[nodiscard]] bool IsRegionGpuModified(VAddr addr, size_t size);

    /// Return buffer id for the specified region
    BufferId FindBuffer(VAddr device_addr, u32 size);

    /// Processes the fault buffer.
    void ProcessFaultBuffer();

    /// Synchronizes all buffers in the specified range.
    void SynchronizeBuffersInRange(VAddr device_addr, u64 size);

    /// Synchronizes all buffers neede for DMA.
    void SynchronizeDmaBuffers();

    /// Record memory barrier. Used for buffers when accessed via BDA.
    void MemoryBarrier();

private:
    template <typename Func>
    void ForEachBufferInRange(VAddr device_addr, u64 size, Func&& func) {
        const u64 page_end = Common::DivCeil(device_addr + size, CACHING_PAGESIZE);
        for (u64 page = device_addr >> CACHING_PAGEBITS; page < page_end;) {
            const BufferId buffer_id = page_table[page].buffer_id;
            if (!buffer_id) {
                ++page;
                continue;
            }
            Buffer& buffer = slot_buffers[buffer_id];
            func(buffer_id, buffer);

            const VAddr end_addr = buffer.CpuAddr() + buffer.SizeBytes();
            page = Common::DivCeil(end_addr, CACHING_PAGESIZE);
        }
    }

    inline bool IsBufferInvalid(BufferId buffer_id) const {
        return !buffer_id || slot_buffers[buffer_id].is_deleted;
    }

    inline void WaitForTargetTick(u64 target_tick) {
        u64 tick = download_tick.load();
        while (tick < target_tick) {
            download_tick.wait(tick);
            tick = download_tick.load();
        }
    }

    void DownloadBufferMemory(const Buffer& buffer, VAddr device_addr, u64 size);

    [[nodiscard]] OverlapResult ResolveOverlaps(VAddr device_addr, u32 wanted_size);

    void JoinOverlap(BufferId new_buffer_id, BufferId overlap_id, bool accumulate_stream_score);

    BufferId CreateBuffer(VAddr device_addr, u32 wanted_size);

    void Register(BufferId buffer_id);

    void Unregister(BufferId buffer_id);

    template <bool insert>
    void ChangeRegister(BufferId buffer_id);

    bool SynchronizeBuffer(Buffer& buffer, VAddr device_addr, u32 size, bool is_texel_buffer);

    bool SynchronizeBufferFromImage(Buffer& buffer, VAddr device_addr, u32 size);

    void InlineDataBuffer(Buffer& buffer, VAddr address, const void* value, u32 num_bytes);

    void WriteDataBuffer(Buffer& buffer, VAddr address, const void* value, u32 num_bytes);

    void DeleteBuffer(BufferId buffer_id);

    void DownloadThread(std::stop_token token);

    const Vulkan::Instance& instance;
    Vulkan::Scheduler& scheduler;
    AmdGpu::Liverpool* liverpool;
    Core::MemoryManager* memory;
    TextureCache& texture_cache;
    PageManager& tracker;
    StreamBuffer staging_buffer;
    StreamBuffer stream_buffer;
    StreamBuffer download_buffer;
    StreamBuffer device_buffer;
    Buffer gds_buffer;
    std::shared_mutex mutex;
    Buffer bda_pagetable_buffer;
    Buffer fault_buffer;
    std::shared_mutex slot_buffers_mutex;
    Common::SlotVector<Buffer> slot_buffers;
    RangeSet pending_download_ranges;
    RangeSet gpu_modified_ranges;
    SplitRangeMap<BufferId> buffer_ranges;
    MemoryTracker memory_tracker;
    PageTable page_table;
    vk::UniqueDescriptorSetLayout fault_process_desc_layout;
    vk::UniquePipeline fault_process_pipeline;
    vk::UniquePipelineLayout fault_process_pipeline_layout;
    std::jthread async_download_thread;
    struct PendingDownload {
        Common::UniqueFunction<void> callback;
        u64 gpu_tick;
        u64 signal_tick;
    };
    std::mutex queue_mutex;
    std::condition_variable_any queue_cv;
    std::queue<PendingDownload> async_downloads;
    u64 current_download_tick{0};
    std::atomic<u64> download_tick{1};
};

} // namespace VideoCore
