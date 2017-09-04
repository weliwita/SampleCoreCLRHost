#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <cstdlib>
#include <cstring>
#include <assert.h>
#include <dirent.h>
#include <dlfcn.h>
#include <limits.h>
#include <set>
#include <string>
#include <string.h>

#include "coreclrhost.h"
#ifndef SUCCEEDED
#define SUCCEEDED(Status) ((Status) >= 0)
#endif // !SUCCEEDED

void AddFilesFromDirectoryToTpaList(const char* directory, std::string& tpaList)
{
    const char * const tpaExtensions[] = {
                ".ni.dll",      // Probe for .ni.dll first so that it's preferred if ni and il coexist in the same dir
                ".dll",
                ".ni.exe",
                ".exe",
                };

    DIR* dir = opendir(directory);
    if (dir == nullptr)
    {
        return;
    }

    std::set<std::string> addedAssemblies;

    // Walk the directory for each extension separately so that we first get files with .ni.dll extension,
    // then files with .dll extension, etc.
    for (int extIndex = 0; extIndex < sizeof(tpaExtensions) / sizeof(tpaExtensions[0]); extIndex++)
    {
        const char* ext = tpaExtensions[extIndex];
        int extLength = strlen(ext);

        struct dirent* entry;

        // For all entries in the directory
        while ((entry = readdir(dir)) != nullptr)
        {
            // We are interested in files only
            switch (entry->d_type)
            {
            case DT_REG:
                break;

            // Handle symlinks and file systems that do not support d_type
            case DT_LNK:
            case DT_UNKNOWN:
                {
                    std::string fullFilename;

                    fullFilename.append(directory);
                    fullFilename.append("/");
                    fullFilename.append(entry->d_name);

                    struct stat sb;
                    if (stat(fullFilename.c_str(), &sb) == -1)
                    {
                        continue;
                    }

                    if (!S_ISREG(sb.st_mode))
                    {
                        continue;
                    }
                }
                break;

            default:
                continue;
            }

            std::string filename(entry->d_name);

            // Check if the extension matches the one we are looking for
            int extPos = filename.length() - extLength;
            if ((extPos <= 0) || (filename.compare(extPos, extLength, ext) != 0))
            {
                continue;
            }

            std::string filenameWithoutExt(filename.substr(0, extPos));

            // Make sure if we have an assembly with multiple extensions present,
            // we insert only one version of it.
            if (addedAssemblies.find(filenameWithoutExt) == addedAssemblies.end())
            {
                addedAssemblies.insert(filenameWithoutExt);

                tpaList.append(directory);
                tpaList.append("/");
                tpaList.append(filename);
                tpaList.append(":");
            }
        }
        
        // Rewind the directory stream to be able to iterate over it for the next extension
        rewinddir(dir);
    }
    
    closedir(dir);
}



int main(int argc, char* argv[])
{
    if (argc < 2)
	{
		printf("ERROR - Specify exe to run as the app's first parameter");
		return -1;
	}

    const char* clrFilesPath;
    const char* managedAssemblyPath;
    //const char** managedAssemblyArgv;
    //int managedAssemblyArgc;

    // get target app path rom argv
    managedAssemblyPath = argv[1];
    
    struct stat sb;
    if (stat(managedAssemblyPath, &sb) == -1)
    {
        perror("Managed assembly not found");
        return -1;
    }

    //
	// STEP 2: Find and load CoreCLR.dll
	//
    int exitCode = -1;
    std::string coreClrDllPath("/usr/share/dotnet/shared/Microsoft.NETCore.App/2.0.0-preview2-25407-01/libcoreclr.so");
    std::string clrFilesAbsolutePath("/usr/share/dotnet/shared/Microsoft.NETCore.App/2.0.0-preview2-25407-01");
    std::string appPath("/home/rasika/Work/github/SampleCoreCLRHost/SampleAppCore/bin/Debug/netcoreapp2.0/publish");

    std::string currentExeAbsolutePath("/home/rasika/Work/github/SampleCoreCLRHost/SampleUnixHost");


    void* coreclrLib = dlopen(coreClrDllPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (coreclrLib != nullptr)
    {
        coreclr_initialize_ptr initializeCoreCLR = (coreclr_initialize_ptr)dlsym(coreclrLib, "coreclr_initialize");
        coreclr_execute_assembly_ptr executeAssembly = (coreclr_execute_assembly_ptr)dlsym(coreclrLib, "coreclr_execute_assembly");
        coreclr_shutdown_2_ptr shutdownCoreCLR = (coreclr_shutdown_2_ptr)dlsym(coreclrLib, "coreclr_shutdown_2");
    


    std::string tpaList;
    std::string nativeDllSearchDirs(appPath);

    AddFilesFromDirectoryToTpaList(clrFilesAbsolutePath.c_str(), tpaList);

    const char* useServerGc = "false"; //GetEnvValueBoolean(serverGcVar);      
    const char* globalizationInvariant = "false"; //GetEnvValueBoolean(globalizationInvariantVar);

    const char *propertyKeys[] = {
                "TRUSTED_PLATFORM_ASSEMBLIES",
                "APP_PATHS",
                "APP_NI_PATHS",
                "NATIVE_DLL_SEARCH_DIRECTORIES",
                "System.GC.Server",
                "System.Globalization.Invariant",
    };

    const char *propertyValues[] = {
                // TRUSTED_PLATFORM_ASSEMBLIES
                tpaList.c_str(),
                // APP_PATHS
                appPath.c_str(),
                // APP_NI_PATHS
                appPath.c_str(),
                // NATIVE_DLL_SEARCH_DIRECTORIES
                nativeDllSearchDirs.c_str(),
                // System.GC.Server
                useServerGc,
                // System.Globalization.Invariant
                globalizationInvariant,
    };

    void* hostHandle;
    unsigned int domainId;

    int st = initializeCoreCLR(
                currentExeAbsolutePath.c_str(), 
                "unixcorerun", 
                sizeof(propertyKeys) / sizeof(propertyKeys[0]), 
                propertyKeys, 
                propertyValues, 
                &hostHandle, 
                &domainId);

    if (!SUCCEEDED(st))
    {
        fprintf(stderr, "coreclr_initialize failed - status: 0x%08x\n", st);
        exitCode = -1;
    }
    printf("initialized successfully/n");
    
    
    std::string managedAssemblyAbsolutePath("/home/rasika/Work/github/SampleCoreCLRHost/SampleAppCore/bin/Debug/netcoreapp2.0/SampleAppCore.dll");
    st = executeAssembly(
            hostHandle,
            domainId,
            0,
            nullptr,
            managedAssemblyAbsolutePath.c_str(),
            (unsigned int*)&exitCode);

    if (!SUCCEEDED(st))
    {
        fprintf(stderr, "coreclr_execute_assembly failed - status: 0x%08x\n", st);
        exitCode = -1;
    }

    printf("ran manageed successfully/n");

    //
	// STEP 3: Get ICLRRuntimeHost2 instance
	//


    //
	// STEP 5: Prepare properties for the AppDomain we will create
	//

    //
	// STEP 6: Create the default AppDomain
	//

    //
	// STEP 7: Run managed code
	//
    }
    


    return 0;
}