using UnrealBuildTool;

public class FPSAnimConnect : ModuleRules
{
	public FPSAnimConnect(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AnimGraph",
			"AnimGraphRuntime",
			"BlueprintGraph",
			"Kismet",
			"KismetCompiler",
			"AssetTools",
			"EditorScriptingUtilities"
		});
	}
}
