#include <PCH.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/Assets/AssetDocumentManager.h>
#include <EditorFramework/EditorApp/EditorApp.moc.h>
#include <Foundation/IO/FileSystem/FileSystem.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/Logging/Log.h>
#include <Foundation/IO/MemoryStream.h>
#include <Foundation/Threading/TaskSystem.h>
#include <Foundation/Algorithm/Hashing.h>
#include <Foundation/IO/FileSystem/FileWriter.h>
#include <ToolsFoundation/Reflection/PhantomRttiManager.h>
#include <GuiFoundation/UIServices/QtProgressbar.h>
#include <Foundation/Serialization/AbstractObjectGraph.h>
#include <Foundation/Serialization/JsonSerializer.h>
#include <Foundation/Serialization/RttiConverter.h>
#include <CoreUtils/Other/Progress.h>

EZ_IMPLEMENT_SINGLETON(ezAssetCurator);

ezAssetCurator::ezAssetCurator()
  : m_SingletonRegistrar(this)
{
  m_bRunUpdateTask = false;
  m_pUpdateTask = nullptr;
  ezDocumentManager::s_Events.AddEventHandler(ezMakeDelegate(&ezAssetCurator::DocumentManagerEventHandler, this));
}

ezAssetCurator::~ezAssetCurator()
{
  Deinitialize();

  ezDocumentManager::s_Events.RemoveEventHandler(ezMakeDelegate(&ezAssetCurator::DocumentManagerEventHandler, this));
}

void ezAssetCurator::BuildFileExtensionSet(ezSet<ezString>& AllExtensions)
{
  ezStringBuilder sTemp;
  AllExtensions.Clear();

  const auto& allDesc = ezDocumentManager::GetAllDocumentDescriptors();

  for (const auto& desc : allDesc)
  {
    if (desc->m_pManager->GetDynamicRTTI()->IsDerivedFrom<ezAssetDocumentManager>())
    {
      sTemp = desc->m_sFileExtension;
      sTemp.ToLower();

      AllExtensions.Insert(sTemp);
    }
  }
}


void ezAssetCurator::SetActivePlatform(const char* szPlatform)
{
  m_sActivePlatform = szPlatform;

  ezAssetCuratorEvent e;
  e.m_Type = ezAssetCuratorEvent::Type::ActivePlatformChanged;

  m_Events.Broadcast(e);
}

void ezAssetCurator::NotifyOfFileChange(const char* szAbsolutePath)
{
  HandleSingleFile(szAbsolutePath);
  MainThreadTick();
}


void ezAssetCurator::NotifyOfAssetChange(const ezUuid& assetGuid)
{
  UpdateAssetTransformState(assetGuid, ezAssetInfo::TransformState::Unknown);
}

ezAssetInfo::TransformState ezAssetCurator::IsAssetUpToDate(const ezUuid& assetGuid, const char* szPlatform, const ezDocumentTypeDescriptor* pTypeDescriptor, ezUInt64& out_AssetHash, ezUInt64& out_ThumbHash)
{
  out_AssetHash = ezAssetCurator::GetSingleton()->GetAssetDependencyHash(assetGuid);
  out_ThumbHash = 0;
  if (out_AssetHash == 0)
    return ezAssetInfo::TransformState::Unknown;

  const ezString sPlatform = ezAssetDocumentManager::DetermineFinalTargetPlatform(szPlatform);
  const ezString sTargetFile = static_cast<ezAssetDocumentManager*>(pTypeDescriptor->m_pManager)->GetFinalOutputFileName(pTypeDescriptor, GetAssetInfo(assetGuid)->m_sAbsolutePath, sPlatform);
  auto flags = static_cast<ezAssetDocumentManager*>(pTypeDescriptor->m_pManager)->GetAssetDocumentTypeFlags(pTypeDescriptor);

  if (ezAssetDocumentManager::IsResourceUpToDate(out_AssetHash, pTypeDescriptor->m_pDocumentType->GetTypeVersion(), sTargetFile))
  {
    if (flags.IsSet(ezAssetDocumentFlags::SupportsThumbnail))
    {
      out_ThumbHash = ezAssetCurator::GetSingleton()->GetAssetReferenceHash(assetGuid);
      ezString sAssetFile;
      {
        EZ_LOCK(m_CuratorMutex);
        sAssetFile = GetAssetInfo(assetGuid)->m_sAbsolutePath;
      }
      if (!ezAssetDocumentManager::IsThumbnailUpToDate(out_ThumbHash, pTypeDescriptor->m_pDocumentType->GetTypeVersion(), sAssetFile))
      {
        UpdateAssetTransformState(assetGuid, ezAssetInfo::TransformState::NeedsThumbnail);
        return ezAssetInfo::TransformState::NeedsThumbnail;
      }
    }

    UpdateAssetTransformState(assetGuid, ezAssetInfo::TransformState::UpToDate);
    return ezAssetInfo::TransformState::UpToDate;
  }
  else
  {
    if (flags.IsSet(ezAssetDocumentFlags::SupportsThumbnail))
    {
      out_ThumbHash = ezAssetCurator::GetSingleton()->GetAssetReferenceHash(assetGuid);
    }
    UpdateAssetTransformState(assetGuid, ezAssetInfo::TransformState::NeedsTransform);
    return ezAssetInfo::TransformState::NeedsTransform;
  }
}


void ezAssetCurator::UpdateAssetTransformState(const ezUuid& assetGuid, ezAssetInfo::TransformState state)
{
  EZ_LOCK(m_CuratorMutex);

  ezAssetInfo* pAssetInfo = nullptr;
  if (m_KnownAssets.TryGetValue(assetGuid, pAssetInfo))
  {
    if (pAssetInfo->m_TransformState != state)
    {
      pAssetInfo->m_TransformState = state;
      m_TransformStateChanged.Insert(assetGuid);
    }

    switch (state)
    {
    case ezAssetInfo::TransformState::Unknown:
      {
        m_TransformStateUnknown.Insert(assetGuid);
        m_TransformStateNeedsTransform.Remove(assetGuid);
        m_TransformStateNeedsThumbnail.Remove(assetGuid);

        auto it = m_InverseDependency.Find(pAssetInfo->m_sAbsolutePath);
        if (it.IsValid())
        {
          for (const ezUuid& guid : it.Value())
          {
            UpdateAssetTransformState(guid, state);
          }
        }

        auto it2 = m_InverseReferences.Find(pAssetInfo->m_sAbsolutePath);
        if (it2.IsValid())
        {
          for (const ezUuid& guid : it2.Value())
          {
            UpdateAssetTransformState(guid, state);
          }
        }
      }
      break;
    case ezAssetInfo::TransformState::NeedsTransform:
      m_TransformStateUnknown.Remove(assetGuid);
      m_TransformStateNeedsTransform.Insert(assetGuid);
      m_TransformStateNeedsThumbnail.Remove(assetGuid);
      break;
    case ezAssetInfo::TransformState::NeedsThumbnail:
      m_TransformStateUnknown.Remove(assetGuid);
      m_TransformStateNeedsTransform.Remove(assetGuid);
      m_TransformStateNeedsThumbnail.Insert(assetGuid);
      break;
    }
  }
}


void ezAssetCurator::GetAssetTransformStats(ezUInt32& out_uiNumAssets, ezUInt32& out_uiNumUnknown, ezUInt32& out_uiNumNeedTransform, ezUInt32& out_uiNumNeedThumb)
{
  EZ_LOCK(m_CuratorMutex);

  out_uiNumAssets = m_KnownAssets.GetCount();
  out_uiNumUnknown = m_TransformStateUnknown.GetCount();
  out_uiNumNeedTransform = m_TransformStateNeedsTransform.GetCount();
  out_uiNumNeedThumb = m_TransformStateNeedsThumbnail.GetCount();
}

void ezAssetCurator::HandleSingleFile(const ezString& sAbsolutePath)
{
  ezFileStats Stats;
  if (ezOSFile::GetFileStats(sAbsolutePath, Stats).Failed())
    return;

  EZ_LOCK(m_CuratorMutex);

  // make sure the set exists, but don't update it
  // this is only updated in CheckFileSystem
  if (m_ValidAssetExtensions.IsEmpty())
    BuildFileExtensionSet(m_ValidAssetExtensions);

  HandleSingleFile(sAbsolutePath, m_ValidAssetExtensions, Stats);
}

void ezAssetCurator::HandleSingleFile(const ezString& sAbsolutePath, const ezSet<ezString>& validExtensions, const ezFileStats& FileStat)
{
  EZ_LOCK(m_CuratorMutex);

  ezStringBuilder sExt = ezPathUtils::GetFileExtension(sAbsolutePath);
  sExt.ToLower();

  // store information for every file, even when it is no asset, it might be a dependency for some asset
  auto& RefFile = m_ReferencedFiles[sAbsolutePath];

  // mark the file as valid (i.e. we saw it on disk, so it hasn't been deleted or such)
  RefFile.m_Status = FileStatus::Status::Valid;

  bool fileChanged = !RefFile.m_Timestamp.IsEqual(FileStat.m_LastModificationTime, ezTimestamp::CompareMode::Identical);
  if (fileChanged)
  {
    RefFile.m_uiHash = 0;
    auto it = m_InverseDependency.Find(sAbsolutePath);
    if (it.IsValid())
    {
      for (const ezUuid& guid : it.Value())
      {
        UpdateAssetTransformState(guid, ezAssetInfo::TransformState::Unknown);
      }
    }

    auto it2 = m_InverseReferences.Find(sAbsolutePath);
    if (it2.IsValid())
    {
      for (const ezUuid& guid : it2.Value())
      {
        UpdateAssetTransformState(guid, ezAssetInfo::TransformState::Unknown);
      }
    }
  }

  // check that this is an asset type that we know
  if (!validExtensions.Contains(sExt))
  {
    if (fileChanged)
    {
      // Only apply timestamp for non-assets as we need it to still be different
      // for EnsureAssetInfoUpdated to not early-out.
      RefFile.m_Timestamp = FileStat.m_LastModificationTime;
    }
    return;
  }

  // the file is a known asset type
  // so make sure it gets a valid GUID assigned

  // File hasn't change, early out.
  if (RefFile.m_AssetGuid.IsValid() && !fileChanged)
    return;

  // store the folder of the asset
  {
    ezStringBuilder sAssetFolder = sAbsolutePath;
    sAssetFolder = sAssetFolder.GetFileDirectory();

    m_AssetFolders.Insert(sAssetFolder);
  }

  // This will update the timestamp
  EnsureAssetInfoUpdated(sAbsolutePath);
}

void ezAssetCurator::IterateDataDirectory(const char* szDataDir, const ezSet<ezString>& validExtensions)
{
  ezStringBuilder sDataDir = szDataDir;
  sDataDir.MakeCleanPath();

  while (sDataDir.EndsWith("/"))
    sDataDir.Shrink(0, 1);

  if (sDataDir.IsEmpty())
    return;

  ezFileSystemIterator iterator;
  if (iterator.StartSearch(sDataDir, true, false).Failed())
    return;

  ezStringBuilder sPath;

  do
  {
    sPath = iterator.GetCurrentPath();
    sPath.AppendPath(iterator.GetStats().m_sFileName);
    sPath.MakeCleanPath();

    HandleSingleFile(sPath, validExtensions, iterator.GetStats());
  }
  while (iterator.Next().Succeeded());
}

void ezAssetCurator::SetAllAssetStatusUnknown()
{
  // tags all known files as unknown, such that we can later remove files
  // that can not be found anymore

  for (auto it = m_ReferencedFiles.GetIterator(); it.IsValid(); ++it)
  {
    it.Value().m_Status = FileStatus::Status::Unknown;
  }

  for (auto it = m_KnownAssets.GetIterator(); it.IsValid(); ++it)
  {
    UpdateAssetTransformState(it.Key(), ezAssetInfo::TransformState::Unknown);
  }
}

void ezAssetCurator::RemoveStaleFileInfos()
{
  for (auto it = m_ReferencedFiles.GetIterator(); it.IsValid();)
  {
    // search for files that existed previously but have not been found anymore recently
    if (it.Value().m_Status == FileStatus::Status::Unknown)
    {
      // if it was a full asset, not just any kind of reference
      if (it.Value().m_AssetGuid.IsValid())
      {
        // retrieve the ezAssetInfo data for this file
        ezAssetInfo* pCache = m_KnownAssets[it.Value().m_AssetGuid];

        // sanity check: if the ezAssetInfo really belongs to this file, remove it as well
        if (it.Key() == pCache->m_sAbsolutePath)
        {
          EZ_ASSERT_DEBUG(pCache->m_Info.m_DocumentID == it.Value().m_AssetGuid, "GUID mismatch, curator state probably corrupt!");
          m_KnownAssets.Remove(pCache->m_Info.m_DocumentID);
          EZ_DEFAULT_DELETE(pCache);
        }
      }

      // remove the file from the list
      it = m_ReferencedFiles.Remove(it);
    }
    else
      ++it;
  }
}

void ezAssetCurator::Initialize(const ezApplicationFileSystemConfig& cfg)
{
  m_bRunUpdateTask = true;
  m_FileSystemConfig = cfg;
  m_sActivePlatform = "PC";

  CheckFileSystem();
}

void ezAssetCurator::Deinitialize()
{
  ShutdownUpdateTask();

  {
    ezLock<ezMutex> ml(m_CuratorMutex);

    m_ReferencedFiles.Clear();

    for (auto it = m_KnownAssets.GetIterator(); it.IsValid(); ++it)
    {
      EZ_DEFAULT_DELETE(it.Value());
    }
    m_KnownAssets.Clear();
    m_TransformStateUnknown.Clear();
    m_TransformStateNeedsTransform.Clear();
    m_TransformStateNeedsThumbnail.Clear();
  }

  // Broadcast reset.
  {
    ezAssetCuratorEvent e;
    e.m_pInfo = nullptr;
    e.m_Type = ezAssetCuratorEvent::Type::AssetListReset;
    m_Events.Broadcast(e);
  }
}


const ezAssetInfo* ezAssetCurator::FindAssetInfo(const char* szRelativePath) const
{
  ezStringBuilder sPath = szRelativePath;

  if (ezConversionUtils::IsStringUuid(sPath))
  {
    return GetAssetInfo(ezConversionUtils::ConvertStringToUuid(sPath));
  }

  sPath.MakeCleanPath();

  if (sPath.IsAbsolutePath())
  {
    ezString sPath2 = sPath;
    if (!ezQtEditorApp::GetSingleton()->MakePathDataDirectoryRelative(sPath2))
      return nullptr;

    sPath = sPath2;
  }

  for (auto it = m_KnownAssets.GetIterator(); it.IsValid(); ++it)
  {
    if (it.Value()->m_sRelativePath.IsEqual_NoCase(sPath))
      return it.Value();
  }

  return nullptr;
}

void ezAssetCurator::CheckFileSystem()
{
  ezTime before = ezTime::Now();

  ezProgressRange range("Check File-System for Assets", m_FileSystemConfig.m_DataDirs.GetCount(), false);

  // make sure the hashing task has finished
  ShutdownUpdateTask();

  ezLock<ezMutex> ml(m_CuratorMutex);

  SetAllAssetStatusUnknown();

  BuildFileExtensionSet(m_ValidAssetExtensions);

  // check every data directory
  for (auto& dd : m_FileSystemConfig.m_DataDirs)
  {
    ezStringBuilder sTemp = m_FileSystemConfig.GetProjectDirectory();
    sTemp.AppendPath(dd.m_sRelativePath);

    range.BeginNextStep(dd.m_sRelativePath);

    IterateDataDirectory(sTemp, m_ValidAssetExtensions);
  }

  RemoveStaleFileInfos();

  // Broadcast reset.
  {
    ezAssetCuratorEvent e;
    e.m_pInfo = nullptr;
    e.m_Type = ezAssetCuratorEvent::Type::AssetListReset;
    m_Events.Broadcast(e);
  }

  RestartUpdateTask();

  ezTime after = ezTime::Now();
  ezTime diff = after - before;
  ezLog::Info("Asset Curator Refresh Time: %.3f ms", diff.GetMilliseconds());
}

ezString ezAssetCurator::FindDataDirectoryForAsset(const char* szAbsoluteAssetPath) const
{
  ezStringBuilder sAssetPath(szAbsoluteAssetPath);

  for (const auto& dd : m_FileSystemConfig.m_DataDirs)
  {
    ezStringBuilder sDataDir(ezApplicationFileSystemConfig::GetProjectDirectory(), "/", dd.m_sRelativePath);

    if (sAssetPath.IsPathBelowFolder(sDataDir))
      return sDataDir;
  }

  EZ_REPORT_FAILURE("Could not find data directory for asset '%s", szAbsoluteAssetPath);
  return ezApplicationFileSystemConfig::GetProjectDirectory();
}

ezResult ezAssetCurator::WriteAssetTable(const char* szDataDirectory, const char* szPlatform)
{
  ezString sPlatform = szPlatform;
  if (sPlatform.IsEmpty())
    sPlatform = m_sActivePlatform;

  ezStringBuilder sDataDir = szDataDirectory;
  sDataDir.MakeCleanPath();

  ezStringBuilder sFinalPath(sDataDir, "/AssetCache/", sPlatform, ".ezAidlt");
  sFinalPath.MakeCleanPath();

  ezFileWriter file;
  if (file.Open(sFinalPath).Failed())
  {
    ezLog::Error("Failed to open asset lookup table file ('%s')", sFinalPath.GetData());
    return EZ_FAILURE;
  }

  ezStringBuilder sTemp;
  ezString sResourcePath;

  for (auto it = m_KnownAssets.GetIterator(); it.IsValid(); ++it)
  {
    sTemp = it.Value()->m_sAbsolutePath;

    // ignore all assets that are not located in this data directory
    if (!sTemp.IsPathBelowFolder(sDataDir))
      continue;

    const ezUuid& guid = it.Key();
    sResourcePath = it.Value()->m_pManager->GenerateRelativeResourceFileName(sDataDir, sTemp, sPlatform);

    sTemp.Format("%s;%s\n", ezConversionUtils::ToString(guid).GetData(), sResourcePath.GetData());

    file.WriteBytes(sTemp.GetData(), sTemp.GetElementCount());
  }

  return EZ_SUCCESS;
}


void ezAssetCurator::TransformAllAssets(const char* szPlatform)
{
  ezProgressRange range("Transforming Assets", 1 + m_KnownAssets.GetCount(), true);

  for (auto it = m_KnownAssets.GetIterator(); it.IsValid(); ++it)
  {
    if (range.WasCanceled())
      break;

    ezAssetInfo* pAssetInfo = it.Value();

    range.BeginNextStep(ezPathUtils::GetFileNameAndExtension(pAssetInfo->m_sRelativePath).GetData());

    auto res = ProcessAsset(pAssetInfo, szPlatform);
    if (res.m_Result.Failed())
    {
      ezLog::Error("%s (%s)", res.m_sMessage.GetData(), pAssetInfo->m_sRelativePath.GetData());
    }

  }

  range.BeginNextStep("Writing Lookup Tables");

  ezAssetCurator::GetSingleton()->WriteAssetTables(szPlatform);
}

ezStatus ezAssetCurator::ProcessAsset(ezAssetInfo* pAssetInfo, const char* szPlatform)
{
  for (const auto& dep : pAssetInfo->m_Info.m_FileDependencies)
  {
    ezString sPath = dep;

    if (sPath.IsEmpty())
      continue;

    if (ezConversionUtils::IsStringUuid(sPath))
    {
      const ezUuid guid = ezConversionUtils::ConvertStringToUuid(sPath);
      ezAssetInfo* pInfo = nullptr;
      if (!m_KnownAssets.TryGetValue(guid, pInfo))
        continue;
      
      ezStatus res = ProcessAsset(pInfo, szPlatform);
      if (res.m_Result.Failed())
      {
        return res;
      }
    }
  }

  // Find the descriptor for the asset.
  const ezDocumentTypeDescriptor* pTypeDesc = nullptr;
  if (ezDocumentManager::FindDocumentTypeFromPath(pAssetInfo->m_sAbsolutePath, false, pTypeDesc).Failed())
  {
    return ezStatus("The asset '%s' could not be queried for its ezDocumentTypeDescriptor, skipping transform!", pAssetInfo->m_sRelativePath.GetData());
  }

  // Skip assets that cannot be auto-transformed.
  EZ_ASSERT_DEV(pTypeDesc->m_pDocumentType->IsDerivedFrom<ezAssetDocument>(), "Asset document does not derive from correct base class ('%s')", pAssetInfo->m_sRelativePath.GetData());
  auto assetFlags = static_cast<ezAssetDocumentManager*>(pTypeDesc->m_pManager)->GetAssetDocumentTypeFlags(pTypeDesc);
  if (assetFlags.IsAnySet(ezAssetDocumentFlags::DisableTransform | ezAssetDocumentFlags::OnlyTransformManually))
    return ezStatus(EZ_SUCCESS);

  ezUInt64 uiHash = 0;
  ezUInt64 uiThumbHash = 0;
  auto state = IsAssetUpToDate(pAssetInfo->m_Info.m_DocumentID, szPlatform, pTypeDesc, uiHash, uiThumbHash);
  if (state == ezAssetInfo::TransformState::UpToDate)
    return ezStatus(EZ_SUCCESS);

  if (uiHash == 0)
  {
    return ezStatus("Computing the hash for asset '%s' or any dependency failed", pAssetInfo->m_sAbsolutePath.GetData());
  }

  ezDocument* pDoc = ezQtEditorApp::GetSingleton()->OpenDocumentImmediate(pAssetInfo->m_sAbsolutePath, false, false);

  if (pDoc == nullptr)
  {
    return ezStatus("Could not open asset document '%s'", pAssetInfo->m_sRelativePath.GetData());
  }

  ezStatus ret(EZ_SUCCESS);
  ezAssetDocument* pAsset = static_cast<ezAssetDocument*>(pDoc);
  if (state == ezAssetInfo::TransformState::NeedsTransform)
  {
    ret = pAsset->TransformAsset(szPlatform);
  }
  
  if (assetFlags.IsSet(ezAssetDocumentFlags::SupportsThumbnail) && !assetFlags.IsSet(ezAssetDocumentFlags::AutoThumbnailOnTransform))
  {
    if (ret.m_Result.Succeeded() && state <= ezAssetInfo::TransformState::NeedsThumbnail)
    {
      ret = pAsset->CreateThumbnail();
    }
  }

  if (!pDoc->HasWindowBeenRequested())
    pDoc->GetDocumentManager()->CloseDocument(pDoc);

  return ret;
}


ezStatus ezAssetCurator::TransformAsset(const ezUuid& assetGuid, const char* szPlatform)
{
  EZ_LOCK(m_CuratorMutex);

  ezAssetInfo* pInfo = nullptr;
  if (!m_KnownAssets.TryGetValue(assetGuid, pInfo))
    return ezStatus("Transform failed, unknown asset.");

  return ProcessAsset(pInfo, szPlatform);
}


ezStatus ezAssetCurator::CreateThumbnail(const ezUuid& assetGuid)
{
  EZ_LOCK(m_CuratorMutex);

  ezAssetInfo* pInfo = nullptr;
  if (!m_KnownAssets.TryGetValue(assetGuid, pInfo))
    return ezStatus("Create thumbnail failed, unknown asset.");

  return ProcessAsset(pInfo, nullptr);
}

ezResult ezAssetCurator::WriteAssetTables(const char* szPlatform /* = nullptr*/)
{
  EZ_LOG_BLOCK("ezAssetCurator::WriteAssetTables");

  ezResult res = EZ_SUCCESS;

  ezStringBuilder s;

  for (const auto& dd : m_FileSystemConfig.m_DataDirs)
  {
    s.Set(ezApplicationFileSystemConfig::GetProjectDirectory(), "/", dd.m_sRelativePath);

    if (WriteAssetTable(s, szPlatform).Failed())
      res = EZ_FAILURE;
  }

  ezSimpleConfigMsgToEngine msg;
  msg.m_sWhatToDo = "ReloadAssetLUT";
  ezEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);

  msg.m_sWhatToDo = "ReloadResources";
  ezEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);

  return res;
}

void ezAssetCurator::MainThreadTick()
{
  ezLock<ezMutex> ml(m_CuratorMutex);

  static bool bReentry = false;
  if (bReentry)
    return;

  bReentry = true;

  /// \todo Christopher wants this faster!!

  for (auto it = m_KnownAssets.GetIterator(); it.IsValid();)
  {
    if (it.Value()->m_ExistanceState == ezAssetInfo::ExistanceState::FileAdded)
    {
      it.Value()->m_ExistanceState = ezAssetInfo::ExistanceState::FileUnchanged;

      // Broadcast reset.
      ezAssetCuratorEvent e;
      e.m_AssetGuid = it.Key();
      e.m_pInfo = it.Value();
      e.m_Type = ezAssetCuratorEvent::Type::AssetAdded;
      m_Events.Broadcast(e);

      ++it;
    }
    else if (it.Value()->m_ExistanceState == ezAssetInfo::ExistanceState::FileRemoved)
    {
      // Broadcast reset.
      ezAssetCuratorEvent e;
      e.m_AssetGuid = it.Key();
      e.m_pInfo = it.Value();
      e.m_Type = ezAssetCuratorEvent::Type::AssetRemoved;
      m_Events.Broadcast(e);

      // Remove asset
      auto oldIt = it;
      ++it;

      EZ_DEFAULT_DELETE(oldIt.Value());
      m_KnownAssets.Remove(oldIt.Key());
    }
    else
      ++it;
  }

  for (const ezUuid& guid : m_TransformStateChanged)
  {
    ezAssetCuratorEvent e;
    e.m_AssetGuid = guid;
    e.m_pInfo = GetAssetInfo(guid);
    e.m_Type = ezAssetCuratorEvent::Type::AssetUpdated;

    if (e.m_pInfo != nullptr)
      m_Events.Broadcast(e);
  }

  m_TransformStateChanged.Clear();

  RunNextUpdateTask();

  bReentry = false;
}

void ezAssetCurator::UpdateAssetLastAccessTime(const ezUuid& assetGuid)
{
  ezAssetInfo* pAssetInfo = nullptr;
  if (!m_KnownAssets.TryGetValue(assetGuid, pAssetInfo))
    return;

  pAssetInfo->m_LastAccess = ezTime::Now();
}

const ezAssetInfo* ezAssetCurator::GetAssetInfo(const ezUuid& assetGuid) const
{
  ezAssetInfo* pAssetInfo = nullptr;
  if (m_KnownAssets.TryGetValue(assetGuid, pAssetInfo))
    return pAssetInfo;

  return nullptr;
}

void ezAssetCurator::ReadAssetDocumentInfo(ezAssetDocumentInfo* pInfo, ezStreamReader& stream)
{
  ezAbstractObjectGraph graph;
  ezAbstractGraphJsonSerializer::Read(stream, &graph);

  ezRttiConverterContext context;
  ezRttiConverterReader rttiConverter(&graph, &context);

  auto* pHeaderNode = graph.GetNodeByName("Header");
  rttiConverter.ApplyPropertiesToObject(pHeaderNode, pInfo->GetDynamicRTTI(), pInfo);
}

ezResult ezAssetCurator::EnsureAssetInfoUpdated(const ezUuid& assetGuid)
{
  ezAssetInfo* pInfo = nullptr;
  if (!m_KnownAssets.TryGetValue(assetGuid, pInfo))
    return EZ_FAILURE;

  return EnsureAssetInfoUpdated(pInfo->m_sAbsolutePath);
}

ezResult ezAssetCurator::EnsureAssetInfoUpdated(const char* szAbsFilePath)
{
  ezFileStats fs;
  if (ezOSFile::GetFileStats(szAbsFilePath, fs).Failed())
    return EZ_FAILURE;

  EZ_LOCK(m_CuratorMutex);

  if (m_ReferencedFiles[szAbsFilePath].m_Timestamp.IsEqual(fs.m_LastModificationTime, ezTimestamp::CompareMode::Identical))
    return EZ_SUCCESS;

  FileStatus& RefFile = m_ReferencedFiles[szAbsFilePath];
  ezAssetInfo assetInfo;
  ezUuid oldGuid = RefFile.m_AssetGuid;
  bool bNew = false;

  // if it already has a valid GUID, an ezAssetInfo object must exist
  bNew = !RefFile.m_AssetGuid.IsValid();
  EZ_VERIFY(bNew == !m_KnownAssets.Contains(RefFile.m_AssetGuid), "guid set in file-status but no asset is actually known under that guid");
  ezResult res = UpdateAssetInfo(szAbsFilePath, RefFile, assetInfo, &fs);

  if (res.Failed())
  {
    return res;
  }

  ezAssetInfo* pAssetInfo = nullptr;
  if (bNew)
  {
    // now the GUID must be valid
    EZ_ASSERT_DEV(assetInfo.m_Info.m_DocumentID.IsValid(), "Asset header read for '%s', but its GUID is invalid! Corrupted document?", szAbsFilePath);
    EZ_ASSERT_DEV(RefFile.m_AssetGuid == assetInfo.m_Info.m_DocumentID, "UpdateAssetInfo broke the GUID!");
    // and we can store the new ezAssetInfo data under that GUID
    EZ_ASSERT_DEV(!m_KnownAssets.Contains(assetInfo.m_Info.m_DocumentID), "The assets '%s' and '%s' share the same GUID!", assetInfo.m_sAbsolutePath.GetData(), m_KnownAssets[assetInfo.m_Info.m_DocumentID]->m_sAbsolutePath.GetData());
    pAssetInfo = EZ_DEFAULT_NEW(ezAssetInfo, assetInfo);
    m_KnownAssets[RefFile.m_AssetGuid] = pAssetInfo;
    TrackDependencies(pAssetInfo);
  }
  else
  {
    // Guid changed, different asset found, mark old as deleted and add new one.
    if (oldGuid != RefFile.m_AssetGuid)
    {
      m_KnownAssets[oldGuid]->m_ExistanceState = ezAssetInfo::ExistanceState::FileRemoved;
      m_TransformStateUnknown.Remove(oldGuid);
      m_TransformStateNeedsTransform.Remove(oldGuid);
      m_TransformStateNeedsThumbnail.Remove(oldGuid);

      if (RefFile.m_AssetGuid.IsValid())
      {
        pAssetInfo = EZ_DEFAULT_NEW(ezAssetInfo, assetInfo);
        m_KnownAssets[RefFile.m_AssetGuid] = pAssetInfo;
        TrackDependencies(pAssetInfo);
      }
    }
    else
    {
      // Update asset info
      pAssetInfo = m_KnownAssets[RefFile.m_AssetGuid];
      UntrackDependencies(pAssetInfo);
      *pAssetInfo = assetInfo;
      TrackDependencies(pAssetInfo);
    }
  }

  UpdateAssetTransformState(RefFile.m_AssetGuid, ezAssetInfo::TransformState::Unknown);

  return res;
}

ezResult ezAssetCurator::UpdateAssetInfo(const char* szAbsFilePath, ezAssetCurator::FileStatus& stat, ezAssetInfo& assetInfo, const ezFileStats* pFileStat)
{
  // try to read the asset JSON file
  ezFileReader file;
  if (file.Open(szAbsFilePath) == EZ_FAILURE)
  {
    stat.m_uiHash = 0;
    stat.m_AssetGuid = ezUuid();
    stat.m_Status = FileStatus::Status::FileLocked;

    ezLog::Error("Failed to open asset file '%s'", szAbsFilePath);
    return EZ_FAILURE;
  }


  // Update time stamp
  {
    ezFileStats fs;

    if (pFileStat == nullptr)
    {
      if (ezOSFile::GetFileStats(szAbsFilePath, fs).Failed())
        return EZ_FAILURE;

      pFileStat = &fs;
    }

    stat.m_Timestamp = pFileStat->m_LastModificationTime;
  }

  // update the paths
  {
    const ezStringBuilder sDataDir = GetSingleton()->FindDataDirectoryForAsset(szAbsFilePath);
    ezStringBuilder sRelPath = szAbsFilePath;
    sRelPath.MakeRelativeTo(sDataDir);

    assetInfo.m_sRelativePath = sRelPath;
    assetInfo.m_sAbsolutePath = szAbsFilePath;
  }

  // if the file was previously tagged as "deleted", it is now "new" again (to ensure the proper events are sent)
  if (assetInfo.m_ExistanceState == ezAssetInfo::ExistanceState::FileRemoved)
  {
    assetInfo.m_ExistanceState = ezAssetInfo::ExistanceState::FileAdded;
  }

  // figure out which manager should handle this asset type
  {
    const ezDocumentTypeDescriptor* pTypeDesc = nullptr;
    if (assetInfo.m_pManager == nullptr)
    {
      if (ezDocumentManager::FindDocumentTypeFromPath(szAbsFilePath, false, pTypeDesc).Failed())
      {
        EZ_REPORT_FAILURE("Invalid asset setup");
      }

      assetInfo.m_pManager = static_cast<ezAssetDocumentManager*>(pTypeDesc->m_pManager);
    }
  }

  ezMemoryStreamStorage storage;
  ezMemoryStreamReader MemReader(&storage);
  MemReader.SetDebugSourceInformation(assetInfo.m_sAbsolutePath);

  ezMemoryStreamWriter MemWriter(&storage);

  // compute the hash for the asset JSON file
  stat.m_uiHash = ezAssetCurator::HashFile(file, &MemWriter);
  file.Close();

  // and finally actually read the asset JSON file (header only) and store the information in the ezAssetDocumentInfo member
  {
    ReadAssetDocumentInfo(&assetInfo.m_Info, MemReader);

    // here we get the GUID out of the JSON document
    // this links the 'file' to the 'asset'
    stat.m_AssetGuid = assetInfo.m_Info.m_DocumentID;
  }

  return EZ_SUCCESS;
}

void ezAssetCurator::UpdateTrackedFiles(const ezUuid& assetGuid, const ezSet<ezString>& files, ezMap<ezString, ezHybridArray<ezUuid, 1> >& inverseTracker, bool bAdd)
{
  for (const auto& dep : files)
  {
    ezString sPath = dep;

    if (sPath.IsEmpty())
      continue;

    if (ezConversionUtils::IsStringUuid(sPath))
    {
      const ezUuid guid = ezConversionUtils::ConvertStringToUuid(sPath);
      const ezAssetInfo* pInfo = GetAssetInfo(guid);
      if (pInfo == nullptr)
        continue;
      sPath = pInfo->m_sAbsolutePath;
    }
    else
    {
      if (!ezQtEditorApp::GetSingleton()->MakeDataDirectoryRelativePathAbsolute(sPath))
      {
        continue;
      }
    }
    auto it = inverseTracker.FindOrAdd(sPath);
    if (bAdd)
    {
      it.Value().PushBack(assetGuid);
    }
    else
    {
      it.Value().Remove(assetGuid);
    }
  }
}
void ezAssetCurator::TrackDependencies(ezAssetInfo* pAssetInfo)
{
  UpdateTrackedFiles(pAssetInfo->m_Info.m_DocumentID, pAssetInfo->m_Info.m_FileDependencies, m_InverseDependency, true);
  UpdateTrackedFiles(pAssetInfo->m_Info.m_DocumentID, pAssetInfo->m_Info.m_FileReferences, m_InverseReferences, true);
  
  const ezDocumentTypeDescriptor* pTypeDesc = nullptr;
  if (ezDocumentManager::FindDocumentTypeFromPath(pAssetInfo->m_sAbsolutePath, false, pTypeDesc).Succeeded())
  {
    const ezString sPlatform = ezAssetDocumentManager::DetermineFinalTargetPlatform(nullptr);
    const ezString sTargetFile = pAssetInfo->m_pManager->GetFinalOutputFileName(pTypeDesc, pAssetInfo->m_sAbsolutePath, sPlatform);
    auto it = m_InverseReferences.FindOrAdd(sTargetFile);
    it.Value().PushBack(pAssetInfo->m_Info.m_DocumentID);
  }
}

void ezAssetCurator::UntrackDependencies(ezAssetInfo* pAssetInfo)
{
  UpdateTrackedFiles(pAssetInfo->m_Info.m_DocumentID, pAssetInfo->m_Info.m_FileDependencies, m_InverseDependency, false);
  UpdateTrackedFiles(pAssetInfo->m_Info.m_DocumentID, pAssetInfo->m_Info.m_FileReferences, m_InverseReferences, false);

  const ezDocumentTypeDescriptor* pTypeDesc = nullptr;
  if (ezDocumentManager::FindDocumentTypeFromPath(pAssetInfo->m_sAbsolutePath, false, pTypeDesc).Succeeded())
  {
    const ezString sPlatform = ezAssetDocumentManager::DetermineFinalTargetPlatform(nullptr);
    const ezString sTargetFile = pAssetInfo->m_pManager->GetFinalOutputFileName(pTypeDesc, pAssetInfo->m_sAbsolutePath, sPlatform);
    auto it = m_InverseReferences.FindOrAdd(sTargetFile);
    it.Value().Remove(pAssetInfo->m_Info.m_DocumentID);
  }
}

void ezAssetCurator::RestartUpdateTask()
{
  ezLock<ezMutex> ml(m_CuratorMutex);
  m_bRunUpdateTask = true;

  RunNextUpdateTask();
}


void ezAssetCurator::ShutdownUpdateTask()
{
  {
    ezLock<ezMutex> ml(m_CuratorMutex);
    m_bRunUpdateTask = false;
  }

  if (m_pUpdateTask)
  {
    ezTaskSystem::WaitForTask((ezTask*)m_pUpdateTask);

    ezLock<ezMutex> ml(m_CuratorMutex);
    EZ_DEFAULT_DELETE(m_pUpdateTask);
  }
}

const ezHashTable<ezUuid, ezAssetInfo*>& ezAssetCurator::GetKnownAssets() const
{
  return m_KnownAssets;
}

void ezAssetCurator::DocumentManagerEventHandler(const ezDocumentManager::Event& r)
{
  // Invoke new scan
}

