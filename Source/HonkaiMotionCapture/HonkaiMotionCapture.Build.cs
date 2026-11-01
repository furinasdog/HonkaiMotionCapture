using System.IO;
using UnrealBuildTool;

public class HonkaiMotionCapture : ModuleRules
{
    public HonkaiMotionCapture(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        if (Target.Platform != UnrealTargetPlatform.Win64 || Target.Architecture == UnrealArch.Arm64)
            throw new BuildException("HonkaiMotionCapture currently requires Windows x64.");
        bEnableExceptions = true;
        PublicDependencyModuleNames.Add("Core");
        PrivateDependencyModuleNames.AddRange(new[] { "Projects", "OpenCV", "OpenCVHelper" });
        PrivateDefinitions.Add("HONKAI_CAPTURE_UNREAL=1");
        PrivateIncludePaths.Add(Path.Combine(PluginDirectory, "Source", "ThirdParty", "MediaPipe", "include"));

        string Dll = Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "Win64", "libmediapipe.dll");
        if (File.Exists(Dll)) RuntimeDependencies.Add(Dll, StagedFileType.NonUFS);
        foreach (string Model in new[] { "face_landmarker.task", "hand_landmarker.task", "pose_landmarker_lite.task" })
        {
            string ModelPath = Path.Combine(PluginDirectory, "Resources", "Models", Model);
            if (File.Exists(ModelPath)) RuntimeDependencies.Add(ModelPath, StagedFileType.NonUFS);
        }
    }
}
