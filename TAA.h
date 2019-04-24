#pragma once

#include "Falcor.h"

class TAA
{
public:
    Falcor::TemporalAA::SharedPtr pTAA;
    Falcor::Fbo::SharedPtr getActiveFbo() { return pTAAFbos[activeFboIndex]; }
    Falcor::Fbo::SharedPtr getInactiveFbo()  { return pTAAFbos[1 - activeFboIndex]; }
    void createFbos(uint32_t width, uint32_t height, const Falcor::Fbo::Desc & fboDesc)
    {
        pTAAFbos[0] = Falcor::FboHelper::create2D(width, height, fboDesc);
        pTAAFbos[1] = Falcor::FboHelper::create2D(width, height, fboDesc);
    }

    void switchFbos() { activeFboIndex = 1 - activeFboIndex; }
    void resetFbos()
    {
        activeFboIndex = 0;
        pTAAFbos[0] = nullptr;
        pTAAFbos[1] = nullptr;
    }

    void resetFboActiveIndex() { activeFboIndex = 0;}

private:
    Falcor::Fbo::SharedPtr pTAAFbos[2];
    uint32_t activeFboIndex = 0;
};
