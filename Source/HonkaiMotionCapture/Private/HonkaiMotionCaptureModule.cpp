// SPDX-License-Identifier: MIT
#include "Modules/ModuleManager.h"

void ShutdownHonkaiCaptureWorkers();

class FHonkaiMotionCaptureModule : public IModuleInterface
{
public:
    virtual void ShutdownModule() override { ShutdownHonkaiCaptureWorkers(); }
    virtual bool SupportsDynamicReloading() override { return false; }
};

IMPLEMENT_MODULE(FHonkaiMotionCaptureModule, HonkaiMotionCapture)
