#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <exception>

inline void DX12_CHECK(HRESULT hr)
{
  if (FAILED(hr))
    throw std::exception();
}

inline void DX12_CHECK(HRESULT hr, const char* msg)
{
  if (FAILED(hr))
    throw std::exception(msg);
}