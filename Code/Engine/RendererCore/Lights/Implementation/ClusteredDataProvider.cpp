#include <PCH.h>
#include <RendererCore/Lights/ClusteredDataExtractor.h>
#include <RendererCore/Lights/ClusteredDataProvider.h>
#include <RendererCore/Lights/Implementation/ClusteredDataUtils.h>
#include <RendererCore/Lights/Implementation/ShadowPool.h>
#include <RendererCore/Pipeline/ExtractedRenderData.h>
#include <RendererCore/RenderContext/RenderContext.h>
#include <RendererFoundation/Profiling/Profiling.h>

ezClusteredDataGPU::ezClusteredDataGPU()
{
  ezGALDevice* pDevice = ezGALDevice::GetDefaultDevice();

  {
    ezGALBufferCreationDescription desc;

    {
      desc.m_uiStructSize = sizeof(ezPerLightData);
      desc.m_uiTotalSize = desc.m_uiStructSize * ezClusteredDataCPU::MAX_LIGHT_DATA;
      desc.m_BufferType = ezGALBufferType::Generic;
      desc.m_bUseAsStructuredBuffer = true;
      desc.m_bAllowShaderResourceView = true;
      desc.m_ResourceAccess.m_bImmutable = false;

      m_hLightDataBuffer = pDevice->CreateBuffer(desc);
    }

    {
      desc.m_uiStructSize = sizeof(ezPerClusterData);
      desc.m_uiTotalSize = desc.m_uiStructSize * NUM_CLUSTERS;

      m_hClusterDataBuffer = pDevice->CreateBuffer(desc);
    }

    {
      desc.m_uiStructSize = sizeof(ezUInt32);
      desc.m_uiTotalSize = desc.m_uiStructSize * ezClusteredDataCPU::MAX_LIGHTS_PER_CLUSTER * NUM_CLUSTERS;

      m_hClusterItemBuffer = pDevice->CreateBuffer(desc);
    }
  }

  m_hConstantBuffer = ezRenderContext::CreateConstantBufferStorage<ezClusteredDataConstants>();

  {
    ezGALSamplerStateCreationDescription desc;
    desc.m_AddressU = ezGALTextureAddressMode::Clamp;
    desc.m_AddressV = ezGALTextureAddressMode::Clamp;
    desc.m_AddressW = ezGALTextureAddressMode::Clamp;
    desc.m_SampleCompareFunc = ezGALCompareFunc::Less;

    m_hShadowSampler = pDevice->CreateSamplerState(desc);
  }

  m_hNoiseTexture = ezResourceManager::LoadResource<ezTexture2DResource>("Textures/BlueNoise.dds");
}

ezClusteredDataGPU::~ezClusteredDataGPU()
{
  ezGALDevice* pDevice = ezGALDevice::GetDefaultDevice();

  pDevice->DestroyBuffer(m_hLightDataBuffer);
  pDevice->DestroyBuffer(m_hClusterDataBuffer);
  pDevice->DestroyBuffer(m_hClusterItemBuffer);
  pDevice->DestroySamplerState(m_hShadowSampler);

  ezRenderContext::DeleteConstantBufferStorage(m_hConstantBuffer);
}

void ezClusteredDataGPU::BindResources(ezRenderContext* pRenderContext)
{
  ezGALDevice* pDevice = ezGALDevice::GetDefaultDevice();

  auto hShadowDataBufferView = pDevice->GetDefaultResourceView(ezShadowPool::UpdateShadowDataBuffer(pRenderContext->GetGALContext()));
  auto hShadowAtlasTextureView = pDevice->GetDefaultResourceView(ezShadowPool::GetShadowAtlasTexture());

  for (ezUInt32 stage = 0; stage < ezGALShaderStage::ENUM_COUNT; ++stage)
  {
    pRenderContext->BindBuffer((ezGALShaderStage::Enum)stage, "perLightDataBuffer", pDevice->GetDefaultResourceView(m_hLightDataBuffer));
    pRenderContext->BindBuffer((ezGALShaderStage::Enum)stage, "perClusterDataBuffer", pDevice->GetDefaultResourceView(m_hClusterDataBuffer));
    pRenderContext->BindBuffer((ezGALShaderStage::Enum)stage, "clusterItemBuffer", pDevice->GetDefaultResourceView(m_hClusterItemBuffer));

    pRenderContext->BindBuffer((ezGALShaderStage::Enum)stage, "shadowDataBuffer", hShadowDataBufferView);
    pRenderContext->BindTexture2D((ezGALShaderStage::Enum)stage, "ShadowAtlasTexture", hShadowAtlasTextureView);
    pRenderContext->BindSamplerState((ezGALShaderStage::Enum)stage, "ShadowSampler", m_hShadowSampler);
  }

  pRenderContext->BindTexture2D(ezGALShaderStage::PixelShader, "NoiseTexture", m_hNoiseTexture, ezResourceAcquireMode::NoFallback);

  pRenderContext->BindConstantBuffer("ezClusteredDataConstants", m_hConstantBuffer);
}

//////////////////////////////////////////////////////////////////////////

EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezClusteredDataProvider, 1, ezRTTIDefaultAllocator<ezClusteredDataProvider>)
{
}
EZ_END_DYNAMIC_REFLECTED_TYPE

ezClusteredDataProvider::ezClusteredDataProvider()
{

}

ezClusteredDataProvider::~ezClusteredDataProvider()
{

}

void* ezClusteredDataProvider::UpdateData(const ezRenderViewContext& renderViewContext, const ezExtractedRenderData& extractedData)
{
  ezGALContext* pGALContext = renderViewContext.m_pRenderContext->GetGALContext();

  EZ_PROFILE_AND_MARKER(pGALContext, "Update Clustered Data");

  if (auto pData = extractedData.GetFrameData<ezClusteredDataCPU>())
  {
    // Update buffer
    if (!pData->m_LightData.IsEmpty())
    {
      pGALContext->UpdateBuffer(m_Data.m_hLightDataBuffer, 0, pData->m_LightData.ToByteArray());
      pGALContext->UpdateBuffer(m_Data.m_hClusterItemBuffer, 0, pData->m_ClusterItemList.ToByteArray());
    }

    pGALContext->UpdateBuffer(m_Data.m_hClusterDataBuffer, 0, pData->m_ClusterData.ToByteArray());

    ezShadowPool::UpdateShadowDataBuffer(pGALContext);

    // Update Constants
    const ezRectFloat& viewport = renderViewContext.m_pViewData->m_ViewPortRect;

    ezClusteredDataConstants* pConstants = renderViewContext.m_pRenderContext->GetConstantBufferData<ezClusteredDataConstants>(m_Data.m_hConstantBuffer);
    pConstants->DepthSliceScale = s_fDepthSliceScale;
    pConstants->DepthSliceBias = s_fDepthSliceBias;
    pConstants->InvTileSize = ezVec2(NUM_CLUSTERS_X / viewport.width, NUM_CLUSTERS_Y / viewport.height);
    pConstants->NumLights = pData->m_LightData.GetCount();
    pConstants->AmbientTopColor = pData->m_AmbientTopColor;
    pConstants->AmbientBottomColor = pData->m_AmbientBottomColor;
  }

  return &m_Data;
}



EZ_STATICLINK_FILE(RendererCore, RendererCore_Pipeline_Implementation_DataProviders_ClusteredDataProvider);

