#include "SVGFPass.h"

using namespace Falcor;

SVGFPass::SVGFPass(uint32_t width, uint32_t height)
    : mAtrousIterations(4),
      mFeedbackTap(1),
      mAtrousRadius(2),
      mAlpha(0.15f),
      mMomentsAlpha(0.2f),
      mPhiColor(10.0f),
      mPhiNormal(128.0f),
      mEnableTemporalReprojection(true),
      mEnableSpatialVarianceEstimation(true)
{
    Fbo::Desc reprojFboDesc;
    reprojFboDesc.setColorTarget(0, ResourceFormat::RGBA16Float); // Input signal, variance
    reprojFboDesc.setColorTarget(1, ResourceFormat::RG16Float); // 1st and 2nd Moments
    reprojFboDesc.setColorTarget(2, ResourceFormat::R16Float); // History length

    mCurrReprojFbo = FboHelper::create2D(width, height, reprojFboDesc);
    mPrevReprojFbo = FboHelper::create2D(width, height, reprojFboDesc);

    Fbo::Desc atrousFboDesc;
    atrousFboDesc.setColorTarget(0, ResourceFormat::RGBA16Float); // Input signal, variance

    mOutputFbo = FboHelper::create2D(width, height, atrousFboDesc);
    mLastFilteredFbo = FboHelper::create2D(width, height, atrousFboDesc);
    mAtrousPingFbo = FboHelper::create2D(width, height, atrousFboDesc);
    mAtrousPongFbo = FboHelper::create2D(width, height, atrousFboDesc);

    mPrevLinearZTexture = Texture::create2D(width, height, ResourceFormat::RGBA16Float, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::RenderTarget);

    mReprojectionPass = FullScreenPass::create("SVGF_Reprojection.slang");
    mReprojectionVars = GraphicsVars::create(mReprojectionPass->getProgram()->getReflector());
    mReprojectionState = GraphicsState::create();

    mVarianceEstimationPass = FullScreenPass::create("SVGF_VarianceEstimation.slang");
    mVarianceEstimationVars = GraphicsVars::create(mVarianceEstimationPass->getProgram()->getReflector());
    mVarianceEstimationState = GraphicsState::create();

    mAtrousPass = FullScreenPass::create("SVGF_Atrous.slang");
    mAtrousPass->getProgram()->addDefine("ATROUS_RADIUS", std::to_string(mAtrousRadius));
    mAtrousVars = GraphicsVars::create(mAtrousPass->getProgram()->getReflector());
    mAtrousState = GraphicsState::create();
}

SVGFPass::~SVGFPass()
{
}

Texture::SharedPtr SVGFPass::Execute(
    RenderContext* renderContext,
    Texture::SharedPtr inputSignal,
    Texture::SharedPtr motionVec,
    Texture::SharedPtr linearZ,
    Texture::SharedPtr normalDepth)
{
    mGBufferInput.inputSignal = inputSignal;
    mGBufferInput.linearZ = linearZ;
    mGBufferInput.motionVec = motionVec;
    mGBufferInput.compactNormalDepth = normalDepth;

    TemporalReprojection(renderContext);
    SpatialVarianceEstimation(renderContext);

    for (uint32_t i = 0; i < mAtrousIterations; ++i)
    {
        Fbo::SharedPtr output = (i == mAtrousIterations - 1) ? mOutputFbo : mAtrousPongFbo;
        AtrousFilter(renderContext, i, mAtrousPingFbo, output);

        if (i == mFeedbackTap)
        {
            renderContext->blit(output->getColorTexture(0)->getSRV(), mLastFilteredFbo->getColorTexture(0)->getRTV());
        }

        std::swap(mAtrousPingFbo, mAtrousPongFbo);
    }

    std::swap(mCurrReprojFbo, mPrevReprojFbo);

    renderContext->blit(mGBufferInput.linearZ->getSRV(), mPrevLinearZTexture->getRTV());

    return mOutputFbo->getColorTexture(0);
}

void SVGFPass::TemporalReprojection(RenderContext* renderContext)
{
    mReprojectionVars->setTexture("gInputSignal", mGBufferInput.inputSignal);
    mReprojectionVars->setTexture("gLinearZ", mGBufferInput.linearZ);
    mReprojectionVars->setTexture("gMotion", mGBufferInput.motionVec);
    mReprojectionVars->setTexture("gPrevLinearZ", mPrevLinearZTexture);
    mReprojectionVars->setTexture("gPrevInputSignal", mLastFilteredFbo->getColorTexture(0));
    mReprojectionVars->setTexture("gPrevMoments", mPrevReprojFbo->getColorTexture(1));
    mReprojectionVars->setTexture("gHistoryLength", mPrevReprojFbo->getColorTexture(2));

    mReprojectionVars["PerPassCB"]["gAlpha"] = mAlpha;
    mReprojectionVars["PerPassCB"]["gMomentsAlpha"] = mMomentsAlpha;
    mReprojectionVars["PerPassCB"]["gEnableTemporalReprojection"] = mEnableTemporalReprojection;

    mReprojectionState->setFbo(mCurrReprojFbo);

    renderContext->pushGraphicsState(mReprojectionState);
    renderContext->pushGraphicsVars(mReprojectionVars);
    mReprojectionPass->execute(renderContext);
    renderContext->popGraphicsVars();
    renderContext->popGraphicsState();
}

void SVGFPass::SpatialVarianceEstimation(RenderContext* renderContext)
{
    mVarianceEstimationVars->setTexture("gCompactNormDepth", mGBufferInput.compactNormalDepth);
    mVarianceEstimationVars->setTexture("gInputSignal", mCurrReprojFbo->getColorTexture(0));
    mVarianceEstimationVars->setTexture("gMoments", mCurrReprojFbo->getColorTexture(1));
    mVarianceEstimationVars->setTexture("gHistoryLength", mCurrReprojFbo->getColorTexture(2));

    mVarianceEstimationVars["PerPassCB"]["gPhiColor"] = mPhiColor;
    mVarianceEstimationVars["PerPassCB"]["gPhiNormal"] = mPhiNormal;
    mVarianceEstimationVars["PerPassCB"]["gEnableSpatialVarianceEstimation"] = mEnableSpatialVarianceEstimation;

    mVarianceEstimationState->setFbo(mAtrousPingFbo);

    renderContext->pushGraphicsState(mVarianceEstimationState);
    renderContext->pushGraphicsVars(mVarianceEstimationVars);
    mVarianceEstimationPass->execute(renderContext);
    renderContext->popGraphicsVars();
    renderContext->popGraphicsState();
}

void SVGFPass::AtrousFilter(RenderContext* renderContext, uint32_t iteration, Fbo::SharedPtr input, Fbo::SharedPtr output)
{
    mAtrousVars->setTexture("gCompactNormDepth", mGBufferInput.compactNormalDepth);
    mAtrousVars->setTexture("gInputSignal", input->getColorTexture(0));

    mAtrousVars["PerPassCB"]["gStepSize"] = 1u << iteration;
    mAtrousVars["PerPassCB"]["gPhiColor"] = mPhiColor;
    mAtrousVars["PerPassCB"]["gPhiNormal"] = mPhiNormal;

    mAtrousState->setFbo(output);

    renderContext->pushGraphicsState(mAtrousState);
    renderContext->pushGraphicsVars(mAtrousVars);
    mAtrousPass->execute(renderContext);
    renderContext->popGraphicsVars();
    renderContext->popGraphicsState();
}

void SVGFPass::RenderGui(Gui* gui)
{
    gui->addCheckBox("Temporal Reprojection", mEnableTemporalReprojection);
    gui->addCheckBox("Spatial Variance Estimation", mEnableSpatialVarianceEstimation);
    gui->addFloatSlider("Color Alpha", mAlpha, 0.0f, 1.0f);
    gui->addFloatSlider("Moments Alpha", mMomentsAlpha, 0.0f, 1.0f);
    gui->addFloatSlider("Phi Color", mPhiColor, 0.0f, 64.0f);
    gui->addFloatSlider("Phi Normal", mPhiNormal, 1.0f, 256.0f);
    gui->addIntSlider("Atrous Iterations", *reinterpret_cast<int32_t*>(&mAtrousIterations), 1, 5);
    gui->addIntSlider("Feedback Tap", *reinterpret_cast<int32_t*>(&mFeedbackTap), 1, 5);
    if (gui->addIntSlider("Atrous Radius", *reinterpret_cast<int32_t*>(&mAtrousRadius), 1, 2))
    {
        mAtrousPass->getProgram()->addDefine("ATROUS_RADIUS", std::to_string(mAtrousRadius));
    }
}
