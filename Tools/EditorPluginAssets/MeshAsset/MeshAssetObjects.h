#pragma once

#include <ToolsFoundation/Object/DocumentObjectBase.h>
#include <ToolsFoundation/Reflection/ReflectedTypeDirectAccessor.h>
#include <EditorFramework/Assets/SimpleAssetDocument.h>

struct ezMaterialResourceSlot
{
  ezString m_sLabel;
  ezString m_sResource;
};

struct ezMeshPrimitive
{
  typedef ezInt8 StorageType;

  enum Enum
  {
    File,
    Box,
    Rect,
    Cylinder,
    Cone,
    Pyramid,
    Sphere,
    HalfSphere,
    GeodesicSphere,
    Capsule,
    Torus,

    Default = File
  };
};

EZ_DECLARE_REFLECTABLE_TYPE(EZ_NO_LINKAGE, ezMeshPrimitive);

EZ_DECLARE_REFLECTABLE_TYPE(EZ_NO_LINKAGE, ezMaterialResourceSlot);

class ezMeshAssetProperties : public ezReflectedClass
{
  EZ_ADD_DYNAMIC_REFLECTION(ezMeshAssetProperties, ezReflectedClass);

public:
  ezMeshAssetProperties();

  ezString m_sMeshFile;
  float m_fUniformScaling;
  ezVec3 m_vNonUniformScaling;
  float m_fRadius;
  float m_fRadius2;
  float m_fHeight;
  float m_fAngle;
  ezUInt16 m_uiDetail;
  ezUInt16 m_uiDetail2;
  bool m_bCap;
  bool m_bCap2;
  ezVec3 m_vScaleXYZ;


  ezEnum<ezBasisAxis> m_ForwardDir;
  ezEnum<ezBasisAxis> m_RightDir;
  ezEnum<ezBasisAxis> m_UpDir;

  ezMeshPrimitive::Enum m_PrimitiveType;
  ezHybridArray<ezMaterialResourceSlot, 8> m_Slots;

  const ezString GetResourceSlotProperty(ezUInt32 uiSlot) const;

  ezUInt32 m_uiVertices;
  ezUInt32 m_uiTriangles;

private:

};
