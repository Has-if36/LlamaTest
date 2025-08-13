// Copyright 2025-current Getnamo, 2022-23 Mika Pi.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnrealBuildTool;

public class LlamaCore : ModuleRules
{
	private string PluginBinariesPath
	{
		get { return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../Binaries")); }
	}

	private string LlamaCppLibPath
	{
		get { return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/LlamaCpp/Lib")); }
	}

	private string LlamaCppBinariesPath
	{
		get { return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/LlamaCpp/Binaries")); }
	}

	private string LlamaCppIncludePath
	{
		get { return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/LlamaCpp/Include")); }
	}

	private string HnswLibIncludePath
	{
		get { return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/HnswLib/Include")); }
	}

	private void LinkDyLib(string DyLib)
	{
		string MacPlatform = "Mac";
		PublicAdditionalLibraries.Add(Path.Combine(LlamaCppLibPath, MacPlatform, DyLib));
		PublicDelayLoadDLLs.Add(Path.Combine(LlamaCppLibPath, MacPlatform, DyLib));
		RuntimeDependencies.Add(Path.Combine(LlamaCppLibPath, MacPlatform, DyLib));
	}

	// 512MB Chunk Size
	private void SplitData(string filePath, string outputPath = "", bool bIntoFolder = false, long chunkSize = 512 * 1024 * 1024)
	{
		if (!File.Exists(filePath))
		{
            System.Console.WriteLine("Warning: File " + filePath + " not found");
            return;
        }

        string filename = Path.GetFileName(filePath);
		string filenameBase = Path.GetFileNameWithoutExtension(filePath);
		string baseOutPath = string.IsNullOrEmpty(outputPath) ? filePath : outputPath;

        byte[] buffer = new byte[chunkSize];
        using (var fs = File.OpenRead(filePath))
        {
            int partNum = 1;
            while (true)
            {
                int bytesRead = fs.Read(buffer, 0, buffer.Length);
                if (bytesRead == 0) break;
				string finalOutputPath = baseOutPath;

                // Can comment if not necessary. Store each part in seperate folder
                if (bIntoFolder) {
                    finalOutputPath = Path.Combine(finalOutputPath, $"{filenameBase}-part{partNum}");
                    Directory.CreateDirectory(finalOutputPath);
                }

                finalOutputPath = Path.Combine(finalOutputPath, filename);
                string partPath = $"{finalOutputPath}.part{partNum++}";
                File.WriteAllBytes(partPath, buffer.Take(bytesRead).ToArray());
            }
        }
    }

	public LlamaCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        	PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);


		PrivateIncludePaths.AddRange(
			new string[] {
			}
			);


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
            }
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				// ... add private dependencies that you statically link with here ...
				"PakFile",
            }
			);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd"
				}
			);
		}

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
		);

		//Includes
		PublicIncludePaths.Add(LlamaCppIncludePath);
		PublicIncludePaths.Add(HnswLibIncludePath);

        string[] modelName = new string[] { "DeepSeek-R1-Distill-Qwen-1-5B-Q4_K_M.gguf" };
        
        PublicDefinitions.Add($"LLAMA_MODELS=\"{string.Join("|", modelName)}\"");
        string modelPath = Path.Combine(PluginDirectory, "Content");

        // Definition to map pak file index range specifically for split models (wrapped into pak files)
        // Note: Only affects within plugin, not global
        PublicDefinitions.Add($"LLAMA_PAK_MIN_INDEX=11");
        PublicDefinitions.Add($"LLAMA_PAK_MAX_INDEX=20");

        // Splits Model
        foreach (string eachModel in modelName)
        {
            string OutputDir = Path.Combine(PluginDirectory, "../../", "Content", "Resources", "Models");
            SplitData(Path.Combine(modelPath, eachModel), OutputDir, true, 500 * 1024 * 1024);
        }

        // After splitting, Go to Project Settings -> Project -> Packaging -> Additional Non-Asset Directories To Copy, add all split folders
        // Make sure each folder (index array) is under 2GB as this is the size limit for each part
        // Android AAB: Higher size has risk of failing to export due to limited Allocated Memory from Java

        if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			//NB: Currently not working for b4879

			PublicAdditionalLibraries.Add(Path.Combine(LlamaCppLibPath, "Linux", "libllama.so"));
		} 
		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string Win64LibPath = Path.Combine(LlamaCppLibPath, "Windows", "x64");
			string CudaPath;

			//We default to vulkan build, turn this off if you want to build with CUDA/cpu only
			bool bTryToUseVulkan = true;
			bool bVulkanGGMLFound = false;

			//Toggle this off if you don't want to include the cuda backend	
			bool bTryToUseCuda = false;
			bool bCudaGGMLFound = false;
			bool bCudaFound = false;

			if(bTryToUseVulkan)
			{
				bVulkanGGMLFound = File.Exists(Path.Combine(Win64LibPath, "ggml-vulkan.lib"));
			}
			if(bTryToUseCuda)
			{
				bCudaGGMLFound = File.Exists(Path.Combine(Win64LibPath, "ggml-cuda.lib"));

				if(bCudaGGMLFound)
				{
					//Almost every dev setup has a CUDA_PATH so try to load cuda in plugin path first;
					//these won't exist unless you're in plugin 'cuda' branch.
					CudaPath = Win64LibPath;

					//Test to see if we have a cuda.lib
					bCudaFound = File.Exists(Path.Combine(Win64LibPath, "cuda.lib"));

					if (!bCudaFound)
					{
						//local cuda not found, try environment path
						CudaPath = Path.Combine(Environment.GetEnvironmentVariable("CUDA_PATH"), "lib", "x64");
						bCudaFound = !string.IsNullOrEmpty(CudaPath);
					}

					if (bCudaFound)
					{
						System.Console.WriteLine("Llama-Unreal building using CUDA dependencies at path " + CudaPath);
					}
				}
			}

			//If you specify LLAMA_PATH, it will take precedence over local path for libs
			string LlamaLibPath = Environment.GetEnvironmentVariable("LLAMA_PATH");
			string LlamaDllPath = LlamaLibPath;
			bool bUsingLlamaEnvPath = !string.IsNullOrEmpty(LlamaLibPath);

			if (!bUsingLlamaEnvPath) 
			{
				LlamaLibPath = Win64LibPath;
				LlamaDllPath = Path.Combine(LlamaCppBinariesPath, "Windows", "x64");
			}

			PublicAdditionalLibraries.Add(Path.Combine(LlamaLibPath, "llama.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LlamaLibPath, "ggml.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LlamaLibPath, "ggml-base.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LlamaLibPath, "ggml-cpu.lib"));

			PublicAdditionalLibraries.Add(Path.Combine(LlamaLibPath, "common.lib"));

			RuntimeDependencies.Add("$(BinaryOutputDir)/ggml.dll", Path.Combine(LlamaDllPath, "ggml.dll"));
			RuntimeDependencies.Add("$(BinaryOutputDir)/ggml-base.dll", Path.Combine(LlamaDllPath, "ggml-base.dll"));
			RuntimeDependencies.Add("$(BinaryOutputDir)/ggml-cpu.dll", Path.Combine(LlamaDllPath, "ggml-cpu.dll"));
			RuntimeDependencies.Add("$(BinaryOutputDir)/llama.dll", Path.Combine(LlamaDllPath, "llama.dll"));

			//System.Console.WriteLine("Llama-Unreal building using llama.lib at path " + LlamaLibPath);

			if(bVulkanGGMLFound)
			{
				PublicAdditionalLibraries.Add(Path.Combine(Win64LibPath, "ggml-vulkan.lib"));
				RuntimeDependencies.Add("$(BinaryOutputDir)/ggml-vulkan.dll", Path.Combine(LlamaDllPath, "ggml-vulkan.dll"));
				//PublicDelayLoadDLLs.Add("ggml-vulkan.dll");
				//System.Console.WriteLine("Llama-Unreal building using ggml-vulkan.lib at path " + Win64LibPath);
			}
			if(bCudaGGMLFound)
			{
				PublicAdditionalLibraries.Add(Path.Combine(Win64LibPath, "ggml-cuda.lib"));
				RuntimeDependencies.Add("$(BinaryOutputDir)/ggml-cuda.dll", Path.Combine(LlamaDllPath, "ggml-cuda.dll"));
				RuntimeDependencies.Add("$(BinaryOutputDir)/cublas64_12.dll", Path.Combine(LlamaDllPath, "cublas64_12.dll"));
				RuntimeDependencies.Add("$(BinaryOutputDir)/cublasLt64_12.dll", Path.Combine(LlamaDllPath, "cublasLt64_12.dll"));
				RuntimeDependencies.Add("$(BinaryOutputDir)/cudart64_12.dll", Path.Combine(LlamaDllPath, "cudart64_12.dll"));
				//PublicDelayLoadDLLs.Add("ggml-cuda.dll");
				//System.Console.WriteLine("Llama-Unreal building using ggml-cuda.lib at path " + Win64LibPath);
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			//NB: Currently not working for b4879

			PublicAdditionalLibraries.Add(Path.Combine(PluginDirectory, "Libraries", "Mac", "libggml_static.a"));
			
			//Dylibs act as both, so include them, add as lib and add as runtime dep
			LinkDyLib("libllama.dylib");
			LinkDyLib("libggml_shared.dylib");
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
            string manifestFile = Path.Combine(ModuleDirectory, "AndroidManifest_UPL.xml");
            AdditionalPropertiesForReceipt.Add("AndroidPlugin", manifestFile);

            PrivateDependencyModuleNames.AddRange(new string[] {
                "Launch",
                "AndroidRuntimeSettings"
            });

            string llamaCppPath = Path.GetFullPath(Path.Combine(PluginDirectory, "ThirdParty", "LlamaCpp"));
            string binPath = Path.GetFullPath(Path.Combine(llamaCppPath, "Binaries", "Android"));
            string libPath = Path.GetFullPath(Path.Combine(llamaCppPath, "Lib", "Android"));
            System.Console.WriteLine("Llama dependancies Path: " + llamaCppPath);

            Dictionary<UnrealArch, string> archs = new Dictionary<UnrealArch, string>() {
				{ UnrealArch.X64, "x86_64" },
				{ UnrealArch.Arm64, "arm64-v8a" }
            };

			foreach (var arch in archs)
			{
                System.Console.WriteLine($"Arch: {arch.Value}");

                string binArchPath = Path.Combine(binPath, arch.Value);
                string libArchPath = Path.Combine(libPath, arch.Value);

                //NB: Currently not working for b4879

                PublicAdditionalLibraries.Add(Path.Combine(libArchPath, "libllama.a"));
                PublicAdditionalLibraries.Add(Path.Combine(libArchPath, "libggml.a"));
                PublicAdditionalLibraries.Add(Path.Combine(libArchPath, "libggml-base.a"));
                PublicAdditionalLibraries.Add(Path.Combine(libArchPath, "libggml-cpu.a"));
				
                PublicAdditionalLibraries.Add(Path.Combine(libArchPath, "libcommon.a"));

				string archStr = arch.Key.ToString();
                RuntimeDependencies.Add(Path.Combine("$(BinaryOutputDir)", archStr, "libllama.so"), Path.Combine(binArchPath, "libllama.so"));
                RuntimeDependencies.Add(Path.Combine("$(BinaryOutputDir)", archStr, "libggml.so"), Path.Combine(binArchPath, "libggml.so"));
                RuntimeDependencies.Add(Path.Combine("$(BinaryOutputDir)", archStr, "libggml-base.so"), Path.Combine(binArchPath, "libggml-base.so"));
                RuntimeDependencies.Add(Path.Combine("$(BinaryOutputDir)", archStr, "libggml-cpu.so"), Path.Combine(binArchPath, "libggml-cpu.so"));

                //We default to vulkan build, turn this off if you want to build with CUDA/cpu only
                bool bTryToUseVulkan = true;
                bool bVulkanGGMLFound = false;

                //We default to vulkan build, turn this off if you want to build with CUDA/cpu only
                bool bTryToUseOpenCL = true;
                bool bOpenCLGGMLFound = false;

                bVulkanGGMLFound = bTryToUseVulkan && File.Exists(Path.Combine(binArchPath, "libggml-vulkan.so"));
                bOpenCLGGMLFound = bTryToUseOpenCL && File.Exists(Path.Combine(binArchPath, "libggml-opencl.so"));

                if (bVulkanGGMLFound)
                {
                    System.Console.WriteLine("Found Vulkan Dependancy");

                    // Set #pragma weak ggml_backend_vk_reg in ggml-vulkan.h

                    // Add this in [Project].Target.cs if using Vulkan for android
                    //if (Target.Platform == UnrealTargetPlatform.Android)
                    //{
                    //    if (AdditionalLinkerArguments is null)
                    //        AdditionalLinkerArguments = "-Wl,--undefined=ggml_backend_vk_reg";
                    //    else
                    //        AdditionalLinkerArguments += " -Wl,--undefined=ggml_backend_vk_reg";
                    //}

                    PublicAdditionalLibraries.Add(Path.Combine(libArchPath, "libggml-vulkan.a"));
                    RuntimeDependencies.Add(Path.Combine("$(BinaryOutputDir)", archStr, "libggml-vulkan.so"), Path.Combine(binArchPath, "libggml-vulkan.so"));
                }
                if (bOpenCLGGMLFound)
                {
                    System.Console.WriteLine("Found OpenCL Dependancy");
                    PublicAdditionalLibraries.Add(Path.Combine(libArchPath, "libggml-opencl.a"));
                    RuntimeDependencies.Add(Path.Combine("$(BinaryOutputDir)", archStr, "libggml-opencl.so"), Path.Combine(binArchPath, "libggml-opencl.so"));
                }
            }
        }
	}
}
