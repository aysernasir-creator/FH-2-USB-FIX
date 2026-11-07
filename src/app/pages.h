// SPDX-License-Identifier: MIT
// Factory functions of the settings window tab pages.
#pragma once
#include "page.h"

namespace app {

Page* NewHeadPage(int index);
Page* NewWheelPage(int index);
Page* NewSimPage(int index);
Page* NewDrawDistPage(int index);
Page* NewMainPage(int index);

// Main page services used by main.cpp
class MainPageApi {
public:
    virtual void AddStatusLine(const std::wstring& line) = 0;       // injector status (any thread-safe caller: UI thread)
    virtual void SetAzimuthPath(const std::wstring& path, bool save) = 0;
    virtual bool StartAzimuthNow() = 0;                              // button action, also used for the command line
protected:
    ~MainPageApi() {}
};
MainPageApi* AsMainApi(Page* p);

} // namespace app
