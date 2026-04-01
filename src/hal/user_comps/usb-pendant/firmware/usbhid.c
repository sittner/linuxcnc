#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#include "usbhid.h"

/** Endpoint address of the Generic HID reporting IN endpoint. */
#define GENERIC_IN_EPADDR         (ENDPOINT_DIR_IN | 1)

/** Size in bytes of the Generic HID reporting endpoint. */
#define GENERIC_EPSIZE            8

/** Type define for the device configuration descriptor structure. This must be defined in the
 *  application code, as the configuration descriptor contains several sub-descriptors which
 *  vary between devices, and which describe the device's usage to the host.
 */
typedef struct
{
  USB_Descriptor_Configuration_Header_t Config;

  // Generic HID Interface
  USB_Descriptor_Interface_t            HID_Interface;
  USB_HID_Descriptor_HID_t              HID_GenericHID;
  USB_Descriptor_Endpoint_t             HID_ReportINEndpoint;
} USB_Descriptor_Configuration_t;

/** Enum for the device interface descriptor IDs within the device. Each interface descriptor
 *  should have a unique ID index associated with it, which can be used to refer to the
 *  interface from other descriptors.
 */
enum InterfaceDescriptors_t
{
  INTERFACE_ID_GenericHID = 0, /**< GenericHID interface descriptor ID */
};

/** Enum for the device string descriptor IDs within the device. Each string descriptor should
 *  have a unique ID index associated with it, which can be used to refer to the string from
 *  other descriptors.
 */
enum StringDescriptors_t
{
  STRING_ID_Language     = 0, /**< Supported Languages string descriptor ID (must be zero) */
  STRING_ID_Manufacturer = 1, /**< Manufacturer string ID */
  STRING_ID_Product      = 2, /**< Product string ID */
  STRING_ID_Serial       = 3, // Product serial no
};

/** HID class report descriptor. This is a special descriptor constructed with values from the
 *  USBIF HID class specification to describe the reports and capabilities of the HID device. This
 *  descriptor is parsed by the host and its contents used to determine what data (and in what encoding)
 *  the device will send, and what it may be sent back from the host. Refer to the HID specification for
 *  more details on HID report descriptors.
 */
const USB_Descriptor_HIDReport_Datatype_t PROGMEM GenericReport[] =
{
  USBHID_DESCRIPTOR
};

/** Device descriptor structure. This descriptor, located in FLASH memory, describes the overall
 *  device characteristics, including the supported USB version, control endpoint size and the
 *  number of device configurations. The descriptor is read out by the USB host when the enumeration
 *  process begins.
 */
const USB_Descriptor_Device_t PROGMEM DeviceDescriptor =
{
  .Header                 = {.Size = sizeof(USB_Descriptor_Device_t), .Type = DTYPE_Device},

  .USBSpecification       = VERSION_BCD(1,1,0),
  .Class                  = USB_CSCP_NoDeviceClass,
  .SubClass               = USB_CSCP_NoDeviceSubclass,
  .Protocol               = USB_CSCP_NoDeviceProtocol,

  .Endpoint0Size          = FIXED_CONTROL_ENDPOINT_SIZE,

  .VendorID               = USBHID_VID,
  .ProductID              = USBHID_PID,
  .ReleaseNumber          = VERSION_BCD(0,0,1),

  .ManufacturerStrIndex   = STRING_ID_Manufacturer,
  .ProductStrIndex        = STRING_ID_Product,
#ifdef USBHID_SERIAL_STR
  .SerialNumStrIndex      = STRING_ID_Serial,
#else
  .SerialNumStrIndex      = NO_DESCRIPTOR,
#endif

  .NumberOfConfigurations = FIXED_NUM_CONFIGURATIONS
};

/** Configuration descriptor structure. This descriptor, located in FLASH memory, describes the usage
 *  of the device in one of its supported configurations, including information about any device interfaces
 *  and endpoints. The descriptor is read out by the USB host during the enumeration process when selecting
 *  a configuration so that the host may correctly communicate with the USB device.
 */
const USB_Descriptor_Configuration_t PROGMEM ConfigurationDescriptor =
{
  .Config =
    {
      .Header                 = {.Size = sizeof(USB_Descriptor_Configuration_Header_t), .Type = DTYPE_Configuration},

      .TotalConfigurationSize = sizeof(USB_Descriptor_Configuration_t),
      .TotalInterfaces        = 1,

      .ConfigurationNumber    = 1,
      .ConfigurationStrIndex  = NO_DESCRIPTOR,

      .ConfigAttributes       = (USB_CONFIG_ATTR_RESERVED | USB_CONFIG_ATTR_SELFPOWERED),

      .MaxPowerConsumption    = USB_CONFIG_POWER_MA(100)
    },

  .HID_Interface =
    {
      .Header                 = {.Size = sizeof(USB_Descriptor_Interface_t), .Type = DTYPE_Interface},

      .InterfaceNumber        = INTERFACE_ID_GenericHID,
      .AlternateSetting       = 0x00,

      .TotalEndpoints         = 1,

      .Class                  = HID_CSCP_HIDClass,
      .SubClass               = HID_CSCP_NonBootSubclass,
      .Protocol               = HID_CSCP_NonBootProtocol,

      .InterfaceStrIndex      = NO_DESCRIPTOR
    },

  .HID_GenericHID =
    {
      .Header                 = {.Size = sizeof(USB_HID_Descriptor_HID_t), .Type = HID_DTYPE_HID},

      .HIDSpec                = VERSION_BCD(1,1,1),
      .CountryCode            = 0x00,
      .TotalReportDescriptors = 1,
      .HIDReportType          = HID_DTYPE_Report,
      .HIDReportLength        = sizeof(GenericReport)
    },

  .HID_ReportINEndpoint =
    {
      .Header                 = {.Size = sizeof(USB_Descriptor_Endpoint_t), .Type = DTYPE_Endpoint},

      .EndpointAddress        = GENERIC_IN_EPADDR,
      .Attributes             = (EP_TYPE_INTERRUPT | ENDPOINT_ATTR_NO_SYNC | ENDPOINT_USAGE_DATA),
      .EndpointSize           = GENERIC_EPSIZE,
      .PollingIntervalMS      = 0x05
    },
};

/** Language descriptor structure. This descriptor, located in FLASH memory, is returned when the host requests
 *  the string descriptor with index 0 (the first index). It is actually an array of 16-bit integers, which indicate
 *  via the language ID table available at USB.org what languages the device supports for its string descriptors.
 */
const USB_Descriptor_String_t PROGMEM LanguageString = USB_STRING_DESCRIPTOR_ARRAY(LANGUAGE_ID_ENG);

/** Manufacturer descriptor string. This is a Unicode string containing the manufacturer's details in human readable
 *  form, and is read out upon request by the host when the appropriate string ID is requested, listed in the Device
 *  Descriptor.
 */
const USB_Descriptor_String_t PROGMEM ManufacturerString = USB_STRING_DESCRIPTOR(USBHID_MANUF_STR);

/** Product descriptor string. This is a Unicode string containing the product's details in human readable form,
 *  and is read out upon request by the host when the appropriate string ID is requested, listed in the Device
 *  Descriptor.
 */
const USB_Descriptor_String_t PROGMEM ProductString = USB_STRING_DESCRIPTOR(USBHID_PROD_STR);

#ifdef USBHID_SERIAL_STR
const USB_Descriptor_String_t PROGMEM SerialString = USB_STRING_DESCRIPTOR(USBHID_SERIAL_STR);
#endif

#ifdef USBHID_COMPARE_REPORT_IN_DATA
/** Buffer to hold the previously generated HID report, for comparison purposes inside the HID class driver. */
static uint8_t PrevHIDReportBuffer[sizeof(USBHID_Report_Data_In_t)];
#endif

static volatile int8_t usbhid_state = USBHID_STATE_DISCON;

/** This function is called by the library when in device mode, and must be overridden (see library "USB Descriptors"
 *  documentation) by the application code so that the address and size of a requested descriptor can be given
 *  to the USB library. When the device receives a Get Descriptor request on the control endpoint, this function
 *  is called so that the descriptor details can be passed back and the appropriate descriptor sent back to the
 *  USB host.
 */
uint16_t CALLBACK_USB_GetDescriptor(const uint16_t wValue,
                                    const uint8_t wIndex,
                                    const void** const DescriptorAddress)
{
  const uint8_t  DescriptorType   = (wValue >> 8);
  const uint8_t  DescriptorNumber = (wValue & 0xFF);

  const void* Address = NULL;
  uint16_t    Size    = NO_DESCRIPTOR;

  switch (DescriptorType)
  {
    case DTYPE_Device:
      Address = &DeviceDescriptor;
      Size    = sizeof(USB_Descriptor_Device_t);
      break;
    case DTYPE_Configuration:
      Address = &ConfigurationDescriptor;
      Size    = sizeof(USB_Descriptor_Configuration_t);
      break;
    case DTYPE_String:
      switch (DescriptorNumber)
      {
        case STRING_ID_Language:
          Address = &LanguageString;
          Size    = pgm_read_byte(&LanguageString.Header.Size);
          break;
        case STRING_ID_Manufacturer:
          Address = &ManufacturerString;
          Size    = pgm_read_byte(&ManufacturerString.Header.Size);
          break;
        case STRING_ID_Product:
          Address = &ProductString;
          Size    = pgm_read_byte(&ProductString.Header.Size);
          break;
#ifdef USBHID_SERIAL_STR
        case STRING_ID_Serial:
          Address = &SerialString;
          Size    = pgm_read_byte(&SerialString.Header.Size);
          break;
#endif
      }

      break;
    case HID_DTYPE_HID:
      Address = &ConfigurationDescriptor.HID_GenericHID;
      Size    = sizeof(USB_HID_Descriptor_HID_t);
      break;
    case HID_DTYPE_Report:
      Address = &GenericReport;
      Size    = sizeof(GenericReport);
      break;
  }

  *DescriptorAddress = Address;
  return Size;
}

/** LUFA HID Class driver interface configuration and state information. This structure is
 *  passed to all HID Class driver functions, so that multiple instances of the same class
 *  within a device can be differentiated from one another.
 */
USB_ClassInfo_HID_Device_t Generic_HID_Interface =
  {
    .Config =
      {
        .InterfaceNumber              = INTERFACE_ID_GenericHID,
        .ReportINEndpoint             =
          {
            .Address              = GENERIC_IN_EPADDR,
            .Size                 = GENERIC_EPSIZE,
            .Banks                = 1,
          },
#ifdef USBHID_COMPARE_REPORT_IN_DATA
        .PrevReportINBuffer           = PrevHIDReportBuffer,
#else
        .PrevReportINBuffer           = NULL,
#endif
        .PrevReportINBufferSize       = sizeof(USBHID_Report_Data_In_t),
      },
  };

// Event handler for the library USB Connection event.
void EVENT_USB_Device_Connect(void)
{
  usbhid_state = USBHID_STATE_ENUM;
#ifdef USBHID_STATE_USE_CALLBACK
  usbhid_state_callback(usbhid_state);
#endif
}

// Event handler for the library USB Disconnection event.
void EVENT_USB_Device_Disconnect(void)
{
  usbhid_state = USBHID_STATE_DISCON;
#ifdef USBHID_STATE_USE_CALLBACK
  usbhid_state_callback(usbhid_state);
#endif
}

// Event handler for the library USB Configuration Changed event.
void EVENT_USB_Device_ConfigurationChanged(void)
{
  if (HID_Device_ConfigureEndpoints(&Generic_HID_Interface))
  {
    usbhid_state = USBHID_STATE_READY;
  }
  else
  {
    usbhid_state = USBHID_STATE_ERROR;
  }

  USB_Device_EnableSOFEvents();

#ifdef USBHID_STATE_USE_CALLBACK
  usbhid_state_callback(usbhid_state);
#endif
}

void usbhid_init(void)
{
  usbhid_state = USBHID_STATE_DISCON;
  USB_Init();
}

int8_t usbhid_get_state(void)
{
  return usbhid_state;
}

void usbhid_task(void)
{
  HID_Device_USBTask(&Generic_HID_Interface);
  USB_USBTask();
}

/** Event handler for the library USB Control Request reception event. */
void EVENT_USB_Device_ControlRequest(void)
{
  HID_Device_ProcessControlRequest(&Generic_HID_Interface);
}

/** Event handler for the USB device Start Of Frame event. */
void EVENT_USB_Device_StartOfFrame(void)
{
  HID_Device_MillisecondElapsed(&Generic_HID_Interface);
}

/** HID class driver callback function for the creation of HID reports to the host.
 *
 *  \param[in]     HIDInterfaceInfo  Pointer to the HID class interface configuration structure being referenced
 *  \param[in,out] ReportID    Report ID requested by the host if non-zero, otherwise callback should set to the generated report ID
 *  \param[in]     ReportType  Type of the report to create, either HID_REPORT_ITEM_In or HID_REPORT_ITEM_Feature
 *  \param[out]    ReportData  Pointer to a buffer where the created report should be stored
 *  \param[out]    ReportSize  Number of bytes written in the report (or zero if no report is to be sent)
 *
 *  \return Boolean \c true to force the sending of the report, \c false to let the library determine if it needs to be sent
 */
bool CALLBACK_HID_Device_CreateHIDReport(USB_ClassInfo_HID_Device_t* const HIDInterfaceInfo,
                                         uint8_t* const ReportID,
                                         const uint8_t ReportType,
                                         void* ReportData,
                                         uint16_t* const ReportSize)
{
  *ReportSize = sizeof(USBHID_Report_Data_In_t);
  return usbhid_create_report_callback((USBHID_Report_Data_In_t *) ReportData);
}

/** HID class driver callback function for the processing of HID reports from the host.
 *
 *  \param[in] HIDInterfaceInfo  Pointer to the HID class interface configuration structure being referenced
 *  \param[in] ReportID    Report ID of the received report from the host
 *  \param[in] ReportType  The type of report that the host has sent, either HID_REPORT_ITEM_Out or HID_REPORT_ITEM_Feature
 *  \param[in] ReportData  Pointer to a buffer where the received report has been stored
 *  \param[in] ReportSize  Size in bytes of the received HID report
 */
void CALLBACK_HID_Device_ProcessHIDReport(USB_ClassInfo_HID_Device_t* const HIDInterfaceInfo,
                                          const uint8_t ReportID,
                                          const uint8_t ReportType,
                                          const void* ReportData,
                                          const uint16_t ReportSize)
{
#ifdef USBHID_PROCESS_REPORT_USE_CALLBACK
extern void usbhid_process_report_callback((USBHID_Report_Data_Out_t *) ReportData);
#endif
}


