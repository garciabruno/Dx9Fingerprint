#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")
//#pragma comment(lib, "DirectXTK.lib")

#include <Windows.h>
#include <stdio.h>
#include <d3d9.h>
#include <vector>
#include <algorithm>

#include "Utils/krunk.h"
#include "Utils/VMTHook.h"

HRESULT(__stdcall* Direct3D9EndScene)(IDirect3DDevice9*);
HRESULT(__stdcall* Direct3D9Present)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT(__stdcall* DrawIndexed_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);

DrawIndexed_t D3D9DrawIndexedPrimitive;
IDirect3DDevice9* lastPDevice = NULL;

std::vector<unsigned int> PrimitiveIndexes;

BOOL BlockPrimitives = FALSE;
BOOL USE_STEAM_GAMEOVERLAY = TRUE;
unsigned int PrimitiveIndex = 0;

LPDIRECT3DTEXTURE9 g_Blue = NULL, g_Green = NULL;

// Thanks to whoever made this function in UC.me forum
HRESULT GenerateTexture(LPDIRECT3DDEVICE9 pDevice, IDirect3DTexture9** ppD3Dtex, DWORD colour32)
{
	if (FAILED(pDevice->CreateTexture(8, 8, 1, 0, D3DFMT_A4R4G4B4, D3DPOOL_MANAGED, ppD3Dtex, NULL)))
		return E_FAIL;

	WORD colour16 = ((WORD)((colour32 >> 28) & 0xF) << 12)
		| (WORD)(((colour32 >> 20) & 0xF) << 8)
		| (WORD)(((colour32 >> 12) & 0xF) << 4)
		| (WORD)(((colour32 >> 4) & 0xF) << 0);

	D3DLOCKED_RECT d3dlr;
	(*ppD3Dtex)->LockRect(0, &d3dlr, 0, 0);
	WORD* pDst16 = (WORD*)d3dlr.pBits;

	for (int xy = 0; xy < 8 * 8; xy++)
		*pDst16++ = colour16;

	(*ppD3Dtex)->UnlockRect(0);

	return S_OK;
}

/*
SIG: \x8B\xFF\x55\x8B\xEC\x83\xE4\xF8\x51\x51\x56\x8B\x75\x08\x8B\xCE\xF7\xD9\x57\x1B\xC9\x8D\x46\x04\x23\xC8\x6A\x00\x51\x8D\x4C\x24\x10\xE8\x42\x02\xF7\xFF\xF7\x46\x30\x02\x00\x00\x00\x74\x07\xBF\x6C\x08\x76\x88\xEB\x17
MSK: x?xx?x??xxxx??x?x?xx?x??x?x?xxx??x????x??????xxx????xx
MODULE: d3d9.dll
LENGTH: 0x18E000
*/

inline unsigned char* FindD3D9EndScene(unsigned char* start, unsigned long length) {
#pragma section(".text",read,execute)
	__declspec(align(16))
		__declspec(allocate(".text"))
		static char scan[] = "\x8B\x4C\x24\x08\x8B\x44\x24\x04\x83\xE9\x36\x01\xC1\xEB\x06\x90\x40\x39\xC8\x73\x74\x80\x38\x8B\x75\xF6\x66\x81\x78\x02\x55\x8B\x75\xEE\x80\x78\x05\x83\x75\xE8\x81\x78\x08\x51\x51\x56\x8B\x75\xDF\x80\x78\x0E\x8B\x75\xD9\x80\x78\x10\xF7\x75\xD3\x66\x81\x78\x12\x57\x1B\x75\xCB\x80\x78\x15\x8D\x75\xC5\x80\x78\x18\x23\x75\xBF\x80\x78\x1A\x6A\x75\xB9\x66\x81\x78\x1C\x51\x8D\x75\xB1\x80\x78\x1E\x4C\x75\xAB\x80\x78\x21\xE8\x75\xA5\x80\x78\x26\xF7\x75\x9F\x66\x81\x78\x2D\x74\x07\x75\x97\x80\x78\x2F\xBF\x75\x91\x66\x81\x78\x34\xEB\x17\x75\x89\xEB\x02\x31\xC0\xC3";
	return ((unsigned char* (*)(unsigned char*, unsigned long)) & scan)((unsigned char*)start, length);
}

/*
SIG: \x55\x8B\xEC\x83\xEC\x40\xB9\xDC\x26\x0F\x59\x53
MSK: xx?x??x????x
MODULE: gameoverlayrenderer.dll
LENGTH: 0x187000
*/

inline unsigned char* FindGameoverlayPresent(unsigned char* start, unsigned long length) {
#pragma section(".text",read,execute)
	__declspec(align(16))
		__declspec(allocate(".text"))
		static char scan[] = "\x8B\x4C\x24\x08\x8B\x44\x24\x04\x83\xE9\x0C\x01\xC1\xEB\x06\x90\x40\x39\xC8\x73\x1B\x66\x81\x38\x55\x8B\x75\xF4\x80\x78\x03\x83\x75\xEE\x80\x78\x06\xB9\x75\xE8\x80\x78\x0B\x53\x75\xE2\xEB\x02\x31\xC0\xC3";
	return ((unsigned char* (*)(unsigned char*, unsigned long)) & scan)((unsigned char*)start, length);
}

HRESULT __stdcall HookDrawIndexed(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount)
{
	std::vector<unsigned int>::iterator targetIt = std::find(PrimitiveIndexes.begin(), PrimitiveIndexes.end(), startIndex);

	// Is this a new primitive index we haven't recorded yet?
	if (targetIt == PrimitiveIndexes.end())
		PrimitiveIndexes.push_back(startIndex);

	if (BlockPrimitives && PrimitiveIndex < PrimitiveIndexes.size())
	{

		// Should we finger print this index in particular?
		if (PrimitiveIndexes[PrimitiveIndex] == startIndex)
		{
			// Render the back of the primitive as solid blue

			pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
			pDevice->SetTexture(0, g_Blue);

			D3D9DrawIndexedPrimitive(pDevice, PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);

			// Render the front of the primitive as solid green
			pDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
			pDevice->SetTexture(0, g_Green);
		}
	}

	return D3D9DrawIndexedPrimitive(pDevice, PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
}

HRESULT __stdcall HookEndScene(IDirect3DDevice9* pDevice)
{
	if (lastPDevice != pDevice)
	{
		printf("pDevice from %p changed to %p ... Re hooking\n", lastPDevice, pDevice);

		GenerateTexture(pDevice, &g_Blue, D3DCOLOR_ARGB(255, 0, 0, 255));
		GenerateTexture(pDevice, &g_Green, D3DCOLOR_ARGB(255, 0, 255, 0));

		lastPDevice = pDevice;

		D3D9DrawIndexedPrimitive = (DrawIndexed_t) HookMethod(pDevice, HookDrawIndexed, 0x148);
	}

	return TrampolineByHook(HookEndScene)(pDevice);
}

HRESULT __stdcall HookPresent(IDirect3DDevice9* pDevice, const RECT* src, const RECT* dest, HWND wnd_override, const RGNDATA* dirty_region)
{
	if (lastPDevice != pDevice)
	{
		printf("pDevice from %p changed to %p ... Re hooking\n", lastPDevice, pDevice);

		GenerateTexture(pDevice, &g_Blue, D3DCOLOR_ARGB(255, 0, 0, 255));
		GenerateTexture(pDevice, &g_Green, D3DCOLOR_ARGB(255, 0, 255, 0));

		lastPDevice = pDevice;
		D3D9DrawIndexedPrimitive = (DrawIndexed_t) HookMethod(pDevice, HookDrawIndexed, 0x148);		
	}
	
	return TrampolineByHook(HookPresent)(pDevice, src, dest, wnd_override, dirty_region);
}

DWORD WINAPI loadThread(LPVOID lpParam)
{
	unsigned char* d3d9_module_base = (unsigned char*) GetModuleHandleA("d3d9.dll");
	unsigned char* gameoverlay_module_base = (unsigned char*) GetModuleHandleA("gameoverlayrenderer.dll");

	printf("d3d9.dll base: %08x\n", (uint32_t)d3d9_module_base);
	printf("gameoverlayrenderer.dll base: %08x\n", (uint32_t) gameoverlay_module_base);

	Direct3D9EndScene = (decltype(Direct3D9EndScene)) FindD3D9EndScene(d3d9_module_base, 0x18E000);
	Direct3D9Present = (decltype(Direct3D9Present)) FindGameoverlayPresent(gameoverlay_module_base, 0x187000);

	printf("IDirect3DDevice9::EndScene: %08x\n", (uint32_t) Direct3D9EndScene);
	printf("Gameoverlay::Present: %08llx\n", (uint64_t) Direct3D9Present);	

	if (USE_STEAM_GAMEOVERLAY)
	{
		printf("Hooking SteamGameOverlay::Present\n");
		Hook(Direct3D9Present, HookPresent);
	}
	else
	{
		printf("Hooking DirectX9::EndScene\n");
		Hook(Direct3D9EndScene, HookEndScene);
	}
	
	while (TRUE)
	{
		if (GetAsyncKeyState(VK_F4) & 1)
		{
			BlockPrimitives = !BlockPrimitives;
			printf("Toggled Block Primitives %s\n", BlockPrimitives == TRUE ? "[ON]" : "[OFF]");
		}

		if (GetAsyncKeyState(VK_F5) & 1)
		{
			PrimitiveIndex = 0;
			printf("Reset PrimitiveIndex to 0\n");
		}

		if (GetAsyncKeyState(VK_F6) & 1)
		{
			PrimitiveIndex = PrimitiveIndex - 1;
			printf("PrimitiveIndex: %d (%d)\n", PrimitiveIndex, PrimitiveIndexes[PrimitiveIndex]);
		}

		if (GetAsyncKeyState(VK_F7) & 1)
		{
			PrimitiveIndex = PrimitiveIndex + 1;
			printf("PrimitiveIndex: %d (%d)\n", PrimitiveIndex, PrimitiveIndexes[PrimitiveIndex]);
		}

		if (GetAsyncKeyState(VK_F10) & 1)
		{
			// Unload module

			HMODULE hModule = GetModuleHandleA("fingerprint.dll");

			if (hModule == NULL)
			{
				printf("Could not get handle to module fingerprint.dll\n");
				continue;
			}

			printf("Unloading fingerprint.dll\n");

			// Unhook D3D9::Present 
			Unhook(Direct3D9Present);
			// Restore method from device ptr VMT
			HookMethod(lastPDevice, D3D9DrawIndexedPrimitive, 0x148);

			fclose(stdout);

			if (!FreeConsole())
			{
				printf("Failed to release console\n");
				continue;
			}

			FreeLibraryAndExitThread(hModule, 0);
			break;
		}
	}

	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
		{
			DisableThreadLibraryCalls(hModule);

			if (!AllocConsole())
				MessageBoxA(NULL, "The console window was not created", NULL, MB_ICONEXCLAMATION);

			AttachConsole(GetProcessId(hModule));
			SetConsoleCtrlHandler(NULL, true);

			freopen("CON", "w", stdout);
			printf("Loading fingerprint.dll\n");

			CreateThread(0, 0, loadThread, 0, 0, 0);

			break;
		}
		
		case DLL_PROCESS_DETACH:
		{
			break;
		}
    }

    return TRUE;
}
