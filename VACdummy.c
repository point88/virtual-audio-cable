
#include <CoreAudio/AudioServerPlugIn.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/syslog.h>
#include <Accelerate/Accelerate.h>

//==================================================================================================
#pragma mark -
#pragma mark Macros
//==================================================================================================

#if TARGET_RT_BIG_ENDIAN
#define    FourCCToCString(the4CC)    { ((char*)&the4CC)[0], ((char*)&the4CC)[1], ((char*)&the4CC)[2], ((char*)&the4CC)[3], 0 }
#else
#define    FourCCToCString(the4CC)    { ((char*)&the4CC)[3], ((char*)&the4CC)[2], ((char*)&the4CC)[1], ((char*)&the4CC)[0], 0 }
#endif

#if DEBUG

    #define    DebugMsg(inFormat, ...)    syslog(LOG_NOTICE, inFormat, ## __VA_ARGS__)

    #define    FailIf(inCondition, inHandler, inMessage)                           \
    if(inCondition)                                                                \
    {                                                                              \
        DebugMsg(inMessage);                                                       \
        goto inHandler;                                                            \
    }

    #define    FailWithAction(inCondition, inAction, inHandler, inMessage)         \
    if(inCondition)                                                                \
    {                                                                              \
        DebugMsg(inMessage);                                                       \
        { inAction; }                                                              \
        goto inHandler;                                                            \
        }

#else

    #define    DebugMsg(inFormat, ...)

    #define    FailIf(inCondition, inHandler, inMessage)                           \
    if(inCondition)                                                                \
    {                                                                              \
    goto inHandler;                                                                \
    }

    #define    FailWithAction(inCondition, inAction, inHandler, inMessage)         \
    if(inCondition)                                                                \
    {                                                                              \
    { inAction; }                                                                  \
    goto inHandler;                                                                \
    }

#endif

#pragma mark -
#pragma mark VAC State

enum
{
    kObjectID_PlugIn                    = kAudioObjectPlugInObject,
    kObjectID_Box                       = 2,
    kObjectID_Device                    = 3,
    kObjectID_Stream_Input              = 4,
    kObjectID_Volume_Input_Master       = 5,
    kObjectID_Mute_Input_Master         = 6,
    kObjectID_Stream_Output             = 7,
    kObjectID_Volume_Output_Master      = 8,
    kObjectID_Mute_Output_Master        = 9,
    kObjectID_Device2                   = 10,
};

enum ObjectType
{
    kObjectType_Stream,
    kObjectType_Control
};

struct ObjectInfo {
    AudioObjectID id;
    enum ObjectType type;
    AudioObjectPropertyScope scope;
};


#ifndef VCHas_Driver_Format
#define                             VCHas_Driver_Format             true
#endif

#if VCHas_Driver_Format
#define                             kBox_UID                            "Virtaul Audio Cable" "%ich" "_UID"
#define                             kDevice_UID                         "Virtaul Audio Cable" "%ich" "_UID"
#define                             kDevice2_UID                        "Virtaul Audio Cable" "%ich" "_2_UID"
#define                             kDevice_ModelUID                    "Virtaul Audio Cable" "%ich" "_ModelUID"
#else
#define                             kBox_UID                            "Virtaul Audio Cable" "_UID"
#define                             kDevice_UID                         "Virtaul Audio Cable" "_UID"
#define                             kDevice2_UID                        "Virtaul Audio Cable" "_2_UID"
#define                             kDevice_ModelUID                    "Virtaul Audio Cable" "_ModelUID"
#endif

#ifndef kDevice_Name
#define                             kDevice_Name                        "Virtaul Audio Cable" ""
#endif

#ifndef kDevice2_Name
#define                             kDevice2_Name                       "Virtaul Audio Cable" ""
#endif

#ifndef kDevice_IsHidden
#define                             kDevice_IsHidden                    false
#endif

#ifndef kDevice2_IsHidden
#define                             kDevice2_IsHidden                   true
#endif



#ifndef kDevice_HasInput
#define                             kDevice_HasInput                    true
#endif

#ifndef kDevice_HasOutput
#define                             kDevice_HasOutput                   true
#endif

#ifndef kDevice2_HasInput
#define                             kDevice2_HasInput                   true
#endif

#ifndef kDevice2_HasOutput
#define                             kDevice2_HasOutput                  true
#endif



#ifndef kManufacturer_Name
#define                             kManufacturer_Name                  "Existential Audio Inc."
#endif

#define                             kLatency_Frame_Size                 0

#ifndef kNumber_Of_Channels
#define                             kNumber_Of_Channels                 2
#endif

#ifndef kEnableVolumeControl
#define                             kEnableVolumeControl                 true
#endif

static pthread_mutex_t              gPlugIn_StateMutex                  = PTHREAD_MUTEX_INITIALIZER;
static UInt32                       gPlugIn_RefCount                    = 0;
static AudioServerPlugInHostRef     gPlugIn_Host                        = NULL;


static CFStringRef                  gBox_Name                           = NULL;

#ifndef kBox_Aquired
#define                             kBox_Aquired                 	true
#endif
static Boolean                      gBox_Acquired                       = kBox_Aquired;


static pthread_mutex_t              gDevice_IOMutex                     = PTHREAD_MUTEX_INITIALIZER;
static Float64                      gDevice_SampleRate                  = 44100.0;
static UInt64                       gDevice_IOIsRunning                 = 0;
static const UInt32                 kDevice_RingBufferSize              = 16384;
static Float64                      gDevice_HostTicksPerFrame           = 0.0;
static UInt64                       gDevice_NumberTimeStamps            = 0;
static Float64                      gDevice_AnchorSampleTime            = 0.0;
static UInt64                       gDevice_AnchorHostTime              = 0;

static bool                         gStream_Input_IsActive              = true;
static bool                         gStream_Output_IsActive             = true;

static const Float32                kVolume_MinDB                       = -64.0;
static const Float32                kVolume_MaxDB                       = 0.0;
static Float32                      gVolume_Master_Value                = 1.0;
static bool                         gMute_Master_Value                  = false;

static struct ObjectInfo            kDevice_ObjectList[]                = {
#if kDevice_HasInput
    { kObjectID_Stream_Input,           kObjectType_Stream,     kAudioObjectPropertyScopeInput  },
    { kObjectID_Volume_Input_Master,    kObjectType_Control,    kAudioObjectPropertyScopeInput  },
    { kObjectID_Mute_Input_Master,      kObjectType_Control,    kAudioObjectPropertyScopeInput  },
#endif
#if kDevice_HasOutput
    { kObjectID_Stream_Output,          kObjectType_Stream,     kAudioObjectPropertyScopeOutput },
    { kObjectID_Volume_Output_Master,   kObjectType_Control,    kAudioObjectPropertyScopeOutput },
    { kObjectID_Mute_Output_Master,     kObjectType_Control,    kAudioObjectPropertyScopeOutput }
#endif
};

static struct ObjectInfo            kDevice2_ObjectList[]                = {
#if kDevice2_HasInput
    { kObjectID_Stream_Input,           kObjectType_Stream,     kAudioObjectPropertyScopeInput  },
    { kObjectID_Volume_Input_Master,    kObjectType_Control,    kAudioObjectPropertyScopeInput  },
    { kObjectID_Mute_Input_Master,      kObjectType_Control,    kAudioObjectPropertyScopeInput  },
#endif
#if kDevice2_HasOutput
    { kObjectID_Stream_Output,          kObjectType_Stream,     kAudioObjectPropertyScopeOutput },
    { kObjectID_Volume_Output_Master,   kObjectType_Control,    kAudioObjectPropertyScopeOutput },
    { kObjectID_Mute_Output_Master,     kObjectType_Control,    kAudioObjectPropertyScopeOutput }
#endif
};

static const UInt32                 kDevice_ObjectListSize              = sizeof(kDevice_ObjectList) / sizeof(struct ObjectInfo);
static const UInt32                 kDevice2_ObjectListSize              = sizeof(kDevice2_ObjectList) / sizeof(struct ObjectInfo);

#ifndef kSampleRates
#define                             kSampleRates       8000, 16000, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, 705600, 768000
#endif

static Float64                      kDevice_SampleRates[]               = { kSampleRates };

static const UInt32                 kDevice_SampleRatesSize             = sizeof(kDevice_SampleRates) / sizeof(Float64);



#define                             kBits_Per_Channel                   32
#define                             kBytes_Per_Channel                  (kBits_Per_Channel/ 8)
#define                             kBytes_Per_Frame                    (kNumber_Of_Channels * kBytes_Per_Channel)
#define                             kRing_Buffer_Frame_Size             ((65536 + kLatency_Frame_Size))
static Float32*                     gRingBuffer;

void*                _Create(CFAllocatorRef inAllocator, CFUUIDRef inRequestedTypeUUID);
static HRESULT        _QueryInterface(void* in_driver, REFIID inUUID, LPVOID* outInterface);
static ULONG        _AddRef(void* in_driver);
static ULONG        _Release(void* in_driver);
static OSStatus        _Initialize(AudioServerPlugInDriverRef in_driver, AudioServerPlugInHostRef inHost);
static OSStatus        _CreateDevice(AudioServerPlugInDriverRef in_driver, CFDictionaryRef inDescription, const AudioServerPlugInClientInfo* inClientInfo, AudioObjectID* outDeviceObjectID);
static OSStatus        _DestroyDevice(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID);
static OSStatus        _AddDeviceClient(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo);
static OSStatus        _RemoveDeviceClient(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo);
static OSStatus        _PerformDeviceConfigurationChange(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo);
static OSStatus        _AbortDeviceConfigurationChange(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo);
static Boolean        _HasProperty(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus        _IsPropertySettable(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
static OSStatus        _GetPropertyDataSize(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
static OSStatus        _GetPropertyData(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus        _SetPropertyData(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData);
static OSStatus        _StartIO(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt32 inClientID);
static OSStatus        _StopIO(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt32 inClientID);
static OSStatus        _GetZeroTimeStamp(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt32 inClientID, Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed);
static OSStatus        _WillDoIOOperation(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, Boolean* outWillDo, Boolean* outWillDoInPlace);
static OSStatus        _BeginIOOperation(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo);
static OSStatus        _DoIOOperation(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, AudioObjectID inStreamObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo, void* ioMainBuffer, void* ioSecondaryBuffer);
static OSStatus        _EndIOOperation(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo);

//    Implementation
static Boolean        _HasPlugInProperty(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus        _IsPlugInPropertySettable(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
static OSStatus        getplugIn_property_size(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
static OSStatus        _GetPlugInPropertyData(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus        set_propertyData( pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2]);

static Boolean        hasbox_property(pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus        box_property_settable(pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
static OSStatus        getbox_property_datasize( pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
static OSStatus        getbox_property_data( pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus        set_box_property( pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2]);

static Boolean        has_device_property(pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus        device_property_settable( pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
static OSStatus        get_device_property_size(AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
static OSStatus        get_device_property(AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus        set_device_property(pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2]);

static Boolean        has_stream_property(pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus        stream_property(pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
static OSStatus        _GetStreamPropertyDataSize(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
static OSStatus        _GetStreamPropertyData(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus        _SetStreamPropertyData(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2]);

static Boolean        _HasControlProperty(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus        _IsControlPropertySettable(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
static OSStatus        _GetControlPropertyDataSize(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
static OSStatus        _GetControlPropertyData(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus        _SetControlPropertyData(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2]);

#pragma mark The Interface

static AudioServerPlugInDriverInterface    g_driver_interface =
{
    NULL,
    _QueryInterface,
    _AddRef,
    _Release,
    _Initialize,
    _CreateDevice,
    _DestroyDevice,
    _AddDeviceClient,
    _RemoveDeviceClient,
    _PerformDeviceConfigurationChange,
    _AbortDeviceConfigurationChange,
    _HasProperty,
    _IsPropertySettable,
    _GetPropertyDataSize,
    _GetPropertyData,
    _SetPropertyData,
    _StartIO,
    _StopIO,
    _GetZeroTimeStamp,
    _WillDoIOOperation,
    _BeginIOOperation,
    _DoIOOperation,
    _EndIOOperation
};
static AudioServerPlugInDriverInterface*    g_driver_interface_ptr    = &g_driver_interface;
static AudioServerPlugInDriverRef            g_driver_ref                = &g_driver_interface_ptr;


#define RETURN_FORMATTED_STRING(_string_fmt)                          \
if(VCHas_Driver_Format)                                           \
{                                                                     \
	return CFStringCreateWithFormat(NULL, NULL, CFSTR(_string_fmt), kNumber_Of_Channels); \
}                                                                     \
else                                                                  \
{                                                                     \
	return CFStringCreateWithCString(NULL, _string_fmt, kCFStringEncodingUTF8); \
}

static CFStringRef get_box_uid()          { RETURN_FORMATTED_STRING(kBox_UID) }
static CFStringRef get_device_uid()       { RETURN_FORMATTED_STRING(kDevice_UID) }
static CFStringRef get_device_name()      { RETURN_FORMATTED_STRING(kDevice_Name) }
static CFStringRef get_device2_uid()       { RETURN_FORMATTED_STRING(kDevice2_UID) }
static CFStringRef get_device2_name()      { RETURN_FORMATTED_STRING(kDevice2_Name) }
static CFStringRef get_device_model_uid() { RETURN_FORMATTED_STRING(kDevice_ModelUID) }

// Volume conversions

static Float32 volume_to_decibel(Float32 volume)
{
	if (volume <= powf(10.0f, kVolume_MinDB / 20.0f))
		return kVolume_MinDB;
	else
		return 20.0f * log10f(volume);
}

static Float32 volume_from_decibel(Float32 decibel)
{
	if (decibel <= kVolume_MinDB)
		return 0.0f;
	else
		return powf(10.0f, decibel / 20.0f);
}

static Float32 volume_to_scalar(Float32 volume)
{
	Float32 decibel = volume_to_decibel(volume);
	return (decibel - kVolume_MinDB) / (kVolume_MaxDB - kVolume_MinDB);
}

static Float32 volume_from_scalar(Float32 scalar)
{
	Float32 decibel = scalar * (kVolume_MaxDB - kVolume_MinDB) + kVolume_MinDB;
	return volume_from_decibel(decibel);
}

static UInt32 device_object_list_size(AudioObjectPropertyScope scope, AudioObjectID objectID) {
    
    switch (objectID) {
        case kObjectID_Device:
            {
                if (scope == kAudioObjectPropertyScopeGlobal)
                {
                    return kDevice_ObjectListSize;
                }

                UInt32 count = 0;
                for (UInt32 i = 0; i < kDevice_ObjectListSize; i++)
                {
                    count += (kDevice_ObjectList[i].scope == scope);
                }

                return count;
            }
            break;
            
        case kObjectID_Device2:
            {
                if (scope == kAudioObjectPropertyScopeGlobal)
                {
                    return kDevice2_ObjectListSize;
                }

                UInt32 count = 0;
                for (UInt32 i = 0; i < kDevice2_ObjectListSize; i++)
                {
                    count += (kDevice2_ObjectList[i].scope == scope);
                }

                return count;
            }
            break;
            
        default:
            return 0;
            break;
    }
}

static UInt32 device_stream_list_size(AudioObjectPropertyScope scope, AudioObjectID objectID) {
    
    switch (objectID) {
        case kObjectID_Device:
            {
                UInt32 count = 0;
                for (UInt32 i = 0; i < kDevice_ObjectListSize; i++)
                {
                    count += (kDevice_ObjectList[i].type == kObjectType_Stream && (kDevice_ObjectList[i].scope == scope || scope == kAudioObjectPropertyScopeGlobal));
                }

                return count;
            }
            break;
            
        case kObjectID_Device2:
            {
                UInt32 count = 0;
                for (UInt32 i = 0; i < kDevice2_ObjectListSize; i++)
                {
                    count += (kDevice2_ObjectList[i].type == kObjectType_Stream && (kDevice2_ObjectList[i].scope == scope || scope == kAudioObjectPropertyScopeGlobal));
                }

                return count;
            }
            break;
            
        default:
            return 0;
            break;
    }
    

}

static UInt32 device_control_list_size(AudioObjectPropertyScope scope, AudioObjectID objectID) {
    
    switch (objectID) {
        case kObjectID_Device:
        {
            
            UInt32 count = 0;
            for (UInt32 i = 0; i < kDevice_ObjectListSize; i++)
            {
                count += (kDevice_ObjectList[i].type == kObjectType_Control && (kDevice_ObjectList[i].scope == scope || scope == kAudioObjectPropertyScopeGlobal));
            }

            return count;
        }
            break;
        case kObjectID_Device2:
        {
            
            UInt32 count = 0;
            for (UInt32 i = 0; i < kDevice2_ObjectListSize; i++)
            {
                count += (kDevice2_ObjectList[i].type == kObjectType_Control && (kDevice2_ObjectList[i].scope == scope || scope == kAudioObjectPropertyScopeGlobal));
            }

            return count;
        }
            break;
            
        default:
            return 0;
            break;
    }

}

static UInt32 minimum(UInt32 a, UInt32 b) {
    return a < b ? a : b;
}

static bool is_valid_sample_rate(Float64 sample_rate)
{
    for(UInt32 i = 0; i < kDevice_SampleRatesSize; i++)
    {
        if (sample_rate == kDevice_SampleRates[i])
        {
            return true;
        }
    }

    return false;
}

#pragma mark Factory

void*	_Create(CFAllocatorRef inAllocator, CFUUIDRef inRequestedTypeUUID)
{
	#pragma unused(inAllocator)
    void* result = NULL;
    if(CFEqual(inRequestedTypeUUID, kAudioServerPlugInTypeUUID))
    {
		result = g_driver_ref;
    }
    return result;
}

#pragma mark Inheritence

static HRESULT	_QueryInterface(void* in_driver, REFIID inUUID, LPVOID* outInterface)
{
	//	declare the local variables
	HRESULT result = 0;
	CFUUIDRef theRequestedUUID = NULL;
	
	theRequestedUUID = CFUUIDCreateFromUUIDBytes(NULL, inUUID);

	if(CFEqual(theRequestedUUID, IUnknownUUID) || CFEqual(theRequestedUUID, kAudioServerPlugInDriverInterfaceUUID))
	{
		pthread_mutex_lock(&gPlugIn_StateMutex);
		++gPlugIn_RefCount;
		pthread_mutex_unlock(&gPlugIn_StateMutex);
		*outInterface = g_driver_ref;
	}
	else
	{
		result = E_NOINTERFACE;
	}
	
	//	make sure to release the UUID we created
	CFRelease(theRequestedUUID);
    
	return result;
}

static ULONG	_AddRef(void* in_driver)
{
	//	declare the local variables
	ULONG result = 0;
	
	//	increment the refcount
	pthread_mutex_lock(&gPlugIn_StateMutex);
	if(gPlugIn_RefCount < UINT32_MAX)
	{
		++gPlugIn_RefCount;
	}
	result = gPlugIn_RefCount;
	pthread_mutex_unlock(&gPlugIn_StateMutex);

	return result;
}

static ULONG	_Release(void* in_driver)
{
	//	declare the local variables
	ULONG result = 0;
	
	//	decrement the refcount
	pthread_mutex_lock(&gPlugIn_StateMutex);
	if(gPlugIn_RefCount > 0)
	{
		--gPlugIn_RefCount;
	}
	result = gPlugIn_RefCount;
	pthread_mutex_unlock(&gPlugIn_StateMutex);

	return result;
}

#pragma mark Basic Operations

static OSStatus	_Initialize(AudioServerPlugInDriverRef in_driver, AudioServerPlugInHostRef inHost)
{

	//	declare the local variables
	OSStatus result = 0;
	
	//	store the AudioServerPlugInHostRef
	gPlugIn_Host = inHost;
	
	//	initialize the box acquired property from the settings
	CFPropertyListRef theSettingsData = NULL;
	gPlugIn_Host->CopyFromStorage(gPlugIn_Host, CFSTR("box acquired"), &theSettingsData);
	if(theSettingsData != NULL)
	{
		if(CFGetTypeID(theSettingsData) == CFBooleanGetTypeID())
		{
			gBox_Acquired = CFBooleanGetValue((CFBooleanRef)theSettingsData);
		}
		else if(CFGetTypeID(theSettingsData) == CFNumberGetTypeID())
		{
			SInt32 theValue = 0;
			CFNumberGetValue((CFNumberRef)theSettingsData, kCFNumberSInt32Type, &theValue);
			gBox_Acquired = theValue ? 1 : 0;
		}
		CFRelease(theSettingsData);
	}
	
	//	initialize the box name from the settings
	gPlugIn_Host->CopyFromStorage(gPlugIn_Host, CFSTR("box acquired"), &theSettingsData);
	if(theSettingsData != NULL)
	{
		if(CFGetTypeID(theSettingsData) == CFStringGetTypeID())
		{
			gBox_Name = (CFStringRef)theSettingsData;
			CFRetain(gBox_Name);
		}
		CFRelease(theSettingsData);
	}
	
	//	set the box name directly as a last resort
	if(gBox_Name == NULL)
	{
		gBox_Name = CFSTR("AVC Box");
	}
	
	//	calculate the host ticks per frame
	struct mach_timebase_info theTimeBaseInfo;
	mach_timebase_info(&theTimeBaseInfo);
	Float64 theHostClockFrequency = (Float64)theTimeBaseInfo.denom / (Float64)theTimeBaseInfo.numer;
	theHostClockFrequency *= 1000000000.0;
	gDevice_HostTicksPerFrame = theHostClockFrequency / gDevice_SampleRate;
    return result;
}

static OSStatus	_CreateDevice(AudioServerPlugInDriverRef in_driver, CFDictionaryRef inDescription, const AudioServerPlugInClientInfo* inClientInfo, AudioObjectID* outDeviceObjectID)
{

	#pragma unused(inDescription, inClientInfo, outDeviceObjectID)
	
	//	declare the local variables
	OSStatus result = kAudioHardwareUnsupportedOperationError;
	
	return result;
}

static OSStatus	_DestroyDevice(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID)
{

	#pragma unused(inDeviceObjectID)
	
	//	declare the local variables
	OSStatus result = kAudioHardwareUnsupportedOperationError;
	
	return result;
}

static OSStatus	_AddDeviceClient(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo)
{

	#pragma unused(inClientInfo)
	
	//	declare the local variables
	OSStatus result = 0;
	
	return result;
}

static OSStatus	_RemoveDeviceClient(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo)
{

	#pragma unused(inClientInfo)
	
	//	declare the local variables
	OSStatus result = 0;
	
	return result;
}

static OSStatus	_PerformDeviceConfigurationChange(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo)
{

	#pragma unused(inChangeInfo)

	//	declare the local variables
	OSStatus result = 0;
	    
	
	//	lock the state mutex
	pthread_mutex_lock(&gPlugIn_StateMutex);
	
	//	change the sample rate
	gDevice_SampleRate = inChangeAction;
	
	//	recalculate the state that depends on the sample rate
	struct mach_timebase_info theTimeBaseInfo;
	mach_timebase_info(&theTimeBaseInfo);
    Float64 theHostClockFrequency = (Float64)theTimeBaseInfo.denom / (Float64)theTimeBaseInfo.numer;
	theHostClockFrequency *= 1000000000.0;
	gDevice_HostTicksPerFrame = theHostClockFrequency / gDevice_SampleRate;

	//	unlock the state mutex
	pthread_mutex_unlock(&gPlugIn_StateMutex);
    
	return result;
}

static OSStatus	_AbortDeviceConfigurationChange(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo)
{

	#pragma unused(inChangeAction, inChangeInfo)

	//	declare the local variables
	OSStatus result = 0;

	return result;
}

#pragma mark Property Operations

static Boolean	_HasProperty(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress)
{

	Boolean result = false;
    switch(inObjectID)
	{
		case kObjectID_PlugIn:
			result = _HasPlugInProperty(in_driver, inObjectID, inClientProcessID, inAddress);
			break;
		
		case kObjectID_Box:
			result = hasbox_property(inClientProcessID, inAddress);
			break;
		
		case kObjectID_Device:
        case kObjectID_Device2:
			result = has_device_property(inClientProcessID, inAddress);
			break;
		
		case kObjectID_Stream_Input:
		case kObjectID_Stream_Output:
			result = has_stream_property(inClientProcessID, inAddress);
			break;
		
		case kObjectID_Volume_Output_Master:
		case kObjectID_Mute_Output_Master:
        case kObjectID_Volume_Input_Master:
        case kObjectID_Mute_Input_Master:
			result = _HasControlProperty(in_driver, inObjectID, inClientProcessID, inAddress);
			break;
	};

	return result;
}

static OSStatus	_IsPropertySettable(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable)
{

	OSStatus result = 0;
	
	switch(inObjectID)
	{
		case kObjectID_PlugIn:
			result = _IsPlugInPropertySettable(in_driver, inObjectID, inClientProcessID, inAddress, outIsSettable);
			break;
		
		case kObjectID_Box:
			result = box_property_settable(inClientProcessID, inAddress, outIsSettable);
			break;
		
		case kObjectID_Device:
        case kObjectID_Device2:
			result = device_property_settable(inClientProcessID, inAddress, outIsSettable);
			break;
		
		case kObjectID_Stream_Input:
		case kObjectID_Stream_Output:
			result = stream_property(inClientProcessID, inAddress, outIsSettable);
			break;
		
		case kObjectID_Volume_Output_Master:
		case kObjectID_Mute_Output_Master:
        case kObjectID_Volume_Input_Master:
        case kObjectID_Mute_Input_Master:
			result = _IsControlPropertySettable(in_driver, inObjectID, inClientProcessID, inAddress, outIsSettable);
			break;
				
		default:
			result = kAudioHardwareBadObjectError;
			break;
	};

	return result;
}

static OSStatus	_GetPropertyDataSize(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize)
{

	OSStatus result = 0;
	
	switch(inObjectID)
	{
		case kObjectID_PlugIn:
			result = getplugIn_property_size(in_driver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, outDataSize);
			break;
		
		case kObjectID_Box:
			result = getbox_property_datasize(inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, outDataSize);
			break;
		
		case kObjectID_Device:
        case kObjectID_Device2:
			result = get_device_property_size(  inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, outDataSize);
			break;
		
		case kObjectID_Stream_Input:
		case kObjectID_Stream_Output:
			result = _GetStreamPropertyDataSize(in_driver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, outDataSize);
			break;
		
		case kObjectID_Volume_Output_Master:
		case kObjectID_Mute_Output_Master:
        case kObjectID_Volume_Input_Master:
        case kObjectID_Mute_Input_Master:
			result = _GetControlPropertyDataSize(in_driver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, outDataSize);
			break;
				
		default:
			result = kAudioHardwareBadObjectError;
			break;
	};

	return result;
}

static OSStatus	_GetPropertyData(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData)
{
	//	declare the local variables
	OSStatus result = 0;
	

	switch(inObjectID)
	{
		case kObjectID_PlugIn:
			result = _GetPlugInPropertyData(in_driver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
		
		case kObjectID_Box:
			result = getbox_property_data( inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
		
		case kObjectID_Device:
        case kObjectID_Device2:
			result = get_device_property( inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
		
		case kObjectID_Stream_Input:
		case kObjectID_Stream_Output:
			result = _GetStreamPropertyData(in_driver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
		
		case kObjectID_Volume_Output_Master:
		case kObjectID_Mute_Output_Master:
        case kObjectID_Volume_Input_Master:
        case kObjectID_Mute_Input_Master:
			result = _GetControlPropertyData(in_driver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
				
		default:
			result = kAudioHardwareBadObjectError;
			break;
	};

	return result;
}

static OSStatus	_SetPropertyData(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData)
{
	//	declare the local variables
	OSStatus result = 0;
	UInt32 theNumberPropertiesChanged = 0;
	AudioObjectPropertyAddress theChangedAddresses[2];
	
	switch(inObjectID)
	{
		case kObjectID_PlugIn:
			result = set_propertyData(inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData, &theNumberPropertiesChanged, theChangedAddresses);
			break;
		
		case kObjectID_Box:
			result = set_box_property( inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData, &theNumberPropertiesChanged, theChangedAddresses);
			break;
		
		case kObjectID_Device:
        case kObjectID_Device2:
			result = set_device_property(inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData, &theNumberPropertiesChanged, theChangedAddresses);
			break;
		
		case kObjectID_Stream_Input:
		case kObjectID_Stream_Output:
			result = _SetStreamPropertyData(in_driver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData, &theNumberPropertiesChanged, theChangedAddresses);
			break;
		
		case kObjectID_Volume_Output_Master:
		case kObjectID_Mute_Output_Master:
        case kObjectID_Volume_Input_Master:
        case kObjectID_Mute_Input_Master:
			result = _SetControlPropertyData(in_driver, inObjectID, inClientProcessID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData, &theNumberPropertiesChanged, theChangedAddresses);
			break;
				
		default:
			result = kAudioHardwareBadObjectError;
			break;
	};

	//	send any notifications
	if(theNumberPropertiesChanged > 0)
	{
		gPlugIn_Host->PropertiesChanged(gPlugIn_Host, inObjectID, theNumberPropertiesChanged, theChangedAddresses);
	}

	return result;
}

#pragma mark PlugIn Property Operations

static Boolean	_HasPlugInProperty(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress)
{
	//	This method returns whether or not the plug-in object has the given property.
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	Boolean result = false;
	
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyManufacturer:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioPlugInPropertyBoxList:
		case kAudioPlugInPropertyTranslateUIDToBox:
		case kAudioPlugInPropertyDeviceList:
		case kAudioPlugInPropertyTranslateUIDToDevice:
		case kAudioPlugInPropertyResourceBundle:
			result = true;
			break;
	};

	return result;
}

static OSStatus	_IsPlugInPropertySettable(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable)
{
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	OSStatus result = 0;
	
	
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyManufacturer:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioPlugInPropertyBoxList:
		case kAudioPlugInPropertyTranslateUIDToBox:
		case kAudioPlugInPropertyDeviceList:
		case kAudioPlugInPropertyTranslateUIDToDevice:
		case kAudioPlugInPropertyResourceBundle:
			*outIsSettable = false;
			break;
		
		default:
			result = kAudioHardwareUnknownPropertyError;
			break;
	};

	return result;
}

static OSStatus	getplugIn_property_size(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize)
{
	//	This method returns the byte size of the property's data.
	
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus result = 0;
	

	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyClass:
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyOwner:
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioObjectPropertyManufacturer:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
			if(gBox_Acquired)
			{
				*outDataSize = 2 * sizeof(AudioClassID);
			}
			else
			{
				*outDataSize = sizeof(AudioClassID);
			}
			break;
			
		case kAudioPlugInPropertyBoxList:
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioPlugInPropertyTranslateUIDToBox:
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioPlugInPropertyDeviceList:
			if(gBox_Acquired)
			{
				*outDataSize = sizeof(AudioClassID)*2;
			}
			else
			{
				*outDataSize = 0;
			}
			break;
			
		case kAudioPlugInPropertyTranslateUIDToDevice:
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioPlugInPropertyResourceBundle:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		default:
			result = kAudioHardwareUnknownPropertyError;
			break;
	};

	return result;
}

static OSStatus    _GetStreamPropertyDataSize(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize)
{
    //    This method returns the byte size of the property's data.
    
    #pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
    
    //    declare the local variables
    OSStatus result = 0;
    
    
    switch(inAddress->mSelector)
    {
        case kAudioObjectPropertyBaseClass:
            *outDataSize = sizeof(AudioClassID);
            break;

        case kAudioObjectPropertyClass:
            *outDataSize = sizeof(AudioClassID);
            break;

        case kAudioObjectPropertyOwner:
            *outDataSize = sizeof(AudioObjectID);
            break;

        case kAudioObjectPropertyOwnedObjects:
            *outDataSize = 0 * sizeof(AudioObjectID);
            break;

        case kAudioStreamPropertyIsActive:
            *outDataSize = sizeof(UInt32);
            break;

        case kAudioStreamPropertyDirection:
            *outDataSize = sizeof(UInt32);
            break;

        case kAudioStreamPropertyTerminalType:
            *outDataSize = sizeof(UInt32);
            break;

        case kAudioStreamPropertyStartingChannel:
            *outDataSize = sizeof(UInt32);
            break;
        
        case kAudioStreamPropertyLatency:
            *outDataSize = sizeof(UInt32);
            break;

        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
            *outDataSize = sizeof(AudioStreamBasicDescription);
            break;

        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
            *outDataSize = kDevice_SampleRatesSize * sizeof(AudioStreamRangedDescription);
            break;

        default:
            result = kAudioHardwareUnknownPropertyError;
            break;
    };

    return result;
}

static OSStatus    _GetStreamPropertyData(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData)
{
    #pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
    
    //    declare the local variables
    OSStatus result = 0;
    UInt32 theNumberItemsToFetch;
    
    switch(inAddress->mSelector)
    {
        case kAudioObjectPropertyBaseClass:
            *((AudioClassID*)outData) = kAudioObjectClassID;
            *outDataSize = sizeof(AudioClassID);
            break;
            
        case kAudioObjectPropertyClass:
            *((AudioClassID*)outData) = kAudioStreamClassID;
            *outDataSize = sizeof(AudioClassID);
            break;
            
        case kAudioObjectPropertyOwner:
            *((AudioObjectID*)outData) = kObjectID_Device;
            *outDataSize = sizeof(AudioObjectID);
            break;
            
        case kAudioObjectPropertyOwnedObjects:
            //    Streams do not own any objects
            *outDataSize = 0 * sizeof(AudioObjectID);
            break;

        case kAudioStreamPropertyIsActive:
            pthread_mutex_lock(&gPlugIn_StateMutex);
            *((UInt32*)outData) = (inObjectID == kObjectID_Stream_Input) ? gStream_Input_IsActive : gStream_Output_IsActive;
            pthread_mutex_unlock(&gPlugIn_StateMutex);
            *outDataSize = sizeof(UInt32);
            break;

        case kAudioStreamPropertyDirection:
            *((UInt32*)outData) = (inObjectID == kObjectID_Stream_Input) ? 1 : 0;
            *outDataSize = sizeof(UInt32);
            break;

        case kAudioStreamPropertyTerminalType:
            *((UInt32*)outData) = (inObjectID == kObjectID_Stream_Input) ? kAudioStreamTerminalTypeMicrophone : kAudioStreamTerminalTypeSpeaker;
            *outDataSize = sizeof(UInt32);
            break;

        case kAudioStreamPropertyStartingChannel:
            *((UInt32*)outData) = 1;
            *outDataSize = sizeof(UInt32);
            break;

        case kAudioStreamPropertyLatency:
            *((UInt32*)outData) = kLatency_Frame_Size;
            *outDataSize = sizeof(UInt32);
            break;

        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
            pthread_mutex_lock(&gPlugIn_StateMutex);
            ((AudioStreamBasicDescription*)outData)->mSampleRate = gDevice_SampleRate;
            ((AudioStreamBasicDescription*)outData)->mFormatID = kAudioFormatLinearPCM;
            ((AudioStreamBasicDescription*)outData)->mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
            ((AudioStreamBasicDescription*)outData)->mBytesPerPacket = kBytes_Per_Channel * kNumber_Of_Channels;
            ((AudioStreamBasicDescription*)outData)->mFramesPerPacket = 1;
            ((AudioStreamBasicDescription*)outData)->mBytesPerFrame = kBytes_Per_Channel * kNumber_Of_Channels;
            ((AudioStreamBasicDescription*)outData)->mChannelsPerFrame = kNumber_Of_Channels;
            ((AudioStreamBasicDescription*)outData)->mBitsPerChannel = kBits_Per_Channel;
            pthread_mutex_unlock(&gPlugIn_StateMutex);
            *outDataSize = sizeof(AudioStreamBasicDescription);
            break;

        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
            theNumberItemsToFetch = inDataSize / sizeof(AudioStreamRangedDescription);
            
            //    clamp it to the number of items we have
            if(theNumberItemsToFetch > kDevice_SampleRatesSize)
            {
                theNumberItemsToFetch = kDevice_SampleRatesSize;
            }

            //    fill out the return array
            for(UInt32 i = 0; i < theNumberItemsToFetch; i++)
            {
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mSampleRate = kDevice_SampleRates[i];
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mFormatID = kAudioFormatLinearPCM;
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mBytesPerPacket = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mFramesPerPacket = 1;
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mBytesPerFrame = kBytes_Per_Frame;
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mChannelsPerFrame = kNumber_Of_Channels;
                ((AudioStreamRangedDescription*)outData)[i].mFormat.mBitsPerChannel = kBits_Per_Channel;
                ((AudioStreamRangedDescription*)outData)[i].mSampleRateRange.mMinimum = kDevice_SampleRates[i];
                ((AudioStreamRangedDescription*)outData)[i].mSampleRateRange.mMaximum = kDevice_SampleRates[i];
            }

            //    report how much we wrote
            *outDataSize = theNumberItemsToFetch * sizeof(AudioStreamRangedDescription);
            break;

        default:
            result = kAudioHardwareUnknownPropertyError;
            break;
    };

    return result;
}

static OSStatus    _SetStreamPropertyData(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2])
{
    #pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
    
    //    declare the local variables
    OSStatus result = 0;
    Float64 theOldSampleRate;
    UInt64 theNewSampleRate;
    
    *outNumberPropertiesChanged = 0;
    
    switch(inAddress->mSelector)
    {
        case kAudioStreamPropertyIsActive:
            pthread_mutex_lock(&gPlugIn_StateMutex);
            if(inObjectID == kObjectID_Stream_Input)
            {
                if(gStream_Input_IsActive != (*((const UInt32*)inData) != 0))
                {
                    gStream_Input_IsActive = *((const UInt32*)inData) != 0;
                    *outNumberPropertiesChanged = 1;
                    outChangedAddresses[0].mSelector = kAudioStreamPropertyIsActive;
                    outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
                    outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;
                }
            }
            else
            {
                if(gStream_Output_IsActive != (*((const UInt32*)inData) != 0))
                {
                    gStream_Output_IsActive = *((const UInt32*)inData) != 0;
                    *outNumberPropertiesChanged = 1;
                    outChangedAddresses[0].mSelector = kAudioStreamPropertyIsActive;
                    outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
                    outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;
                }
            }
            pthread_mutex_unlock(&gPlugIn_StateMutex);
            break;
            
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
            pthread_mutex_lock(&gPlugIn_StateMutex);
            theOldSampleRate = gDevice_SampleRate;
            pthread_mutex_unlock(&gPlugIn_StateMutex);
            if(((const AudioStreamBasicDescription*)inData)->mSampleRate != theOldSampleRate)
            {
                theOldSampleRate = ((const AudioStreamBasicDescription*)inData)->mSampleRate;
                theNewSampleRate = (UInt64)theOldSampleRate;
                dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{ gPlugIn_Host->RequestDeviceConfigurationChange(gPlugIn_Host, kObjectID_Device, theNewSampleRate, NULL); });
            }
            break;
        
        default:
            result = kAudioHardwareUnknownPropertyError;
            break;
    };

    return result;
}

#pragma mark Control Property Operations

static Boolean    _HasControlProperty(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress)
{
    
    #pragma unused(inClientProcessID)
    
    //    declare the local variables
    Boolean result = false;
    
    switch(inObjectID)
    {
        case kObjectID_Volume_Input_Master:
        case kObjectID_Volume_Output_Master:
            switch(inAddress->mSelector)
            {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyOwnedObjects:
                case kAudioControlPropertyScope:
                case kAudioControlPropertyElement:
                case kAudioLevelControlPropertyScalarValue:
                case kAudioLevelControlPropertyDecibelValue:
                case kAudioLevelControlPropertyDecibelRange:
                case kAudioLevelControlPropertyConvertScalarToDecibels:
                case kAudioLevelControlPropertyConvertDecibelsToScalar:
                    result = true;
                    break;
            };
            break;
        
        case kObjectID_Mute_Input_Master:
        case kObjectID_Mute_Output_Master:
            switch(inAddress->mSelector)
            {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyOwnedObjects:
                case kAudioControlPropertyScope:
                case kAudioControlPropertyElement:
                case kAudioBooleanControlPropertyValue:
                    result = true;
                    break;
            };
            break;
    };

    return result;
}

static OSStatus    _IsControlPropertySettable(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable)
{
    #pragma unused(inClientProcessID)
    
    OSStatus result = 0;
    
    switch(inObjectID)
    {
        case kObjectID_Volume_Input_Master:
        case kObjectID_Volume_Output_Master:
            switch(inAddress->mSelector)
            {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyOwnedObjects:
                case kAudioControlPropertyScope:
                case kAudioControlPropertyElement:
                case kAudioLevelControlPropertyDecibelRange:
                case kAudioLevelControlPropertyConvertScalarToDecibels:
                case kAudioLevelControlPropertyConvertDecibelsToScalar:
                    *outIsSettable = false;
                    break;
                
                case kAudioLevelControlPropertyScalarValue:
                case kAudioLevelControlPropertyDecibelValue:
                    *outIsSettable = true;
                    break;
                
                default:
                    result = kAudioHardwareUnknownPropertyError;
                    break;
            };
            break;
        
        case kObjectID_Mute_Input_Master:
        case kObjectID_Mute_Output_Master:
            switch(inAddress->mSelector)
            {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyOwnedObjects:
                case kAudioControlPropertyScope:
                case kAudioControlPropertyElement:
                    *outIsSettable = false;
                    break;
                
                case kAudioBooleanControlPropertyValue:
                    *outIsSettable = true;
                    break;
                
                default:
                    result = kAudioHardwareUnknownPropertyError;
                    break;
            };
            break;
                
        default:
            result = kAudioHardwareBadObjectError;
            break;
    };

    return result;
}

static OSStatus    _GetControlPropertyDataSize(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize)
{
    #pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
    
    //    declare the local variables
    OSStatus result = 0;
    
    switch(inObjectID)
    {
        case kObjectID_Volume_Input_Master:
        case kObjectID_Volume_Output_Master:
            switch(inAddress->mSelector)
            {
                case kAudioObjectPropertyBaseClass:
                    *outDataSize = sizeof(AudioClassID);
                    break;

                case kAudioObjectPropertyClass:
                    *outDataSize = sizeof(AudioClassID);
                    break;

                case kAudioObjectPropertyOwner:
                    *outDataSize = sizeof(AudioObjectID);
                    break;

                case kAudioObjectPropertyOwnedObjects:
                    *outDataSize = 0 * sizeof(AudioObjectID);
                    break;

                case kAudioControlPropertyScope:
                    *outDataSize = sizeof(AudioObjectPropertyScope);
                    break;

                case kAudioControlPropertyElement:
                    *outDataSize = sizeof(AudioObjectPropertyElement);
                    break;

                case kAudioLevelControlPropertyScalarValue:
                    *outDataSize = sizeof(Float32);
                    break;

                case kAudioLevelControlPropertyDecibelValue:
                    *outDataSize = sizeof(Float32);
                    break;

                case kAudioLevelControlPropertyDecibelRange:
                    *outDataSize = sizeof(AudioValueRange);
                    break;

                case kAudioLevelControlPropertyConvertScalarToDecibels:
                    *outDataSize = sizeof(Float32);
                    break;

                case kAudioLevelControlPropertyConvertDecibelsToScalar:
                    *outDataSize = sizeof(Float32);
                    break;

                default:
                    result = kAudioHardwareUnknownPropertyError;
                    break;
            };
            break;
        
        case kObjectID_Mute_Input_Master:
        case kObjectID_Mute_Output_Master:
            switch(inAddress->mSelector)
            {
                case kAudioObjectPropertyBaseClass:
                    *outDataSize = sizeof(AudioClassID);
                    break;

                case kAudioObjectPropertyClass:
                    *outDataSize = sizeof(AudioClassID);
                    break;

                case kAudioObjectPropertyOwner:
                    *outDataSize = sizeof(AudioObjectID);
                    break;

                case kAudioObjectPropertyOwnedObjects:
                    *outDataSize = 0 * sizeof(AudioObjectID);
                    break;

                case kAudioControlPropertyScope:
                    *outDataSize = sizeof(AudioObjectPropertyScope);
                    break;

                case kAudioControlPropertyElement:
                    *outDataSize = sizeof(AudioObjectPropertyElement);
                    break;

                case kAudioBooleanControlPropertyValue:
                    *outDataSize = sizeof(UInt32);
                    break;

                default:
                    result = kAudioHardwareUnknownPropertyError;
                    break;
            };
            break;
                
        default:
            result = kAudioHardwareBadObjectError;
            break;
    };

    return result;
}

static OSStatus    _GetControlPropertyData(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData)
{
    #pragma unused(inClientProcessID, inQualifierData, inQualifierDataSize)
    
    //    declare the local variables
    OSStatus result = 0;
    
    switch(inObjectID)
    {
        case kObjectID_Volume_Input_Master:
        case kObjectID_Volume_Output_Master:
            switch(inAddress->mSelector)
            {
                case kAudioObjectPropertyBaseClass:
                    *((AudioClassID*)outData) = kAudioLevelControlClassID;
                    *outDataSize = sizeof(AudioClassID);
                    break;
                    
                case kAudioObjectPropertyClass:
                    *((AudioClassID*)outData) = kAudioVolumeControlClassID;
                    *outDataSize = sizeof(AudioClassID);
                    break;
                    
                case kAudioObjectPropertyOwner:
                    *((AudioObjectID*)outData) = kObjectID_Device;
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                    
                case kAudioObjectPropertyOwnedObjects:
                    //    Controls do not own any objects
                    *outDataSize = 0 * sizeof(AudioObjectID);
                    break;

                case kAudioControlPropertyScope:
                    *((AudioObjectPropertyScope*)outData) = (inObjectID == kObjectID_Volume_Input_Master) ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;
                    *outDataSize = sizeof(AudioObjectPropertyScope);
                    break;

                case kAudioControlPropertyElement:
                    *((AudioObjectPropertyElement*)outData) = kAudioObjectPropertyElementMain;
                    *outDataSize = sizeof(AudioObjectPropertyElement);
                    break;

                case kAudioLevelControlPropertyScalarValue:
                    pthread_mutex_lock(&gPlugIn_StateMutex);
                    *((Float32*)outData) = volume_to_scalar(gVolume_Master_Value);
                    pthread_mutex_unlock(&gPlugIn_StateMutex);
                    *outDataSize = sizeof(Float32);
                    break;

                case kAudioLevelControlPropertyDecibelValue:
                    pthread_mutex_lock(&gPlugIn_StateMutex);
                    *((Float32*)outData) = gVolume_Master_Value;
                    pthread_mutex_unlock(&gPlugIn_StateMutex);
                    *((Float32*)outData) = volume_to_decibel(*((Float32*)outData));
                    
                    //    report how much we wrote
                    *outDataSize = sizeof(Float32);
                    break;

                case kAudioLevelControlPropertyDecibelRange:
                    ((AudioValueRange*)outData)->mMinimum = kVolume_MinDB;
                    ((AudioValueRange*)outData)->mMaximum = kVolume_MaxDB;
                    *outDataSize = sizeof(AudioValueRange);
                    break;

                case kAudioLevelControlPropertyConvertScalarToDecibels:
                    
                    if(*((Float32*)outData) < 0.0)
                    {
                        *((Float32*)outData) = 0;
                    }
                    if(*((Float32*)outData) > 1.0)
                    {
                        *((Float32*)outData) = 1.0;
                    }
                    
                    *((Float32*)outData) *= *((Float32*)outData);
                    *((Float32*)outData) = kVolume_MinDB + (*((Float32*)outData) * (kVolume_MaxDB - kVolume_MinDB));
                    
                    //    report how much we wrote
                    *outDataSize = sizeof(Float32);
                    break;

                case kAudioLevelControlPropertyConvertDecibelsToScalar:
                    
                    if(*((Float32*)outData) < kVolume_MinDB)
                    {
                        *((Float32*)outData) = kVolume_MinDB;
                    }
                    if(*((Float32*)outData) > kVolume_MaxDB)
                    {
                        *((Float32*)outData) = kVolume_MaxDB;
                    }
                    
                    *((Float32*)outData) = *((Float32*)outData) - kVolume_MinDB;
                    *((Float32*)outData) /= kVolume_MaxDB - kVolume_MinDB;
                    *((Float32*)outData) = sqrtf(*((Float32*)outData));
                    
                    //    report how much we wrote
                    *outDataSize = sizeof(Float32);
                    break;

                default:
                    result = kAudioHardwareUnknownPropertyError;
                    break;
            };
            break;
        
        case kObjectID_Mute_Input_Master:
        case kObjectID_Mute_Output_Master:
            switch(inAddress->mSelector)
            {
                case kAudioObjectPropertyBaseClass:
                    *((AudioClassID*)outData) = kAudioBooleanControlClassID;
                    *outDataSize = sizeof(AudioClassID);
                    break;
                    
                case kAudioObjectPropertyClass:
                    *((AudioClassID*)outData) = kAudioMuteControlClassID;
                    *outDataSize = sizeof(AudioClassID);
                    break;
                    
                case kAudioObjectPropertyOwner:
                    *((AudioObjectID*)outData) = kObjectID_Device;
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                    
                case kAudioObjectPropertyOwnedObjects:
                    *outDataSize = 0 * sizeof(AudioObjectID);
                    break;

                case kAudioControlPropertyScope:
                    *((AudioObjectPropertyScope*)outData) = (inObjectID == kObjectID_Mute_Input_Master) ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;
                    *outDataSize = sizeof(AudioObjectPropertyScope);
                    break;

                case kAudioControlPropertyElement:
                    *((AudioObjectPropertyElement*)outData) = kAudioObjectPropertyElementMain;
                    *outDataSize = sizeof(AudioObjectPropertyElement);
                    break;

                case kAudioBooleanControlPropertyValue:
                    pthread_mutex_lock(&gPlugIn_StateMutex);
                    *((UInt32*)outData) = gMute_Master_Value ? 1 : 0;
                    pthread_mutex_unlock(&gPlugIn_StateMutex);
                    *outDataSize = sizeof(UInt32);
                    break;

                default:
                    result = kAudioHardwareUnknownPropertyError;
                    break;
            };
            break;
                
        default:
            result = kAudioHardwareBadObjectError;
            break;
    };

    return result;
}

static OSStatus    _SetControlPropertyData(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2])
{
    #pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
    
    //    declare the local variables
    OSStatus result = 0;
    Float32 theNewVolume;
    
    *outNumberPropertiesChanged = 0;
    
    switch(inObjectID)
    {
        case kObjectID_Volume_Input_Master:
        case kObjectID_Volume_Output_Master:
            switch(inAddress->mSelector)
            {
                case kAudioLevelControlPropertyScalarValue:
                    theNewVolume = volume_from_scalar(*((const Float32*)inData));
                    if(theNewVolume < 0.0)
                    {
                        theNewVolume = 0.0;
                    }
                    else if(theNewVolume > 1.0)
                    {
                        theNewVolume = 1.0;
                    }
                    pthread_mutex_lock(&gPlugIn_StateMutex);
                    if(gVolume_Master_Value != theNewVolume)
                    {
                        gVolume_Master_Value = theNewVolume;
                        *outNumberPropertiesChanged = 2;
                        outChangedAddresses[0].mSelector = kAudioLevelControlPropertyScalarValue;
                        outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
                        outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;
                        outChangedAddresses[1].mSelector = kAudioLevelControlPropertyDecibelValue;
                        outChangedAddresses[1].mScope = kAudioObjectPropertyScopeGlobal;
                        outChangedAddresses[1].mElement = kAudioObjectPropertyElementMain;
                    }
                    pthread_mutex_unlock(&gPlugIn_StateMutex);
                    break;
                
                case kAudioLevelControlPropertyDecibelValue:
                    theNewVolume = *((const Float32*)inData);
                    if(theNewVolume < kVolume_MinDB)
                    {
                        theNewVolume = kVolume_MinDB;
                    }
                    else if(theNewVolume > kVolume_MaxDB)
                    {
                        theNewVolume = kVolume_MaxDB;
                    }
                    theNewVolume = volume_from_decibel(theNewVolume);
                    pthread_mutex_lock(&gPlugIn_StateMutex);
                    if(gVolume_Master_Value != theNewVolume)
                    {
                        gVolume_Master_Value = theNewVolume;
                        *outNumberPropertiesChanged = 2;
                        outChangedAddresses[0].mSelector = kAudioLevelControlPropertyScalarValue;
                        outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
                        outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;
                        outChangedAddresses[1].mSelector = kAudioLevelControlPropertyDecibelValue;
                        outChangedAddresses[1].mScope = kAudioObjectPropertyScopeGlobal;
                        outChangedAddresses[1].mElement = kAudioObjectPropertyElementMain;
                    }
                    pthread_mutex_unlock(&gPlugIn_StateMutex);
                    break;
                
                default:
                    result = kAudioHardwareUnknownPropertyError;
                    break;
            };
            break;
        
        case kObjectID_Mute_Input_Master:
        case kObjectID_Mute_Output_Master:
            switch(inAddress->mSelector)
            {
                case kAudioBooleanControlPropertyValue:
                    pthread_mutex_lock(&gPlugIn_StateMutex);
                    if(gMute_Master_Value != (*((const UInt32*)inData) != 0))
                    {
                        gMute_Master_Value = *((const UInt32*)inData) != 0;
                        *outNumberPropertiesChanged = 1;
                        outChangedAddresses[0].mSelector = kAudioBooleanControlPropertyValue;
                        outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
                        outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;
                    }
                    pthread_mutex_unlock(&gPlugIn_StateMutex);
                    break;
                
                default:
                    result = kAudioHardwareUnknownPropertyError;
                    break;
            };
            break;
                
        default:
            result = kAudioHardwareBadObjectError;
            break;
    };

    return result;
}

#pragma mark IO Operations

static OSStatus    _StartIO(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt32 inClientID)
{
    #pragma unused(inClientID, inDeviceObjectID)
    
    OSStatus result = 0;
    
    pthread_mutex_lock(&gPlugIn_StateMutex);
    
    if(gDevice_IOIsRunning == UINT64_MAX)
    {
        result = kAudioHardwareIllegalOperationError;
    }
    else if(gDevice_IOIsRunning == 0)
    {
        gDevice_IOIsRunning = 1;
        gDevice_NumberTimeStamps = 0;
        gDevice_AnchorSampleTime = 0;
        gDevice_AnchorHostTime = mach_absolute_time();
        
        gRingBuffer = calloc(kRing_Buffer_Frame_Size * kNumber_Of_Channels, sizeof(Float32));
    }
    else
    {
        ++gDevice_IOIsRunning;
    }
    
    pthread_mutex_unlock(&gPlugIn_StateMutex);
    
    return result;
}

static OSStatus    _StopIO(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt32 inClientID)
{
    
    #pragma unused(inClientID, inDeviceObjectID)
    
    OSStatus result = 0;
    
    pthread_mutex_lock(&gPlugIn_StateMutex);
    
    if(gDevice_IOIsRunning == 0)
    {
        result = kAudioHardwareIllegalOperationError;
    }
    else if(gDevice_IOIsRunning == 1)
    {
        gDevice_IOIsRunning = 0;
        free(gRingBuffer);
    }
    else
    {
        --gDevice_IOIsRunning;
    }
    
    pthread_mutex_unlock(&gPlugIn_StateMutex);
    
    return result;
}

static OSStatus    _GetZeroTimeStamp(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt32 inClientID, Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed)
{
    #pragma unused(inClientID, inDeviceObjectID)
    
    OSStatus result = 0;
    UInt64 theCurrentHostTime;
    Float64 theHostTicksPerRingBuffer;
    Float64 theHostTickOffset;
    UInt64 theNextHostTime;
    
    pthread_mutex_lock(&gDevice_IOMutex);
    
    theCurrentHostTime = mach_absolute_time();
    
    theHostTicksPerRingBuffer = gDevice_HostTicksPerFrame * ((Float64)kDevice_RingBufferSize);
    
    theHostTickOffset = ((Float64)(gDevice_NumberTimeStamps + 1)) * theHostTicksPerRingBuffer;
    
    theNextHostTime = gDevice_AnchorHostTime + ((UInt64)theHostTickOffset);
    
    if(theNextHostTime <= theCurrentHostTime)
    {
        ++gDevice_NumberTimeStamps;
    }
    
    *outSampleTime = gDevice_NumberTimeStamps * kDevice_RingBufferSize;
    *outHostTime = gDevice_AnchorHostTime + (((Float64)gDevice_NumberTimeStamps) * theHostTicksPerRingBuffer);
    *outSeed = 1;
    
    pthread_mutex_unlock(&gDevice_IOMutex);
    
    return result;
}

static OSStatus    _WillDoIOOperation(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, Boolean* outWillDo, Boolean* outWillDoInPlace)
{
    #pragma unused(inClientID, inDeviceObjectID)
    
    OSStatus result = 0;
    bool willDo = false;
    bool willDoInPlace = true;
    switch(inOperationID)
    {
        case kAudioServerPlugInIOOperationReadInput:
            willDo = true;
            willDoInPlace = true;
            break;
            
        case kAudioServerPlugInIOOperationWriteMix:
            willDo = true;
            willDoInPlace = true;
            break;
            
    };
    
    if(outWillDo != NULL)
    {
        *outWillDo = willDo;
    }
    if(outWillDoInPlace != NULL)
    {
        *outWillDoInPlace = willDoInPlace;
    }

    return result;
}

static OSStatus    _BeginIOOperation(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo)
{
    
    #pragma unused(inClientID, inOperationID, inIOBufferFrameSize, inIOCycleInfo, inDeviceObjectID)
    
    OSStatus result = 0;

    return result;
}


static OSStatus    _EndIOOperation(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo)
{
    #pragma unused(inClientID, inOperationID, inIOBufferFrameSize, inIOCycleInfo, inDeviceObjectID)
    
    OSStatus result = 0;

    return result;
}


static OSStatus    _DoIOOperation(AudioServerPlugInDriverRef in_driver, AudioObjectID inDeviceObjectID, AudioObjectID inStreamObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo, void* ioMainBuffer, void* ioSecondaryBuffer)
{
    #pragma unused(inClientID, inIOCycleInfo, ioSecondaryBuffer, inDeviceObjectID)
    
    OSStatus the_answer = 0;
    static Float64 lastOutputSampleTime = 0;
    static Boolean isBufferClear = true;
    
    UInt64 m_sample_time = inOperationID == kAudioServerPlugInIOOperationReadInput ? inIOCycleInfo->mInputTime.mSampleTime : inIOCycleInfo->mOutputTime.mSampleTime;
    UInt32 ringBufferFrameLocationStart = m_sample_time % kRing_Buffer_Frame_Size;
    UInt32 firstPartFrameSize = kRing_Buffer_Frame_Size - ringBufferFrameLocationStart;
    UInt32 secondPartFrameSize = 0;
    
    if (firstPartFrameSize >= inIOBufferFrameSize)
        firstPartFrameSize = inIOBufferFrameSize;
    else
        secondPartFrameSize = inIOBufferFrameSize - firstPartFrameSize;
    
    
    
    if(inOperationID == kAudioServerPlugInIOOperationReadInput)
    {
        if (gMute_Master_Value || lastOutputSampleTime - inIOBufferFrameSize < inIOCycleInfo->mInputTime.mSampleTime)
        {
            vDSP_vclr(ioMainBuffer, 1, inIOBufferFrameSize * kNumber_Of_Channels);
            
            if (!isBufferClear)
            {
                vDSP_vclr(gRingBuffer, 1, kRing_Buffer_Frame_Size * kNumber_Of_Channels);
                isBufferClear = true;
            }
        }
        else
        {
            memcpy(ioMainBuffer, gRingBuffer + ringBufferFrameLocationStart * kNumber_Of_Channels, firstPartFrameSize * kNumber_Of_Channels * sizeof(Float32));
            memcpy((Float32*)ioMainBuffer + firstPartFrameSize * kNumber_Of_Channels, gRingBuffer, secondPartFrameSize * kNumber_Of_Channels * sizeof(Float32));
            
        if(kEnableVolumeControl)
        {
         vDSP_vsmul(ioMainBuffer, 1, &gVolume_Master_Value, ioMainBuffer, 1, inIOBufferFrameSize * kNumber_Of_Channels);
        }

        }
    }
    
    if(inOperationID == kAudioServerPlugInIOOperationWriteMix)
    {
        
        if (inIOCycleInfo->mCurrentTime.mSampleTime > inIOCycleInfo->mOutputTime.mSampleTime + inIOBufferFrameSize + kLatency_Frame_Size)
            return kAudioHardwareUnspecifiedError;
        
        
        memcpy(gRingBuffer + ringBufferFrameLocationStart * kNumber_Of_Channels, ioMainBuffer, firstPartFrameSize * kNumber_Of_Channels * sizeof(Float32));
        memcpy(gRingBuffer, (Float32*)ioMainBuffer + firstPartFrameSize * kNumber_Of_Channels, secondPartFrameSize * kNumber_Of_Channels * sizeof(Float32));
        
        lastOutputSampleTime = inIOCycleInfo->mOutputTime.mSampleTime + inIOBufferFrameSize;
        isBufferClear = false;
    }

    return the_answer;
}

static OSStatus	_GetPlugInPropertyData(AudioServerPlugInDriverRef in_driver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData)
{
	//	declare the local variables
	OSStatus result = 0;
	UInt32 theNumberItemsToFetch;
	
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			*((AudioClassID*)outData) = kAudioObjectClassID;
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyClass:
			*((AudioClassID*)outData) = kAudioPlugInClassID;
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyOwner:
			*((AudioObjectID*)outData) = kAudioObjectUnknown;
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioObjectPropertyManufacturer:
			*((CFStringRef*)outData) = CFSTR("Apple Inc.");
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:

			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	Clamp that to the number of boxes this driver implements (which is just 1)
			if(theNumberItemsToFetch > (gBox_Acquired ? 2 : 1))
			{
				theNumberItemsToFetch = (gBox_Acquired ? 2 : 1);
			}
			
			//	Write the devices' object IDs into the return value
			if(theNumberItemsToFetch > 1)
			{
				((AudioObjectID*)outData)[0] = kObjectID_Box;
				((AudioObjectID*)outData)[0] = kObjectID_Device;
			}
			else if(theNumberItemsToFetch > 0)
			{
				((AudioObjectID*)outData)[0] = kObjectID_Box;
			}
			
			//	Return how many bytes we wrote to
			*outDataSize = theNumberItemsToFetch * sizeof(AudioClassID);
			break;
			
		case kAudioPlugInPropertyBoxList:

			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	Clamp that to the number of boxes this driver implements (which is just 1)
			if(theNumberItemsToFetch > 1)
			{
				theNumberItemsToFetch = 1;
			}
			
			//	Write the devices' object IDs into the return value
			if(theNumberItemsToFetch > 0)
			{
				((AudioObjectID*)outData)[0] = kObjectID_Box;
			}
			
			//	Return how many bytes we wrote to
			*outDataSize = theNumberItemsToFetch * sizeof(AudioClassID);
			break;
			
		case kAudioPlugInPropertyTranslateUIDToBox:
			FailWithAction(inDataSize < sizeof(AudioObjectID), result = kAudioHardwareBadPropertySizeError, Done, "_GetPlugInPropertyData: not enough space for the return value of kAudioPlugInPropertyTranslateUIDToBox");

			CFStringRef boxUID = get_box_uid();

			if(CFStringCompare(*((CFStringRef*)inQualifierData), boxUID, 0) == kCFCompareEqualTo)
			{
				CFStringRef formattedString = CFStringCreateWithFormat(NULL, NULL, CFSTR(kBox_UID), kNumber_Of_Channels);
				if(CFStringCompare(*((CFStringRef*)inQualifierData), formattedString, 0) == kCFCompareEqualTo)
				{
					*((AudioObjectID*)outData) = kObjectID_Box;
				}
				else
				{
					*((AudioObjectID*)outData) = kAudioObjectUnknown;
				}
				*outDataSize = sizeof(AudioObjectID);
				CFRelease(formattedString);

				*((AudioObjectID*)outData) = kObjectID_Box;
			}
			else
			{
				*((AudioObjectID*)outData) = kAudioObjectUnknown;
			}
			*outDataSize = sizeof(AudioObjectID);
			CFRelease(boxUID);
			break;
			
		case kAudioPlugInPropertyDeviceList:
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	Clamp that to the number of devices this driver implements (which is just 1 if the
			//	box has been acquired)
			if(theNumberItemsToFetch > (gBox_Acquired ? 2 : 0))
			{
				theNumberItemsToFetch = (gBox_Acquired ? 2 : 0);
			}
			
			//	Write the devices' object IDs into the return value
			if(theNumberItemsToFetch > 1)
			{
				((AudioObjectID*)outData)[0] = kObjectID_Device;
                ((AudioObjectID*)outData)[1] = kObjectID_Device2;
			}
            else if(theNumberItemsToFetch > 0)
            {
                ((AudioObjectID*)outData)[0] = kObjectID_Device;
            }
			
			//	Return how many bytes we wrote to
			*outDataSize = theNumberItemsToFetch * sizeof(AudioClassID);
			break;
			
		case kAudioPlugInPropertyTranslateUIDToDevice:

			FailWithAction(inDataSize < sizeof(AudioObjectID), result = kAudioHardwareBadPropertySizeError, Done, "_GetPlugInPropertyData: not enough space for the return value of kAudioPlugInPropertyTranslateUIDToDevice");
            
			CFStringRef deviceUID = get_device_uid();
            CFStringRef device2UID = get_device2_uid();

			if(CFStringCompare(*((CFStringRef*)inQualifierData), deviceUID, 0) == kCFCompareEqualTo)
			{
				*((AudioObjectID*)outData) = kObjectID_Device;
			}
            else if(CFStringCompare(*((CFStringRef*)inQualifierData), device2UID, 0) == kCFCompareEqualTo)
            {
                *((AudioObjectID*)outData) = kObjectID_Device2;
            }
			else
			{
				*((AudioObjectID*)outData) = kAudioObjectUnknown;
			}
			*outDataSize = sizeof(AudioObjectID);
			CFRelease(deviceUID);
            CFRelease(device2UID);
			break;
			
		case kAudioPlugInPropertyResourceBundle:
			*((CFStringRef*)outData) = CFSTR("");
			*outDataSize = sizeof(CFStringRef);
			break;
			
		default:
			result = kAudioHardwareUnknownPropertyError;
			break;
	};
Done:
	return result;
}

static OSStatus	set_propertyData(pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2])
{
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData, inDataSize, inData)
	
	//	declare the local variables
	OSStatus result = 0;
	
	//	initialize the returned number of changed properties
	*outNumberPropertiesChanged = 0;
	
	switch(inAddress->mSelector)
	{
		default:
			result = kAudioHardwareUnknownPropertyError;
			break;
	};

	return result;
}

#pragma mark Box Property Operations

static Boolean	hasbox_property(pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress)
{
	//	This method returns whether or not the box object has the given property.
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	Boolean result = false;
	
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyName:
		case kAudioObjectPropertyModelName:
		case kAudioObjectPropertyManufacturer:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioObjectPropertyIdentify:
		case kAudioObjectPropertySerialNumber:
		case kAudioObjectPropertyFirmwareVersion:
		case kAudioBoxPropertyBoxUID:
		case kAudioBoxPropertyTransportType:
		case kAudioBoxPropertyHasAudio:
		case kAudioBoxPropertyHasVideo:
		case kAudioBoxPropertyHasMIDI:
		case kAudioBoxPropertyIsProtected:
		case kAudioBoxPropertyAcquired:
		case kAudioBoxPropertyAcquisitionFailed:
		case kAudioBoxPropertyDeviceList:
            result = true;
			break;
	};

	return result;
}

static OSStatus	box_property_settable(pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable)
{

	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	OSStatus result = 0;
	

	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyModelName:
		case kAudioObjectPropertyManufacturer:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioObjectPropertySerialNumber:
		case kAudioObjectPropertyFirmwareVersion:
		case kAudioBoxPropertyBoxUID:
		case kAudioBoxPropertyTransportType:
		case kAudioBoxPropertyHasAudio:
		case kAudioBoxPropertyHasVideo:
		case kAudioBoxPropertyHasMIDI:
		case kAudioBoxPropertyIsProtected:
		case kAudioBoxPropertyAcquisitionFailed:
		case kAudioBoxPropertyDeviceList:
			*outIsSettable = false;
			break;
		
		case kAudioObjectPropertyName:
		case kAudioObjectPropertyIdentify:
		case kAudioBoxPropertyAcquired:
			*outIsSettable = true;
			break;
		
		default:
			result = kAudioHardwareUnknownPropertyError;
			break;
	};

	return result;
}

static OSStatus	getbox_property_datasize(pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize)
{
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus result = 0;
	
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyClass:
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyOwner:
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioObjectPropertyName:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyModelName:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyManufacturer:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
			*outDataSize = 0;
			break;
			
		case kAudioObjectPropertyIdentify:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioObjectPropertySerialNumber:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyFirmwareVersion:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioBoxPropertyBoxUID:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioBoxPropertyTransportType:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyHasAudio:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyHasVideo:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyHasMIDI:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyIsProtected:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyAcquired:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyAcquisitionFailed:
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyDeviceList:
			{
				pthread_mutex_lock(&gPlugIn_StateMutex);
				*outDataSize = gBox_Acquired ? sizeof(AudioObjectID) * 2 : 0;
				pthread_mutex_unlock(&gPlugIn_StateMutex);
			}
			break;
			
		default:
			result = kAudioHardwareUnknownPropertyError;
			break;
	};

	return result;
}

static OSStatus	getbox_property_data(pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData)
{
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus result = 0;
	
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			*((AudioClassID*)outData) = kAudioObjectClassID;
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyClass:
			*((AudioClassID*)outData) = kAudioBoxClassID;
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyOwner:
			*((AudioObjectID*)outData) = kObjectID_PlugIn;
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioObjectPropertyName:
			pthread_mutex_lock(&gPlugIn_StateMutex);
			*((CFStringRef*)outData) = gBox_Name;
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			if(*((CFStringRef*)outData) != NULL)
			{
				CFRetain(*((CFStringRef*)outData));
			}
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyModelName:
			*((CFStringRef*)outData) = CFSTR("Null Model");
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyManufacturer:
			*((CFStringRef*)outData) = CFSTR("Apple Inc.");
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
			//	This returns the objects directly owned by the object. Boxes don't own anything.
			*outDataSize = 0;
			break;
			
		case kAudioObjectPropertyIdentify:
			*((UInt32*)outData) = 0;
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioObjectPropertySerialNumber:			*((CFStringRef*)outData) = CFSTR("00000001");
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyFirmwareVersion:
			*((CFStringRef*)outData) = CFSTR("1.0");
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioBoxPropertyBoxUID:

			*((CFStringRef*)outData) = get_box_uid();
			break;
			
		case kAudioBoxPropertyTransportType:
			*((UInt32*)outData) = kAudioDeviceTransportTypeVirtual;
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyHasAudio:
			*((UInt32*)outData) = 1;
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyHasVideo:
			*((UInt32*)outData) = 0;
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyHasMIDI:
			*((UInt32*)outData) = 0;
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyIsProtected:
			*((UInt32*)outData) = 0;
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyAcquired:
			pthread_mutex_lock(&gPlugIn_StateMutex);
			*((UInt32*)outData) = gBox_Acquired ? 1 : 0;
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyAcquisitionFailed:
			*((UInt32*)outData) = 0;
			*outDataSize = sizeof(UInt32);
			break;
			
		case kAudioBoxPropertyDeviceList:
			//	This is used to indicate which devices came from this box
			pthread_mutex_lock(&gPlugIn_StateMutex);
			if(gBox_Acquired)
			{
                if(inDataSize < sizeof(AudioObjectID))
                {
                    result = kAudioHardwareBadPropertySizeError;
                    *outDataSize = 0;
                }
                else
                {
                    if (inDataSize >= sizeof(AudioObjectID) * 2)
                    {
                        ((AudioObjectID*)outData)[0] = kObjectID_Device;
                        ((AudioObjectID*)outData)[1] = kObjectID_Device2;
                        *outDataSize = sizeof(AudioObjectID) * 2;
                    }
                    else
                    {
                        ((AudioObjectID*)outData)[0] = kObjectID_Device;
                        *outDataSize = sizeof(AudioObjectID) * 1;
                    }
                }
			}
			else
			{
				*outDataSize = 0;
			}
            
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			break;
			
		default:
			result = kAudioHardwareUnknownPropertyError;
			break;
	};

	return result;
}

static OSStatus	set_box_property(pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2])
{
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData, inDataSize, inData)
	
	//	declare the local variables
	OSStatus result = 0;
	
	//	initialize the returned number of changed properties
	*outNumberPropertiesChanged = 0;
	
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyName:
			//	Boxes should allow their name to be editable
			{
				CFStringRef* theNewName = (CFStringRef*)inData;
				pthread_mutex_lock(&gPlugIn_StateMutex);
				if((theNewName != NULL) && (*theNewName != NULL))
				{
					CFRetain(*theNewName);
				}
				if(gBox_Name != NULL)
				{
					CFRelease(gBox_Name);
				}
				gBox_Name = *theNewName;
				pthread_mutex_unlock(&gPlugIn_StateMutex);
				*outNumberPropertiesChanged = 1;
				outChangedAddresses[0].mSelector = kAudioObjectPropertyName;
				outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
				outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;
			}
			break;
			
		case kAudioObjectPropertyIdentify:
			{
				dispatch_after(dispatch_time(0, 2ULL * 1000ULL * 1000ULL * 1000ULL), dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),	^()
																																		{
																																			AudioObjectPropertyAddress theAddress = { kAudioObjectPropertyIdentify, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
																																			gPlugIn_Host->PropertiesChanged(gPlugIn_Host, kObjectID_Box, 1, &theAddress);
																																		});
			}
			break;
			
		case kAudioBoxPropertyAcquired:
			//	When the box is acquired, it means the contents, namely the device, are available to the system
			{
				pthread_mutex_lock(&gPlugIn_StateMutex);
				if(gBox_Acquired != (*((UInt32*)inData) != 0))
				{
					//	the new value is different from the old value, so save it
					gBox_Acquired = *((UInt32*)inData) != 0;
					gPlugIn_Host->WriteToStorage(gPlugIn_Host, CFSTR("box acquired"), gBox_Acquired ? kCFBooleanTrue : kCFBooleanFalse);
					
					//	and it means that this property and the device list property have changed
					*outNumberPropertiesChanged = 2;
					outChangedAddresses[0].mSelector = kAudioBoxPropertyAcquired;
					outChangedAddresses[0].mScope = kAudioObjectPropertyScopeGlobal;
					outChangedAddresses[0].mElement = kAudioObjectPropertyElementMain;
					outChangedAddresses[1].mSelector = kAudioBoxPropertyDeviceList;
					outChangedAddresses[1].mScope = kAudioObjectPropertyScopeGlobal;
					outChangedAddresses[1].mElement = kAudioObjectPropertyElementMain;
					
					//	but it also means that the device list has changed for the plug-in too
					dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),	^()
																									{
																										AudioObjectPropertyAddress theAddress = { kAudioPlugInPropertyDeviceList, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
																										gPlugIn_Host->PropertiesChanged(gPlugIn_Host, kObjectID_PlugIn, 1, &theAddress);
																									});
				}
				pthread_mutex_unlock(&gPlugIn_StateMutex);
			}
			break;
			
		default:
			result = kAudioHardwareUnknownPropertyError;
			break;
	};

	return result;
}

#pragma mark Device Property Operations

static Boolean	has_device_property( pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress)
{
	//	This method returns whether or not the given object has the given property.
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	Boolean result = false;
	
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyName:
		case kAudioObjectPropertyManufacturer:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioDevicePropertyDeviceUID:
		case kAudioDevicePropertyModelUID:
		case kAudioDevicePropertyTransportType:
		case kAudioDevicePropertyRelatedDevices:
		case kAudioDevicePropertyClockDomain:
		case kAudioDevicePropertyDeviceIsAlive:
		case kAudioDevicePropertyDeviceIsRunning:
		case kAudioObjectPropertyControlList:
		case kAudioDevicePropertyNominalSampleRate:
		case kAudioDevicePropertyAvailableNominalSampleRates:
		case kAudioDevicePropertyIsHidden:
		case kAudioDevicePropertyZeroTimeStampPeriod:
		case kAudioDevicePropertyIcon:
		case kAudioDevicePropertyStreams:
            result = true;
			break;
			
		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
		case kAudioDevicePropertyLatency:
		case kAudioDevicePropertySafetyOffset:
		case kAudioDevicePropertyPreferredChannelsForStereo:
		case kAudioDevicePropertyPreferredChannelLayout:
            result = (inAddress->mScope == kAudioObjectPropertyScopeInput) || (inAddress->mScope == kAudioObjectPropertyScopeOutput);
			break;
	};

	return result;
}

static OSStatus	device_property_settable( pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable)
{
	//	This method returns whether or not the given property on the object can have its value
	//	changed.
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	OSStatus result = 0;

	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyName:
		case kAudioObjectPropertyManufacturer:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioDevicePropertyDeviceUID:
		case kAudioDevicePropertyModelUID:
		case kAudioDevicePropertyTransportType:
		case kAudioDevicePropertyRelatedDevices:
		case kAudioDevicePropertyClockDomain:
		case kAudioDevicePropertyDeviceIsAlive:
		case kAudioDevicePropertyDeviceIsRunning:
		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
		case kAudioDevicePropertyLatency:
		case kAudioDevicePropertyStreams:
		case kAudioObjectPropertyControlList:
		case kAudioDevicePropertySafetyOffset:
		case kAudioDevicePropertyAvailableNominalSampleRates:
		case kAudioDevicePropertyIsHidden:
		case kAudioDevicePropertyPreferredChannelsForStereo:
		case kAudioDevicePropertyPreferredChannelLayout:
		case kAudioDevicePropertyZeroTimeStampPeriod:
		case kAudioDevicePropertyIcon:
			*outIsSettable = false;
			break;
		
		case kAudioDevicePropertyNominalSampleRate:
			*outIsSettable = true;
			break;
		
		default:
			result = kAudioHardwareUnknownPropertyError;
			break;
	};

	return result;
}

static OSStatus	get_device_property_size( AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize)
{
	//	This method returns the byte size of the property's data.
	
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus result = 0;
	
	
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyClass:
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyOwner:
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioObjectPropertyName:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyManufacturer:
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
            *outDataSize = device_object_list_size(inAddress->mScope, inObjectID) * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertyDeviceUID:
			*outDataSize = sizeof(CFStringRef);
			break;

		case kAudioDevicePropertyModelUID:
			*outDataSize = sizeof(CFStringRef);
			break;

		case kAudioDevicePropertyTransportType:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyRelatedDevices:
			*outDataSize = sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertyClockDomain:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceIsAlive:
			*outDataSize = sizeof(AudioClassID);
			break;

		case kAudioDevicePropertyDeviceIsRunning:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyLatency:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyStreams:
            *outDataSize = device_stream_list_size(inAddress->mScope, inObjectID) * sizeof(AudioObjectID);
			break;

		case kAudioObjectPropertyControlList:
            *outDataSize = device_control_list_size(inAddress->mScope, inObjectID) * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertySafetyOffset:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyNominalSampleRate:
			*outDataSize = sizeof(Float64);
			break;

		case kAudioDevicePropertyAvailableNominalSampleRates:
			*outDataSize = kDevice_SampleRatesSize * sizeof(AudioValueRange);
			break;
		
		case kAudioDevicePropertyIsHidden:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelsForStereo:
			*outDataSize = 2 * sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelLayout:
			*outDataSize = offsetof(AudioChannelLayout, mChannelDescriptions) + (kNumber_Of_Channels * sizeof(AudioChannelDescription));
			break;

		case kAudioDevicePropertyZeroTimeStampPeriod:
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyIcon:
			*outDataSize = sizeof(CFURLRef);
			break;

		default:
			result = kAudioHardwareUnknownPropertyError;
			break;
	};

	return result;
}

static OSStatus	get_device_property( AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData)
{
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus result = 0;
	UInt32 theNumberItemsToFetch;
	UInt32 theItemIndex;
	
	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			*((AudioClassID*)outData) = kAudioObjectClassID;
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyClass:
			*((AudioClassID*)outData) = kAudioDeviceClassID;
			*outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyOwner:
			*((AudioObjectID*)outData) = kObjectID_PlugIn;
			*outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioObjectPropertyName:
            
            switch (inObjectID) {
                case kObjectID_Device:
                    *((CFStringRef*)outData) = get_device_name();
                    *outDataSize = sizeof(CFStringRef);
                    break;
                    
                case kObjectID_Device2:
                    *((CFStringRef*)outData) = get_device2_name();
                    *outDataSize = sizeof(CFStringRef);
                    break;
            }
			break;
			
		case kAudioObjectPropertyManufacturer:
			*((CFStringRef*)outData) = CFSTR(kManufacturer_Name);
			*outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
            theNumberItemsToFetch = minimum(inDataSize / sizeof(AudioObjectID), device_object_list_size(inAddress->mScope, inObjectID));

            //    fill out the list with the right objects
            for (UInt32 i = 0, k = 0; k < theNumberItemsToFetch; i++)
            {
                if (kDevice_ObjectList[i].scope == inAddress->mScope || inAddress->mScope == kAudioObjectPropertyScopeGlobal)
                {
                    ((AudioObjectID*)outData)[k++] = kDevice_ObjectList[i].id;
                }
            }
			//	report how much we wrote
			*outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertyDeviceUID:

            switch (inObjectID) {
                case kObjectID_Device:
                    *((CFStringRef*)outData) = get_device_uid();
                    *outDataSize = sizeof(CFStringRef);
                    break;
                    
                case kObjectID_Device2:
                    *((CFStringRef*)outData) = get_device2_uid();
                    *outDataSize = sizeof(CFStringRef);
                    break;
            }
            

			break;

		case kAudioDevicePropertyModelUID:

            *((CFStringRef*)outData) = get_device_model_uid();
			*outDataSize = sizeof(CFStringRef);
			break;

		case kAudioDevicePropertyTransportType:
			*((UInt32*)outData) = kAudioDeviceTransportTypeVirtual;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyRelatedDevices:
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	we only have the one device...
			if(theNumberItemsToFetch > 1)
			{
				theNumberItemsToFetch = 1;
			}
			
			//	Write the devices' object IDs into the return value
			if(theNumberItemsToFetch > 0)
			{
                switch (inObjectID) {
                    case kObjectID_Device:
                        ((AudioObjectID*)outData)[0] = kObjectID_Device;
                        break;
                        
                    case kObjectID_Device2:
                        ((AudioObjectID*)outData)[0] = kObjectID_Device2;
                        break;
                }
				
			}
			
			//	report how much we wrote
			*outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertyClockDomain:
			*((UInt32*)outData) = 0;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceIsAlive:
			*((UInt32*)outData) = 1;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceIsRunning:
			pthread_mutex_lock(&gPlugIn_StateMutex);
			*((UInt32*)outData) = ((gDevice_IOIsRunning > 0) > 0) ? 1 : 0;
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
			*((UInt32*)outData) = 1;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
			*((UInt32*)outData) = 1;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyLatency:
			*((UInt32*)outData) = 0;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyStreams:
            theNumberItemsToFetch = minimum(inDataSize / sizeof(AudioObjectID), device_stream_list_size(inAddress->mScope, inObjectID));

            //    fill out the list with as many objects as requested
            for (UInt32 i = 0, k = 0; k < theNumberItemsToFetch; i++)
            {
                if ((kDevice_ObjectList[i].type == kObjectType_Stream) &&
                    (kDevice_ObjectList[i].scope == inAddress->mScope || inAddress->mScope == kAudioObjectPropertyScopeGlobal))
                {
                    ((AudioObjectID*)outData)[k++] = kDevice_ObjectList[i].id;
                }
            }

			//	report how much we wrote
			*outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioObjectPropertyControlList:
            theNumberItemsToFetch = minimum(inDataSize / sizeof(AudioObjectID), device_control_list_size(inAddress->mScope, inObjectID));

            //    fill out the list with as many objects as requested
            for (UInt32 i = 0, k = 0; k < theNumberItemsToFetch; i++)
            {
                if (kDevice_ObjectList[i].type == kObjectType_Control)
                {
                    ((AudioObjectID*)outData)[k++] = kDevice_ObjectList[i].id;
                }
            }
			//	report how much we wrote
			*outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertySafetyOffset:
			*((UInt32*)outData) = kLatency_Frame_Size;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyNominalSampleRate:
			pthread_mutex_lock(&gPlugIn_StateMutex);
			*((Float64*)outData) = gDevice_SampleRate;
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			*outDataSize = sizeof(Float64);
			break;

		case kAudioDevicePropertyAvailableNominalSampleRates:
			theNumberItemsToFetch = inDataSize / sizeof(AudioValueRange);
			
			//	clamp it to the number of items we have
			if(theNumberItemsToFetch > kDevice_SampleRatesSize)
			{
				theNumberItemsToFetch = kDevice_SampleRatesSize;
			}
			
            //	fill out the return array
            for(UInt32 i = 0; i < theNumberItemsToFetch; i++)
            {
                ((AudioValueRange*)outData)[i].mMinimum = kDevice_SampleRates[i];
                ((AudioValueRange*)outData)[i].mMaximum = kDevice_SampleRates[i];
            }

			//	report how much we wrote
			*outDataSize = theNumberItemsToFetch * sizeof(AudioValueRange);
			break;
		
		case kAudioDevicePropertyIsHidden:
            
            switch (inObjectID) {
                case kObjectID_Device:
                    *((UInt32*)outData) = kDevice_IsHidden;
                    break;
                
                case kObjectID_Device2:
                    *((UInt32*)outData) = kDevice2_IsHidden;
                    break;
            }
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelsForStereo:
			((UInt32*)outData)[0] = 1;
			((UInt32*)outData)[1] = 2;
			*outDataSize = 2 * sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelLayout:
			{
				//	calcualte how big the
				UInt32 theACLSize = offsetof(AudioChannelLayout, mChannelDescriptions) + (kNumber_Of_Channels * sizeof(AudioChannelDescription));
				((AudioChannelLayout*)outData)->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
				((AudioChannelLayout*)outData)->mChannelBitmap = 0;
				((AudioChannelLayout*)outData)->mNumberChannelDescriptions = kNumber_Of_Channels;
				for(theItemIndex = 0; theItemIndex < kNumber_Of_Channels; ++theItemIndex)
				{
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mChannelLabel = kAudioChannelLabel_Left + theItemIndex;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mChannelFlags = 0;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mCoordinates[0] = 0;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mCoordinates[1] = 0;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mCoordinates[2] = 0;
				}
				*outDataSize = theACLSize;
			}
			break;

		case kAudioDevicePropertyZeroTimeStampPeriod:
			*((UInt32*)outData) = kDevice_RingBufferSize;
			*outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyIcon:
			{
				CFBundleRef theBundle = CFBundleGetBundleWithIdentifier(CFSTR("audio.existential.VAC.Speaker"));
				CFURLRef theURL = CFBundleCopyResourceURL(theBundle, CFSTR("VAC.ai.Speaker.icns"), NULL, NULL);
				*((CFURLRef*)outData) = theURL;
				*outDataSize = sizeof(CFURLRef);
			}
			break;
			
		default:
			result = kAudioHardwareUnknownPropertyError;
			break;
	};

	return result;
}

static OSStatus	set_device_property(pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData, UInt32* outNumberPropertiesChanged, AudioObjectPropertyAddress outChangedAddresses[2])
{
	#pragma unused(inClientProcessID, inQualifierDataSize, inQualifierData)
	
	//	declare the local variables
	OSStatus result = 0;
	Float64 theOldSampleRate;
	UInt64 theNewSampleRate;
	

	*outNumberPropertiesChanged = 0;
	
	switch(inAddress->mSelector)
	{
		case kAudioDevicePropertyNominalSampleRate:


			//	make sure that the new value is different than the old value
			pthread_mutex_lock(&gPlugIn_StateMutex);
			theOldSampleRate = gDevice_SampleRate;
			pthread_mutex_unlock(&gPlugIn_StateMutex);
			if(*((const Float64*)inData) != theOldSampleRate)
			{
				//	we dispatch this so that the change can happen asynchronously
				theOldSampleRate = *((const Float64*)inData);
				theNewSampleRate = (UInt64)theOldSampleRate;
				dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{ gPlugIn_Host->RequestDeviceConfigurationChange(gPlugIn_Host, kObjectID_Device, theNewSampleRate, NULL); });
			}
			break;
		
		default:
            result = kAudioHardwareUnknownPropertyError;
			break;
	};

	return result;
}

#pragma mark Stream Property Operations

static Boolean	has_stream_property(pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress)
{
	
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	Boolean result = false;

	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioStreamPropertyIsActive:
		case kAudioStreamPropertyDirection:
		case kAudioStreamPropertyTerminalType:
		case kAudioStreamPropertyStartingChannel:
		case kAudioStreamPropertyLatency:
		case kAudioStreamPropertyVirtualFormat:
		case kAudioStreamPropertyPhysicalFormat:
		case kAudioStreamPropertyAvailableVirtualFormats:
		case kAudioStreamPropertyAvailablePhysicalFormats:
			result = true;
			break;
	};

	return result;
}

static OSStatus	stream_property(pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable)
{
	#pragma unused(inClientProcessID)
	
	//	declare the local variables
	OSStatus reuslt = 0;
	

	switch(inAddress->mSelector)
	{
		case kAudioObjectPropertyBaseClass:
		case kAudioObjectPropertyClass:
		case kAudioObjectPropertyOwner:
		case kAudioObjectPropertyOwnedObjects:
		case kAudioStreamPropertyDirection:
		case kAudioStreamPropertyTerminalType:
		case kAudioStreamPropertyStartingChannel:
		case kAudioStreamPropertyLatency:
		case kAudioStreamPropertyAvailableVirtualFormats:
		case kAudioStreamPropertyAvailablePhysicalFormats:
			*outIsSettable = false;
			break;
		
		case kAudioStreamPropertyIsActive:
		case kAudioStreamPropertyVirtualFormat:
		case kAudioStreamPropertyPhysicalFormat:
			*outIsSettable = true;
			break;
		
		default:
            reuslt = kAudioHardwareUnknownPropertyError;
			break;
	};

	return reuslt;
}

