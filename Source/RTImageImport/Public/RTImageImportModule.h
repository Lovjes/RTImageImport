#pragma once

#include "Modules/ModuleInterface.h"


class FRTImageImportModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};