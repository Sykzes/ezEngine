#pragma once

#include <Core/World/Implementation/SpatialData.h>
#include <Core/World/Implementation/WorldData.h>

/// \brief A world encapsulates a scene graph of game objects and various component managers and their components.
///
/// There can be multiple worlds active at a time, but only 64 at most. The world manages all object storage and might move objects around in memory.
/// Thus it is not allowed to store pointers to objects. They should be referenced by handles.\n
/// The world has a multi-phase update mechanism which is divided in the following phases:\n
/// * Pre-async phase: The corresponding component manager update functions are called synchronously in the order of their dependencies.
/// * Async phase: The update functions are called in batches asynchronously on multiple threads. There is absolutely no guarantee in which order the functions are called.
///   Thus it is not allowed to access any data other than the components own data during that phase.
/// * Post-async phase: Another synchronous phase like the pre-async phase.
/// * Actual deletion of dead objects and components as well as re-parenting of objects are done now.
/// * Transform update: The world transformation of all dynamic objects is updated.
/// * Post-transform phase: Another synchronous phase like the pre-async phase after the transformation has been updated.
class EZ_CORE_DLL ezWorld
{
public:
  /// \brief Creates a new world with the given name.
  ezWorld(const char* szWorldName);
  ~ezWorld();

  /// \brief Returns the name of this world.
  const char* GetName() const;
  
  /// \brief Create a new game object from the given description and returns a handle to it.
  ezGameObjectHandle CreateObject(const ezGameObjectDesc& desc);

  /// \brief Create a new game object from the given description, writes a pointer to it to out_pObject and returns a handle to it.
  ezGameObjectHandle CreateObject(const ezGameObjectDesc& desc, ezGameObject*& out_pObject);

  /// \brief Deletes the given object. Note that the object and all its components and children will be invalidated first and the actual deletion is postponed.
  void DeleteObject(const ezGameObjectHandle& object);

  /// \brief Deletes the given object at the beginning of the next world update. The object and its components and children stay completely valid until then.
  void DeleteObjectDelayed(const ezGameObjectHandle& object);

  /// \brief Returns whether the given handle corresponds to a valid object.
  bool IsValidObject(const ezGameObjectHandle& object) const;
  
  /// \brief Returns if an object with the given handle exists and if so writes out the corresponding pointer to out_pObject.
  bool TryGetObject(const ezGameObjectHandle& object, ezGameObject*& out_pObject) const;
  
  /// \brief Returns if an object with the given name exists and if so writes out the corresponding pointer to the first object with that name to out_pObject.
  // bool TryGetObjectWithName(const char* szName, ezGameObject*& out_pObject) const;  


  /// \brief Returns the total number of objects in this world.
  ezUInt32 GetObjectCount() const;

  /// \brief Returns an iterator over all objects in this world in no specific order.
  ezInternal::WorldData::ObjectStorage::Iterator GetObjects();

  /// \brief Returns an iterator over all objects in this world in no specific order.
  ezInternal::WorldData::ObjectStorage::ConstIterator GetObjects() const;

  /// \brief Defines a visitor function that is called for every game-object when using the traverse method. 
  /// The function takes a pointer to the game object as argument and returns a bool which indicates whether to continue (true) or abort (false) traversal.
  typedef ezInternal::WorldData::VisitorFunc VisitorFunc;

  enum TraversalMethod
  {
    BreadthFirst,
    DepthFirst
  };

  /// \brief Traverses the game object tree starting at the top level objects and then recursively all children. The given callback function is called for every object.
  void Traverse(VisitorFunc visitorFunc, TraversalMethod method = DepthFirst);


  /// \brief Creates an instance of the given component manager type or returns a pointer to an already existing instance.
  template <typename ManagerType>
  ManagerType* CreateComponentManager();

  /// \brief Deletes the component manager of the given type and all its components.
  template <typename ManagerType>
  void DeleteComponentManager();

  /// \brief Returns the instance to the given component manager type.
  template <typename ManagerType>
  ManagerType* GetComponentManager() const;

  /// \brief Returns the component manager that handles the given rtti component type.
  ezComponentManagerBase* GetComponentManager(const ezRTTI* pRtti) const;

  /// \brief Checks whether the given handle references a valid component.
  bool IsValidComponent(const ezComponentHandle& component) const;
  
  /// \brief Returns if a component with the given handle exists and if so writes out the corresponding pointer to out_pComponent.
  template <typename ComponentType>
  bool TryGetComponent(const ezComponentHandle& component, ComponentType*& out_pComponent) const;


  /// \brief Sends a message to all components of the receiverObject. Depending on the routing options the message is also send to parents or children.
  void SendMessage(const ezGameObjectHandle& receiverObject, ezMessage& msg, 
    ezObjectMsgRouting::Enum routing = ezObjectMsgRouting::Default);

  /// \brief Queues the message for the given phase and send it later in that phase to the receiverObject.
  void PostMessage(const ezGameObjectHandle& receiverObject, ezMessage& msg, 
    ezObjectMsgQueueType::Enum queueType, ezObjectMsgRouting::Enum routing = ezObjectMsgRouting::Default);

  /// \brief Queues the message for the given phase. The message is send to the receiverObject after the given delay in the corresponding phase.
  void PostMessage(const ezGameObjectHandle& receiverObject, ezMessage& msg, 
    ezObjectMsgQueueType::Enum queueType, ezTime delay, ezObjectMsgRouting::Enum routing = ezObjectMsgRouting::Default);

  /// \brief If enabled, the full simulation should be executed, otherwise only the rendering related updates should be done
  void SetWorldSimulationEnabled(bool bEnable) { m_bSimulateWorld = bEnable; }

  /// \brief If enabled, the full simulation should be executed, otherwise only the rendering related updates should be done
  bool GetWorldSimulationEnabled() const { return m_bSimulateWorld; }

  /// \brief Updates the world by calling the various update methods on the component managers and also updates the transformation data of the game objects. 
  /// See ezWorld for a detailed description of the update phases.
  void Update();

  /// \brief Returns a task implementation that calls Update on this world.
  ezTask* GetUpdateTask();


  /// \brief
  const ezInternal::SpatialData& GetSpatialData() const;


  /// \brief Returns the coordinate system for the given position.
  /// By default this always returns a coordinate system with forward = +X, right = +Y and up = +Z.
  /// This can be customized by setting a different coordinate system provider.
  void GetCoordinateSystem(const ezVec3& vGlobalPosition, ezCoordinateSystem& out_CoordinateSystem) const;

  /// \brief Sets the coordinate system provider that should be used in this world.
  void SetCoordinateSystemProvider(ezUniquePtr<ezCoordinateSystemProvider>&& pProvider);

  /// \brief Returns the coordinate system provider that is associated with this world.
  ezCoordinateSystemProvider* GetCoordinateSystemProvider() const;


  /// \brief Returns the allocator used by this world.
  ezAllocatorBase* GetAllocator();

  /// \brief Returns the block allocator used by this world.
  ezInternal::WorldLargeBlockAllocator* GetBlockAllocator();

  /// \brief Mark the world for reading by using EZ_LOCK(world.GetReadMarker()). Multiple threads can read simultaneously if none is writing. 
  ezInternal::WorldData::ReadMarker& GetReadMarker() const;

  /// \brief Mark the world for writing by using EZ_LOCK(world.GetWriteMarker()). Only one thread can write at a time.
  ezInternal::WorldData::WriteMarker& GetWriteMarker();


  /// \brief Associates the given user data with the world. The user is responsible for the life time of user data.
  void SetUserData(void* pUserData);

  /// \brief Returns the associated user data.
  void* GetUserData() const;

  /// \brief Used by component managers to queue a new component for initialization during the next update
  void AddComponentToInitialize(ezComponentHandle hComponent);

public:
  /// \brief Returns the number of active worlds.
  static ezUInt32 GetWorldCount();

  /// \brief Returns the world with the given index.
  static ezWorld* GetWorld(ezUInt32 uiIndex);
  
private:
  friend class ezGameObject;
  friend class ezComponentManagerBase;

  void CheckForReadAccess() const;
  void CheckForWriteAccess() const;

  ezGameObject* GetObjectUnchecked(ezUInt32 uiIndex) const;

  void SetParent(ezGameObject* pObject, ezGameObject* pNewParent, ezGameObject::TransformPreservation preserve = ezGameObject::TransformPreservation::PreserveGlobal);
  void LinkToParent(ezGameObject* pObject);
  void UnlinkFromParent(ezGameObject* pObject);

  void ProcessQueuedMessages(ezObjectMsgQueueType::Enum queueType);

  ezResult RegisterUpdateFunction(const ezComponentManagerBase::UpdateFunctionDesc& desc);
  ezResult RegisterUpdateFunctionWithDependency(const ezComponentManagerBase::UpdateFunctionDesc& desc, bool bInsertAsUnresolved);
  ezResult DeregisterUpdateFunction(const ezComponentManagerBase::UpdateFunctionDesc& desc);
  void DeregisterUpdateFunctions(ezComponentManagerBase* pManager);

  void UpdateFromThread();
  void UpdateSynchronous(const ezArrayPtr<ezInternal::WorldData::RegisteredUpdateFunction>& updateFunctions);
  void UpdateAsynchronous();
  void DeleteDeadObjects();
  void DeleteDeadComponents();

  void PatchHierarchyData(ezGameObject* pObject, ezGameObject::TransformPreservation preserve);

  // if true, the full simulation should be executed, otherwise only the rendering related updates should be done
  bool m_bSimulateWorld;

  ezProfilingId m_UpdateProfilingID;
  ezDelegateTask<void> m_UpdateTask;

  ezInternal::WorldData m_Data;
  typedef ezInternal::WorldData::ObjectStorage::Entry ObjectStorageEntry;

  typedef ezInternal::WorldData::QueuedMsgMetaData QueuedMsgMetaData;

  ezInternal::SpatialData m_SpatialData;

  ezUInt32 m_uiIndex;
  static ezStaticArray<ezWorld*, 64> s_Worlds;

  ezDeque<ezComponentHandle> m_ComponentsToInitialize;
  ezDeque<ezComponent*> m_ComponentsToInitialize2;
};

#include <Core/World/Implementation/World_inl.h>

