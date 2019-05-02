//
//  AsusSMC.cpp
//  AsusSMC
//
//  Copyright © 2018 Le Bao Hiep
//

#include "AsusSMC.hpp"

bool ADDPR(debugEnabled) = true;
uint32_t ADDPR(debugPrintDelay) = 0;

#pragma mark -
#pragma mark WMI functions ported from Linux
#pragma mark -

int AsusSMC::wmi_data2Str(const char *in, char *out) {
    int i;

    for (i = 3; i >= 0; i--)
        out += snprintf(out, 3, "%02X", in[i] & 0xFF);

    out += snprintf(out, 2, "-");
    out += snprintf(out, 3, "%02X", in[5] & 0xFF);
    out += snprintf(out, 3, "%02X", in[4] & 0xFF);
    out += snprintf(out, 2, "-");
    out += snprintf(out, 3, "%02X", in[7] & 0xFF);
    out += snprintf(out, 3, "%02X", in[6] & 0xFF);
    out += snprintf(out, 2, "-");
    out += snprintf(out, 3, "%02X", in[8] & 0xFF);
    out += snprintf(out, 3, "%02X", in[9] & 0xFF);
    out += snprintf(out, 2, "-");

    for (i = 10; i <= 15; i++)
        out += snprintf(out, 3, "%02X", in[i] & 0xFF);

    *out = '\0';
    return 0;
}

OSString * AsusSMC::flagsToStr(UInt8 flags) {
    char str[80];
    char *pos = str;
    if (flags != 0) {
        if (flags & ACPI_WMI_EXPENSIVE) {
            lilu_os_strncpy(pos, "ACPI_WMI_EXPENSIVE ", 20);
            pos += strlen(pos);
        }
        if (flags & ACPI_WMI_METHOD) {
            lilu_os_strncpy(pos, "ACPI_WMI_METHOD ", 20);
            pos += strlen(pos);
            DBGLOG("guid", "WMI METHOD");
        }
        if (flags & ACPI_WMI_STRING) {
            lilu_os_strncpy(pos, "ACPI_WMI_STRING ", 20);
            pos += strlen(pos);
        }
        if (flags & ACPI_WMI_EVENT) {
            lilu_os_strncpy(pos, "ACPI_WMI_EVENT ", 20);
            pos += strlen(pos);
            DBGLOG("guid", "WMI EVENT");
        }
        //suppress the last trailing space
        str[strlen(str)] = 0;
    }
    else
        str[0] = 0;
    return (OSString::withCString(str));
}

void AsusSMC::wmi_wdg2reg(struct guid_block *g, OSArray *array, OSArray *dataArray) {
    char guid_string[37];
    char object_id_string[3];
    OSDictionary *dict = OSDictionary::withCapacity(6);

    wmi_data2Str(g->guid, guid_string);

    dict->setObject("UUID", OSString::withCString(guid_string));
    if (g->flags & ACPI_WMI_EVENT)
        dict->setObject("notify_value", OSNumber::withNumber(g->notify_id, 8));
    else {
        snprintf(object_id_string, 3, "%c%c", g->object_id[0], g->object_id[1]);
        dict->setObject("object_id", OSString::withCString(object_id_string));
    }
    dict->setObject("instance_count", OSNumber::withNumber(g->instance_count, 8));
    dict->setObject("flags", OSNumber::withNumber(g->flags, 8));
#if DEBUG
    dict->setObject("flags Str", flagsToStr(g->flags));
#endif
    if (g->flags == 0)
        dataArray->setObject(readDataBlock(object_id_string));

    array->setObject(dict);
}

OSDictionary * AsusSMC::readDataBlock(char *str) {
    OSObject    *wqxx;
    OSData        *data = NULL;
    OSDictionary *dict;
    char name[5];

    snprintf(name, 5, "WQ%s", str);
    dict = OSDictionary::withCapacity(1);

    do {
        if (atkDevice->evaluateObject(name, &wqxx) != kIOReturnSuccess) {
            SYSLOG("guid", "No object of method %s", name);
            continue;
        }

        data = OSDynamicCast(OSData, wqxx);
        if (data == NULL) {
            SYSLOG("guid", "Cast error %s", name);
            continue;
        }
        dict->setObject(name, data);
    } while (false);
    return dict;
}

int AsusSMC::parse_wdg(OSDictionary *dict) {
    UInt32 i, total;
    OSObject *wdg;
    OSData *data;
    OSArray *array, *dataArray;

    do {
        if (atkDevice->evaluateObject("_WDG", &wdg) != kIOReturnSuccess) {
            SYSLOG("guid", "No object of method _WDG");
            continue;
        }

        data = OSDynamicCast(OSData, wdg);
        if (data == NULL) {
            SYSLOG("guid", "Cast error _WDG");
            continue;
        }
        total = data->getLength() / sizeof(struct guid_block);
        array = OSArray::withCapacity(total);
        dataArray = OSArray::withCapacity(1);

        for (i = 0; i < total; i++)
            wmi_wdg2reg((struct guid_block *) data->getBytesNoCopy(i * sizeof(struct guid_block), sizeof(struct guid_block)), array, dataArray);
        setProperty("WDG", array);
        setProperty("DataBlocks", dataArray);
        data->release();
    } while (false);

    return 0;
}

OSDictionary* AsusSMC::getDictByUUID(const char * guid) {
    UInt32 i;
    OSDictionary *dict = NULL;
    OSString *uuid;
    OSArray *array = OSDynamicCast(OSArray, properties->getObject("WDG"));
    if (NULL == array)
        return NULL;
    for (i = 0; i < array->getCount(); i++) {
        dict = OSDynamicCast(OSDictionary, array->getObject(i));
        uuid = OSDynamicCast(OSString, dict->getObject("UUID"));
        if (uuid->isEqualTo(guid)) {
            break;
        }
    }
    return dict;
}

#pragma mark -
#pragma mark IOService overloading
#pragma mark -

#define super IOService

OSDefineMetaClassAndStructors(AsusSMC, IOService)

bool AsusSMC::init(OSDictionary *dict) {
    _notificationServices = OSSet::withCapacity(1);

    kev.setVendorID("com.hieplpvip");
    kev.setEventCode(AsusSMCEventCode);

    bool result = super::init(dict);
    properties = dict;
    DBGLOG("atk", "Init AsusSMC");
    return result;
}

IOService * AsusSMC::probe(IOService *provider, SInt32 *score) {
    IOService * ret = NULL;
    OSObject * obj;
    OSString * name;
    IOACPIPlatformDevice *dev;
    do {
        if (!super::probe(provider, score))
            continue;

        dev = OSDynamicCast(IOACPIPlatformDevice, provider);
        if (NULL == dev)
            continue;

        dev->evaluateObject("_UID", &obj);

        name = OSDynamicCast(OSString, obj);
        if (NULL == name)
            continue;

        if (name->isEqualTo("ATK")) {
            *score +=20;
            ret = this;
        }
        name->release();
    } while (false);

    return (ret);
}

bool AsusSMC::start(IOService *provider) {
    if (!provider || !super::start(provider)) {
        SYSLOG("atk", "%s::Error loading kext");
        return false;
    }

    atkDevice = (IOACPIPlatformDevice *) provider;
    atkDevice->evaluateObject("INIT", NULL, NULL, NULL);

    SYSLOG("atk", "Found WMI Device %s", atkDevice->getName());

    parse_wdg(properties);

    checkKBALS();

    initVirtualKeyboard();

    registerNotifications();

    registerVSMC();

    PMinit();
    registerPowerDriver(this, powerStateArray, kAsusSMCIOPMNumberPowerStates);
    provider->joinPMtree(this);

    this->registerService(0);

    workloop = getWorkLoop();
    if (!workloop) {
        DBGLOG("atk", "Failed to get workloop!");
        return false;
    }
    workloop->retain();

    command_gate = IOCommandGate::commandGate(this);
    if (!command_gate)
        return false;

    workloop->addEventSource(command_gate);

    setProperty("TouchpadEnabled", true);

    setProperty("Copyright", "Copyright © 2018 hieplpvip");

    return true;
}

void AsusSMC::stop(IOService *provider) {
    DBGLOG("atk", "Stop");

    if (poller)
        poller->cancelTimeout();
    if (workloop && poller)
        workloop->removeEventSource(poller);
    if (workloop && command_gate)
        workloop->removeEventSource(command_gate);
    OSSafeReleaseNULL(workloop);
    OSSafeReleaseNULL(poller);
    OSSafeReleaseNULL(command_gate);

    _publishNotify->remove();
    _terminateNotify->remove();
    _notificationServices->flushCollection();
    OSSafeReleaseNULL(_publishNotify);
    OSSafeReleaseNULL(_terminateNotify);
    OSSafeReleaseNULL(_notificationServices);

    OSSafeReleaseNULL(_virtualKBrd);
    PMstop();

    super::stop(provider);
    return;
}

IOReturn AsusSMC::setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) {
    if (whatDevice != this)
        return IOPMAckImplied;

    if (!powerStateOrdinal)
        DBGLOG("atk", "Going to sleep");
    else {
        DBGLOG("atk", "Woke up from sleep");
        IOSleep(1000);
    }

    return IOPMAckImplied;
}

#pragma mark -
#pragma mark AsusSMC Methods
#pragma mark -

IOReturn AsusSMC::message(UInt32 type, IOService * provider, void * argument) {
    if (type == kIOACPIMessageDeviceNotification) {
        UInt32 event = *((UInt32 *) argument);
        OSObject * wed;

        OSNumber * number = OSNumber::withNumber(event,32);
        atkDevice->evaluateObject("_WED", &wed, (OSObject**)&number, 1);
        number->release();
        number = OSDynamicCast(OSNumber, wed);
        if (NULL == number) {
            // try a package
            OSArray * array = OSDynamicCast(OSArray, wed);
            if (NULL == array) {
                // try a buffer
                OSData * data = OSDynamicCast(OSData, wed);
                if ((NULL == data) || (data->getLength() == 0)) {
                    DBGLOG("atk", "Fail to cast _WED returned objet %s", wed->getMetaClass()->getClassName());
                    return kIOReturnError;
                }
                const char * bytes = (const char *) data->getBytesNoCopy();
                number = OSNumber::withNumber(bytes[0],32);
            } else {
                number = OSDynamicCast(OSNumber, array->getObject(0));
                if (NULL == number) {
                    DBGLOG("atk", "Fail to cast _WED returned 1st objet in array %s", array->getObject(0)->getMetaClass()->getClassName());
                    return kIOReturnError;
                }
            }
        }

        handleMessage(number->unsigned32BitValue());
    }
    else
        DBGLOG("atk", "Unexpected message: %u Type %x Provider %s", *((UInt32 *) argument), uint(type), provider->getName());

    return kIOReturnSuccess;
}

void AsusSMC::handleMessage(int code) {
    int loopCount = 0;

    // Processing the code
    switch (code) {
        case 0x57: // AC disconnected
        case 0x58: // AC connected
            // ignore silently
            break;

        case 0x30: // Volume up
            dispatchKBReport(kHIDUsage_Csmr_VolumeIncrement);
            break;

        case 0x31: // Volume down
            dispatchKBReport(kHIDUsage_Csmr_VolumeDecrement);
            break;

        case 0x32: // Mute
            dispatchKBReport(kHIDUsage_Csmr_Mute);
            break;

        // Media buttons
        case 0x40:
        case 0x8A:
            dispatchKBReport(kHIDUsage_Csmr_ScanPreviousTrack);
            break;

        case 0x41:
        case 0x82:
            dispatchKBReport(kHIDUsage_Csmr_ScanNextTrack);
            break;

        case 0x45:
        case 0x5C:
            dispatchKBReport(kHIDUsage_Csmr_PlayOrPause);
            break;

        case 0x33: // hardwired On
        case 0x34: // hardwired Off
        case 0x35: // Soft Event, Fn + F7
            if (isPanelBackLightOn) {
                // Read Panel brigthness value to restore later with backlight toggle
                readPanelBrightnessValue();

                dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessDown, 16);
            } else {
                dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessUp, panelBrightnessLevel);
            }

            isPanelBackLightOn = !isPanelBackLightOn;
            break;

        case 0x61: // Video Mirror
            dispatchTCReport(kHIDUsage_AV_TopCase_VideoMirror);
            break;

        case 0x6B: // Fn + F9, Touchpad On/Off
            touchpadEnabled = !touchpadEnabled;
            if (touchpadEnabled) {
                setProperty("TouchpadEnabled", true);
                removeProperty("TouchpadDisabled");
                DBGLOG("atk", "Touchpad Enabled");
            } else {
                removeProperty("TouchpadEnabled");
                setProperty("TouchpadDisabled", true);
                DBGLOG("atk", "Touchpad Disabled");
            }

            dispatchMessage(kKeyboardSetTouchStatus, &touchpadEnabled);
            break;

        case 0x5E:
            kev.sendMessage(kevSleep, 0, 0);
            break;

        case 0x7A: // Fn + A, ALS Sensor
            if (hasALSensor) {
                isALSenabled = !isALSenabled;
                toggleALS(isALSenabled);
            }
            break;

        case 0x7D: // Airplane mode
            kev.sendMessage(kevAirplaneMode, 0, 0);
            break;

        case 0xC6:
        case 0xC7: // ALS Notifcations
            // ignore
            break;

        case 0xC5: // Keyboard Backlight Down
            if (hasKeybrdBLight) dispatchTCReport(kHIDUsage_AV_TopCase_IlluminationDown);
            break;

        case 0xC4: // Keyboard Backlight Up
            if (hasKeybrdBLight) dispatchTCReport(kHIDUsage_AV_TopCase_IlluminationUp);
            break;

        default:
            if (code >= NOTIFY_BRIGHTNESS_DOWN_MIN && code<= NOTIFY_BRIGHTNESS_DOWN_MAX) // Brightness Down
                dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessDown);
            else if (code >= NOTIFY_BRIGHTNESS_UP_MIN && code<= NOTIFY_BRIGHTNESS_UP_MAX) // Brightness Up
                dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessUp);
            break;
    }

    DBGLOG("atk", "Received Key %d(0x%x)", code, code);
}

void AsusSMC::checkKBALS() {
    // Check keyboard backlight support
    if (atkDevice->validateObject("SKBV") == kIOReturnSuccess) {
        SYSLOG("atk", "Keyboard backlight is supported");
        hasKeybrdBLight = true;
    }
    else {
        hasKeybrdBLight = false;
        DBGLOG("atk", "Keyboard backlight is not supported");
    }

    // Check ALS sensor
    if (atkDevice->validateObject("ALSC") == kIOReturnSuccess && atkDevice->validateObject("ALSS") == kIOReturnSuccess) {
        SYSLOG("atk", "Found ALS sensor");
        hasALSensor = isALSenabled = true;
        toggleALS(isALSenabled);
        SYSLOG("atk", "ALS turned on at boot");
    } else {
        hasALSensor = false;
        DBGLOG("atk", "No ALS sensors were found");
    }
}

void AsusSMC::toggleALS(bool state) {
    OSObject * params[1];
    UInt32 res;
    params[0] = OSNumber::withNumber(state, 8);

    if (atkDevice->evaluateInteger("ALSC", &res, params, 1) == kIOReturnSuccess)
        DBGLOG("atk", "ALS %s %d", state ? "enabled" : "disabled", res);
    else
        DBGLOG("atk", "Failed to call ALSC");
}

int AsusSMC::checkBacklightEntry() {
    if (IORegistryEntry::fromPath(backlightEntry))
        return 1;
    else {
        DBGLOG("atk", "Failed to find backlight entry for %s", backlightEntry);
        return 0;
    }
}

int AsusSMC::findBacklightEntry() {
    snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/GFX0@2/AppleIntelFramebuffer@0/display0/AppleBacklightDisplay");
    if (checkBacklightEntry())
        return 1;

    snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/IGPU@2/AppleIntelFramebuffer@0/display0/AppleBacklightDisplay");
    if (checkBacklightEntry())
        return 1;

    char deviceName[5][5] = {"PEG0", "PEGP", "PEGR", "P0P2", "IXVE"};
    for (int i = 0; i < 5; i++) {
        snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@1/IOPP/GFX0@0", deviceName[i]);
        snprintf(backlightEntry, 1000, "%s%s", backlightEntry, "/NVDA,Display-A@0/NVDA/display0/AppleBacklightDisplay");
        if (checkBacklightEntry())
            return 1;

        snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@3/IOPP/GFX0@0", deviceName[i]);
        snprintf(backlightEntry, 1000, "%s%s", backlightEntry, "/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay");
        if (checkBacklightEntry())
            return 1;

        snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@10/IOPP/GFX0@0", deviceName[i]);
        snprintf(backlightEntry, 1000, "%s%s", backlightEntry, "/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay");
        if (checkBacklightEntry())
            return 1;

        snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@1/IOPP/display@0", deviceName[i]);
        snprintf(backlightEntry, 1000, "%s%s", backlightEntry, "/NVDA,Display-A@0/NVDA/display0/AppleBacklightDisplay");
        if (checkBacklightEntry())
            return 1;

        snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@3/IOPP/display@0", deviceName[i]);
        snprintf(backlightEntry, 1000, "%s%s", backlightEntry, "/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay");
        if (checkBacklightEntry())
            return 1;

        snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@10/IOPP/display@0", deviceName[i]);
        snprintf(backlightEntry, 1000, "%s%s", backlightEntry, "/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay");
        if (checkBacklightEntry())
            return 1;
    }

    return 0;
}

void AsusSMC::readPanelBrightnessValue() {
    if (!findBacklightEntry()) {
        DBGLOG("atk", "GPU device not found");
        return;
    }

    IORegistryEntry *displayDeviceEntry = IORegistryEntry::fromPath(backlightEntry);

    if (displayDeviceEntry != NULL) {
        if (OSDictionary* ioDisplayParaDict = OSDynamicCast(OSDictionary, displayDeviceEntry->getProperty("IODisplayParameters"))) {
            if (OSDictionary* brightnessDict = OSDynamicCast(OSDictionary, ioDisplayParaDict->getObject("brightness"))) {
                if (OSNumber* brightnessValue = OSDynamicCast(OSNumber, brightnessDict->getObject("value"))) {
                    panelBrightnessLevel = brightnessValue->unsigned32BitValue() / 64;
                    DBGLOG("atk", "Panel brightness level from AppleBacklightDisplay: %d", brightnessValue->unsigned32BitValue());
                    DBGLOG("atk", "Read panel brightness level: %d", panelBrightnessLevel);
                } else
                    DBGLOG("atk", "Can't not read brightness value");
            } else
                DBGLOG("atk", "Can't not find dictionary brightness");
        } else
            DBGLOG("atk", "Can't not find dictionary IODisplayParameters");
    }
}

#pragma mark -
#pragma mark VirtualKeyboard
#pragma mark -

void AsusSMC::initVirtualKeyboard() {
    _virtualKBrd = new VirtualHIDKeyboard;

    if (!_virtualKBrd || !_virtualKBrd->init() || !_virtualKBrd->attach(this) || !_virtualKBrd->start(this)) {
        _virtualKBrd->release();
        SYSLOG("virtkbrd", "Error init VirtualHIDKeyboard");
    } else
        _virtualKBrd->setCountryCode(0);
}

IOReturn AsusSMC::postKeyboardInputReport(const void* report, uint32_t reportSize) {
    IOReturn result = kIOReturnError;

    if (!report || reportSize == 0) {
        return kIOReturnBadArgument;
    }

    if (_virtualKBrd) {
        if (auto buffer = IOBufferMemoryDescriptor::withBytes(report, reportSize, kIODirectionNone)) {
            result = _virtualKBrd->handleReport(buffer, kIOHIDReportTypeInput, kIOHIDOptionsTypeNone);
            buffer->release();
        }
    }

    return result;
}

void AsusSMC::dispatchKBReport(int code, int bLoopCount)
{
    DBGLOG("atk", "Loop Count %d, dispatch Key %d(0x%x)", bLoopCount, code, code);
    while (bLoopCount--) {
        kbreport.keys.insert(code);
        postKeyboardInputReport(&kbreport, sizeof(kbreport));
        kbreport.keys.erase(code);
        postKeyboardInputReport(&kbreport, sizeof(kbreport));
    }
}

void AsusSMC::dispatchTCReport(int code, int bLoopCount)
{
    DBGLOG("atk", "Loop Count %d, dispatch Key %d(0x%x)", bLoopCount, code, code);
    while (bLoopCount--) {
        tcreport.keys.insert(code);
        postKeyboardInputReport(&tcreport, sizeof(tcreport));
        tcreport.keys.erase(code);
        postKeyboardInputReport(&tcreport, sizeof(tcreport));
    }
}

#pragma mark -
#pragma mark Notification methods
#pragma mark -

void AsusSMC::registerNotifications() {
    OSDictionary * propertyMatch = propertyMatching(OSSymbol::withCString(kDeliverNotifications), OSBoolean::withBoolean(true));

    IOServiceMatchingNotificationHandler notificationHandler = OSMemberFunctionCast(IOServiceMatchingNotificationHandler, this, &AsusSMC::notificationHandler);

    _publishNotify = addMatchingNotification(gIOFirstPublishNotification,
                                             propertyMatch,
                                             notificationHandler,
                                             this,
                                             0, 10000);

    _terminateNotify = addMatchingNotification(gIOTerminatedNotification,
                                               propertyMatch,
                                               notificationHandler,
                                               this,
                                               0, 10000);

    propertyMatch->release();
}

void AsusSMC::notificationHandlerGated(IOService * newService, IONotifier * notifier) {
    if (notifier == _publishNotify) {
        SYSLOG("notify", "Notification consumer published: %s", newService->getName());
        _notificationServices->setObject(newService);
    }

    if (notifier == _terminateNotify) {
        SYSLOG("notify", "Notification consumer terminated: %s", newService->getName());
        _notificationServices->removeObject(newService);
    }
}

bool AsusSMC::notificationHandler(void * refCon, IOService * newService, IONotifier * notifier) {
    command_gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &AsusSMC::notificationHandlerGated), newService, notifier);
    return true;
}

void AsusSMC::dispatchMessageGated(int* message, void* data) {
    OSCollectionIterator* i = OSCollectionIterator::withCollection(_notificationServices);

    if (i != NULL) {
        while (IOService* service = OSDynamicCast(IOService, i->getNextObject()))
            service->message(*message, this, data);
        i->release();
    }
}

void AsusSMC::dispatchMessage(int message, void* data) {
    command_gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &AsusSMC::dispatchMessageGated), &message, data);
}

#pragma mark -
#pragma mark VirtualSMC plugin
#pragma mark -

void AsusSMC::registerVSMC() {
    vsmcNotifier = VirtualSMCAPI::registerHandler(vsmcNotificationHandler, this);

    ALSSensor sensor {ALSSensor::Type::Unknown7, true, 6, false};
    ALSSensor noSensor {ALSSensor::Type::NoSensor, false, 0, false};
    SMCALSValue::Value emptyValue;
    SMCKBrdBLightValue::lkb lkb;
    SMCKBrdBLightValue::lks lks;

    VirtualSMCAPI::addKey(KeyAL, vsmcPlugin.data, VirtualSMCAPI::valueWithUint16(0, &forceBits, SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE));

    VirtualSMCAPI::addKey(KeyALI0, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&sensor), sizeof(sensor), SmcKeyTypeAli, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyALI1, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&noSensor), sizeof(noSensor), SmcKeyTypeAli, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyALRV, vsmcPlugin.data, VirtualSMCAPI::valueWithUint16(1, nullptr, SMC_KEY_ATTRIBUTE_READ));

    VirtualSMCAPI::addKey(KeyALV0, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&emptyValue), sizeof(emptyValue), SmcKeyTypeAlv, new SMCALSValue(&currentLux, &forceBits),
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyALV1, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&emptyValue), sizeof(emptyValue), SmcKeyTypeAlv, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyLKSB, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&lkb), sizeof(lkb), SmcKeyTypeLkb, new SMCKBrdBLightValue(atkDevice),
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyLKSS, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&lks), sizeof(lks), SmcKeyTypeLks, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyMSLD, vsmcPlugin.data, VirtualSMCAPI::valueWithUint8(0, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));
}

bool AsusSMC::vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier) {
    if (sensors && vsmc) {
        DBGLOG("alsd", "got vsmc notification");
        auto self = static_cast<AsusSMC *>(sensors);
        auto ret = vsmc->callPlatformFunction(VirtualSMCAPI::SubmitPlugin, true, sensors, &self->vsmcPlugin, nullptr, nullptr);
        if (ret == kIOReturnSuccess) {
            DBGLOG("alsd", "submitted plugin");

            self->workloop = self->getWorkLoop();
            self->poller = IOTimerEventSource::timerEventSource(self, [](OSObject *object, IOTimerEventSource *sender) {
                auto ls = OSDynamicCast(AsusSMC, object);
                if (ls) ls->refreshSensor(true);
            });

            if (!self->poller || !self->workloop) {
                SYSLOG("alsd", "failed to create poller or workloop");
                return false;
            }

            if (self->workloop->addEventSource(self->poller) != kIOReturnSuccess) {
                SYSLOG("alsd", "failed to add timer event source to workloop");
                return false;
            }

            if (self->poller->setTimeoutMS(SensorUpdateTimeoutMS) != kIOReturnSuccess) {
                SYSLOG("alsd", "failed to set timeout");
                return false;
            }

            return true;
        } else if (ret != kIOReturnUnsupported) {
            SYSLOG("alsd", "plugin submission failure %X", ret);
        } else {
            DBGLOG("alsd", "plugin submission to non vsmc");
        }
    } else {
        SYSLOG("alsd", "got null vsmc notification");
    }

    return false;
}

bool AsusSMC::refreshSensor(bool post) {
    uint32_t lux = 0;
    auto ret = atkDevice->evaluateInteger("ALSS", &lux);
    if (ret != kIOReturnSuccess)
        lux = 0xFFFFFFFF; // ACPI invalid

    atomic_store_explicit(&currentLux, lux, memory_order_release);

    if (post) {
        VirtualSMCAPI::postInterrupt(SmcEventALSChange);
        poller->setTimeoutMS(SensorUpdateTimeoutMS);
    }

    DBGLOG("alsd", "refreshSensor lux %u", lux);

    return ret == kIOReturnSuccess;
}

EXPORT extern "C" kern_return_t ADDPR(kern_start)(kmod_info_t *, void *) {
    // Report success but actually do not start and let I/O Kit unload us.
    // This works better and increases boot speed in some cases.
    PE_parse_boot_argn("liludelay", &ADDPR(debugPrintDelay), sizeof(ADDPR(debugPrintDelay)));
    ADDPR(debugEnabled) = checkKernelArgument("-asussmcdbg");
    return KERN_SUCCESS;
}

EXPORT extern "C" kern_return_t ADDPR(kern_stop)(kmod_info_t *, void *) {
    // It is not safe to unload VirtualSMC plugins!
    return KERN_FAILURE;
}
