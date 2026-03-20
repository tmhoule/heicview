#pragma once
#include "common.h"

HRESULT RegisterWICDecoder(HMODULE hModule);
HRESULT UnregisterWICDecoder();

HRESULT RegisterThumbnailProvider(HMODULE hModule);
HRESULT UnregisterThumbnailProvider();
