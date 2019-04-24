#pragma once

#include "Falcor.h"
#include "FalcorExperimental.h"
#include "TAA.h"

using namespace Falcor;

class RaysRenderer : public Renderer
{
public:
    void onLoad(SampleCallbacks* sample, RenderContext* renderContext) override;
    void onFrameRender(SampleCallbacks* sample, RenderContext* renderContext, const Fbo::SharedPtr& targetFbo) override;
    void onShutdown(SampleCallbacks* sample) override;
    void onResizeSwapChain(SampleCallbacks* sample, uint32_t width, uint32_t height) override;
    bool onKeyEvent(SampleCallbacks* sample, const KeyboardEvent& keyEvent) override;
    bool onMouseEvent(SampleCallbacks* sample, const MouseEvent& mouseEvent) override;
    void onDataReload(SampleCallbacks* sample) override;
    void onGuiRender(SampleCallbacks* sample, Gui* gui) override;

private:
    void SetupScene();
    void SetupRendering(uint32_t width, uint32_t height);
    void SetupRaytracing(uint32_t width, uint32_t height);
    void SetupTAA(uint32_t width, uint32_t height);
    void ConfigureDeferredProgram();

    void DeferredPass(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo);
    void ForwardRaytrace(RenderContext* renderContext);
    void RaytraceShadows(RenderContext* renderContext);
    void RaytraceReflection(RenderContext* renderContext);
    void RunTAA(RenderContext* renderContext, const Fbo::SharedPtr& colorFbo);

    RtScene::SharedPtr mScene;
    Material::SharedPtr mBasicMaterial;
    Material::SharedPtr mGroundMaterial;

    Camera::SharedPtr mCamera;
    FirstPersonCameraController mCamController;

    SceneRenderer::SharedPtr mSceneRenderer;
    RtSceneRenderer::SharedPtr mRaytracer;

    RtProgram::SharedPtr mForwardRaytraceProgram;
    RtProgramVars::SharedPtr mForwardRaytraceVars;
    RtState::SharedPtr mForwardRaytraceState;
    Texture::SharedPtr mForwardRaytraceOutput;

    RtProgram::SharedPtr mRtReflectionProgram;
    RtProgramVars::SharedPtr mRtReflectionVars;
    RtState::SharedPtr mRtReflectionState;
    Texture::SharedPtr mReflectionTexture;

    RtProgram::SharedPtr mRtShadowProgram;
    RtProgramVars::SharedPtr mRtShadowVars;
    RtState::SharedPtr mRtShadowState;
    Texture::SharedPtr mShadowTexture;

    GraphicsProgram::SharedPtr mForwardProgram;
    GraphicsVars::SharedPtr mForwardVars;
    GraphicsState::SharedPtr mForwardState;

    GraphicsProgram::SharedPtr mGBufferProgram;
    GraphicsVars::SharedPtr mGBufferVars;
    GraphicsState::SharedPtr mGBufferState;
    Fbo::SharedPtr mGBuffer;

    FullScreenPass::UniquePtr mDeferredPass;
    GraphicsVars::SharedPtr mDeferredVars;
    GraphicsState::SharedPtr mDeferredState;

    TAA mTAA;

    enum RenderMode : uint32_t { Forward = 0, Deferred, Hybrid, Count };
    RenderMode mRenderMode;

    bool mEnableRaytracedShadows;
    bool mEnableRaytracedReflection;

    uint32_t mFrameCount;
};
