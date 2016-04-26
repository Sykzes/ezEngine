#pragma once

#include <Core/World/World.h>
#include <RendererCore/Material/MaterialResource.h>
#include <RendererCore/Meshes/MeshResource.h>
#include <RendererCore/Pipeline/RenderData.h>

class EZ_RENDERERCORE_DLL ezMeshRenderData : public ezRenderData
{
  EZ_ADD_DYNAMIC_REFLECTION(ezMeshRenderData, ezRenderData);

public:
  ezBoundingBoxSphere m_GlobalBounds;
  ezMeshResourceHandle m_hMesh;
  ezMaterialResourceHandle m_hMaterial;
  ezUInt32 m_uiPartIndex;
  ezUInt32 m_uiEditorPickingID;
};

class ezMeshComponent;
typedef ezComponentManager<ezMeshComponent> ezMeshComponentManager;

class EZ_RENDERERCORE_DLL ezMeshComponent : public ezComponent
{
  EZ_DECLARE_COMPONENT_TYPE(ezMeshComponent, ezComponent, ezMeshComponentManager);

public:
  ezMeshComponent();

  void SetMesh(const ezMeshResourceHandle& hMesh);
  EZ_FORCE_INLINE const ezMeshResourceHandle& GetMesh() const
  {
    return m_hMesh;
  }

  EZ_FORCE_INLINE void SetMaterial(ezUInt32 uiIndex, const ezMaterialResourceHandle& hMaterial)
  {
    if (uiIndex >= m_Materials.GetCount())
      m_Materials.SetCount(uiIndex + 1);

    m_Materials[uiIndex] = hMaterial;
  }

  EZ_FORCE_INLINE ezMaterialResourceHandle GetMaterial(ezUInt32 uiIndex) const
  {
    if (uiIndex >= m_Materials.GetCount())
      return ezMaterialResourceHandle();

    return m_Materials[uiIndex];
  }

  EZ_FORCE_INLINE void SetRenderDataCategory(ezRenderData::Category category)
  {
    m_RenderDataCategory = category;
  }

  virtual void OnAfterAttachedToObject() override;
  virtual void OnBeforeDetachedFromObject() override;

  void OnUpdateLocalBounds(ezUpdateLocalBoundsMessage& msg) const;
  void OnExtractRenderData(ezExtractRenderDataMessage& msg) const;

  void SetMeshFile(const char* szFile);
  const char* GetMeshFile() const;

  virtual void SerializeComponent(ezWorldWriter& stream) const override;
  virtual void DeserializeComponent(ezWorldReader& stream) override;

protected:
  virtual ezMeshRenderData* CreateRenderData(ezUInt32 uiBatchId) const;

private:
  ezUInt32 Materials_GetCount() const;
  const char* Materials_GetValue(ezUInt32 uiIndex) const;
  void Materials_SetValue(ezUInt32 uiIndex, const char* value);
  void Materials_Insert(ezUInt32 uiIndex, const char* value);
  void Materials_Remove(ezUInt32 uiIndex);

  ezRenderData::Category m_RenderDataCategory;
  ezMeshResourceHandle m_hMesh;
  ezDynamicArray<ezMaterialResourceHandle> m_Materials;
};

