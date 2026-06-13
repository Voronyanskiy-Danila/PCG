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

#include "dxlib/Diligent/IDescriptorAllocator.hpp"

namespace Diligent
{

    class DescriptorHeapAllocationManager;

    // The class represents descriptor heap allocation (continuous descriptor range in a descriptor heap)
    //
    //                  m_FirstCpuHandle
    //                   |
    //  | ~  ~  ~  ~  ~  X  X  X  X  X  X  X  ~  ~  ~  ~  ~  ~ |  D3D12 Descriptor Heap
    //                   |
    //                  m_FirstGpuHandle
    //
    class DescriptorHeapAllocation
    {
    public:
        // Creates null allocation
        DescriptorHeapAllocation() noexcept :
            // clang-format off
            m_pDescriptorHeap{ nullptr },
            m_NumHandles{ 1 }, // One null descriptor handle
            m_DescriptorSize{ 0 }
            // clang-format on
        {
            m_FirstCpuHandle.ptr = 0;
            m_FirstGpuHandle.ptr = 0;
        }

        // Initializes non-null allocation
        DescriptorHeapAllocation(IDescriptorAllocator& Allocator,
            ID3D12DescriptorHeap* pHeap,
            D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle,
            uint32_t                      NHandles,
            uint16_t                      AllocationManagerId) noexcept :
            // clang-format off
            m_FirstCpuHandle{ CpuHandle },
            m_FirstGpuHandle{ GpuHandle },
            m_pAllocator{ &Allocator },
            m_pDescriptorHeap{ pHeap },
            m_NumHandles{ NHandles },
            m_AllocationManagerId{ AllocationManagerId }
            // clang-format on
        {
            assert(m_pAllocator != nullptr && m_pDescriptorHeap != nullptr);
            auto DescriptorSize = m_pAllocator->GetDescriptorSize();
            assert(DescriptorSize < std::numeric_limits<uint16_t>::max() && "DescriptorSize exceeds allowed limit");
            m_DescriptorSize = static_cast<uint16_t>(DescriptorSize);
        }

        // Move constructor (copy is not allowed)
        DescriptorHeapAllocation(DescriptorHeapAllocation&& Allocation) noexcept :
            // clang-format off
            m_FirstCpuHandle{ std::move(Allocation.m_FirstCpuHandle) },
            m_FirstGpuHandle{ std::move(Allocation.m_FirstGpuHandle) },
            m_pAllocator{ std::move(Allocation.m_pAllocator) },
            m_pDescriptorHeap{ std::move(Allocation.m_pDescriptorHeap) },
            m_NumHandles{ std::move(Allocation.m_NumHandles) },
            m_AllocationManagerId{ std::move(Allocation.m_AllocationManagerId) },
            m_DescriptorSize{ std::move(Allocation.m_DescriptorSize) }
            // clang-format on
        {
            Allocation.Reset();
        }

        // Move assignment (assignment is not allowed)
        DescriptorHeapAllocation& operator=(DescriptorHeapAllocation&& Allocation) noexcept
        {
            m_FirstCpuHandle = std::move(Allocation.m_FirstCpuHandle);
            m_FirstGpuHandle = std::move(Allocation.m_FirstGpuHandle);
            m_NumHandles = std::move(Allocation.m_NumHandles);
            m_pAllocator = std::move(Allocation.m_pAllocator);
            m_AllocationManagerId = std::move(Allocation.m_AllocationManagerId);
            m_pDescriptorHeap = std::move(Allocation.m_pDescriptorHeap);
            m_DescriptorSize = std::move(Allocation.m_DescriptorSize);

            Allocation.Reset();

            return *this;
        }

        void Reset()
        {
            m_FirstCpuHandle.ptr = 0;
            m_FirstGpuHandle.ptr = 0;
            m_pAllocator = nullptr;
            m_pDescriptorHeap = nullptr;
            m_NumHandles = 0;
            m_AllocationManagerId = InvalidAllocationMgrId;
            m_DescriptorSize = 0;
        }

        // clang-format off
        DescriptorHeapAllocation(const DescriptorHeapAllocation&) = delete;
        DescriptorHeapAllocation& operator=(const DescriptorHeapAllocation&) = delete;
        // clang-format on


        // Destructor automatically releases this allocation through the allocator
        ~DescriptorHeapAllocation()
        {
            if (!IsNull() && m_pAllocator)
            {
                m_pAllocator->Free(std::move(*this));
            }
            // Allocation must have been disposed by the allocator
            assert(IsNull() && "Non-null descriptor is being destroyed");
        }

        // Returns CPU descriptor handle at the specified offset
        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(uint32_t Offset = 0) const
        {
            assert(Offset >= 0 && Offset < m_NumHandles);

            D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = m_FirstCpuHandle;
            CPUHandle.ptr += SIZE_T{ m_DescriptorSize } *SIZE_T{ Offset };

            return CPUHandle;
        }

        // Returns GPU descriptor handle at the specified offset
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(uint32_t Offset = 0) const
        {
            assert(Offset >= 0 && Offset < m_NumHandles);
            D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = m_FirstGpuHandle;
            GPUHandle.ptr += SIZE_T{ m_DescriptorSize } *SIZE_T{ Offset };

            return GPUHandle;
        }

        template <typename HandleType>
        HandleType GetHandle(uint32_t Offset = 0) const;

        template <>
        D3D12_CPU_DESCRIPTOR_HANDLE GetHandle<D3D12_CPU_DESCRIPTOR_HANDLE>(uint32_t Offset) const
        {
            return GetCpuHandle(Offset);
        }

        template <>
        D3D12_GPU_DESCRIPTOR_HANDLE GetHandle<D3D12_GPU_DESCRIPTOR_HANDLE>(uint32_t Offset) const
        {
            return GetGpuHandle(Offset);
        }


        // Returns pointer to D3D12 descriptor heap that contains this allocation
        ID3D12DescriptorHeap* GetDescriptorHeap() const { return m_pDescriptorHeap; }


        // clang-format off
        size_t GetNumHandles()          const { return m_NumHandles; }
        bool   IsNull()                 const { return m_FirstCpuHandle.ptr == 0; }
        bool   IsShaderVisible()        const { return m_FirstGpuHandle.ptr != 0; }
        size_t GetAllocationManagerId() const { return m_AllocationManagerId; }
        UINT  GetDescriptorSize()       const { return m_DescriptorSize; }
        // clang-format on

    private:
        // First CPU descriptor handle in this allocation
        D3D12_CPU_DESCRIPTOR_HANDLE m_FirstCpuHandle = { 0 };

        // First GPU descriptor handle in this allocation
        D3D12_GPU_DESCRIPTOR_HANDLE m_FirstGpuHandle = { 0 };

        // Keep strong reference to the parent heap to make sure it is alive while allocation is alive - TOO EXPENSIVE
        //RefCntAutoPtr<IDescriptorAllocator> m_pAllocator;

        // Pointer to the descriptor heap allocator that created this allocation
        IDescriptorAllocator* m_pAllocator = nullptr;

        // Pointer to the D3D12 descriptor heap that contains descriptors in this allocation
        ID3D12DescriptorHeap* m_pDescriptorHeap = nullptr;

        // Number of descriptors in the allocation
        uint32_t m_NumHandles = 0;

        static constexpr uint16_t InvalidAllocationMgrId = 0xFFFF;

        // Allocation manager ID. One allocator may support several
        // allocation managers. This field is required to identify
        // the manager within the allocator that was used to create
        // this allocation
        uint16_t m_AllocationManagerId = InvalidAllocationMgrId;

        // Descriptor size
        uint16_t m_DescriptorSize = 0;
    };

} // namespace Diligent
