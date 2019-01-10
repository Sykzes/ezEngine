#pragma once

#include <GameEngine/GameApplication/GameApplication.h>
#include <RendererCore/ShaderCompiler/PermutationGenerator.h>

class ezShaderCompilerApplication : public ezGameApplication
{
public:
  typedef ezGameApplication SUPER;

  ezShaderCompilerApplication();

  virtual ezApplication::ApplicationExecution Run() override;

private:
  void PrintConfig();
  ezResult CompileShader(const char* szShaderFile);
  ezResult ExtractPermutationVarValues(const char* szShaderFile);

  virtual void BeforeCoreSystemsStartup() override;
  virtual void AfterCoreSystemsStartup() override;
  virtual void Init_LoadRequiredPlugins() override;
  virtual void Init_LoadProjectPlugins() override {}
  virtual void Init_SetupDefaultResources() override {}
  virtual void Init_ConfigureInput() override {}
  virtual void Init_ConfigureTags() override {}
  virtual void ProcessApplicationInput() override {}

  ezPermutationGenerator m_PermutationGenerator;
  ezString m_sPlatforms;
  ezString m_sShaderFiles;
  ezMap<ezString, ezHybridArray<ezString, 4>> m_FixedPermVars;
};


