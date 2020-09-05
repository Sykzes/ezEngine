#pragma once

#include <ModelImporter2/Importer/Importer.h>

#include <assimp/Importer.hpp>

class ezEditableSkeletonJoint;
struct aiNode;
struct aiMesh;

namespace ezModelImporter2
{
  class ImporterAssimp : public Importer
  {
  public:
    ImporterAssimp();
    ~ImporterAssimp();

  protected:
    virtual ezResult DoImport() override;

  private:
    ezResult TraverseAiScene();

    ezResult PrepareOutputMesh();
    ezResult RecomputeTangents();

    ezResult TraverseAiNode(aiNode* pNode, const ezMat4& parentTransform, ezEditableSkeletonJoint* pCurJoint);
    ezResult ProcessAiMesh(aiMesh* pMesh, const ezMat4& transform);

    ezResult ImportMaterials();
    ezResult ImportAnimations();

    Assimp::Importer m_aiImporter;
    const aiScene* m_aiScene = nullptr;
    ezUInt32 m_uiTotalMeshVertices = 0;
    ezUInt32 m_uiTotalMeshTriangles = 0;

    struct MeshInstance
    {
      ezMat4 m_GlobalTransform;
      aiMesh* m_pMesh;
    };

    ezMap<ezUInt32, ezHybridArray<MeshInstance, 4>> m_MeshInstances;
  };

  extern ezColor ConvertAssimpType(const aiColor3D& value, bool invert = false);
  extern ezColor ConvertAssimpType(const aiColor4D& value, bool invert = false);
  extern ezMat4 ConvertAssimpType(const aiMatrix4x4& value, bool dummy = false);
  extern ezVec3 ConvertAssimpType(const aiVector3D& value, bool dummy = false);
  extern ezQuat ConvertAssimpType(const aiQuaternion& value, bool dummy = false);
  extern float ConvertAssimpType(float value, bool dummy = false);
  extern int ConvertAssimpType(int value, bool dummy = false);

} // namespace ezModelImporter2
