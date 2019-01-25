#include <PCH.h>

#include <TexConvLib/Processing/Processor.h>

 ezTexConvProcessor::ezTexConvProcessor()
 {
   m_pCurrentScratchImage = &m_ScratchImage1;
   m_pOtherScratchImage = &m_ScratchImage2;
 }

ezResult ezTexConvProcessor::Process()
{
  EZ_SUCCEED_OR_RETURN(LoadInputImages());

  EZ_SUCCEED_OR_RETURN(AdjustTargetFormat());

  EZ_SUCCEED_OR_RETURN(ChooseOutputFormat());

  EZ_SUCCEED_OR_RETURN(DetermineTargetResolution());

  EZ_SUCCEED_OR_RETURN(ConvertInputImagesToFloat32());

  EZ_SUCCEED_OR_RETURN(ResizeInputImagesToSameDimensions());

  EZ_SUCCEED_OR_RETURN(Assemble2DTexture());

  EZ_SUCCEED_OR_RETURN(GenerateMipmaps());

  EZ_SUCCEED_OR_RETURN(GenerateOutput());

  return EZ_SUCCESS;
}

ezResult ezTexConvProcessor::GenerateOutput()
{
  m_OutputImage = std::move(*m_pCurrentScratchImage);

  m_pCurrentScratchImage = nullptr;
  m_pOtherScratchImage = nullptr;

  if (m_OutputImage.Convert(m_OutputImageFormat).Failed())
  {
    ezLog::Error("Failed to convert result image to final output format '{}'", ezImageFormat::GetName(m_OutputImageFormat));
    return EZ_FAILURE;
  }

  return EZ_SUCCESS;
}


