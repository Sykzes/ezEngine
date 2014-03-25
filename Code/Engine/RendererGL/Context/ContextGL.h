
#pragma once

#include <RendererGL/Basics.h>
#include <RendererGL/Shader/VertexDeclarationGL.h>
#include <RendererFoundation/Device/Device.h>
#include <RendererFoundation/Context/Context.h>
#include <Foundation/Basics/Types/Bitflags.h>
#include <Foundation/Containers/HashTable.h>

namespace ezGALGL
{
  EZ_DECLARE_FLAGS(ezUInt16, DeferredStateChanged, VertexBuffer, VertexDeclaration, SamplerState);
}

class ezGALDeviceGL;

/// \brief The GL implementation of the graphics context.
class EZ_RENDERERGL_DLL ezGALContextGL : public ezGALContext
{
public:

  // State helper

  /// \brief Enables or disables a given OpenGL state using glEnable/glDisable.
  ///
  /// Will check if statechange is necessary.
  /// \see IsStateActive
  ezResult SetGLState(ezUInt32 uiStateIdentifier, bool on);

  /// \brief Returns if a given OpenGL state is active.
  ///
  /// Will ready state from an intern hash-table. If not yet known glIsEnable will be called once and then stored into the intern hash-table.
  /// \see SetGLState
  bool IsStateActive(ezUInt32 uiStateIdentifier);

  /// OpenGL buffer bindings. First elements are compatible with ezGALBufferType.
  struct GLBufferBinding
  {
    enum Enum
    {
      StorageBuffer = ezGALBufferType::Storage,
      VertexBuffer = ezGALBufferType::VertexBuffer,
      IndexBuffer = ezGALBufferType::IndexBuffer,
      ConstantBuffer = ezGALBufferType::ConstantBuffer,

      TransformFeedBack,
      IndirectDraw,

      ENUM_COUNT
    };
  };

  /// \brief Binds a gl buffer to the given binding point.
  ///
  /// Will avoid redundant bindings.
  ezResult BindBuffer(GLBufferBinding::Enum binding, glBufferId buffer);

  /// \brief Buffer binding that will reset itself when leaving the scope.
  ///
  /// Useful for different kind of resource allocation or manipulation where a buffer needs to be bind but the general state should not be affected.
  class ScopedBufferBinding
  {
  public:
    ScopedBufferBinding(ezGALContextGL* pContext, GLBufferBinding::Enum binding, glBufferId buffer);
    ~ScopedBufferBinding();

  private:
    ezGALContextGL* m_pContext;
    GLBufferBinding::Enum m_binding;
    glBufferId m_bufferBefore;
  };

  /// \brief Binds a texture to a given slot.
  /// 
  /// Will avoid unnecessary state changes.
  ///
  /// \param textureType
  ///   Binding type.
  /// \param textureHandle
  ///   OpenGL texture handle.
  /// \param iBindingSlot
  ///   Binding slot used for glActiveTexture. Negative values will ignore the binding slot. This is useful for allocation operations.
  ezResult BindTexture(ezGALTextureType::Enum textureType, glTextureId textureHandle, ezInt32 iBindingSlot = -1);

  /// \brief Texture binding that will reset itself when leaving the scope.
  ///
  /// Useful for different kind of resource allocation or manipulation where a buffer needs to be bind but the general state should not be affected.
  class ScopedTextureBinding
  {
  public:
    ScopedTextureBinding(ezGALContextGL* pContext, ezGALTextureType::Enum textureType, glTextureId texture);
    ~ScopedTextureBinding();

  private:
    ezGALContextGL* m_pContext;
    ezGALTextureType::Enum m_textureType;
    glTextureId m_textureBefore;
    ezUInt32 m_uiUsedSlot;
  };

  // Conversion tables.

  /// Maps ezGALPrimitiveTopology to OpenGL types.
  static const ezUInt32 s_GALTopologyToGL[ezGALPrimitiveTopology::ENUM_COUNT];

  /// Maps ezGALIndexType to OpenGL types.
  static const ezUInt32 s_GALIndexTypeToGL[ezGALIndexType::ENUM_COUNT];

  /// Maps GLBufferBindings to actual OpenGL binding points.
  static const ezUInt32 s_GALBufferBindingToGL[GLBufferBinding::ENUM_COUNT];

  /// Maps ezGALTextureType to OpenGL texture types.
  static const ezUInt32 s_GALTextureTypeToGL[ezGALTextureType::ENUM_COUNT];

protected:

  friend class ezGALDeviceGL;
  friend ScopedBufferBinding;


  ezGALContextGL(ezGALDeviceGL* pDevice);

  ~ezGALContextGL();

  // Draw functions

  virtual void ClearPlatform(const ezColor& ClearColor, ezUInt32 uiRenderTargetClearMask, bool bClearDepth, bool bClearStencil, float fDepthClear, ezUInt8 uiStencilClear) EZ_OVERRIDE;

  virtual void DrawPlatform(ezUInt32 uiVertexCount) EZ_OVERRIDE;

  virtual void DrawIndexedPlatform(ezUInt32 uiIndexCount, ezUInt32 uiStartIndex) EZ_OVERRIDE;

  virtual void DrawIndexedInstancedPlatform(ezUInt32 uiIndexCountPerInstance, ezUInt32 uiInstanceCount, ezUInt32 uiStartIndex) EZ_OVERRIDE;

  virtual void DrawIndexedInstancedIndirectPlatform(ezGALBuffer* pIndirectArgumentBuffer, ezUInt32 uiArgumentOffsetInBytes) EZ_OVERRIDE;

  virtual void DrawInstancedPlatform(ezUInt32 uiVertexCountPerInstance, ezUInt32 uiInstanceCount) EZ_OVERRIDE;

  virtual void DrawInstancedIndirectPlatform(ezGALBuffer* pIndirectArgumentBuffer, ezUInt32 uiArgumentOffsetInBytes) EZ_OVERRIDE;

  virtual void DrawAutoPlatform() EZ_OVERRIDE;

  virtual void BeginStreamOutPlatform() EZ_OVERRIDE;

  virtual void EndStreamOutPlatform() EZ_OVERRIDE;

  // Dispatch

  virtual void DispatchPlatform(ezUInt32 uiThreadGroupCountX, ezUInt32 uiThreadGroupCountY, ezUInt32 uiThreadGroupCountZ) EZ_OVERRIDE;

  virtual void DispatchIndirectPlatform(ezGALBuffer* pIndirectArgumentBuffer, ezUInt32 uiArgumentOffsetInBytes) EZ_OVERRIDE;


  // State setting functions

  virtual void SetShaderPlatform(ezGALShader* pShader) EZ_OVERRIDE;

  virtual void SetIndexBufferPlatform(ezGALBuffer* pIndexBuffer) EZ_OVERRIDE;

  virtual void SetVertexBufferPlatform(ezUInt32 uiSlot, ezGALBuffer* pVertexBuffer) EZ_OVERRIDE;

  virtual void SetVertexDeclarationPlatform(ezGALVertexDeclaration* pVertexDeclaration) EZ_OVERRIDE;

  virtual void SetPrimitiveTopologyPlatform(ezGALPrimitiveTopology::Enum Topology) EZ_OVERRIDE;

  virtual void SetConstantBufferPlatform(ezUInt32 uiSlot, ezGALBuffer* pBuffer) EZ_OVERRIDE;

  virtual void SetSamplerStatePlatform(ezGALShaderStage::Enum Stage, ezUInt32 uiSlot, ezGALSamplerState* pSamplerState) EZ_OVERRIDE;

  virtual void SetResourceViewPlatform(ezGALShaderStage::Enum Stage, ezUInt32 uiSlot, ezGALResourceView* pResourceView) EZ_OVERRIDE;

  virtual void SetRenderTargetConfigPlatform(ezGALRenderTargetConfig* pRenderTargetConfig) EZ_OVERRIDE;

  virtual void SetUnorderedAccessViewPlatform(ezUInt32 uiSlot, ezGALResourceView* pResourceView) EZ_OVERRIDE;

  virtual void SetBlendStatePlatform(ezGALBlendState* pBlendState) EZ_OVERRIDE;

  virtual void SetDepthStencilStatePlatform(ezGALDepthStencilState* pDepthStencilState) EZ_OVERRIDE;

  virtual void SetRasterizerStatePlatform(ezGALRasterizerState* pRasterizerState) EZ_OVERRIDE;

  virtual void SetViewportPlatform(float fX, float fY, float fWidth, float fHeight, float fMinDepth, float fMaxDepth) EZ_OVERRIDE;

  virtual void SetScissorRectPlatform(ezUInt32 uiX, ezUInt32 uiY, ezUInt32 uiWidth, ezUInt32 uiHeight) EZ_OVERRIDE;

  virtual void SetStreamOutBufferPlatform(ezUInt32 uiSlot, ezGALBuffer* pBuffer, ezUInt32 uiOffset) EZ_OVERRIDE;

  // Fence & Query functions

  virtual void InsertFencePlatform(ezGALFence* pFence) EZ_OVERRIDE;

  virtual bool IsFenceReachedPlatform(ezGALFence* pFence) EZ_OVERRIDE;

  virtual void BeginQueryPlatform(ezGALQuery* pQuery) EZ_OVERRIDE;

  virtual void EndQueryPlatform(ezGALQuery* pQuery) EZ_OVERRIDE;

  // Resource update functions

  virtual void CopyBufferPlatform(ezGALBuffer* pDestination, ezGALBuffer* pSource) EZ_OVERRIDE;

  virtual void CopyBufferRegionPlatform(ezGALBuffer* pDestination, ezUInt32 uiDestOffset, ezGALBuffer* pSource, ezUInt32 uiSourceOffset, ezUInt32 uiByteCount) EZ_OVERRIDE;

  virtual void UpdateBufferPlatform(ezGALBuffer* pDestination, ezUInt32 uiDestOffset, const void* pSourceData, ezUInt32 uiByteCount) EZ_OVERRIDE;

  virtual void CopyTexturePlatform(ezGALTexture* pDestination, ezGALTexture* pSource) EZ_OVERRIDE;

  virtual void CopyTextureRegionPlatform(ezGALTexture* pDestination, const ezGALTextureSubresource& DestinationSubResource, const ezVec3U32& DestinationPoint, ezGALTexture* pSource, const ezGALTextureSubresource& SourceSubResource, const ezBoundingBoxu32& Box) EZ_OVERRIDE;

  virtual void UpdateTexturePlatform(ezGALTexture* pDestination, const ezGALTextureSubresource& DestinationSubResource, const ezBoundingBoxu32& DestinationBox, const void* pSourceData, ezUInt32 uiSourceRowPitch, ezUInt32 uiSourceDepthPitch) EZ_OVERRIDE;

  virtual void ResolveTexturePlatform(ezGALTexture* pDestination, const ezGALTextureSubresource& DestinationSubResource, ezGALTexture* pSource, const ezGALTextureSubresource& SourceSubResource) EZ_OVERRIDE;

  virtual void ReadbackTexturePlatform(ezGALTexture* pTexture) EZ_OVERRIDE;

  virtual void CopyTextureReadbackResultPlatform(ezGALTexture* pTexture, const ezArrayPtr<ezGALSystemMemoryDescription>* pData) EZ_OVERRIDE;

  // Debug helper functions

  virtual void PushMarkerPlatform(const char* Marker) EZ_OVERRIDE;

  virtual void PopMarkerPlatform() EZ_OVERRIDE;

  virtual void InsertEventMarkerPlatform(const char* Marker) EZ_OVERRIDE;


  void FlushDeferredStateChanges();




  // Bound objects for deferred state flushes

  /// Already bound texture samplers, to be bound in a single glBindSamplers call.
  glSamplerId m_BoundSamplerStates[EZ_GAL_MAX_SHADER_RESOURCE_VIEW_COUNT];

  /// Already bound vertex buffers, to be bound in a single glBindVertexBuffers call .
  glBufferId m_BoundVertexBuffers[EZ_GAL_MAX_VERTEX_BUFFER_COUNT];

  /// Vertex buffer strides to be set with next glBindVertexBuffers call.
  ezInt32 m_VertexBufferStrides[EZ_GAL_MAX_VERTEX_BUFFER_COUNT];

  /// Preprocessed GL vertex attribute information.
  const ezDynamicArray<ezGALVertexDeclarationGL::VertexAttributeDesc>* m_pBoundVertexAttributes;

  /// Currently changed deferred states.
  ezBitflags<ezGALGL::DeferredStateChanged> m_DeferredStateChanged;



  // Various states needed for different operations.

  /// Primitive topology to use for upcoming draw-calls.
  ezGALPrimitiveTopology::Enum m_PrimitiveTopology;

  /// Index type to use for upcoming indexed draw-calls.
  ezGALIndexType::Enum m_IndexType;

  /// Number of currently bound render-targets. 
  ezUInt32 m_uiNumColorTarget;

  /// Number of currently bound OpenGL vertex buffers.
  ezUInt32 m_uiNumGLVertexAttributesBound;

  /// Maps OpenGL glEnable/glDisable options to their current state.
  /// \see SetGLState, IsStateActive
  ezHashTable<ezUInt32, bool> m_EnableStates;

  /// Current depth write mask.
  ezUInt32 m_DepthWriteMask;

  /// Current depth write mask.
  ezUInt32 m_StencilWriteMask;

  /// Current stencil read mask.
  ezUInt32 m_StencilReadMask;

  /// Current stencil func.
  ezUInt32 m_StencilFunc;

  /// Current state of glCullFace.
  ezUInt32 m_CullFaceState;

  /// Current state of glPolygonMode.
  ezUInt32 m_PolygonMode;

  /// Currently bound buffers.
  ezUInt32 m_BufferBindings[GLBufferBinding::ENUM_COUNT];

  /// Currently active Texture unit - controlled with glActiveTexture.
  ezUInt32 m_uiActiveTextureUnit;

  /// Maximum of texture bindings.
  ezInt32 m_iMaxNumTextureBindings;

  /// Currently bound textures.
  ezArrayPtr<ezUInt32> m_TextureBindings[ezGALTextureType::ENUM_COUNT];
};

#include <RendererGL/Context/Implementation/ContextGL_inl.h>
