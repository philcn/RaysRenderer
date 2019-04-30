#include "RaysRenderer.h"

namespace
{
    static const char* kDefaultScene = "../../Media/Pica/Pica.fscene";
    static const glm::vec4 kClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    static const glm::vec4 kSkyColor(0.2f, 0.6f, 0.9f, 1.0f);

    enum GBuffer : uint32_t
    {
        WorldPosition = 0,
        NormalRoughness,
        Albedo,
        MotionVector,
        SVGF_LinearZ,
        SVGF_CompactNormDepth
    };
}

void RaysRenderer::onLoad(SampleCallbacks* sample, RenderContext* renderContext)
{
    mFrameCount = 0;
    mEnableRaytracedShadows = true;
    mEnableRaytracedReflection = true;
    mEnableRaytracedAO = true;
    mEnableDenoiseShadows = true;
    mEnableDenoiseReflection = true;
    mEnableDenoiseAO = true;
    mEnableTAA = true;
    mRenderMode = RenderMode::Hybrid;
    mAODistance = 3.0f;

    uint32_t width = sample->getCurrentFbo()->getWidth();
    uint32_t height = sample->getCurrentFbo()->getHeight();

    mCamera = Camera::create();
    mCamera->setAspectRatio((float)width / (float)height);
    mCamController.attachCamera(mCamera);
    mCamController.setCameraSpeed(16.0f);

    SetupScene();
    SetupRendering(width, height);
    SetupRaytracing(width, height);
    SetupDenoising(width, height);
    SetupTAA(width, height);

    ConfigureDeferredProgram();
}

void RaysRenderer::SetupScene()
{
    mScene = RtScene::loadFromFile(kDefaultScene, RtBuildFlags::None, Model::LoadFlags::None, Scene::LoadFlags::None);

    // Create and bind materials
    mBasicMaterial = Material::create("Model");
    mBasicMaterial->setBaseColor(glm::vec4(0.95f, 0.0f, 0.0f, 1.0f));
    mBasicMaterial->setSpecularParams(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));

    mGroundMaterial = Material::create("Ground");
    mGroundMaterial->setBaseColor(glm::vec4(0.03f, 0.0f, 0.0f, 1.0f));
    mGroundMaterial->setSpecularParams(glm::vec4(0.0f, 0.54f, 0.0f, 0.0f));

    auto& model = mScene->getModel(0);
    model->getMesh(0)->setMaterial(mBasicMaterial);
    mScene->getModel(1)->getMesh(0)->setMaterial(mGroundMaterial);

    // Set scene specific camera parameters
    float radius = model->getRadius();
    float nearZ = std::max(0.1f, radius / 750.0f);
    float farZ = radius * 20;
        
    mCamera->setPosition(glm::vec3(0.2f, 0.5f, 1.0f) * radius);
    mCamera->setTarget(model->getCenter());
    mCamera->setDepthRange(nearZ, farZ);
}

void RaysRenderer::SetupRendering(uint32_t width, uint32_t height)
{
    mSceneRenderer = SceneRenderer::create(mScene);

    // Forward pass
    mForwardProgram = GraphicsProgram::createFromFile("Forward.slang", "", "main");
    mForwardVars = GraphicsVars::create(mForwardProgram->getReflector());
    mForwardState = GraphicsState::create();
    mForwardState->setProgram(mForwardProgram);
    mForwardState->setRasterizerState(RasterizerState::create(RasterizerState::Desc().setCullMode(RasterizerState::CullMode::None)));

    // G-Buffer pass
    mGBufferProgram = GraphicsProgram::createFromFile("GBuffer.slang", "", "main");
    mGBufferVars = GraphicsVars::create(mGBufferProgram->getReflector());
    mGBufferState = GraphicsState::create();
    mGBufferState->setProgram(mGBufferProgram);

    Fbo::Desc fboDesc;
    fboDesc.setColorTarget(GBuffer::WorldPosition, ResourceFormat::RGBA32Float);
    fboDesc.setColorTarget(GBuffer::NormalRoughness, ResourceFormat::RGBA32Float);
    fboDesc.setColorTarget(GBuffer::Albedo, ResourceFormat::RGBA8Unorm);
    fboDesc.setColorTarget(GBuffer::MotionVector, ResourceFormat::RGBA16Float);
    fboDesc.setColorTarget(GBuffer::SVGF_LinearZ, ResourceFormat::RGBA16Float);
    fboDesc.setColorTarget(GBuffer::SVGF_CompactNormDepth, ResourceFormat::RGBA16Float);
    fboDesc.setDepthStencilTarget(ResourceFormat::D32Float);
    mGBuffer = FboHelper::create2D(width, height, fboDesc);

    // Deferred pass
    mDeferredPass = FullScreenPass::create("Deferred.slang");
    mDeferredVars = GraphicsVars::create(mDeferredPass->getProgram()->getReflector());
    mDeferredState = GraphicsState::create();
}

void RaysRenderer::SetupRaytracing(uint32_t width, uint32_t height)
{
    mRaytracer = RtSceneRenderer::create(mScene);

    // Raytraced reflection
    RtProgram::Desc reflectionProgDesc;
    reflectionProgDesc.addShaderLibrary("RaytracedReflection.slang");
    reflectionProgDesc.setRayGen("RayGen");
    reflectionProgDesc.addHitGroup(0, "PrimaryCHS", "");
    reflectionProgDesc.addHitGroup(1, "", "ShadowAHS");
    reflectionProgDesc.addMiss(0, "PrimaryMiss");
    reflectionProgDesc.addMiss(1, "ShadowMiss");

    mRtReflectionProgram = RtProgram::create(reflectionProgDesc);
    mRtReflectionVars = RtProgramVars::create(mRtReflectionProgram, mScene);

    mRtReflectionState = RtState::create();
    mRtReflectionState->setProgram(mRtReflectionProgram);
    mRtReflectionState->setMaxTraceRecursionDepth(4); // 1 camera ray, 2 reflection and 1 NEE ray

    Resource::BindFlags bindFlags = Resource::BindFlags::UnorderedAccess | Resource::BindFlags::ShaderResource;
    mReflectionTexture = Texture::create2D(width, height, ResourceFormat::RGBA16Float, 1, 1, nullptr, bindFlags);

    // Raytraced shadows
    RtProgram::Desc shadowProgDesc;
    shadowProgDesc.addShaderLibrary("RaytracedShadows.slang");
    shadowProgDesc.setRayGen("RayGen");
    shadowProgDesc.addHitGroup(0, "", "PrimaryAHS");
    shadowProgDesc.addMiss(0, "PrimaryMiss");

    mRtShadowProgram = RtProgram::create(shadowProgDesc);
    mRtShadowVars = RtProgramVars::create(mRtShadowProgram, mScene);

    mRtShadowState = RtState::create();
    mRtShadowState->setProgram(mRtShadowProgram);
    mRtShadowState->setMaxTraceRecursionDepth(1); // no recursion

    mShadowTexture = Texture::create2D(width, height, ResourceFormat::R8Unorm, 1, 1, nullptr, bindFlags);

    // Raytraced AO
    RtProgram::Desc aoProgDesc;
    aoProgDesc.addShaderLibrary("RaytracedAO.slang");
    aoProgDesc.setRayGen("RayGen");
    aoProgDesc.addHitGroup(0, "", "PrimaryAHS");
    aoProgDesc.addMiss(0, "PrimaryMiss");

    mRtAOProgram = RtProgram::create(aoProgDesc);
    mRtAOVars = RtProgramVars::create(mRtAOProgram, mScene);

    mRtAOState = RtState::create();
    mRtAOState->setProgram(mRtAOProgram);
    mRtAOState->setMaxTraceRecursionDepth(1);

    mAOTexture = Texture::create2D(width, height, ResourceFormat::R8Unorm, 1, 1, nullptr, bindFlags);
}

void RaysRenderer::SetupDenoising(uint32_t width, uint32_t height)
{
    mShadowFilter = std::make_shared<SVGFPass>(width, height);
    mReflectionFilter = std::make_shared<SVGFPass>(width, height);
    mAOFilter = std::make_shared<SVGFPass>(width, height);
}

void RaysRenderer::SetupTAA(uint32_t width, uint32_t height)
{
    mTAA.pTAA = TemporalAA::create();

    Fbo::Desc fboDesc;
    fboDesc.setColorTarget(0, ResourceFormat::RGBA8UnormSrgb);
    mTAA.createFbos(width, height, fboDesc);

    PatternGenerator::SharedPtr generator;
    generator = HaltonSamplePattern::create();
    mCamera->setPatternGenerator(generator, 1.0f / vec2(width, height));
}

void RaysRenderer::ConfigureDeferredProgram()
{
    const auto& program = mDeferredPass->getProgram();
    
    #define HANDLE_DEFINE(condition, literal)                                            \
        if (mRenderMode == RenderMode::Hybrid && condition) program->addDefine(literal); \
        else program->removeDefine(literal);

    HANDLE_DEFINE(mEnableRaytracedReflection, "RAYTRACE_REFLECTIONS");
    HANDLE_DEFINE(mEnableRaytracedShadows, "RAYTRACE_SHADOWS");
    HANDLE_DEFINE(mEnableRaytracedAO, "RAYTRACE_AO");
}

void RaysRenderer::onFrameRender(SampleCallbacks* sample, RenderContext* renderContext, const Fbo::SharedPtr& targetFbo)
{
    mCamera->beginFrame();
    mCamController.update();
    mSceneRenderer->update(sample->getCurrentTime());

    renderContext->clearFbo(targetFbo.get(), kSkyColor, 1.0f, 0u, FboAttachmentType::All);
    renderContext->clearFbo(mTAA.getActiveFbo().get(), kClearColor, 1.0f, 0u, FboAttachmentType::Color);

    if (mRenderMode == RenderMode::Forward)
    {
        PROFILE("Forward");

        mForwardState->setFbo(targetFbo);
        renderContext->setGraphicsState(mForwardState);
        renderContext->setGraphicsVars(mForwardVars);
        mSceneRenderer->renderScene(renderContext, mCamera.get());
    }
    else if (mRenderMode == RenderMode::Deferred)
    {
        PROFILE("Deferred");

        RenderGBuffer(renderContext);
        DeferredPass(renderContext, targetFbo);
    }
    else if (mRenderMode == RenderMode::Hybrid)
    {
        PROFILE("Hybrid");

        RenderGBuffer(renderContext);

        const auto& motionTex = mGBuffer->getColorTexture(GBuffer::MotionVector);
        const auto& linearZTex = mGBuffer->getColorTexture(GBuffer::SVGF_LinearZ);
        const auto& compactTex = mGBuffer->getColorTexture(GBuffer::SVGF_CompactNormDepth);

        if (mEnableRaytracedShadows)
        {
            RaytraceShadows(renderContext);
        }
        if (mEnableRaytracedReflection)
        {
            RaytraceReflection(renderContext);
        }
        if (mEnableRaytracedAO)
        {
            RaytraceAmbientOcclusion(renderContext);
        }
        if (mEnableRaytracedShadows && mEnableDenoiseShadows)
        {
            PROFILE("DenoiseShadows");
            mDenoisedShadowTexture = mShadowFilter->Execute(renderContext, mShadowTexture, motionTex, linearZTex, compactTex);
        }
        if (mEnableRaytracedReflection && mEnableDenoiseReflection)
        {
            PROFILE("DenoiseReflection");
            mDenoisedReflectionTexture = mReflectionFilter->Execute(renderContext, mReflectionTexture, motionTex, linearZTex, compactTex);
        }
        if (mEnableRaytracedAO && mEnableDenoiseAO)
        {
            PROFILE("DenoiseAO");
            mDenoisedAOTexture = mAOFilter->Execute(renderContext, mAOTexture, motionTex, linearZTex, compactTex);
        }

        DeferredPass(renderContext, targetFbo);
    }

    if (mEnableTAA)
    {
        RunTAA(renderContext, targetFbo);
    }

    mFrameCount++;
}

void RaysRenderer::RenderGBuffer(RenderContext* renderContext)
{
    PROFILE("GBuffer");
    mGBufferVars["PerFrameCB"]["gRenderTargetDim"] = glm::vec2(mGBuffer->getWidth(), mGBuffer->getHeight());

    mGBufferState->setFbo(mGBuffer);
    renderContext->clearFbo(mGBuffer.get(), kClearColor, 1.0f, 0u);
    renderContext->setGraphicsState(mGBufferState);
    renderContext->setGraphicsVars(mGBufferVars);
    mSceneRenderer->renderScene(renderContext, mCamera.get());
}

void RaysRenderer::ForwardRaytrace(RenderContext* renderContext)
{
    uint32_t width = mForwardRaytraceOutput->getWidth();
    uint32_t height = mForwardRaytraceOutput->getHeight();

    mForwardRaytraceVars->getRayGenVars()->setTexture("gOutput", mForwardRaytraceOutput);

    auto vars = mForwardRaytraceVars->getGlobalVars();
    vars["PerFrameCB"]["invView"] = glm::inverse(mCamera->getViewMatrix());
    vars["PerFrameCB"]["viewportDims"] = vec2(width, height);
    float fovY = focalLengthToFovY(mCamera->getFocalLength(), Camera::kDefaultFrameHeight);
    vars["PerFrameCB"]["tanHalfFovY"] = tanf(fovY * 0.5f);
    vars["PerFrameCB"]["gFrameCount"] = mFrameCount;

    renderContext->clearUAV(mForwardRaytraceOutput->getUAV().get(), kClearColor);
    mRaytracer->renderScene(renderContext, mForwardRaytraceVars, mForwardRaytraceState, uvec3(width, height, 1), mCamera.get());
}

void RaysRenderer::RaytraceShadows(RenderContext* renderContext)
{
    PROFILE("RaytraceShadows");

    uint32_t width = mShadowTexture->getWidth();
    uint32_t height = mShadowTexture->getHeight();

    mRtShadowVars->getRayGenVars()->setTexture("gOutput", mShadowTexture);
    mRtShadowVars->getRayGenVars()->setTexture("gGBuf0", mGBuffer->getColorTexture(GBuffer::WorldPosition));

    auto shadowVars = mRtShadowVars->getGlobalVars();
    shadowVars["PerFrameCB"]["gFrameCount"] = mFrameCount;

    renderContext->clearUAV(mShadowTexture->getUAV().get(), kClearColor);
    mRaytracer->renderScene(renderContext, mRtShadowVars, mRtShadowState, uvec3(width, height, 1), mCamera.get());
}

void RaysRenderer::RaytraceReflection(RenderContext* renderContext)
{
    PROFILE("RaytraceReflection");

    uint32_t width = mReflectionTexture->getWidth();
    uint32_t height = mReflectionTexture->getHeight();

    mRtReflectionVars->getRayGenVars()->setTexture("gOutput", mReflectionTexture);
    mRtReflectionVars->getRayGenVars()->setTexture("gGBuf0", mGBuffer->getColorTexture(GBuffer::WorldPosition));
    mRtReflectionVars->getRayGenVars()->setTexture("gGBuf1", mGBuffer->getColorTexture(GBuffer::NormalRoughness));
    mRtReflectionVars->getRayGenVars()->setTexture("gGBuf2", mGBuffer->getColorTexture(GBuffer::Albedo));

    auto reflectionVars = mRtReflectionVars->getGlobalVars();
    reflectionVars["PerFrameCB"]["gFrameCount"] = mFrameCount;

    renderContext->clearUAV(mReflectionTexture->getUAV().get(), kClearColor);
    mRaytracer->renderScene(renderContext, mRtReflectionVars, mRtReflectionState, uvec3(width, height, 1), mCamera.get());
}

void RaysRenderer::RaytraceAmbientOcclusion(RenderContext* renderContext)
{
    PROFILE("RaytraceAO");

    uint32_t width = mAOTexture->getWidth();
    uint32_t height = mAOTexture->getHeight();

    mRtAOVars->getRayGenVars()->setTexture("gOutput", mAOTexture);
    mRtAOVars->getRayGenVars()->setTexture("gGBuf0", mGBuffer->getColorTexture(GBuffer::WorldPosition));
    mRtAOVars->getRayGenVars()->setTexture("gGBuf1", mGBuffer->getColorTexture(GBuffer::NormalRoughness));

    auto aoVars = mRtAOVars->getGlobalVars();
    aoVars["PerFrameCB"]["gFrameCount"] = mFrameCount;
    aoVars["PerFrameCB"]["gAODistance"] = mAODistance;

    renderContext->clearUAV(mAOTexture->getUAV().get(), kClearColor);
    mRaytracer->renderScene(renderContext, mRtAOVars, mRtAOState, uvec3(width, height, 1), mCamera.get());
}

void RaysRenderer::DeferredPass(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo)
{
    PROFILE("DeferredPass");

    mCamera->setIntoConstantBuffer(mDeferredVars["InternalPerFrameCB"].get(), 0);
    mScene->getLight(0)->setIntoProgramVars(mDeferredVars.get(), mDeferredVars["InternalPerFrameCB"].get(), "gLights[0]");

    mDeferredVars->setTexture("gGBuf0", mGBuffer->getColorTexture(GBuffer::WorldPosition));
    mDeferredVars->setTexture("gGBuf1", mGBuffer->getColorTexture(GBuffer::NormalRoughness));
    mDeferredVars->setTexture("gGBuf2", mGBuffer->getColorTexture(GBuffer::Albedo));
    mDeferredVars->setTexture("gGBuf3", mGBuffer->getColorTexture(GBuffer::MotionVector));

    if (mRenderMode == RenderMode::Hybrid)
    {
        mDeferredVars->setTexture("gReflectionTexture", mEnableDenoiseReflection ? mDenoisedReflectionTexture : mReflectionTexture);
        mDeferredVars->setTexture("gShadowTexture", mEnableDenoiseShadows ? mDenoisedShadowTexture : mShadowTexture);
        mDeferredVars->setTexture("gAOTexture", mEnableDenoiseAO ? mDenoisedAOTexture : mAOTexture);
    }

    mDeferredState->setFbo(targetFbo);
    renderContext->setGraphicsState(mDeferredState);
    renderContext->setGraphicsVars(mDeferredVars);
    mDeferredPass->execute(renderContext);
}

void RaysRenderer::RunTAA(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo)
{
    PROFILE("TAA");

    const Texture::SharedPtr pCurColor = targetFbo->getColorTexture(0);
    const Texture::SharedPtr pMotionVec = mGBuffer->getColorTexture(GBuffer::MotionVector);
    const Texture::SharedPtr pPrevColor = mTAA.getInactiveFbo()->getColorTexture(0);

    renderContext->getGraphicsState()->pushFbo(mTAA.getActiveFbo());
    mTAA.pTAA->execute(renderContext, pCurColor, pPrevColor, pMotionVec);
    renderContext->getGraphicsState()->popFbo();

    renderContext->blit(mTAA.getActiveFbo()->getColorTexture(0)->getSRV(0, 1), targetFbo->getColorTexture(0)->getRTV());

    mTAA.switchFbos();
}

void RaysRenderer::onGuiRender(SampleCallbacks* sample, Gui* gui)
{
    if (gui->beginGroup("Rendering"))
    {
        if (mRenderMode == RenderMode::Hybrid)
        {
            if (gui->addCheckBox("Raytraced Reflection", mEnableRaytracedReflection))
            {
                ConfigureDeferredProgram();
            }
            if (gui->addCheckBox("Raytraced Shadows", mEnableRaytracedShadows))
            {
                ConfigureDeferredProgram();
            }
            if (gui->addCheckBox("Raytraced AO", mEnableRaytracedAO))
            {
                ConfigureDeferredProgram();
            }

            gui->addFloatSlider("AO Distance", mAODistance, 0.1f, 20.0f);

            gui->addCheckBox("Denoise Reflection", mEnableDenoiseReflection);
            gui->addCheckBox("Denoise Shadows", mEnableDenoiseShadows);
            gui->addCheckBox("Denoise AO", mEnableDenoiseAO);

            if (gui->beginGroup("Reflection Filter"))
            {
                mReflectionFilter->RenderGui(gui);
                gui->endGroup();
            }

            if (gui->beginGroup("Shadow Filter"))
            {
                mReflectionFilter->RenderGui(gui);
                gui->endGroup();
            }

            if (gui->beginGroup("AO Filter"))
            {
                mAOFilter->RenderGui(gui);
                gui->endGroup();
            }
        }

        gui->addCheckBox("TAA", mEnableTAA);
        gui->endGroup();
    }

    #define EDIT_MATERIAL(name, material)																		  \
        if (gui->beginGroup(name))																				  \
        {																										  \
            auto spec = material->getSpecularParams();															  \
            if (gui->addFloatSlider(name ## "_Roughness", spec.g, 0.0f, 1.0f)) material->setSpecularParams(spec); \
            if (gui->addFloatSlider(name ## "_Metalness", spec.b, 0.0f, 1.0f)) material->setSpecularParams(spec); \
            auto diff = material->getBaseColor();																  \
            if (gui->addRgbaColor(name ## "Diffuse", diff)) material->setBaseColor(diff);						  \
            gui->endGroup();																					  \
        }

    EDIT_MATERIAL("Ground", mGroundMaterial)
    EDIT_MATERIAL("Model", mBasicMaterial)
}

bool RaysRenderer::onKeyEvent(SampleCallbacks* sample, const KeyboardEvent& keyEvent)
{
    if (mCamController.onKeyEvent(keyEvent)) return true;

    if (keyEvent.type == KeyboardEvent::Type::KeyPressed)
    {
        if (keyEvent.key == KeyboardEvent::Key::R)
        {
            mRenderMode = (RenderMode)((mRenderMode + 1) % RenderMode::Count);
            ConfigureDeferredProgram();
            return true;
        }
    }
    return false;
}

bool RaysRenderer::onMouseEvent(SampleCallbacks* sample, const MouseEvent& mouseEvent)
{
    if (mCamController.onMouseEvent(mouseEvent)) return true;
    return false;
}

void RaysRenderer::onDataReload(SampleCallbacks* sample)
{
}

void RaysRenderer::onResizeSwapChain(SampleCallbacks* sample, uint32_t width, uint32_t height)
{
}

void RaysRenderer::onShutdown(SampleCallbacks* sample)
{
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    Logger::setVerbosity(Logger::Level::Error);

    RaysRenderer::UniquePtr pRenderer = std::make_unique<RaysRenderer>();
    SampleConfig config;
    config.windowDesc.title = "Rays Renderer";
    config.windowDesc.resizableWindow = true;
    Sample::run(config, pRenderer);
    return 0;
}
