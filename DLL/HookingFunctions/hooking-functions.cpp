#include "pch.h"

// Forgotten PPC intrinsic
#define __isync() __emit(0x4C00012C)

const DWORD MW2_TITLE_ID = 0x41560817;

// Imports from the Xbox libraries
extern "C" 
{
    DWORD XamGetCurrentTitleId();

    DWORD __stdcall ExCreateThread(
        PHANDLE pHandle,
        DWORD dwStackSize,
        LPDWORD lpThreadId,
        PVOID apiThreadStartup,
        LPTHREAD_START_ROUTINE lpStartAddress,
        LPVOID lpParameters,
        DWORD dwCreationFlagsMod
    );

    VOID DbgPrint(LPCSTR s, ...);
}

VOID PatchInJump(LPDWORD address, DWORD destination, BOOL linked)
{
    DWORD writeBuffer;

    if (destination & 0x8000)
        writeBuffer = 0x3D600000 + (((destination >> 16) & 0xFFFF) + 1);
    else
        writeBuffer = 0x3D600000 + ((destination >> 16) & 0xFFFF);

    address[0] = writeBuffer;
    writeBuffer = 0x396B0000 + (destination & 0xFFFF);
    address[1] = writeBuffer;
    writeBuffer = 0x7D6903A6;
    address[2] = writeBuffer;

    if (linked)
        writeBuffer = 0x4E800421;
    else
        writeBuffer = 0x4E800420;

    address[3] = writeBuffer;

    __dcbst(0, address);
    __sync();
    __isync();
}

VOID __declspec(naked) GLPR()
{
    __asm
    {
        std     r14, -0x98(sp)
        std     r15, -0x90(sp)
        std     r16, -0x88(sp)
        std     r17, -0x80(sp)
        std     r18, -0x78(sp)
        std     r19, -0x70(sp)
        std     r20, -0x68(sp)
        std     r21, -0x60(sp)
        std     r22, -0x58(sp)
        std     r23, -0x50(sp)
        std     r24, -0x48(sp)
        std     r25, -0x40(sp)
        std     r26, -0x38(sp)
        std     r27, -0x30(sp)
        std     r28, -0x28(sp)
        std     r29, -0x20(sp)
        std     r30, -0x18(sp)
        std     r31, -0x10(sp)
        stw     r12, -0x8(sp)
        blr
    }
}

DWORD RelinkGPLR(INT offset, LPDWORD saveStubAddr, LPDWORD orgAddr)
{
    DWORD inst = 0, repl;
    INT i;
    LPDWORD saver = (LPDWORD)GLPR;

    if (offset & 0x2000000)
        offset = offset | 0xFC000000;

    repl = orgAddr[offset / 4];

    for (i = 0; i < 20; i++)
    {
        if (repl == saver[i])
        {
            INT newOffset = (INT)&saver[i] - (INT)saveStubAddr;
            inst = 0x48000001 | (newOffset & 0x3FFFFFC);
        }
    }

    return inst;
}

VOID HookFunctionStart(LPDWORD address, LPDWORD saveStub, DWORD destination)
{
    if (saveStub != NULL && address != NULL)
    {
        INT i;
        DWORD addrReloc = (DWORD)(&address[4]);
        DWORD writeBuffer;

        if (addrReloc & 0x8000)
            writeBuffer = 0x3D600000 + (((addrReloc >> 16) & 0xFFFF) + 1);
        else
            writeBuffer = 0x3D600000 + ((addrReloc >> 16) & 0xFFFF);

        saveStub[0] = writeBuffer;
        writeBuffer = 0x396B0000 + (addrReloc & 0xFFFF);
        saveStub[1] = writeBuffer;
        writeBuffer = 0x7D6903A6;
        saveStub[2] = writeBuffer;
    
        for (i = 0; i < 4; i++)
        {
            if ((address[i] & 0x48000003) == 0x48000001)
            {
                writeBuffer = RelinkGPLR((address[i] &~ 0x48000003), &saveStub[i + 3], &address[i]);
                saveStub[i + 3] = writeBuffer;
            }
            else
            {
                writeBuffer = address[i];
                saveStub[i + 3] = writeBuffer;
            }
        }

        writeBuffer = 0x4E800420;
        saveStub[7] = writeBuffer;

        __dcbst(0, saveStub);
        __sync();
        __isync();

        PatchInJump(address, destination, FALSE);
    }
}

// Function we found in the previous section
VOID (*SV_GameSendServerCommand)(INT clientNum, INT type, LPCSTR text) = (VOID(*)(INT, INT, LPCSTR))0x822548D8;

__declspec(naked) VOID SV_ExecuteClientCommandStub(INT client, LPCSTR s, INT clientOK, INT fromOldServer)
{
    // The stub needs to, at least, contain 7 instructions
    __asm
    {
        nop
        nop
        nop
        nop
        nop
        nop
        nop
    }
}

VOID SV_ExecuteClientCommandHook(INT client, LPCSTR s, INT clientOK, INT fromOldServer)
{
    // Calling the original SV_ExecuteClientCommand function
    SV_ExecuteClientCommandStub(client, s, clientOK, fromOldServer);

    // Printing the command in the killfeed
    std::string command = "f \"";
    command += s;
    command += "\"";
    SV_GameSendServerCommand(-1, 0, command.c_str());
}

// Sets up the hook
VOID InitMW2()
{
    // Waiting a little bit for the game to be fully loaded in memory
    Sleep(200);

    const DWORD SV_ExecuteClientCommandAddr = 0x82253140;

    // Hooking SV_ExecuteClientCommand
    HookFunctionStart((LPDWORD)SV_ExecuteClientCommandAddr, (LPDWORD)SV_ExecuteClientCommandStub, (DWORD)SV_ExecuteClientCommandHook);
}

DWORD MonitorTitleId(LPVOID lpThreadParameter)
{
    DWORD currentTitle;

    while (true)
    {
        DWORD newTitle = XamGetCurrentTitleId();

        if (newTitle != currentTitle)
        {
            currentTitle = newTitle;

            switch (newTitle)
            {
                case MW2_TITLE_ID:
                    InitMW2();
                    break;
            }
        }
    }

    return 0;
}

BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
    switch (fdwReason) 
    {
        case DLL_PROCESS_ATTACH:
            // Runs MonitorTitleId in separate thread
            ExCreateThread(nullptr, 0, nullptr, nullptr, (LPTHREAD_START_ROUTINE)MonitorTitleId, nullptr, 2);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}