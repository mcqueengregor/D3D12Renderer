#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <queue>

class CommandQueue
{
public:
	CommandQueue(Microsoft::WRL::ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type);
	virtual ~CommandQueue();

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> GetCommandList();
	uint64_t ExecuteCommandList(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList);

	uint64_t	Signal();
	bool			IsFenceComplete(uint64_t fenceVal);
	void			WaitForFenceValue(uint64_t fenceVal);
	void			Flush();

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> GetD3D12CommandQueue() const;

protected:
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator>			CreateCommandAllocator();
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2>	CreateCommandList(Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator);

private:
	struct CommandAllocatorEntry
	{
		uint64_t fenceVal;
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
	};

	using CommandAllocatorQueue = std::queue<CommandAllocatorEntry>;
	using CommandListQueue			= std::queue < Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> >;

	D3D12_COMMAND_LIST_TYPE											m_commandListType;
	Microsoft::WRL::ComPtr<ID3D12Device2>				m_device;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue>	m_commandQueue;
	Microsoft::WRL::ComPtr<ID3D12Fence>					m_fence;
	HANDLE																			m_fenceEvent;
	uint64_t																		m_fenceValue;

	CommandAllocatorQueue												m_commandAllocatorQueue;
	CommandListQueue														m_commandListQueue;
};

