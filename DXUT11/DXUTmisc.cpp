//--------------------------------------------------------------------------------------
// File: DXUTMisc.cpp
//
// Shortcut macros and functions for using DX objects
//
// Copyright (c) Microsoft Corporation. All rights reserved
//--------------------------------------------------------------------------------------
#include "dxut.h"
#include <xinput.h>


//--------------------------------------------------------------------------------------
// Direct3D9 dynamic linking support -- calls top-level D3D9 APIs with graceful
// failure if APIs are not present.
//--------------------------------------------------------------------------------------

// Function prototypes
typedef INT         (WINAPI * LPD3DPERF_BEGINEVENT)(D3DCOLOR, LPCWSTR);
typedef INT         (WINAPI * LPD3DPERF_ENDEVENT)(void);
typedef VOID        (WINAPI * LPD3DPERF_SETMARKER)(D3DCOLOR, LPCWSTR);
typedef VOID        (WINAPI * LPD3DPERF_SETREGION)(D3DCOLOR, LPCWSTR);
typedef BOOL        (WINAPI * LPD3DPERF_QUERYREPEATFRAME)(void);
typedef VOID        (WINAPI * LPD3DPERF_SETOPTIONS)( DWORD dwOptions );
typedef DWORD       (WINAPI * LPD3DPERF_GETSTATUS)( void );
typedef HRESULT     (WINAPI * LPCREATEDXGIFACTORY)(REFIID, void ** );
typedef HRESULT     (WINAPI * LPD3D11CREATEDEVICE)( IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT32, D3D_FEATURE_LEVEL*, UINT, UINT32, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext** );

// Module and function pointers
static HMODULE                              s_hModDXGI = NULL;
static LPCREATEDXGIFACTORY                  s_DynamicCreateDXGIFactory = NULL;
static HMODULE                              s_hModD3D11 = NULL;
static LPD3D11CREATEDEVICE                  s_DynamicD3D11CreateDevice = NULL;

bool DXUT_EnsureD3D11APIs( void )
{
    // If both modules are non-NULL, this function has already been called.  Note
    // that this doesn't guarantee that all ProcAddresses were found.
    if( s_hModD3D11 != NULL && s_hModDXGI != NULL )
        return true;

    // This may fail if Direct3D 11 isn't installed
    s_hModD3D11 = LoadLibrary( L"d3d11.dll" );
    if( s_hModD3D11 != NULL )
    {
        s_DynamicD3D11CreateDevice = ( LPD3D11CREATEDEVICE )GetProcAddress( s_hModD3D11, "D3D11CreateDevice" );
    }

    if( !s_DynamicCreateDXGIFactory )
    {
        s_hModDXGI = LoadLibrary( L"dxgi.dll" );
        if( s_hModDXGI )
        {
            s_DynamicCreateDXGIFactory = ( LPCREATEDXGIFACTORY )GetProcAddress( s_hModDXGI, "CreateDXGIFactory1" );
        }

        return ( s_hModDXGI != NULL ) && ( s_hModD3D11 != NULL );
    }

    return ( s_hModD3D11 != NULL );
}

HRESULT WINAPI DXUT_Dynamic_CreateDXGIFactory1( REFIID rInterface, void** ppOut )
{
    if( DXUT_EnsureD3D11APIs() && s_DynamicCreateDXGIFactory != NULL )
        return s_DynamicCreateDXGIFactory( rInterface, ppOut );
    else
        return DXUTERR_NODIRECT3D11;
}



HRESULT WINAPI DXUT_Dynamic_D3D11CreateDevice( IDXGIAdapter* pAdapter,
                                               D3D_DRIVER_TYPE DriverType,
                                               HMODULE Software,
                                               UINT32 Flags,
                                               D3D_FEATURE_LEVEL* pFeatureLevels,
                                               UINT FeatureLevels,
                                               UINT32 SDKVersion,
                                               ID3D11Device** ppDevice,
                                               D3D_FEATURE_LEVEL* pFeatureLevel,
                                               ID3D11DeviceContext** ppImmediateContext )
{
    if( DXUT_EnsureD3D11APIs() && s_DynamicD3D11CreateDevice != NULL )
        return s_DynamicD3D11CreateDevice( pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
                                           SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext );
    else
        return DXUTERR_NODIRECT3D11;
}

//--------------------------------------------------------------------------------------
// Multimon API handling for OSes with or without multimon API support
//--------------------------------------------------------------------------------------
#define DXUT_PRIMARY_MONITOR ((HMONITOR)0x12340042)
typedef HMONITOR ( WINAPI* LPMONITORFROMWINDOW )( HWND, DWORD );
typedef BOOL ( WINAPI* LPGETMONITORINFO )( HMONITOR, LPMONITORINFO );
typedef HMONITOR ( WINAPI* LPMONITORFROMRECT )( LPCRECT lprcScreenCoords, DWORD dwFlags );

BOOL WINAPI DXUTGetMonitorInfo( HMONITOR hMonitor, LPMONITORINFO lpMonitorInfo )
{
    static bool s_bInited = false;
    static LPGETMONITORINFO s_pFnGetMonitorInfo = NULL;
    if( !s_bInited )
    {
        s_bInited = true;
        HMODULE hUser32 = GetModuleHandle( L"USER32" );
        if( hUser32 )
        {
            OSVERSIONINFOA osvi =
            {
                0
            }; osvi.dwOSVersionInfoSize = sizeof( osvi ); GetVersionExA( ( OSVERSIONINFOA* )&osvi );
            bool bNT = ( VER_PLATFORM_WIN32_NT == osvi.dwPlatformId );
            s_pFnGetMonitorInfo = ( LPGETMONITORINFO )( bNT ? GetProcAddress( hUser32,
                                                                              "GetMonitorInfoW" ) :
                                                        GetProcAddress( hUser32, "GetMonitorInfoA" ) );
        }
    }

    if( s_pFnGetMonitorInfo )
        return s_pFnGetMonitorInfo( hMonitor, lpMonitorInfo );

    RECT rcWork;
    if( ( hMonitor == DXUT_PRIMARY_MONITOR ) && lpMonitorInfo && ( lpMonitorInfo->cbSize >= sizeof( MONITORINFO ) ) &&
        SystemParametersInfoA( SPI_GETWORKAREA, 0, &rcWork, 0 ) )
    {
        lpMonitorInfo->rcMonitor.left = 0;
        lpMonitorInfo->rcMonitor.top = 0;
        lpMonitorInfo->rcMonitor.right = GetSystemMetrics( SM_CXSCREEN );
        lpMonitorInfo->rcMonitor.bottom = GetSystemMetrics( SM_CYSCREEN );
        lpMonitorInfo->rcWork = rcWork;
        lpMonitorInfo->dwFlags = MONITORINFOF_PRIMARY;
        return TRUE;
    }
    return FALSE;
}


HMONITOR WINAPI DXUTMonitorFromWindow( HWND hWnd, DWORD dwFlags )
{
    static bool s_bInited = false;
    static LPMONITORFROMWINDOW s_pFnGetMonitorFromWindow = NULL;
    if( !s_bInited )
    {
        s_bInited = true;
        HMODULE hUser32 = GetModuleHandle( L"USER32" );
        if( hUser32 ) s_pFnGetMonitorFromWindow = ( LPMONITORFROMWINDOW )GetProcAddress( hUser32,
                                                                                         "MonitorFromWindow" );
    }

    if( s_pFnGetMonitorFromWindow )
        return s_pFnGetMonitorFromWindow( hWnd, dwFlags );
    else
        return DXUT_PRIMARY_MONITOR;
}


HMONITOR WINAPI DXUTMonitorFromRect( LPCRECT lprcScreenCoords, DWORD dwFlags )
{
    static bool s_bInited = false;
    static LPMONITORFROMRECT s_pFnGetMonitorFromRect = NULL;
    if( !s_bInited )
    {
        s_bInited = true;
        HMODULE hUser32 = GetModuleHandle( L"USER32" );
        if( hUser32 ) s_pFnGetMonitorFromRect = ( LPMONITORFROMRECT )GetProcAddress( hUser32, "MonitorFromRect" );
    }

    if( s_pFnGetMonitorFromRect )
        return s_pFnGetMonitorFromRect( lprcScreenCoords, dwFlags );
    else
        return DXUT_PRIMARY_MONITOR;
}


//--------------------------------------------------------------------------------------
// Get the desktop resolution of an adapter. This isn't the same as the current resolution 
// from GetAdapterDisplayMode since the device might be fullscreen 
//--------------------------------------------------------------------------------------
void WINAPI DXUTGetDesktopResolution( UINT AdapterOrdinal, UINT* pWidth, UINT* pHeight )
{
    DXUTDeviceSettings DeviceSettings = DXUTGetDeviceSettings();

    WCHAR strDeviceName[256] = {0};
    DEVMODE devMode;
    ZeroMemory( &devMode, sizeof( DEVMODE ) );
    devMode.dmSize = sizeof( DEVMODE );

    CD3D11Enumeration* pd3dEnum = DXUTGetD3D11Enumeration();
    assert( pd3dEnum != NULL );
    CD3D11EnumOutputInfo* pOutputInfo = pd3dEnum->GetOutputInfo( AdapterOrdinal, DeviceSettings.d3d11.Output );
    if( pOutputInfo )
    {
        wcscpy_s( strDeviceName, 256, pOutputInfo->Desc.DeviceName );
    }

    EnumDisplaySettings( strDeviceName, ENUM_REGISTRY_SETTINGS, &devMode );
    if( pWidth )
        *pWidth = devMode.dmPelsWidth;
    if( pHeight )
        *pHeight = devMode.dmPelsHeight;
}


typedef DWORD ( WINAPI* LPXINPUTGETSTATE )( DWORD dwUserIndex, XINPUT_STATE* pState );
typedef DWORD ( WINAPI* LPXINPUTSETSTATE )( DWORD dwUserIndex, XINPUT_VIBRATION* pVibration );
typedef DWORD ( WINAPI* LPXINPUTGETCAPABILITIES )( DWORD dwUserIndex, DWORD dwFlags,
                                                   XINPUT_CAPABILITIES* pCapabilities );
typedef void ( WINAPI* LPXINPUTENABLE )( BOOL bEnable );

//-------------------------------------------------------------------------------------- 
HRESULT DXUTSnapD3D11Screenshot( LPCTSTR szFileName, D3DX11_IMAGE_FILE_FORMAT iff )
{
    IDXGISwapChain *pSwap = DXUTGetDXGISwapChain();

    if (!pSwap)
        return E_FAIL;
    
    ID3D11Texture2D* pBackBuffer;
    HRESULT hr = pSwap->GetBuffer( 0, __uuidof( *pBackBuffer ), ( LPVOID* )&pBackBuffer );
    if (hr != S_OK)
        return hr;

    ID3D11DeviceContext *dc  = DXUTGetD3D11DeviceContext();
    if (!dc) {
        SAFE_RELEASE(pBackBuffer);
        return E_FAIL;
    }
    ID3D11Device *pDevice = DXUTGetD3D11Device();
    if (!dc) {
        SAFE_RELEASE(pBackBuffer);
        return E_FAIL;
    }

    D3D11_TEXTURE2D_DESC dsc;
    pBackBuffer->GetDesc(&dsc);
    D3D11_RESOURCE_DIMENSION dim;
    pBackBuffer->GetType(&dim);
    // special case msaa textures
    ID3D11Texture2D *pCompatableTexture = pBackBuffer;
    if ( dsc.SampleDesc.Count > 1) {
        D3D11_TEXTURE2D_DESC dsc_new = dsc;
        dsc_new.SampleDesc.Count = 1;
        dsc_new.SampleDesc.Quality = 0;
        dsc_new.Usage = D3D11_USAGE_DEFAULT;
        dsc_new.BindFlags = 0;
        dsc_new.CPUAccessFlags = 0;
        ID3D11Texture2D *resolveTexture;
        hr = pDevice->CreateTexture2D(&dsc_new, NULL, &resolveTexture);
        if ( SUCCEEDED(hr) )
        {
            dc->ResolveSubresource(resolveTexture, 0, pBackBuffer, 0, dsc.Format);
            pCompatableTexture = resolveTexture;
        }
        pCompatableTexture->GetDesc(&dsc);
    }

    hr = D3DX11SaveTextureToFileW(dc, pCompatableTexture, iff, szFileName); 
        
    SAFE_RELEASE(pBackBuffer);
    SAFE_RELEASE(pCompatableTexture);

    return hr;

}
