#pragma once

#include <GameEngine/Basics.h>
#include <GameEngine/GameState/GameState.h>

#include <Foundation/Threading/DelegateTask.h>
#include <Foundation/Types/UniquePtr.h>
#include <GameEngine/Console/ConsoleFunction.h>

#include <GameEngine/GameApplication/GameApplicationBase.h>

class ezDefaultTimeStepSmoothing;
class ezConsole;
struct ezWorldDesc;
class ezImage;
struct ezWindowCreationDesc;

/// Allows custom code to inject logic at specific update points.
/// The events are listed in the order in which they typically happen.
struct ezGameApplicationEvent
{
  enum class Type
  {
    BeginAppTick,
    BeforeWorldUpdates,
    AfterWorldUpdates,
    BeforeUpdatePlugins,
    AfterUpdatePlugins,
    BeforePresent,
    AfterPresent,
    EndAppTick,
    AfterWorldCreated,    // m_pData -> ezWorld*
    BeforeWorldDestroyed, // m_pData -> ezWorld*
  };

  Type m_Type;
  void* m_pData = nullptr;
};

/// \brief The base class for all typical game applications made with ezEngine
///
/// While ezApplication is an abstraction for the operating system entry point,
/// ezGameApplication extends this to implement startup and tear down functionality
/// of a typical game that uses the standard functionality of ezEngine.
///
/// ezGameApplication implements a lot of functionality needed by most games,
/// such as setting up data directories, loading plugins, configuring the input system, etc.
///
/// For every such step a virtual function is called, allowing to override steps in custom applications.
///
/// The default implementation tries to do as much of this in a data-driven way. E.g. plugin and data
/// directory configurations are read from DDL files. These can be configured by hand or using ezEditor.
///
/// You are NOT supposed to implement game functionality by deriving from ezGameApplication.
/// Instead see ezGameState.
///
/// ezGameApplication will create exactly one ezGameState by looping over all available ezGameState types
/// (through reflection) and picking the one whose DeterminePriority function returns the highest priority.
/// That game state will live throughout the entire application life-time and will be stepped every frame.
class EZ_GAMEENGINE_DLL ezGameApplication : public ezGameApplicationBase
{
public:
  typedef ezGameApplicationBase SUPER;

  /// szProjectPath may be nullptr, if FindProjectDirectory() is overridden.
  ezGameApplication(const char* szAppName, const char* szProjectPath);
  ~ezGameApplication();

  /// \brief Returns the ezGameApplication singleton
  static ezGameApplication* GetGameApplicationInstance() { return s_pGameApplicationInstance; }

  /// \brief Overrides ezApplication::Run() and implements a typical game update.
  ///
  /// Calls ezWindowBase::ProcessWindowMessages() on all windows that have been added through AddWindow()
  /// As long as there are any main views added to ezRenderLoop it \n
  ///   Updates the global ezClock. \n
  ///   Calls UpdateInput() \n
  ///   Calls UpdateWorldsAndRender() \n
  virtual ezApplication::ApplicationExecution Run() override;


  /// \brief When the graphics device is created, by default the game application will pick a platform specific implementation. This
  /// function allows to override that by setting a custom function that creates a graphics device.
  static void SetOverrideDefaultDeviceCreator(ezDelegate<ezGALDevice*(const ezGALDeviceCreationDescription&)> creator);

  /// \todo Fix this comment
  /// \brief virtual function that is called by DoProjectSetup(). The result is passed to ezFileSystem::SetProjectDirectory
  ///
  /// The default implementation relies on a valid path in m_sAppProjectPath.
  /// It passes that to SearchProjectDirectory() together with the path to the application binary,
  /// to search for a project somewhere relative to where the application is installed.
  ///
  /// Override this, if your application uses a different folder structure or way to specify the project directory.
  virtual ezString FindProjectDirectory() const override;

  /// \brief Used at runtime (by the editor) to reload input maps. Forwards to Init_ConfigureInput()
  void ReinitializeInputConfig();

  ezEvent<const ezGameApplicationEvent&> m_Events;

protected:
  virtual ezUniquePtr<ezWindowOutputTargetBase> CreateWindowOutputTarget(ezWindowBase* pWindow) override;
  virtual void DestroyWindowOutputTarget(ezUniquePtr<ezWindowOutputTargetBase> pOutputTarget) override;

protected:

  /// \brief Calls Update on all worlds and renders all views through ezRenderLoop::Render()
  void UpdateWorldsAndRender();

  virtual void UpdateWorldsAndRender_Begin();
  virtual void UpdateWorldsAndRender_Middle();
  virtual void UpdateWorldsAndRender_End();

  virtual void BeforeCoreSystemsStartup() override;

protected:

  virtual void Init_ConfigureAssetManagement() override;
  virtual void Init_LoadRequiredPlugins();
  virtual void Init_SetupDefaultResources();
  virtual void Init_SetupGraphicsDevice() override;
  virtual void Deinit_ShutdownGraphicsDevice();


  ///
  /// Application Update
  ///

  /// \brief Override to implement proper input handling.
  ///
  /// The default implementation handles ESC (close app), F5 (reload resources) and F8 (capture profiling info).
  virtual void ProcessApplicationInput();

  /// \brief Does all input handling on input manager and game states.
  void UpdateInput();

  /// \brief Stores what is given to the constructor
  ezString m_sAppProjectPath;

  

protected:
  static ezGameApplication* s_pGameApplicationInstance;

  void RenderFps();
  void RenderConsole();

  void UpdateWorldsAndExtractViews();
  ezDelegateTask<void> m_UpdateTask;

  static ezDelegate<ezGALDevice*(const ezGALDeviceCreationDescription&)> s_DefaultDeviceCreator;
  
  bool m_bShowConsole = false;
  
  ezUniquePtr<ezConsole> m_pConsole;


#ifdef BUILDSYSTEM_ENABLE_MIXEDREALITY_SUPPORT
  ezUniquePtr<class ezMixedRealityFramework> m_pMixedRealityFramework;
#endif
};
