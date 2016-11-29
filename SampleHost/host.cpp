
#include <stdio.h>
#include <stdlib.h>

#if WINDOWS
	#include <Windows.h>
	#include "mscoree.h"

	#define FS_SEPERATOR L"\\"
	#define PATH_DELIMITER L";"
	#define L(t) L##t
#else // WINDOWS
	// TODO -	To run cross-plat, more translation needs implemented here (ConvertToWSTR, for example).
	//			This is just a start.
	#include <string.h>
	#include <wchar.h>

	typedef WCHAR *LPWSTR;
	typedef unsigned int DWORD;

	#define FS_SEPERATOR L"/"
	#define PATH_DELIMITER L":"
	#define L(t) ConvertToWSTR(t)
#endif // WINDOWS

static const wchar_t *coreCLRInstallDirectory = L("%programfiles%\\dotnet\\shared\\Microsoft.NETCore.App\\1.0.1");
static const wchar_t* coreCLRDll = L("coreclr.dll"); // Main clr library to load



// Helper method to check for CoreCLR.dll in a given path and load it, if possible
HMODULE LoadCoreCLR(const wchar_t* directoryPath) 
{
	wchar_t coreDllPath[MAX_PATH];
	wcscpy_s(coreDllPath, MAX_PATH, directoryPath);
	wcscat_s(coreDllPath, MAX_PATH, FS_SEPERATOR);
	wcscat_s(coreDllPath, MAX_PATH, coreCLRDll);

#if WINDOWS
	HMODULE ret = LoadLibraryExW(coreDllPath, NULL, 0);
	if (!ret)
	{
		wprintf(L("WARNING - Failed to load %s\n"), coreDllPath);
	}
	return ret;
#else

#endif
}



// One uber-main method to keep the sample streamlined
int wmain(int argc, wchar_t* argv[])
{

	//
	// STEP 1: Get the app to run from the command line
	//
	if (argc < 2)
	{
		printf("ERROR - Specify exe to run as first parameter");
		return -1;
	}
	wchar_t targetApp[MAX_PATH];
	GetFullPathNameW(argv[1], MAX_PATH, targetApp, NULL);




	//
	// STEP 2: Find and load CoreCLR.dll
	//
	HMODULE coreCLRModule;

	// Look in %CoreRoot%
	wchar_t coreRoot[MAX_PATH];
	size_t outSize;
	_wgetenv_s(&outSize, coreRoot, MAX_PATH, L("CORE_ROOT"));
	coreCLRModule = LoadCoreCLR(coreRoot);

	// Look in common 1.0.1 install directory
	if (!coreCLRModule)
	{
		::ExpandEnvironmentStringsW(coreCLRInstallDirectory, coreRoot, MAX_PATH);
		coreCLRModule = LoadCoreCLR(coreRoot);
	}
	
	if (!coreCLRModule)
	{
		printf("ERROR - CoreCLR.dll could not be found");
		return -1;
	}




	//
	// STEP 3: Get ICLRRuntimeHost2 instance
	//
	ICLRRuntimeHost2* runtimeHost;

	FnGetCLRRuntimeHost pfnGetCLRRuntimeHost =
		(FnGetCLRRuntimeHost)::GetProcAddress(coreCLRModule, "GetCLRRuntimeHost");

	if (!pfnGetCLRRuntimeHost)
	{
		printf("ERROR - GetCLRRuntimeHost not found");
		return -1;
	}

	HRESULT hr = pfnGetCLRRuntimeHost(IID_ICLRRuntimeHost2, (IUnknown**)&runtimeHost);
	if (FAILED(hr))
	{
		printf("ERROR - Failed to get ICLRRuntimeHost2 instance.\nError code:%x\n", hr);
		return -1;
	}




	//
	// STEP 4: Set desired startup flags and start the CLR
	//
	hr = runtimeHost->SetStartupFlags(
		static_cast<STARTUP_FLAGS>(
			// STARTUP_FLAGS::STARTUP_SERVER_GC |						// Use server GC
			// STARTUP_FLAGS::STARTUP_LOADER_OPTIMIZATION_MULTI_DOMAIN |	// Maximize domain-neutral loading
			// STARTUP_FLAGS::STARTUP_LOADER_OPTIMIZATION_MULTI_DOMAIN_HOST | // Domain-neutral loading for strongly-named assemblies
			STARTUP_FLAGS::STARTUP_CONCURRENT_GC |						// Use concurrent GC
			STARTUP_FLAGS::STARTUP_SINGLE_APPDOMAIN |					// All code executes in the default app-domain
			STARTUP_FLAGS::STARTUP_LOADER_OPTIMIZATION_SINGLE_DOMAIN	// Prevents domain-neutral loading
		)
	);
	if (FAILED(hr))
	{
		printf("ERROR - Failed to set startup flags.\nError code:%x\n", hr);
		return -1;
	}

	hr = runtimeHost->Start();
	if (FAILED(hr))
	{
		printf("ERROR - Failed to start the runtime.\nError code:%x\n", hr);
		return -1;
	}




	//
	// STEP 5: Prepare properties for the AppDomain we will create
	//

	// Flags
	int appDomainFlags =
		// APPDOMAIN_FORCE_TRIVIAL_WAIT_OPERATIONS |		// Do not pump messages during wait
		// APPDOMAIN_SECURITY_SANDBOXED |					// Causes assemblies not from the TPA list to be loaded as partially trusted
		APPDOMAIN_ENABLE_PLATFORM_SPECIFIC_APPS |			// Enable platform-specific assemblies to run
		APPDOMAIN_ENABLE_PINVOKE_AND_CLASSIC_COMINTEROP |	// Allow PInvoking from non-TPA assemblies
		APPDOMAIN_DISABLE_TRANSPARENCY_ENFORCEMENT;			// Disables transparency checks


	// TRUSTED_PLATFORM_ASSEMBLIES
	// "Trusted Platform Assemblies" are prioritized by the loader and always loaded with full trust
	// A good default is to include any assemblies next to CoreCLR.dll as platform assemblies

	int tpaSize = 100 * MAX_PATH; // Starting size for our TPA list
	wchar_t* trustedPlatformAssemblies = new wchar_t[tpaSize];
	trustedPlatformAssemblies[0] = L('\0');

	wchar_t *tpaExtensions[] = {
		L("*.ni.dll"),		
		L("*.dll"),
		L("*.ni.exe"),
		L("*.exe"),
		L("*.ni.winmd")
		L("*.winmd")
	};

	// Probe next to CoreCLR.dll for any files matching the extensions from tpaExtensions and
	// add them to the TPA list. In a real host, this would likely be extracted into a separate function
	// and may be run on other directories of interest.

	for (int i = 0; i < _countof(tpaExtensions); i++)
	{
		wchar_t searchPath[MAX_PATH];
		wcscpy_s(searchPath, MAX_PATH, coreRoot);
		wcscat_s(searchPath, MAX_PATH, FS_SEPERATOR);
		wcscat_s(searchPath, MAX_PATH, tpaExtensions[i]);

		WIN32_FIND_DATAW findData;
		HANDLE fileHandle = FindFirstFileW(searchPath, &findData);

		if (fileHandle != INVALID_HANDLE_VALUE)
		{
			do
			{
				wchar_t pathToAdd[MAX_PATH];
				wcscpy_s(pathToAdd, MAX_PATH, coreRoot);
				wcscat_s(pathToAdd, MAX_PATH, FS_SEPERATOR);
				wcscat_s(pathToAdd, MAX_PATH, findData.cFileName);

				// Check to see if TPA list needs expanded
				if (wcslen(pathToAdd) + (3) + wcslen(trustedPlatformAssemblies) >= tpaSize)
				{
					tpaSize *= 2;
					wchar_t* newTPAList = new wchar_t[tpaSize];
					wcscpy_s(newTPAList, tpaSize, trustedPlatformAssemblies);
					trustedPlatformAssemblies = newTPAList;
				}

				wcscat_s(trustedPlatformAssemblies, tpaSize, pathToAdd);
				wcscat_s(trustedPlatformAssemblies, tpaSize, PATH_DELIMITER);
			}
			while (FindNextFileW(fileHandle, &findData));
			FindClose(fileHandle);
		}
	}


	// APP_PATHS
	// App paths are directories to probe in for assemblies which are not one of the well-known Framework assemblies
	// included in the TPA list.

	// For this simple sample, we just include the directory the exe is in.
	// More complex hosts may want to also check the current working directory or other
	// locations known to contain application assets.
	wchar_t appPaths[MAX_PATH * 50];

	// Just use the targetApp provided by the user and remove the file name
	wcscpy_s(appPaths, targetApp);

	int i = wcslen(appPaths - 1);
	while (i >= 0 && appPaths[i] != FS_SEPERATOR[0]) i--;
	appPaths[i] = L('\0');


	// APP_NI_PATHS
	// App (NI) paths are the paths that will be probed for native images not found on the TPA list. It will typically be similar
	// to the app paths.
	wchar_t appNiPaths[MAX_PATH * 50];
	wcscpy_s(appNiPaths, appPaths);
	wcscat_s(appNiPaths, MAX_PATH * 50, PATH_DELIMITER);
	wcscat_s(appNiPaths, MAX_PATH * 50, appPaths);
	wcscat_s(appNiPaths, MAX_PATH * 50, L("NI"));


	// NATIVE_DLL_SEARCH_DIRECTORIES
	// Native dll search directories are paths that the runtime will probe for native DLLs called via PInvoke
	wchar_t nativeDllSearchDirectories[MAX_PATH * 50];
	wcscpy_s(nativeDllSearchDirectories, appPaths);
	wcscat_s(nativeDllSearchDirectories, MAX_PATH * 50, PATH_DELIMITER);
	wcscat_s(nativeDllSearchDirectories, MAX_PATH * 50, coreRoot);
	wcscat_s(nativeDllSearchDirectories, MAX_PATH * 50, PATH_DELIMITER);

	wchar_t systemRoot[MAX_PATH];
	::ExpandEnvironmentStringsW(L("%SystemRoot%\System32"), systemRoot, MAX_PATH);

	wcscat_s(nativeDllSearchDirectories, MAX_PATH * 50, systemRoot);


	// PLATFORM_RESOURCE_ROOTS
	// Platform resource roots are paths to probe in for resource assemblies (in culture-specific sub-directories)
	wchar_t platformResourceRoots[MAX_PATH * 50];
	wcscpy_s(platformResourceRoots, appPaths);


	// AppDomainCompatSwitch
	// Specifies compatibility behavior for the app domain
	wchar_t* appDomainCompatSwitch = L("UseLatestBehaviorWhenTFMNotSpecified");




	//
	// STEP 6: Create the default AppDomain
	//
	DWORD domainId;

	const wchar_t* propertyKeys[] = {
		L("TRUSTED_PLATFORM_ASSEMBLIES"),
		L("APP_PATHS"),
		L("APP_NI_PATHS"),
		L("NATIVE_DLL_SEARCH_DIRECTORIES"),
		L("PLATFORM_RESOURCE_ROOTS"),
		L("AppDomainCompatSwitch")
	};

	const wchar_t* propertyValues[] = {
		trustedPlatformAssemblies,
		appPaths,
		appNiPaths,
		nativeDllSearchDirectories,
		platformResourceRoots,
		appDomainCompatSwitch
	};

	hr = runtimeHost->CreateAppDomainWithManager(
		L("Sample Host AppDomain"),		// Friendly AD name
		appDomainFlags,
		NULL,							// Optional AppDomain manager assembly name
		NULL,							// Optional AppDomain manager type (including namespace)
		sizeof(propertyKeys)/sizeof(wchar_t*),
		propertyKeys,
		propertyValues,
		&domainId);

	if (FAILED(hr))
	{
		printf("ERROR - Failed to create AppDomain.\nError code:%x\n", hr);
		return -1;
	}




	//
	// STEP 7: Run managed code
	//
	DWORD exitCode = -1;

	printf("Executing managed code...\n\n");
	hr = runtimeHost->ExecuteAssembly(domainId, targetApp, argc - 1, (LPCWSTR*)(argc > 1 ? &argv[1] : NULL), &exitCode);

	if (FAILED(hr))
	{
		printf("ERROR - Failed to execute %s.\nError code:%x\n", targetApp, hr);
		return -1;
	}
	// TODO - Mention running a method instead of an exe




	//
	// STEP 8: Clean up
	//
	runtimeHost->UnloadAppDomain(domainId, true /* Wait until unload complete */);
	runtimeHost->Stop();
	runtimeHost->Release();


	printf("\nDone\n");

	return exitCode;
}