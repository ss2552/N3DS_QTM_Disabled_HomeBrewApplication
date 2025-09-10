#include <3ds/types.h>
#include <3ds/svc.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <memory.h>

void remote_play_DoQTMPatch(void);
void print(char *msg, ...);

extern bool qtmDisabled;
Handle hProcess;

s32 ret;

#define COPY_REMOTE_MEMORY_TIMEOUT (100000000)

u32 copyRemoteMemoryTimeout(Handle hDst, void *ptrDst, Handle hSrc, void *ptrSrc, u32 size, s64 timeout)
{
    u8 dmaConfig[sizeof(DmaConfig)] = {-1, 0, 4};
    u32 hdma = 0;
    u32 ret;

    ret = svcFlushProcessDataCache(hSrc, (u32)ptrSrc, size);
    if (ret != 0)
    {
        print("svcFlushProcessDataCache src failed: %lu", ret);
        return ret;
    }
    ret = svcFlushProcessDataCache(hDst, (u32)ptrDst, size);
    if (ret != 0)
    {
        print("svcFlushProcessDataCache dst failed: %lu", ret);
        return ret;
    }

    ret = svcStartInterProcessDma(&hdma, hDst, (u32)ptrDst, hSrc, (u32)ptrSrc, size, (DmaConfig *)dmaConfig);
    if (ret != 0)
    {
        print("svcStartInterProcessDma failed: %lu", ret);
        return ret;
    }
    ret = svcWaitSynchronization(hdma, timeout);
    if (ret != 0)
    {
        print("copyRemoteMemory time out (or error) %lu", ret);
        svcCloseHandle(hdma);
        return 1;
    }

    svcCloseHandle(hdma);
    ret = svcInvalidateProcessDataCache(hDst, (u32)ptrDst, size);
    if (ret != 0)
    {
        print("svcInvalidateProcessDataCache failed: %lu", ret);
        return ret;
    }
    return 0;
}

#define REMOTE_PLAY_QTM_HDR_SIZE (4)
#define REMOTE_PLAY_QTM_PAYLOAD_SIZE (32)

typedef enum ProcessOp
{
    PROCESSOP_GET_ALL_HANDLES,
    PROCESSOP_SET_MMU_TO_RWX,
    PROCESSOP_GET_ON_MEMORY_CHANGE_EVENT,
    PROCESSOP_SIGNAL_ON_EXIT,
    PROCESSOP_GET_PA_FROM_VA,
    PROCESSOP_SCHEDULE_THREADS
} ProcessOp;

Result svcControlProcess(Handle process, ProcessOp op, u32 varg2, u32 varg3);

u32 rtGetPageOfAddress(u32 addr)
{
#define PAGE_OF_ADDR(addr) ((addr) / 0x1000 * 0x1000)
    return PAGE_OF_ADDR(addr);
}

u32 protectRemoteMemory(Handle hProcess, void *addr, u32 size, u32 perm)
{
    return svcControlProcessMemory(hProcess, (u32)addr, 0, size, MEMOP_PROT, perm);
}

u32 rtCheckRemoteMemory(Handle hProcess, u32 addr, u32 size, MemPerm perm)
{
    MemInfo memInfo;
    PageInfo pageInfo;
    s32 ret = svcQueryMemory(&memInfo, &pageInfo, addr);
    if (ret != 0)
    {
        print("svcQueryMemory failed for addr %08: %08", addr, ret);
        return ret;
    }
    if (memInfo.perm == 0)
    {
        return -1;
    }
    if (memInfo.base_addr + memInfo.size < addr + size)
    {
        return -1;
    }

    if (perm & MEMPERM_WRITE)
        perm |= MEMPERM_READ;
    if ((memInfo.perm & perm) == perm)
    {
        return 0;
    }

    perm |= memInfo.perm;

    u32 startPage, endPage;

    startPage = rtGetPageOfAddress(addr);
    endPage = rtGetPageOfAddress(addr + size - 1);
    size = endPage - startPage + 0x1000;

    ret = protectRemoteMemory(hProcess, (void *)startPage, size, perm);
    return ret;
}

void remote_play_DoQTMPatch(void)
{
#define QTM_PROCESS 0x15
#define REMOTE_PC 0x00119a48
#define RP_QTM_HDR_SIZE (4)
#define RP_QTM_PAYLOAD_SIZE (32)

    // rt == リソース タイプ

    if ((ret = svcOpenProcess(&hProcess, QTM_PROCESS)) != 0)
    {
        print("Open QTM process failed: %lu", ret);
        return;
    }

    if ((ret = svcControlProcess(hProcess, PROCESSOP_SCHEDULE_THREADS, 1, 0)) != 0)
    {
        print("Locking QTM failed: %lu", ret);
        if (hProcess)
            svcCloseHandle(hProcess);
    }

    if ((ret = rtCheckRemoteMemory(hProcess, REMOTE_PC, RP_QTM_HDR_SIZE, MEMPERM_READWRITE | MEMPERM_EXECUTE)) != 0)
    {
        print("QTM protectRemoteMemory failed: %lu", ret);
        goto final_unlock;
    }

    u32 qtmPayloadAddr = 0x001ac000 - RP_QTM_PAYLOAD_SIZE;
    // エラー const u32 qtmPayloadAddr = 0x001abfe0; // QTMの先頭アドレス
    // const u32 qtmPayloadAddr = 1abfce

    {// rtCheckRemoteMemory
        u32 addr = qtmPayloadAddr;
        u32 size = RP_QTM_PAYLOAD_SIZE;
        Memperm perm = MEMPERM_READWRITE | MEMPERM_EXECUTE
        
        MemInfo memInfo;
        PageInfo pageInfo;

        if ((ret = svcQueryMemory(&memInfo, &pageInfo, addr)) != 0){
            print("svcQueryMemory failed for addr %08: %08", addr, ret);
            goto final_unlock;
        }
        
        perm |= memInfo.perm;
        
        u32 startPage, endPage;

        startPage = rtGetPageOfAddress(addr);
        endPage = rtGetPageOfAddress(addr + size - 1);
        size = endPage - startPage + 0x1000;

        //ret = protectRemoteMemory(hProcess, (void *)startPage, size, perm);
        {
            
            void *addr = (void *)startPage;
            
            if ((ret = svcControlProcessMemory(hProcess, (u32)addr, 0, size, MEMOP_PROT, perm) != 0){
                print("FATAIL: %lu", ret);
                goto final_unlock;
            }
            
        }
    }
    
    u8 payload[RP_QTM_PAYLOAD_SIZE] = {
        0x01, 0x01, 0xA0, 0xE3, // mov r0, #0x40000000
        0x00, 0x10, 0xA0, 0xE3, // mov r1, #0
        0x0A, 0x00, 0x00, 0xEF, // svc #0xa
        0x00, 0x20, 0xA0, 0xE3, // mov r2, #0
        0x00, 0x30, 0xA0, 0xE3, // mov r3, #0
        0x0F, 0x00, 0x85, 0xE8, // stm r5, {r0, r1, r2, r3}
        0x70, 0x80, 0xBD, 0xE8, // ldmia sp!, {r4, r5, r6, pc}
        0x00, 0xF0, 0x20, 0xE3, // nop
    };

    if ((ret = copyRemoteMemoryTimeout(hProcess, (void *)qtmPayloadAddr, CUR_PROCESS_HANDLE, payload, RP_QTM_PAYLOAD_SIZE, COPY_REMOTE_MEMORY_TIMEOUT)) != 0)
    {
        print("Write QTM memory for payload at %lu failed: %lu", qtmPayloadAddr, ret);
        goto final_unlock;
    }

    u32 branchDistance = qtmPayloadAddr - REMOTE_PC;
    u32 replacementInst = (branchDistance / 4 - 2) | 0xea000000; // b inst
    ret = copyRemoteMemoryTimeout(hProcess, (void *)REMOTE_PC, CUR_PROCESS_HANDLE, &replacementInst, RP_QTM_HDR_SIZE, COPY_REMOTE_MEMORY_TIMEOUT);
    if (ret != 0)
    {
        print("Write QTM memory at %lu failed: %lu", REMOTE_PC, ret);
        goto final_unlock;
    }

    qtmDisabled = 1;
    print("Patch QTM success");
final_unlock:
    ret = svcControlProcess(hProcess, PROCESSOP_SCHEDULE_THREADS, 0, 0);
    if (ret != 0)
    {
        print("Unlocking QTM process failed: %lu", ret);
    }
    if (hProcess)
        svcCloseHandle(hProcess);
}
