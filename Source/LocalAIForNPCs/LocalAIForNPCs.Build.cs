// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class LocalAIForNPCs : ModuleRules
{
	public LocalAIForNPCs(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        string FvadPath = Path.Combine(ModuleDirectory, "../ThirdParty/libfvad");
        string TenvadPath = Path.Combine(ModuleDirectory, "../ThirdParty/ten-vad");

        PublicIncludePaths.AddRange(
			new string[] {
                Path.Combine(FvadPath, "Include"),
                Path.Combine(TenvadPath, "Include")
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "AudioCaptureCore",
                "EnhancedInput",
                "UMG",
                "AudioPlatformConfiguration",
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
                "HTTP",
				"Json",
				"Sockets",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add(Path.Combine(FvadPath, "Lib/Win64/fvad.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(TenvadPath, "Lib/Win64/ten_vad.lib"));

            RuntimeDependencies.Add("$(BinaryOutputDir)/ten_vad.dll", Path.Combine(TenvadPath, "Lib/Win64/ten_vad.dll"));
        }

        if (Plugins.GetPlugin("NV_ACE_Reference") != null)
        {
            PublicDefinitions.Add("WITH_AUDIO2FACE=1");
            PrivateDependencyModuleNames.Add("ACERuntime");
            PrivateDependencyModuleNames.Add("ACECore");
        }
        else
        {
            PublicDefinitions.Add("WITH_AUDIO2FACE=0");
        }
    }
}
