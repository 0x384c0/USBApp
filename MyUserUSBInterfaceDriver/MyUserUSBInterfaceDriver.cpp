//
//  MyUserUSBInterfaceDriver.cpp
//  MyUserUSBInterfaceDriver
//
//  Created by Scott Knight on 6/13/19.
//  Copyright © 2019 Scott Knight. All rights reserved.
//


#include <os/log.h>

#include <DriverKit/IOLib.h>

#include <DriverKit/IOUserServer.h>
#include <DriverKit/IOLib.h>
#include <USBDriverKit/IOUSBHostInterface.h>
#include <USBDriverKit/IOUSBHostPipe.h>
#import <DriverKit/IOBufferMemoryDescriptor.h>

#include "MyUserUSBInterfaceDriver.h"

#define Log(fmt, ...) os_log(OS_LOG_DEFAULT, "NullDriver Base - " fmt "\n", ##__VA_ARGS__)

#define JSON_PAYLOAD "{\"message_type\":1,\"device_type\":1,\"rnr_id\":1,\"data\":{\"options\":{}}}"


struct MyUserUSBInterfaceDriver_IVars {
    IOUSBHostInterface* usbInterface;
};


// MARK: Dext Lifecycle Management
bool MyUserUSBInterfaceDriver::init(void)
{
    bool result = false;

    Log("init()");

    result = super::init();
    if (result != true)
    {
        Log("init() - super::init failed.");
        goto Exit;
    }

    ivars = IONewZero(MyUserUSBInterfaceDriver_IVars, 1);
    if (ivars == nullptr)
    {
        Log("init() - Failed to allocate memory for ivars.");
        goto Exit;
    }

    Log("init() - Finished.");
    return true;

Exit:
    return false;
}

// log stream | grep "com.0x384c0.mac.driver.test"

kern_return_t MyUserUSBInterfaceDriver::Start_Impl(IOService* provider)
{
    Log("Start() - Entered Start_Impl");
    
    ivars->usbInterface = OSDynamicCast(IOUSBHostInterface, provider);
    if (!ivars->usbInterface) {
        Log("Start() - OSDynamicCast to IOUSBHostInterface failed");
        return false;
    }

    kern_return_t ret = ivars->usbInterface->Open(this, 0, nullptr);
    Log("Start() - usbInterface->Open returned 0x%x", ret);
    if (ret != kIOReturnSuccess) {
        Log("Start() - Failed to open usbInterface");
        return false;
    }

    // Find OUT and IN pipes (assume pipe 1 is OUT, pipe 2 is IN for example)
    
    IOUSBHostPipe* inPipe = nullptr;
    ret = ivars->usbInterface->CopyPipe(0x81, &inPipe); // IN endpoint
    Log("Start() - CopyPipe(2) IN returned 0x%x, inPipe=%p", ret, inPipe);
    if (ret != kIOReturnSuccess || !inPipe) {
        Log("Start() - Failed to get IN pipe");
        ivars->usbInterface->Close(this, 0);
        return false;
    }
    
    IOUSBHostPipe* outPipe = nullptr;
    ret = ivars->usbInterface->CopyPipe(0x1, &outPipe); // OUT endpoint
    Log("Start() - CopyPipe(1) OUT returned 0x%x, outPipe=%p", ret, outPipe);
    if (ret != kIOReturnSuccess || !outPipe) {
        Log("Start() - Failed to get OUT pipe");
        ivars->usbInterface->Close(this, 0);
        return false;
    }

    size_t length = strlen(JSON_PAYLOAD);
    IOBufferMemoryDescriptor* outBuffer = nullptr;
    ret = IOBufferMemoryDescriptor::Create(kIOMemoryDirectionOut, length, 0, &outBuffer);
    Log("Start() - IOBufferMemoryDescriptor::Create OUT returned 0x%x, outBuffer=%p", ret, outBuffer);
    if (ret != kIOReturnSuccess || !outBuffer) {
        Log("Start() - Failed to create outBuffer");
        outPipe->release();
        inPipe->release();
        ivars->usbInterface->Close(this, 0);
        return false;
    }
    IOAddressSegment outSeg = {};
    outBuffer->GetAddressRange(&outSeg);
    Log("Start() - outBuffer address: 0x%llx, length: %zu", outSeg.address, length);
    memcpy((void*)outSeg.address, JSON_PAYLOAD, length);

    // Send payload to OUT pipe
    ret = outPipe->IO(outBuffer, uint32_t(length), nullptr, 1000);
    Log("Start() - outPipe->IO returned 0x%x", ret);
    if (ret != kIOReturnSuccess) {
        Log("Start() - Failed to write to OUT pipe");
        outBuffer->release();
        outPipe->release();
        inPipe->release();
        ivars->usbInterface->Close(this, 0);
        return false;
    }

    // Prepare buffer for response
    const size_t maxPacketSize = 512; // adjust as needed
    IOBufferMemoryDescriptor* inBuffer = nullptr;
    ret = IOBufferMemoryDescriptor::Create(kIOMemoryDirectionIn, maxPacketSize, 0, &inBuffer);
    Log("Start() - IOBufferMemoryDescriptor::Create IN returned 0x%x, inBuffer=%p", ret, inBuffer);
    if (ret != kIOReturnSuccess || !inBuffer) {
        Log("Start() - Failed to create inBuffer");
        outBuffer->release();
        outPipe->release();
        inPipe->release();
        ivars->usbInterface->Close(this, 0);
        return false;
    }

    // Read response from IN pipe
    ret = inPipe->IO(inBuffer, maxPacketSize, nullptr, 1000);
    Log("Start() - inPipe->IO returned 0x%x", ret);
    if (ret == kIOReturnSuccess) {
        // Print response as string (ensure null-terminated)
        IOAddressSegment inSeg = {};
        inBuffer->GetAddressRange(&inSeg);
        char* resp = (char*)inSeg.address;
        uint64_t respLen;
        inBuffer->GetLength(&respLen);
        char printBuf[513] = {0};
        size_t copyLen = respLen < 512 ? respLen : 512;
        memcpy(printBuf, resp, copyLen);
        printBuf[copyLen] = '\0';
        Log("Received response: %{public}s", printBuf);
        Log("Start() - Response length: %llu", respLen);
    } else {
        Log("Failed to read response from IN pipe");
    }

    // Continuously read input and log it, but stop after ~5 seconds (50 × 100ms)
    const int maxLoops = 10;
    for (int loop = 0; loop < maxLoops; ++loop) {
        ret = inPipe->IO(inBuffer, maxPacketSize, nullptr, 1000);
        Log("Continuous read - inPipe->IO returned 0x%x", ret);
        if (ret == kIOReturnSuccess) {
            IOAddressSegment inSeg = {};
            inBuffer->GetAddressRange(&inSeg);
            char* resp = (char*)inSeg.address;
            uint64_t respLen;
            inBuffer->GetLength(&respLen);
            char printBuf[513] = {0};
            size_t copyLen = respLen < 512 ? respLen : 512;
            memcpy(printBuf, resp, copyLen);
            printBuf[copyLen] = '\0';
            Log("Continuous read - Received: %{public}s", printBuf);
            Log("Continuous read - Response length: %llu", respLen);
        } else {
            Log("Continuous read - Failed to read from IN pipe, ret=0x%x", ret);
        }
        IOSleep(100); // Sleep 100ms between reads
    }

    // Cleanup
    Log("Start() - Cleaning up resources");
    outBuffer->release();
    inBuffer->release();
    outPipe->release();
    inPipe->release();

    ivars->usbInterface->Close(this, 0);
    Log("Start() - Exiting Start_Impl with ret=0x%x", ret);

    return (ret == kIOReturnSuccess);
}

kern_return_t MyUserUSBInterfaceDriver::Stop_Impl(IOService* provider)
{
    super::Stop(provider);
    return 0;
}
