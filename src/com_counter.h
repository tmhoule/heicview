#pragma once
#include <windows.h>

class COMCounter {
public:
    static void ObjectCreated()  { InterlockedIncrement(&s_objectCount); }
    static void ObjectDestroyed(){ InterlockedDecrement(&s_objectCount); }
    static void LockServer()     { InterlockedIncrement(&s_lockCount); }
    static void UnlockServer()   { InterlockedDecrement(&s_lockCount); }
    static bool CanUnload()      { return s_objectCount == 0 && s_lockCount == 0; }

private:
    static inline volatile LONG s_objectCount = 0;
    static inline volatile LONG s_lockCount = 0;
};
