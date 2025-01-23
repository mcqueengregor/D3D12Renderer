#include "CommandQueue.h"
#include "Helpers.h"
#include <cassert>

CommandQueue::CommandQueue(Microsoft::WRL::ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type)
  : m_fenceValue(0)
  , m_commandListType(type)
  , m_device(device)
{
  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Type = type;
  desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  desc.NodeMask = 0;

  DX12_CHECK(m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_commandQueue)));
  DX12_CHECK(m_device->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));

  m_fenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
  assert(m_fenceEvent && "Failed to create fence event handle!");
}

CommandQueue::~CommandQueue()
{
}

Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> CommandQueue::GetCommandList()
{
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList;

  if (m_commandAllocatorQueue.empty()
    && IsFenceComplete(m_commandAllocatorQueue.front().fenceVal))
  {
    commandAllocator = m_commandAllocatorQueue.front().commandAllocator;
    m_commandAllocatorQueue.pop();

    DX12_CHECK(commandAllocator->Reset());
  }
  else
    commandAllocator = CreateCommandAllocator();

  if (!m_commandListQueue.empty())
  {
    commandList = m_commandListQueue.front();
    m_commandListQueue.pop();

    DX12_CHECK(commandList->Reset(commandAllocator.Get(), nullptr));
  }
  else
    commandList = CreateCommandList(commandAllocator);

  DX12_CHECK(commandList->SetPrivateDataInterface(
    __uuidof(ID3D12CommandAllocator), commandAllocator.Get()));

  return commandList;
}

uint64_t CommandQueue::ExecuteCommandList(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList)
{
  commandList->Close();

  ID3D12CommandAllocator* commandAllocator;
  UINT dataSize = sizeof(commandAllocator);
  DX12_CHECK(commandList->GetPrivateData(
    __uuidof(ID3D12CommandAllocator), &dataSize, &commandAllocator));

  ID3D12CommandList* const ppCommandLists[] = {
    commandList.Get(),
  };

  m_commandQueue->ExecuteCommandLists(1, ppCommandLists);
  uint64_t fenceVal = Signal();

  m_commandAllocatorQueue.emplace(
    CommandAllocatorEntry{ fenceVal, commandAllocator });
  m_commandListQueue.push(commandList);

  commandAllocator->Release();

  return fenceVal;
}

uint64_t CommandQueue::Signal()
{
  return 0;
}

bool CommandQueue::IsFenceComplete(uint64_t fenceVal)
{
  return false;
}

void CommandQueue::WaitForFenceValue(uint64_t fenceVal)
{
}

void CommandQueue::Flush()
{
}

Microsoft::WRL::ComPtr<ID3D12CommandQueue> CommandQueue::GetD3D12CommandQueue() const
{
  return Microsoft::WRL::ComPtr<ID3D12CommandQueue>();
}

Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CommandQueue::CreateCommandAllocator()
{
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> newCommandAllocator;
  DX12_CHECK(m_device->CreateCommandAllocator(m_commandListType, IID_PPV_ARGS(&newCommandAllocator)));
  
  return newCommandAllocator;
}

Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> CommandQueue::CreateCommandList(Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator)
{
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> newCommandList;
  DX12_CHECK(m_device->CreateCommandList(0, m_commandListType, allocator.Get(), nullptr, IID_PPV_ARGS(&newCommandList)));

  return newCommandList;
}
