/*
 *  Copyright 2019-2022 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

// Descriptor heap management utilities.
// See http://diligentgraphics.com/diligent-engine/architecture/d3d12/managing-descriptor-heaps/ for details

#pragma once

#include <mutex>
#include <vector>
#include <queue>
#include <string>
#include <unordered_set>
#include <atomic>

#include "VariableSizeAllocationsManager.hpp"
#include "IDescriptorAllocator.hpp"
#include "DescriptorHeapAllocation.hpp"


namespace Diligent
{

class DescriptorHeapAllocation;
class DescriptorHeapAllocationManager;


// The class performs suballocations within one D3D12 descriptor heap.
// It uses VariableSizeAllocationsManager to manage free space in the heap
//
// |  X  X  X  X  O  O  O  X  X  O  O  X  O  O  O  O  |  D3D12 descriptor heap
//
//  X - used descriptor
//  O - available descriptor
//
class DescriptorHeapAllocationManager
{
public:
    // Creates a new D3D12 descriptor heap
    DescriptorHeapAllocationManager(IMemoryAllocator&                 Allocator,
                                    IDescriptorAllocator&             ParentAllocator,
                                    size_t                            ThisManagerId,
                                    const D3D12_DESCRIPTOR_HEAP_DESC& HeapDesc);

    // Uses subrange of descriptors in the existing D3D12 descriptor heap
    // that starts at offset FirstDescriptor and uses NumDescriptors descriptors
    DescriptorHeapAllocationManager(IMemoryAllocator&      Allocator,
                                    IDescriptorAllocator&  ParentAllocator,
                                    size_t                 ThisManagerId,
                                    ComPtr<ID3D12DescriptorHeap>  pd3d12DescriptorHeap,
                                    uint32_t                 FirstDescriptor,
                                    uint32_t                 NumDescriptors);


    // = default causes compiler error when instantiating std::vector::emplace_back() in Visual Studio 2015 (Version 14.0.23107.0 D14REL)
    DescriptorHeapAllocationManager(DescriptorHeapAllocationManager&& rhs) noexcept :
        // clang-format off
        m_ParentAllocator           {rhs.m_ParentAllocator           },
        m_ThisManagerId             {rhs.m_ThisManagerId             },
        m_HeapDesc                  {rhs.m_HeapDesc                  },
        m_DescriptorSize            {rhs.m_DescriptorSize            },
        m_NumDescriptorsInAllocation{rhs.m_NumDescriptorsInAllocation},
        // Mutex is not movable
        //m_FreeBlockManagerMutex     (std::move(rhs.m_FreeBlockManagerMutex))
        m_FreeBlockManager          {std::move(rhs.m_FreeBlockManager)    },
        m_pd3d12DescriptorHeap      {std::move(rhs.m_pd3d12DescriptorHeap)},
        m_FirstCPUHandle            {rhs.m_FirstCPUHandle            },
        m_FirstGPUHandle            {rhs.m_FirstGPUHandle            },
        m_MaxAllocatedSize          {rhs.m_MaxAllocatedSize          }
    // clang-format on
    {
        rhs.m_NumDescriptorsInAllocation = 0; // Must be set to zero so that debug check in dtor passes
        rhs.m_ThisManagerId              = static_cast<size_t>(-1);
        rhs.m_FirstCPUHandle.ptr         = 0;
        rhs.m_FirstGPUHandle.ptr         = 0;
        rhs.m_MaxAllocatedSize           = 0;

    }

    // clang-format off
    // No copies or move-assignments
    DescriptorHeapAllocationManager& operator = (DescriptorHeapAllocationManager&&)      = delete;
    DescriptorHeapAllocationManager             (const DescriptorHeapAllocationManager&) = delete;
    DescriptorHeapAllocationManager& operator = (const DescriptorHeapAllocationManager&) = delete;
    // clang-format on

    ~DescriptorHeapAllocationManager();

    // Allocates Count descriptors
    DescriptorHeapAllocation Allocate(uint32_t Count);
    void                     FreeAllocation(DescriptorHeapAllocation&& Allocation);

    // clang-format off
    size_t GetNumAvailableDescriptors()const { return m_FreeBlockManager.GetFreeSize(); }
    uint32_t GetMaxDescriptors()         const { return m_NumDescriptorsInAllocation;     }
    size_t GetMaxAllocatedSize()       const { return m_MaxAllocatedSize;               }
    // clang-format on


private:
    IDescriptorAllocator&  m_ParentAllocator;

    // External ID assigned to this descriptor allocations manager
    size_t m_ThisManagerId = static_cast<size_t>(-1);

    // Heap description
    const D3D12_DESCRIPTOR_HEAP_DESC m_HeapDesc;

    const UINT m_DescriptorSize = 0;

    // Number of descriptors in the allocation.
    // If this manager was initialized as a subrange in the existing heap,
    // this value may be different from m_HeapDesc.NumDescriptors
    uint32_t m_NumDescriptorsInAllocation = 0;

    // Allocations manager used to handle descriptor allocations within the heap
    std::mutex                     m_FreeBlockManagerMutex;
    VariableSizeAllocationsManager m_FreeBlockManager;

    // Strong reference to D3D12 descriptor heap object
    ComPtr<ID3D12DescriptorHeap> m_pd3d12DescriptorHeap;

    // First CPU descriptor handle in the available descriptor range
    D3D12_CPU_DESCRIPTOR_HANDLE m_FirstCPUHandle = {0};

    // First GPU descriptor handle in the available descriptor range
    D3D12_GPU_DESCRIPTOR_HANDLE m_FirstGPUHandle = {0};

    size_t m_MaxAllocatedSize = 0;

    // Note: when adding new members, do not forget to update move ctor
};


// CPU descriptor heap is intended to provide storage for resource view descriptor handles.
// It contains a pool of DescriptorHeapAllocationManager object instances, where every instance manages
// its own CPU-only D3D12 descriptor heap:
//
//           m_HeapPool[0]                m_HeapPool[1]                 m_HeapPool[2]
//   |  X  X  X  X  X  X  X  X |, |  X  X  X  O  O  X  X  O  |, |  X  O  O  O  O  O  O  O  |
//
//    X - used descriptor                m_AvailableHeaps = {1,2}
//    O - available descriptor
//
// Allocation routine goes through the list of managers that have available descriptors and tries to process
// the request using every manager. If there are no available managers or no manager was able to handle the request,
// the function creates a new descriptor heap manager and lets it handle the request
//
// Render device contains four CPUDescriptorHeap object instances (one for each D3D12 heap type). The heaps are accessed
// when a texture or a buffer view is created.
//
class CPUDescriptorHeap : public IDescriptorAllocator
{
public:
    // Initializes the heap
    CPUDescriptorHeap(IMemoryAllocator&           Allocator,
                      uint32_t                    NumDescriptorsInHeap,
                      D3D12_DESCRIPTOR_HEAP_TYPE  Type,
                      D3D12_DESCRIPTOR_HEAP_FLAGS Flags);

    // clang-format off
    CPUDescriptorHeap             (const CPUDescriptorHeap&) = delete;
    CPUDescriptorHeap             (CPUDescriptorHeap&&)      = delete;
    CPUDescriptorHeap& operator = (const CPUDescriptorHeap&) = delete;
    CPUDescriptorHeap& operator = (CPUDescriptorHeap&&)      = delete;
    // clang-format on

    ~CPUDescriptorHeap() override;

    virtual DescriptorHeapAllocation Allocate(uint32_t Count) override final;
    virtual void                     Free(DescriptorHeapAllocation&& Allocation) override;
    virtual uint32_t                 GetDescriptorSize() const override final { return m_DescriptorSize; }

#ifdef DILIGENT_DEVELOPMENT
    int32_t DvpGetTotalAllocationCount();
#endif

private:
    void FreeAllocation(DescriptorHeapAllocation&& Allocation);

    IMemoryAllocator&      m_MemAllocator;

    // Pool of descriptor heap managers
    std::mutex                                                                                        m_HeapPoolMutex;
    std::vector<DescriptorHeapAllocationManager, STDAllocatorRawMem<DescriptorHeapAllocationManager>> m_HeapPool;
    // Indices of available descriptor heap managers
    std::unordered_set<size_t, std::hash<size_t>, std::equal_to<size_t>, STDAllocatorRawMem<size_t>> m_AvailableHeaps;

    D3D12_DESCRIPTOR_HEAP_DESC m_HeapDesc;
    const UINT                 m_DescriptorSize = 0;

    // Maximum heap size during the application lifetime - for statistic purposes
    uint32_t m_MaxSize     = 0;
    uint32_t m_CurrentSize = 0;
};


// GPU descriptor heap provides storage for shader-visible descriptors
// The heap contains single D3D12 descriptor heap that is split into two parts.
// The first part stores static and mutable resource descriptor handles.
// The second part is intended to provide temporary storage for dynamic resources.
// Space for dynamic resources is allocated in chunks, and then descriptors are suballocated within every
// chunk. DynamicSuballocationsManager facilitates this process.
//
//
//     static and mutable handles      ||                 dynamic space
//                                     ||    chunk 0     chunk 1     chunk 2     unused
//  | X O O X X O X O O O O X X X X O  ||  | X X X O | | X X O O | | O O O O |  O O O O  ||
//                                               |         |
//                                     suballocation       suballocation
//                                    within chunk 0       within chunk 1
//
// Render device contains two GPUDescriptorHeap instances (CBV_SRV_UAV and SAMPLER). The heaps
// are used to allocate GPU-visible descriptors for shader resource binding objects. The heaps
// are also used by the command contexts (through DynamicSuballocationsManager to allocated dynamic descriptors)
//
//  _______________________________________________________________________________________________________________________________
// | Render Device                                                                                                                 |
// |                                                                                                                               |
// | m_CPUDescriptorHeaps[CBV_SRV_UAV] |  X  X  X  X  X  X  X  X  |, |  X  X  X  X  X  X  X  X  |, |  X  O  O  X  O  O  O  O  |    |
// | m_CPUDescriptorHeaps[SAMPLER]     |  X  X  X  X  O  O  O  X  |, |  X  O  O  X  O  O  O  O  |                                  |
// | m_CPUDescriptorHeaps[RTV]         |  X  X  X  O  O  O  O  O  |, |  O  O  O  O  O  O  O  O  |                                  |
// | m_CPUDescriptorHeaps[DSV]         |  X  X  X  O  X  O  X  O  |                                                                |
// |                                                                               ctx1        ctx2                                |
// | m_GPUDescriptorHeaps[CBV_SRV_UAV]  | X O O X X O X O O O O X X X X O  ||  | X X X O | | X X O O | | O O O O |  O O O O  ||    |
// | m_GPUDescriptorHeaps[SAMPLER]      | X X O O X O X X X O O X O O O O  ||  | X X O O | | X O O O | | O O O O |  O O O O  ||    |
// |                                                                                                                               |
// |_______________________________________________________________________________________________________________________________|
//
//  ________________________________________________               ________________________________________________
// |Device Context 1                                |             |Device Context 2                                |
// |                                                |             |                                                |
// | m_DynamicGPUDescriptorAllocator[CBV_SRV_UAV]   |             | m_DynamicGPUDescriptorAllocator[CBV_SRV_UAV]   |
// | m_DynamicGPUDescriptorAllocator[SAMPLER]       |             | m_DynamicGPUDescriptorAllocator[SAMPLER]       |
// |________________________________________________|             |________________________________________________|
//
class GPUDescriptorHeap final : public IDescriptorAllocator
{
public:
    GPUDescriptorHeap(IMemoryAllocator&           Allocator,
                      uint32_t                      NumDescriptorsInHeap,
                      uint32_t                      NumDynamicDescriptors,
                      D3D12_DESCRIPTOR_HEAP_TYPE  Type,
                      D3D12_DESCRIPTOR_HEAP_FLAGS Flags);

    // clang-format off
    GPUDescriptorHeap             (const GPUDescriptorHeap&) = delete;
    GPUDescriptorHeap             (GPUDescriptorHeap&&)      = delete;
    GPUDescriptorHeap& operator = (const GPUDescriptorHeap&) = delete;
    GPUDescriptorHeap& operator = (GPUDescriptorHeap&&)      = delete;
    // clang-format on

    virtual ~GPUDescriptorHeap();

    virtual DescriptorHeapAllocation Allocate(uint32_t Count) override final
    {
        return m_HeapAllocationManager.Allocate(Count);
    }

    virtual void   Free(DescriptorHeapAllocation&& Allocation) override final;
    virtual uint32_t GetDescriptorSize() const override final { return m_DescriptorSize; }

    DescriptorHeapAllocation AllocateDynamic(uint32_t Count)
    {
        return m_DynamicAllocationsManager.Allocate(Count);
    }

    const D3D12_DESCRIPTOR_HEAP_DESC& GetHeapDesc() const { return m_HeapDesc; }
    uint32_t                            GetMaxStaticDescriptors() const { return m_HeapAllocationManager.GetMaxDescriptors(); }
    uint32_t                            GetMaxDynamicDescriptors() const { return m_DynamicAllocationsManager.GetMaxDescriptors(); }

    ID3D12DescriptorHeap* GetHeap() const { return m_pd3d12DescriptorHeap.Get(); };

#ifdef DILIGENT_DEVELOPMENT
    int32_t DvpGetTotalAllocationCount() const
    {
        return m_HeapAllocationManager.DvpGetAllocationsCounter() +
            m_DynamicAllocationsManager.DvpGetAllocationsCounter();
    }
#endif

protected:
    const D3D12_DESCRIPTOR_HEAP_DESC m_HeapDesc;
    ComPtr<ID3D12DescriptorHeap>    m_pd3d12DescriptorHeap; // Must be defined after m_HeapDesc

    const UINT m_DescriptorSize;

    // Allocation manager for static/mutable part
    DescriptorHeapAllocationManager m_HeapAllocationManager;

    // Allocation manager for dynamic part
    DescriptorHeapAllocationManager m_DynamicAllocationsManager;
};


// The class facilitates allocation of dynamic descriptor handles. It requests a chunk of heap
// from the master GPU descriptor heap and then performs linear suballocation within the chunk
// At the end of the frame all allocations are disposed.

//     static and mutable handles     ||                 dynamic space
//                                    ||    chunk 0                 chunk 2
//  |                                 ||  | X X X O |             | O O O O |           || GPU Descriptor Heap
//                                        |                       |
//                                        m_Suballocations[0]     m_Suballocations[1]
//
class DynamicSuballocationsManager final : public IDescriptorAllocator
{
public:
    DynamicSuballocationsManager(IMemoryAllocator&  Allocator,
                                 GPUDescriptorHeap& ParentGPUHeap,
                                 uint32_t             DynamicChunkSize,
                                 std::string             ManagerName);

    // clang-format off
    DynamicSuballocationsManager             (const DynamicSuballocationsManager&) = delete;
    DynamicSuballocationsManager             (DynamicSuballocationsManager&&)      = delete;
    DynamicSuballocationsManager& operator = (const DynamicSuballocationsManager&) = delete;
    DynamicSuballocationsManager& operator = (DynamicSuballocationsManager&&)      = delete;
    // clang-format on

    virtual ~DynamicSuballocationsManager() override;

    void ReleaseAllocations();

    virtual DescriptorHeapAllocation Allocate(uint32_t Count) override final;
    virtual void                     Free(DescriptorHeapAllocation&& Allocation) override final
    {
        // Do nothing. Dynamic allocations are not disposed individually, but as whole chunks
        // at the end of the frame by ReleaseAllocations()
        Allocation.Reset();
    }

    virtual uint32_t GetDescriptorSize() const override final { return m_ParentGPUHeap.GetDescriptorSize(); }

    size_t GetSuballocationCount() const { return m_Suballocations.size(); }

private:
    // Parent GPU descriptor heap that is used to allocate chunks
    GPUDescriptorHeap& m_ParentGPUHeap;
    const std::string       m_ManagerName;

    // List of chunks allocated from the master GPU descriptor heap. All chunks are disposed at the end
    // of the frame
    std::vector<DescriptorHeapAllocation, STDAllocatorRawMem<DescriptorHeapAllocation>> m_Suballocations;

    uint32_t m_CurrentSuballocationOffset = 0;
    uint32_t m_DynamicChunkSize           = 0;

    uint32_t m_CurrDescriptorCount         = 0;
    uint32_t m_PeakDescriptorCount         = 0;
    uint32_t m_CurrSuballocationsTotalSize = 0;
    uint32_t m_PeakSuballocationsTotalSize = 0;
};

} // namespace Diligent
